/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "WindowsLoginStartup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#ifdef Q_OS_WIN
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <Windows.h>

#include <memory>
#endif

namespace deskflow::gui {

namespace {
constexpr auto kShortcutName = "Deskflow FileCopy.lnk";

#ifdef Q_OS_WIN
struct ReleaseCom
{
  template <typename T> void operator()(T *object) const
  {
    if (object)
      object->Release();
  }
};

struct ComApartment
{
  HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ~ComApartment()
  {
    if (SUCCEEDED(result))
      CoUninitialize();
  }
  bool available() const
  {
    return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
  }
};

QString shortcutTarget(const QString &shortcut, QString *error)
{
  ComApartment apartment;
  if (!apartment.available()) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not initialize Windows shortcuts.");
    return {};
  }
  IShellLinkW *rawLink = nullptr;
  if (FAILED(CoCreateInstance(
          CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&rawLink)
      ))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not open the login startup shortcut.");
    return {};
  }
  std::unique_ptr<IShellLinkW, ReleaseCom> link(rawLink);
  IPersistFile *rawFile = nullptr;
  if (FAILED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&rawFile)))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not open the login startup shortcut.");
    return {};
  }
  std::unique_ptr<IPersistFile, ReleaseCom> file(rawFile);
  if (FAILED(file->Load(reinterpret_cast<LPCOLESTR>(shortcut.utf16()), STGM_READ))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut cannot be read.");
    return {};
  }
  wchar_t arguments[2]{};
  if (FAILED(link->GetArguments(arguments, 2)) || arguments[0] != L'\0') {
    *error =
        QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut belongs to another program.");
    return {};
  }
  wchar_t target[32768]{};
  if (FAILED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)) || target[0] == L'\0') {
    *error = QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut cannot be read.");
    return {};
  }
  return QString::fromWCharArray(target);
}

bool writeShortcut(const QString &shortcut, const QString &executable, QString *error)
{
  ComApartment apartment;
  if (!apartment.available()) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not initialize Windows shortcuts.");
    return false;
  }
  IShellLinkW *rawLink = nullptr;
  if (FAILED(CoCreateInstance(
          CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&rawLink)
      ))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not create the login startup shortcut.");
    return false;
  }
  std::unique_ptr<IShellLinkW, ReleaseCom> link(rawLink);
  const auto directory = QFileInfo(executable).absolutePath();
  if (FAILED(link->SetPath(reinterpret_cast<LPCWSTR>(executable.utf16()))) ||
      FAILED(link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(directory.utf16())))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not create the login startup shortcut.");
    return false;
  }
  IPersistFile *rawFile = nullptr;
  if (FAILED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&rawFile)))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not create the login startup shortcut.");
    return false;
  }
  std::unique_ptr<IPersistFile, ReleaseCom> file(rawFile);
  const auto temporary =
      shortcut + QLatin1Char('.') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".tmp");
  if (FAILED(file->Save(reinterpret_cast<LPCOLESTR>(temporary.utf16()), TRUE))) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not write the login startup shortcut.");
    return false;
  }
  if (!MoveFileExW(
          reinterpret_cast<LPCWSTR>(temporary.utf16()), reinterpret_cast<LPCWSTR>(shortcut.utf16()),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
      )) {
    QFile::remove(temporary);
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not write the login startup shortcut.");
    return false;
  }
  return true;
}
#endif
} // namespace

WindowsLoginStartup::WindowsLoginStartup(QString startupDirectory, QString executablePath)
    : m_startupDirectory(std::move(startupDirectory)),
      m_executablePath(std::move(executablePath))
{
}

WindowsLoginStartup WindowsLoginStartup::current()
{
#ifdef Q_OS_WIN
  PWSTR startup = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &startup)))
    return {{}, QCoreApplication::applicationFilePath()};
  const QString directory = QString::fromWCharArray(startup);
  CoTaskMemFree(startup);
  return {directory, QCoreApplication::applicationFilePath()};
#else
  return {{}, QCoreApplication::applicationFilePath()};
#endif
}

QString WindowsLoginStartup::shortcutPath() const
{
  return QDir(m_startupDirectory).filePath(QString::fromLatin1(kShortcutName));
}

WindowsLoginStartup::State WindowsLoginStartup::state(QString *error) const
{
  QString localError;
  if (!error)
    error = &localError;
  error->clear();
#ifndef Q_OS_WIN
  return State::Disabled;
#else
  if (m_startupDirectory.isEmpty()) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not locate the Windows login startup folder.");
    return State::Error;
  }
  // QFileInfo treats .lnk files as symbolic links and reports broken targets as absent.
  const auto path = shortcutPath();
  const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
  if (attributes == INVALID_FILE_ATTRIBUTES &&
      (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
    return State::Disabled;
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut cannot be read.");
    return State::Error;
  }
  if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
    *error =
        QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut belongs to another program.");
    return State::Conflict;
  }
  const auto target = shortcutTarget(path, error);
  if (!error->isEmpty())
    return State::Conflict;
  const QFileInfo targetInfo(target);
  if (targetInfo.fileName().compare(QStringLiteral("deskflow.exe"), Qt::CaseInsensitive) != 0) {
    *error =
        QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut belongs to another program.");
    return State::Conflict;
  }
  const bool currentTarget =
      QDir::cleanPath(QDir::fromNativeSeparators(target))
          .compare(QDir::cleanPath(QDir::fromNativeSeparators(m_executablePath)), Qt::CaseInsensitive) == 0;
  QFile marker(targetInfo.dir().filePath(QStringLiteral("deskflow-filecopy.package")));
  const bool branded = marker.open(QIODevice::ReadOnly) && marker.readLine().trimmed() == "Deskflow FileCopy";
  if (!currentTarget && !branded) {
    *error =
        QCoreApplication::translate("WindowsLoginStartup", "The login startup shortcut belongs to another program.");
    return State::Conflict;
  }
  if (!targetInfo.isFile())
    return State::Stale;
  return currentTarget ? State::Enabled : State::OtherInstallation;
#endif
}

bool WindowsLoginStartup::setEnabled(bool enabled, QString *error) const
{
  QString localError;
  if (!error)
    error = &localError;
  const auto currentState = state(error);
  if (currentState == State::Conflict || currentState == State::Error)
    return false;
  if (!enabled) {
    if (currentState == State::Disabled)
      return true;
    if (!QFile::remove(shortcutPath())) {
      *error = QCoreApplication::translate("WindowsLoginStartup", "Could not remove the login startup shortcut.");
      return false;
    }
    return true;
  }
#ifdef Q_OS_WIN
  if (!QFileInfo(m_executablePath).isFile() || !QDir().mkpath(m_startupDirectory)) {
    *error = QCoreApplication::translate("WindowsLoginStartup", "Could not write the login startup shortcut.");
    return false;
  }
  return writeShortcut(shortcutPath(), m_executablePath, error);
#else
  *error = QCoreApplication::translate("WindowsLoginStartup", "Login startup is only available on Windows.");
  return false;
#endif
}

} // namespace deskflow::gui
