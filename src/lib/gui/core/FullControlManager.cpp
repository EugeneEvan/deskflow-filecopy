/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FullControlManager.h"
#include "common/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <AclAPI.h>
#include <Windows.h>
#include <shellapi.h>
#endif

namespace deskflow::gui {
namespace {
bool safeLocalPath(QString path)
{
  path.replace('\\', '/');
  static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:/"));
  if (!drive.match(path).hasMatch() || path.mid(2).contains(':') || path.contains('"') || path.contains(QChar(0)) ||
      path.contains('\r') || path.contains('\n'))
    return false;
  for (const auto character : path) {
    if (character.unicode() < 32 || QStringLiteral("<>|?*").contains(character))
      return false;
  }
  for (const auto &part : path.mid(3).split('/')) {
    if (part == "." || part == ".." || part.endsWith(' ') || part.endsWith('.'))
      return false;
  }
  return true;
}

QString normalized(QString path)
{
  return QDir::cleanPath(path.replace('\\', '/'));
}

#ifdef Q_OS_WIN
bool protectedPath(const QString &path, bool parentOnly = false)
{
  const auto native = QDir::toNativeSeparators(path).toStdWString();
  const auto attributes = GetFileAttributesW(native.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return false;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PSID owner = nullptr;
  PACL dacl = nullptr;
  if (GetNamedSecurityInfoW(
          const_cast<wchar_t *>(native.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
          &owner, nullptr, &dacl, nullptr, &descriptor
      ) != ERROR_SUCCESS)
    return false;
  const auto trusted = [](PSID sid) {
    return sid && (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
  };
  bool secure = trusted(owner) && dacl;
  const DWORD forbidden = parentOnly
                              ? FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_ALL
                              : FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                    FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
  for (DWORD index = 0; secure && index < dacl->AceCount; ++index) {
    void *entry = nullptr;
    if (!GetAce(dacl, index, &entry)) {
      secure = false;
      break;
    }
    const auto header = static_cast<ACE_HEADER *>(entry);
    if (header->AceFlags & INHERIT_ONLY_ACE)
      continue;
    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
      const auto ace = static_cast<ACCESS_ALLOWED_ACE *>(entry);
      secure = !(ace->Mask & forbidden) || trusted(&ace->SidStart);
    } else if (header->AceType != ACCESS_DENIED_ACE_TYPE) {
      secure = false;
    }
  }
  LocalFree(descriptor);
  return secure;
}
#endif
} // namespace

bool FullControlManager::isProtectedDirectory(const QString &directory)
{
#ifdef Q_OS_WIN
  if (!safeLocalPath(directory) || !protectedPath(directory.left(3), true) || !protectedPath(directory) ||
      !protectedPath(directory + "/full-control-manager.ps1") ||
      !protectedPath(directory + "/deskflow-full-control.component"))
    return false;
  // TLS/private settings are intentionally unreadable by this ordinary-user GUI.
  QFile marker(directory + "/deskflow-full-control.component");
  return marker.open(QIODevice::ReadOnly) && marker.size() <= 256 &&
         marker.readAll().trimmed() == "Deskflow FileCopy Full Control v1";
#else
  Q_UNUSED(directory)
  return false;
#endif
}

QString FullControlManager::protectedDirectory(const QString &applicationDirectory)
{
  if (!safeLocalPath(applicationDirectory))
    return {};
  return applicationDirectory.left(1).toUpper() + QStringLiteral(":/Deskflow FileCopy Control");
}

QStringList
FullControlManager::arguments(Action action, const QString &script, const QString &source, const QString &settings)
{
  if (!safeLocalPath(script) || !safeLocalPath(source) || !safeLocalPath(settings))
    return {};
  const auto name = QMetaEnum::fromType<Action>().valueToKey(static_cast<int>(action));
  if (!name)
    return {};
  return {"-NoLogo", "-NoProfile", "-NonInteractive",         "-ExecutionPolicy", "Bypass", "-File",
          script,    "-Action",    QString::fromLatin1(name), "-SourceDirectory", source,   "-SourceSettings",
          settings};
}

QString FullControlManager::quoteArguments(const QStringList &arguments)
{
  QStringList result;
  for (const auto &argument : arguments) {
    if (argument.contains('"') || argument.contains(QChar(0)) || argument.contains('\r') || argument.contains('\n'))
      return {};
    // CommandLineToArgvW treats trailing backslashes before a quote as escapes.
    qsizetype trailing = 0;
    for (auto index = argument.size(); index > 0 && argument.at(index - 1) == '\\'; --index)
      ++trailing;
    result.append('"' + argument + QString(trailing, '\\') + '"');
  }
  return result.join(' ');
}

FullControlManager::Status FullControlManager::parseStatus(const QByteArray &json, const QString &expectedDirectory)
{
  Status status;
  const auto document = QJsonDocument::fromJson(json);
  const auto object = document.object();
  status.error = object.value("error").toString();
  if (!document.isObject() || !object.value("installed").isBool() || !object.value("running").isBool() ||
      !object.value("directory").isString() || !object.value("settings").isString() || !status.error.isEmpty()) {
    if (status.error.isEmpty())
      status.error = tr("The full control component returned an invalid status.");
    return status;
  }
  status.directory = object.value("directory").toString();
  const auto settings = object.value("settings").toString();
  if (expectedDirectory.isEmpty() || !safeLocalPath(status.directory) || !safeLocalPath(settings) ||
      normalized(status.directory).compare(normalized(expectedDirectory), Qt::CaseInsensitive) != 0 ||
      normalized(settings).compare(normalized(expectedDirectory + "/settings/Deskflow.conf"), Qt::CaseInsensitive) !=
          0) {
    status.error = tr("The full control component path does not match this installation.");
    return status;
  }
  status.installed = object.value("installed").toBool();
  status.running = object.value("running").toBool();
  status.valid = status.installed || !status.running;
  if (!status.valid)
    status.error = tr("The full control component returned an invalid status.");
  return status;
}

std::optional<bool> FullControlManager::confirmedMode(Action action, bool succeeded, const Status &status)
{
  if (!succeeded || !status.valid)
    return std::nullopt;
  if ((action == Action::Install || action == Action::Start) && status.installed && status.running)
    return true;
  if (action == Action::Uninstall && !status.installed && !status.running)
    return false;
  return std::nullopt;
}

FullControlManager::FullControlManager(QObject *parent) : QObject(parent)
{
  m_poll.setInterval(200);
  m_queryTimeout.setSingleShot(true);
  m_queryTimeout.setInterval(15000);
  connect(&m_queryTimeout, &QTimer::timeout, this, [this] { m_query.kill(); });
  connect(&m_query, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
    m_queryTimeout.stop();
    const bool querySucceeded = exit == QProcess::NormalExit && code == 0;
    m_status = parseStatus(m_query.readAllStandardOutput(), protectedDirectory(QCoreApplication::applicationDirPath()));
    if (!querySucceeded) {
      m_status.valid = false;
      m_status.error = tr("Could not read the full control component status (exit code %1).").arg(code);
    }
    Q_EMIT statusChanged();
    if (m_completingAction) {
      const bool stopped = *m_action == Action::Stop && m_status.valid && m_status.installed && !m_status.running;
      const bool applied = *m_action == Action::Apply && m_status.valid && m_status.installed;
      const bool confirmed = confirmedMode(*m_action, true, m_status).has_value() || stopped || applied;
      finish(
          confirmed,
          confirmed ? QString() : tr("The requested component state could not be confirmed. %1").arg(m_status.error)
      );
    } else {
      setBusy(false);
    }
  });
  connect(&m_query, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    m_queryTimeout.stop();
    m_status = {};
    m_status.error = tr("Could not start the component status query.");
    Q_EMIT statusChanged();
    if (m_completingAction)
      finish(false, m_status.error);
    else
      setBusy(false);
  });
#ifdef Q_OS_WIN
  m_query.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
    args->flags |= CREATE_NO_WINDOW;
  });
  connect(&m_poll, &QTimer::timeout, this, [this] {
    const auto wait = WaitForSingleObject(m_processHandle, 0);
    if (wait == WAIT_TIMEOUT)
      return;
    m_poll.stop();
    DWORD code = 1;
    const bool succeeded = wait == WAIT_OBJECT_0 && GetExitCodeProcess(m_processHandle, &code) && code == 0;
    CloseHandle(m_processHandle);
    m_processHandle = nullptr;
    if (!succeeded) {
      finish(false, tr("The component action failed (exit code %1). Your selected mode was preserved.").arg(code));
      refresh();
      return;
    }
    readStatus(true);
  });
#endif
}

FullControlManager::~FullControlManager()
{
#ifdef Q_OS_WIN
  if (m_processHandle)
    CloseHandle(m_processHandle);
#endif
}

QString FullControlManager::powerShellPath() const
{
#ifdef Q_OS_WIN
  wchar_t buffer[MAX_PATH + 1] = {};
  const auto length = GetSystemDirectoryW(buffer, MAX_PATH);
  if (length > 0 && length < MAX_PATH)
    return QString::fromWCharArray(buffer, length) + "/WindowsPowerShell/v1.0/powershell.exe";
#endif
  return {};
}

QString FullControlManager::scriptPath(Action action) const
{
  const auto directory = protectedDirectory(QCoreApplication::applicationDirPath());
  const auto protectedScript = directory + "/full-control-manager.ps1";
  // Once installed, all privileged actions use the protected copy, including updates.
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(directory).toStdWString();
  const auto attributes = GetFileAttributesW(native.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return isProtectedDirectory(directory) ? protectedScript : QString();
  const auto error = GetLastError();
  if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
    return {};
#endif
  if (action == Action::Install || action == Action::Status)
    return QCoreApplication::applicationDirPath() + "/full-control-manager.ps1";
  return {};
}

void FullControlManager::setBusy(bool busy)
{
  if (m_busy == busy)
    return;
  m_busy = busy;
  Q_EMIT busyChanged(busy);
}

void FullControlManager::refresh()
{
  if (!m_busy)
    readStatus(false);
}

void FullControlManager::readStatus(bool completingAction)
{
  m_completingAction = completingAction;
  const auto script = scriptPath(Action::Status);
  const auto args = arguments(Action::Status, script, QCoreApplication::applicationDirPath(), Settings::settingsFile());
  if (!QFileInfo::exists(script) || args.isEmpty() || powerShellPath().isEmpty()) {
    m_status = {};
    m_status.error = tr(
        "The full control manager is missing, its path is invalid, or its protected permissions could not be verified."
    );
    Q_EMIT statusChanged();
    if (completingAction)
      finish(false, m_status.error);
    return;
  }
  setBusy(true);
  m_queryTimeout.start();
  m_query.start(powerShellPath(), args);
}

void FullControlManager::run(Action action)
{
  if (m_busy || action == Action::Status)
    return;
  m_action = action;
  const auto script = scriptPath(action);
  const auto args = arguments(action, script, QCoreApplication::applicationDirPath(), Settings::settingsFile());
  const auto parameters = quoteArguments(args);
  const auto executable = QDir::toNativeSeparators(powerShellPath()).toStdWString();
  if (!QFileInfo::exists(script) || args.isEmpty() || parameters.isEmpty() || executable.empty()) {
    finish(
        false, tr("The full control manager is missing, its path is invalid, or its protected permissions could not be "
                  "verified.")
    );
    return;
  }
  setBusy(true);
#ifdef Q_OS_WIN
  const auto commandLine = parameters.toStdWString();
  const auto workingDirectory = QDir::toNativeSeparators(QFileInfo(script).absolutePath()).toStdWString();
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"runas";
  info.lpFile = executable.c_str();
  info.lpParameters = commandLine.c_str();
  info.lpDirectory = workingDirectory.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info)) {
    const auto error = GetLastError();
    finish(
        false, error == ERROR_CANCELLED
                   ? tr("Administrator confirmation was cancelled. Your selected mode was preserved.")
                   : tr("Could not launch the component manager (Windows error %1).").arg(error)
    );
    return;
  }
  m_processHandle = info.hProcess;
  m_poll.start();
#else
  finish(false, tr("Full control is available on Windows only."));
#endif
}

void FullControlManager::finish(bool succeeded, const QString &error)
{
  const auto action = m_action.value_or(Action::Status);
  if (const auto mode = confirmedMode(action, succeeded, m_status)) {
    Settings::setValue(Settings::Gui::FullControlEnabled, *mode);
    if (!*mode)
      Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
    Settings::save(false);
  }
  m_action.reset();
  m_completingAction = false;
  setBusy(false);
  Q_EMIT finished(action, succeeded, error);
}

} // namespace deskflow::gui
