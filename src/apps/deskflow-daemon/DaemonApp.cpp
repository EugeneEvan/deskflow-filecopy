/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonApp.h"

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "base/LogOutputters.h"
#include "common/ExitCodes.h"
#include "common/Settings.h"
#include "deskflow/ipc/DaemonIpcServer.h"

#if defined(Q_OS_WIN)
#include "arch/win32/ArchDaemonWindows.h"
#include "deskflow/Screen.h"
#include "platform/MSWindowsDebugOutputter.h"
#include "platform/MSWindowsEventQueueBuffer.h"
#include "platform/MSWindowsWatchdog.h"

#define WIN32_LEAN_AND_MEAN
#include <AclAPI.h>
#include <Windows.h>

#endif

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <stdexcept>

using namespace deskflow::core;

DaemonApp::DaemonApp(IEventQueue &events) : m_events(events)
{
  // do nothing
}

DaemonApp::~DaemonApp() = default;

#if defined(Q_OS_WIN)
namespace {

void requireProtectedPath(const QString &path, bool parentOnly = false)
{
  const auto native = QDir::toNativeSeparators(path).toStdWString();
  const auto attributes = GetFileAttributesW(native.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    throw std::runtime_error("full-control path is missing or is a reparse point: " + path.toStdString());
  }
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PSID owner = nullptr;
  PACL dacl = nullptr;
  if (GetNamedSecurityInfoW(
          const_cast<wchar_t *>(native.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
          &owner, nullptr, &dacl, nullptr, &descriptor
      ) != ERROR_SUCCESS) {
    throw std::runtime_error("cannot read full-control path security: " + path.toStdString());
  }
  const auto trusted = [](PSID sid) {
    return sid && (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
  };
  bool secure = trusted(owner) && dacl;
  const DWORD forbidden = parentOnly
                              ? FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_ALL
                              : FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                    FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
  for (DWORD i = 0; secure && i < dacl->AceCount; ++i) {
    void *entry = nullptr;
    if (!GetAce(dacl, i, &entry)) {
      secure = false;
      break;
    }
    const auto header = static_cast<ACE_HEADER *>(entry);
    if (header->AceFlags & INHERIT_ONLY_ACE) {
      continue;
    }
    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
      const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(entry);
      if ((ace->Mask & forbidden) && !trusted(&ace->SidStart)) {
        secure = false;
      }
    } else if (header->AceType != ACCESS_DENIED_ACE_TYPE) {
      // The installer emits simple allow ACEs. Fail closed for object/callback ACEs.
      secure = false;
    }
  }
  LocalFree(descriptor);
  if (!secure) {
    throw std::runtime_error("full-control path permits unprivileged replacement: " + path.toStdString());
  }
}

} // namespace
#endif

void DaemonApp::setFullControlSettings(const QString &settingsFile)
{
#if defined(Q_OS_WIN)
  const auto appDirectory = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
  static const QRegularExpression expectedDirectory(QStringLiteral("^[A-Za-z]:/Deskflow FileCopy Control$"));
  const auto fixedSettings = appDirectory + QStringLiteral("/settings/Deskflow.conf");
  if (!expectedDirectory.match(appDirectory).hasMatch() ||
      QDir::fromNativeSeparators(settingsFile).compare(fixedSettings, Qt::CaseInsensitive) != 0) {
    throw std::runtime_error("full-control requires the fixed protected installation and settings paths");
  }
  requireProtectedPath(appDirectory.left(3), true);
  requireProtectedPath(appDirectory);
  QDirIterator entries(
      appDirectory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories
  );
  while (entries.hasNext()) {
    requireProtectedPath(entries.next());
  }
  QFile marker(appDirectory + QStringLiteral("/deskflow-full-control.component"));
  if (!marker.open(QIODevice::ReadOnly) || marker.readAll().trimmed() != "Deskflow FileCopy Full Control v1") {
    throw std::runtime_error("full-control component marker is missing or invalid");
  }
  requireProtectedPath(fixedSettings);
  QSettings config(fixedSettings, QSettings::IniFormat);
  const auto requirePath = [&config, &appDirectory](const QString &key, const QString &relative) {
    if (QDir::fromNativeSeparators(config.value(key).toString())
            .compare(appDirectory + relative, Qt::CaseInsensitive) != 0) {
      throw std::runtime_error("full-control settings contain a non-protected path: " + key.toStdString());
    }
  };
  requirePath(Settings::Security::Certificate, QStringLiteral("/settings/tls/deskflow.pem"));
  requirePath(Settings::Log::File, QStringLiteral("/logs/core.log"));
  requirePath(Settings::Daemon::LogFile, QStringLiteral("/logs/daemon.log"));
  if (!config.value(Settings::Security::TlsEnabled).toBool() ||
      !config.value(Settings::Security::CheckPeers).toBool() ||
      config.value(Settings::Core::EnableEnterCommand).toBool() ||
      config.value(Settings::Core::EnableExitCommand).toBool() ||
      config.value(Settings::Server::ExternalConfig).toBool()) {
    throw std::runtime_error(
        "full-control requires TLS peer checks and disabled screen commands/external configuration"
    );
  }
  m_fullControl = true;
  m_configFile = fixedSettings;
  Settings::setSettingsFile(fixedSettings);
  // Working directory and subsequent DLL loads must never resolve in a user directory.
  SetCurrentDirectoryW(QDir::toNativeSeparators(appDirectory).toStdWString().c_str());
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  QCoreApplication::setLibraryPaths({appDirectory + QStringLiteral("/plugins")});
#else
  Q_UNUSED(settingsFile)
  throw std::runtime_error("full-control is only supported on Windows");
#endif
}

void DaemonApp::saveLogLevel(const QString &logLevel) const
{
  LOG_DEBUG("log level changed: %s", logLevel.toUtf8().constData());
  CLOG->setFilter(logLevel.toUtf8().constData());
  Settings::setValue(Settings::Daemon::LogLevel, logLevel);
}

void DaemonApp::setConfigFile(const QString &configFile)
{
  LOG_DEBUG("config file updated: %s", configFile.toUtf8().constData());
  m_configFile = configFile;
  Settings::setValue(Settings::Daemon::ConfigFile, configFile);
}

void DaemonApp::applyWatchdogCommand() const
{
#if defined(Q_OS_WIN)
  if (m_configFile.isEmpty()) {
    LOG_ERR("cannot apply watchdog command: no config file set");
    return;
  }

  // QFileInfo::exists on a UNC path triggers SMB auth from this SYSTEM-context
  // process, leaking the machine NTLM hash to whoever controls the remote host.
  // Any local user can reach this via the IPC pipe, so reject remote paths up front.
  if (m_configFile.startsWith(QStringLiteral("\\\\")) || m_configFile.startsWith(QStringLiteral("//"))) {
    LOG_ERR("cannot apply watchdog command: remote config file paths are not allowed: %s", qPrintable(m_configFile));
    return;
  }

  if (!QFileInfo::exists(m_configFile)) {
    LOG_ERR("cannot apply watchdog command: config file does not exist: %s", qPrintable(m_configFile));
    return;
  }

  QSettings config(m_configFile, QSettings::IniFormat);
  const auto coreMode = config.value(Settings::Core::CoreMode).toInt();
  const auto elevate = m_fullControl || config.value(Settings::Daemon::Elevate, !Settings::isPortableMode()).toBool();

  QString modeArg;
  if (coreMode == Settings::CoreMode::Server) {
    modeArg = QStringLiteral("server");
  } else if (coreMode == Settings::CoreMode::Client) {
    modeArg = QStringLiteral("client");
  } else {
    LOG_ERR("cannot apply watchdog command: invalid core mode in config: %d", coreMode);
    return;
  }

  const auto corePath = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  const auto command = QStringLiteral("\"%1\" %2 --settings \"%3\"").arg(corePath, modeArg, m_configFile).toStdString();

  LOG_DEBUG("applying watchdog command (elevate: %s)", elevate ? "yes" : "no");
  m_pWatchdog->setProcessConfig(command, elevate);
#else
  LOG_ERR("applying watchdog command not implemented on this platform");
#endif
}

void DaemonApp::clearWatchdogCommand()
{
  LOG_DEBUG("clearing watchdog command");

  // Clear the persisted config path so the daemon does not auto-start the core on next boot.
  m_configFile.clear();
  Settings::setValue(Settings::Daemon::ConfigFile);

#if defined(Q_OS_WIN)
  m_pWatchdog->setProcessConfig("", false);
#else
  LOG_ERR("clearing watchdog command not implemented on this platform");
#endif
}

void DaemonApp::clearSettings()
{
  LOG_INFO("clearing daemon settings");
  m_configFile.clear();
  Settings::setValue(Settings::Daemon::ConfigFile);
  Settings::setValue(Settings::Daemon::LogFile);
  Settings::setValue(Settings::Daemon::LogLevel);
}

void DaemonApp::connectIpcServer(const ipc::DaemonIpcServer *ipcServer) const
{
  if (m_fullControl) {
    throw std::runtime_error("legacy daemon management IPC is disabled in full-control mode");
  }
  // Use direct connection as this object is on it's own thread,
  // and so is on a different event loop to the main Qt loop.
  connect(ipcServer, &ipc::DaemonIpcServer::logLevelChanged, this, &DaemonApp::saveLogLevel, Qt::DirectConnection);
  connect(ipcServer, &ipc::DaemonIpcServer::configFileChanged, this, &DaemonApp::setConfigFile, Qt::DirectConnection);
  connect(
      ipcServer, &ipc::DaemonIpcServer::startProcessRequested, this, &DaemonApp::applyWatchdogCommand,
      Qt::DirectConnection
  );
  connect(
      ipcServer, &ipc::DaemonIpcServer::stopProcessRequested, this, &DaemonApp::clearWatchdogCommand,
      Qt::DirectConnection
  );
  connect(
      ipcServer, &ipc::DaemonIpcServer::clearSettingsRequested, this, &DaemonApp::clearSettings, Qt::DirectConnection
  );
}

void DaemonApp::run(QThread &daemonThread)
{
  LOG_INFO("starting daemon");

  // Important: Move the daemon app to the daemon thread before creating any more Qt objects
  // owned by the daemon app, as they will be created on the daemon thread.
  moveToThread(&daemonThread);

  connect(&daemonThread, &QThread::started, this, [this, &daemonThread]() {
    LOG_DEBUG("daemon thread started");

    if (m_foreground) {
      LOG_DEBUG("running daemon in foreground");
      mainLoop();
    } else {
      LOG_DEBUG("running daemon in background (daemonizing)");
      ARCH->daemonize([this] { return daemonLoop(); });
    }

    daemonThread.quit();
    LOG_DEBUG("daemon thread finished");
  });

#if defined(Q_OS_WIN)
  m_pWatchdog = std::make_unique<MSWindowsWatchdog>(m_foreground, *m_pFileLogOutputter, m_fullControl);

  if (m_fullControl) {
    applyWatchdogCommand();
  } else if (const auto persistedConfig = Settings::value(Settings::Daemon::ConfigFile).toString();
             !persistedConfig.isEmpty()) {
    LOG_DEBUG("using last known config file: %s", persistedConfig.toUtf8().constData());
    m_configFile = persistedConfig;
    applyWatchdogCommand();
  }
#endif

  LOG_DEBUG("starting daemon thread");
  daemonThread.start();
}

int DaemonApp::daemonLoop()
{
#if defined(Q_OS_WIN)
  // Runs the daemon through the Windows service controller, which controls the program lifecycle.
  return ArchDaemonWindows::runDaemon([this]() { return mainLoop(); });
#else
  return mainLoop();
#endif
}

int DaemonApp::mainLoop()
{
#if defined(Q_OS_WIN)
  if (m_pWatchdog == nullptr) {
    LOG_ERR("watchdog not initialized");
    return s_exitFailed;
  }
  ArchDaemonWindows::daemonRunning(true);
#endif

  try {
#if defined(Q_OS_WIN)
    // Install the platform event queue to handle service stop events.
    // This must be done on the same thread as the event loop, otherwise the service stop
    // request will not add the quit event to the event queue, and the service won't stop.
    m_events.adoptBuffer(new MSWindowsEventQueueBuffer(&m_events));

    LOG_DEBUG("starting watchdog threads");
    m_pWatchdog->startAsync();
#endif

    LOG_INFO("daemon is running");
    m_events.loop();
  } catch (std::exception &e) { // NOSONAR - Catching all exceptions
    LOG_CRIT("daemon error: %s", e.what());
  } catch (...) { // NOSONAR - Catching remaining exceptions
    LOG_CRIT("daemon unknown error");
  }

  LOG_INFO("daemon is stopping");

#if defined(Q_OS_WIN)
  try {
    LOG_DEBUG("stopping process watchdog");
    m_pWatchdog->stop();
  } catch (std::exception &e) { // NOSONAR - Catching all exceptions
    LOG_CRIT("daemon stop watchdog error: %s", e.what());
  } catch (...) { // NOSONAR - Catching remaining exceptions
    LOG_CRIT("daemon stop watchdog unknown error");
  }
  ArchDaemonWindows::daemonRunning(false);
#endif

  return s_exitSuccess;
}

QString DaemonApp::logFilename()
{
  return Settings::value(Settings::Daemon::LogFile).toString();
}

void DaemonApp::setForeground()
{
  m_foreground = true;
  showConsole();
}

void DaemonApp::initLogging()
{
#if defined(Q_OS_WIN)
  if (!m_foreground) {
    // Only use MS debug outputter when the process is daemonized, since stdout won't be accessible
    // in that case, but is accessible when running in the foreground.
    CLOG->insert(new MSWindowsDebugOutputter()); // NOSONAR - Adopted by `Log`
  }
#endif

  m_pFileLogOutputter = new FileLogOutputter(qPrintable(logFilename())); // NOSONAR - Adopted by `Log`
  CLOG->insert(m_pFileLogOutputter);
}

void DaemonApp::showConsole()
{
#if defined(Q_OS_WIN)
  // The daemon bin is compiled using the Win32 subsystem which works best for Windows services,
  // so when running as a foreground process we need to allocate a console (or we won't see output).
  // It is important to do this inside the arg check loop so that we can attach console ahead
  // of log output generated by handling other args.
  AllocConsole();
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
#endif
}
