/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsDialogTests.h"

#include "common/I18N.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "gui/WindowsLoginStartup.h"
#include "gui/dialogs/FileTransferCacheDialog.h"
#include "gui/dialogs/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>

deskflow::gui::WindowsLoginStartup SettingsDialogTests::isolatedStartup() const
{
  return {
      m_temp.filePath(QStringLiteral("settings-dialog-startup")),
      m_temp.filePath(QStringLiteral("settings-dialog-app/deskflow.exe"))
  };
}

void SettingsDialogTests::initTestCase()
{
  QVERIFY(m_temp.isValid());
  QVERIFY(QDir().mkpath(m_temp.filePath(QStringLiteral("settings-dialog-startup"))));
  // Settings inspects a portable file before setSettingsFile can redirect it. The test executable
  // has its own output directory; never overwrite a pre-existing configuration there.
  m_seedFile = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("settings/Deskflow.conf"));
  QVERIFY(!QFile::exists(m_seedFile));
  QVERIFY(QDir().mkpath(QFileInfo(m_seedFile).absolutePath()));
  QFile seed(m_seedFile);
  QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  m_seedCreated = true;
  seed.close();
  m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
  m_previousStateHome = qgetenv("XDG_STATE_HOME");
  qputenv("XDG_CONFIG_HOME", m_temp.filePath(QStringLiteral("initial-config")).toUtf8());
  qputenv("XDG_STATE_HOME", m_temp.filePath(QStringLiteral("initial-state")).toUtf8());
  Settings::setSettingsFile(m_temp.filePath(QStringLiteral("Deskflow.conf")));
  Settings::setStateFile(m_temp.filePath(QStringLiteral("state.conf")));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void SettingsDialogTests::init()
{
  Settings::restoreDefaultSettings();
  Settings::setValue(Settings::Core::Language, QStringLiteral("en"));
  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
  Settings::setValue(Settings::Core::FileTransferCachePath, m_temp.filePath(QStringLiteral("original-cache")));
  I18N::setLanguage(QStringLiteral("en"));
  QCOMPARE(I18N::currentLanguage(), QStringLiteral("en"));
}

void SettingsDialogTests::cleanupTestCase()
{
  if (!m_seedCreated)
    return;
  Settings::save(false);
  if (m_previousConfigHome.isNull())
    qunsetenv("XDG_CONFIG_HOME");
  else
    qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
  if (m_previousStateHome.isNull())
    qunsetenv("XDG_STATE_HOME");
  else
    qputenv("XDG_STATE_HOME", m_previousStateHome);
  QVERIFY(QFile::remove(m_seedFile));
  QDir().rmdir(QFileInfo(m_seedFile).absolutePath());
}

void SettingsDialogTests::transferShortcutSelectsSupportedPage()
{
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
  QVERIFY(tabs);
  QCOMPARE(tabs->count(), 4);
  QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("tabNetwork"));
  dialog.selectFileTransferTab();
  const auto supported = deskflow::platform::isWindows();
  QCOMPARE(tabs->isTabVisible(1), supported);
  QCOMPARE(
      tabs->currentWidget()->objectName(), supported ? QStringLiteral("tabFileTransfer") : QStringLiteral("tabNetwork")
  );
  QVERIFY(dialog.maximumHeight() > dialog.height());
  QVERIFY(dialog.maximumWidth() > dialog.width());
}

void SettingsDialogTests::serviceTogglePreservesStagedTls()
{
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Security::KeySize, 2048);
  const auto storedCertificate = Settings::value(Settings::Security::Certificate).toString();
  const auto stagedCertificate = m_temp.filePath(QStringLiteral("staged.pem"));
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *service = dialog.findChild<QGroupBox *>(QStringLiteral("groupService"));
  auto *tls = dialog.findChild<QGroupBox *>(QStringLiteral("groupSecurity"));
  auto *keySize = dialog.findChild<QComboBox *>(QStringLiteral("comboTlsKeyLength"));
  auto *certificate = dialog.findChild<QLineEdit *>(QStringLiteral("lineTlsCertPath"));
  QVERIFY(service && tls && keySize && certificate);
  keySize->setCurrentText(QStringLiteral("4096"));
  certificate->setText(stagedCertificate);
  tls->setChecked(false);
  service->setChecked(true);
  service->setChecked(false);
  QCOMPARE(keySize->currentText(), QStringLiteral("4096"));
  QCOMPARE(certificate->text(), stagedCertificate);
  QVERIFY(!tls->isChecked());
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 2048);
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), storedCertificate);
  QVERIFY(Settings::value(Settings::Security::TlsEnabled).toBool());
}

void SettingsDialogTests::restoreDefaultsKeepsWindowChoicesIndependent()
{
  const auto defaultHide = Settings::defaultValue(Settings::Gui::Autohide).toBool();
  const auto defaultClose = Settings::defaultValue(Settings::Gui::CloseToTray).toBool();
  Settings::setValue(Settings::Gui::Autohide, !defaultHide);
  Settings::setValue(Settings::Gui::CloseToTray, !defaultClose);
  Settings::setValue(Settings::Security::KeySize, 4096);
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  auto *autoHide = dialog.findChild<QRadioButton *>(QStringLiteral("rbAutoHide"));
  auto *closeToTray = dialog.findChild<QRadioButton *>(QStringLiteral("rbCloseToTray"));
  auto *keySize = dialog.findChild<QComboBox *>(QStringLiteral("comboTlsKeyLength"));
  QVERIFY(buttons && autoHide && closeToTray && keySize);
  buttons->button(QDialogButtonBox::RestoreDefaults)->click();
  QCOMPARE(autoHide->isChecked(), defaultHide);
  QCOMPARE(closeToTray->isChecked(), defaultClose);
  QCOMPARE(keySize->currentText().toInt(), Settings::defaultValue(Settings::Security::KeySize).toInt());
  QCOMPARE(Settings::value(Settings::Gui::Autohide).toBool(), !defaultHide);
  QCOMPARE(Settings::value(Settings::Gui::CloseToTray).toBool(), !defaultClose);
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 4096);
}

void SettingsDialogTests::resetRestoresStoredPreferences_data()
{
  QTest::addColumn<QString>("storedLogLevel");
  QTest::newRow("default-enum-name") << QStringLiteral("Info");
  QTest::newRow("normalized-option") << QStringLiteral("INFO");
}

void SettingsDialogTests::resetRestoresStoredPreferences()
{
  QFETCH(QString, storedLogLevel);
  Settings::setValue(Settings::Log::Level, storedLogLevel);
  Settings::setValue(Settings::Security::KeySize, 2048);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Gui::Autohide, true);
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  auto *autoHide = dialog.findChild<QRadioButton *>(QStringLiteral("rbAutoHide"));
  auto *show = dialog.findChild<QRadioButton *>(QStringLiteral("rbShowOnStart"));
  auto *tls = dialog.findChild<QGroupBox *>(QStringLiteral("groupSecurity"));
  auto *keySize = dialog.findChild<QComboBox *>(QStringLiteral("comboTlsKeyLength"));
  QVERIFY(buttons && autoHide && show && tls && keySize);
  QVERIFY(!buttons->button(QDialogButtonBox::Save)->isEnabled());
  show->setChecked(true);
  tls->setChecked(false);
  keySize->setCurrentText(QStringLiteral("4096"));
  QVERIFY(buttons->button(QDialogButtonBox::Reset)->isEnabled());
  buttons->button(QDialogButtonBox::Reset)->click();
  QVERIFY(autoHide->isChecked());
  QVERIFY(tls->isChecked());
  QCOMPARE(keySize->currentText(), QStringLiteral("2048"));
  QVERIFY(!buttons->button(QDialogButtonBox::Save)->isEnabled());
}

void SettingsDialogTests::cancelDoesNotSaveEdits()
{
  Settings::setValue(Settings::Security::KeySize, 2048);
  Settings::setValue(Settings::Gui::Autohide, false);
  const auto originalLanguage = Settings::value(Settings::Core::Language).toString();
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  auto *autoHide = dialog.findChild<QRadioButton *>(QStringLiteral("rbAutoHide"));
  auto *keySize = dialog.findChild<QComboBox *>(QStringLiteral("comboTlsKeyLength"));
  auto *language = dialog.findChild<QComboBox *>(QStringLiteral("comboLanguage"));
  QVERIFY(buttons && autoHide && keySize && language);
  QVERIFY(language->count() > 1);
  const auto chinese = I18N::toNativeName(QStringLiteral("zh_CN"));
  QVERIFY(!chinese.isEmpty());
  QVERIFY(language->findText(chinese) >= 0);
  autoHide->setChecked(true);
  keySize->setCurrentText(QStringLiteral("4096"));
  language->setCurrentText(chinese);
  QCOMPARE(I18N::nativeTo639Name(language->currentText()), QStringLiteral("zh_CN"));
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), originalLanguage);
  QCOMPARE(I18N::currentLanguage(), originalLanguage);
  buttons->button(QDialogButtonBox::Cancel)->click();
  QCOMPARE(dialog.result(), QDialog::Rejected);
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 2048);
  QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), originalLanguage);
  QCOMPARE(I18N::currentLanguage(), originalLanguage);
}

void SettingsDialogTests::saveAppliesStagedPreferences()
{
  Settings::setValue(Settings::Security::KeySize, 2048);
  Settings::setValue(Settings::Gui::Autohide, false);
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  auto *autoHide = dialog.findChild<QRadioButton *>(QStringLiteral("rbAutoHide"));
  auto *keySize = dialog.findChild<QComboBox *>(QStringLiteral("comboTlsKeyLength"));
  QVERIFY(buttons && autoHide && keySize);
  autoHide->setChecked(true);
  keySize->setCurrentText(QStringLiteral("4096"));
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 2048);
  buttons->button(QDialogButtonBox::Save)->click();
  QCOMPARE(dialog.result(), QDialog::Accepted);
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 4096);
  QVERIFY(Settings::value(Settings::Gui::Autohide).toBool());
}

void SettingsDialogTests::saveLanguageAppliesTranslation()
{
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  auto *language = dialog.findChild<QComboBox *>(QStringLiteral("comboLanguage"));
  QVERIFY(buttons && language);
  QVERIFY(language->count() > 1);
  const auto chinese = I18N::toNativeName(QStringLiteral("zh_CN"));
  QVERIFY(!chinese.isEmpty());
  QVERIFY(language->findText(chinese) >= 0);
  language->setCurrentText(chinese);
  QCOMPARE(I18N::nativeTo639Name(language->currentText()), QStringLiteral("zh_CN"));
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), QStringLiteral("en"));
  QCOMPARE(I18N::currentLanguage(), QStringLiteral("en"));
  QVERIFY(buttons->button(QDialogButtonBox::Save)->isEnabled());
  buttons->button(QDialogButtonBox::Save)->click();
  QCOMPARE(dialog.result(), QDialog::Accepted);
  QCOMPARE(Settings::value(Settings::Core::Language).toString(), QStringLiteral("zh_CN"));
  QCOMPARE(I18N::currentLanguage(), QStringLiteral("zh_CN"));
  QTRY_VERIFY(dialog.windowTitle() != QStringLiteral("Preferences"));
  QCOMPARE(dialog.windowTitle(), QCoreApplication::translate("SettingsDialog", "Preferences"));
}

void SettingsDialogTests::cacheEditsAreStagedUntilSave_data()
{
  QTest::addColumn<bool>("save");
  QTest::newRow("cancel") << false;
  QTest::newRow("save") << true;
}

void SettingsDialogTests::cacheEditsAreStagedUntilSave()
{
  if (!deskflow::platform::isWindows())
    QSKIP("The file cache settings page is only available on Windows.");
  QFETCH(bool, save);
  const auto originalPath = Settings::value(Settings::Core::FileTransferCachePath).toString();
  const auto originalLimit = Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt();
  const auto chosenPath = m_temp.filePath(QStringLiteral("chosen-cache"));
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *cacheButton = dialog.findChild<QPushButton *>(QStringLiteral("btnFileCache"));
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(cacheButton && buttons);
  bool cacheAccepted = false;
  QTimer selectCache;
  selectCache.setInterval(10);
  connect(&selectCache, &QTimer::timeout, &dialog, [&] {
    auto *cacheDialog = dialog.findChild<FileTransferCacheDialog *>();
    if (!cacheDialog)
      return;
    auto *path = cacheDialog->findChild<QLineEdit *>(QStringLiteral("fileCachePath"));
    auto *limit = cacheDialog->findChild<QSpinBox *>(QStringLiteral("fileCacheLimitGiB"));
    auto *cacheButtons = cacheDialog->findChild<QDialogButtonBox *>();
    if (!path || !limit || !cacheButtons || !cacheButtons->button(QDialogButtonBox::Ok)->isEnabled())
      return;
    path->setText(chosenPath);
    limit->setValue(37);
    cacheButtons->button(QDialogButtonBox::Ok)->click();
    cacheAccepted = cacheDialog->result() == QDialog::Accepted;
    selectCache.stop();
  });
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &dialog, [&] {
    if (auto *cacheDialog = dialog.findChild<FileTransferCacheDialog *>())
      cacheDialog->reject();
  });
  selectCache.start();
  timeout.start(5000);
  cacheButton->click();
  selectCache.stop();
  timeout.stop();
  QVERIFY(cacheAccepted);
  QCOMPARE(Settings::value(Settings::Core::FileTransferCachePath).toString(), originalPath);
  QCOMPARE(Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt(), originalLimit);
  buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel)->click();
  QCOMPARE(Settings::value(Settings::Core::FileTransferCachePath).toString(), save ? chosenPath : originalPath);
  QCOMPARE(Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt(), save ? 37 : originalLimit);
}

void SettingsDialogTests::loginStartupOptionAppearsOnWindows()
{
  const ServerConfig config;
  SettingsDialog dialog(nullptr, config, isolatedStartup());
  auto *startup = dialog.findChild<QCheckBox *>(QStringLiteral("cbStartAtLogin"));
  QVERIFY(startup);
  QCOMPARE(!startup->isHidden(), deskflow::platform::isWindows());
}

void SettingsDialogTests::loginStartupShortcutUsesIsolatedFolder()
{
  if (!deskflow::platform::isWindows())
    QSKIP("Windows shortcuts are only available on Windows.");
  const auto folder = m_temp.filePath(QStringLiteral("isolated-startup"));
  const auto appDir = m_temp.filePath(QStringLiteral("portable-app"));
  QVERIFY(QDir().mkpath(folder));
  QVERIFY(QDir().mkpath(appDir));
  const auto executable = QDir(appDir).filePath(QStringLiteral("deskflow.exe"));
  QFile binary(executable);
  QVERIFY(binary.open(QIODevice::WriteOnly));
  binary.close();
  const deskflow::gui::WindowsLoginStartup startup(folder, executable);
  QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
  QString error;
  QVERIFY2(startup.setEnabled(true, &error), qPrintable(error));
  QVERIFY2(startup.state(&error) == deskflow::gui::WindowsLoginStartup::State::Enabled, qPrintable(error));
  QVERIFY(QFileInfo(QDir(folder).filePath(QStringLiteral("Deskflow FileCopy.lnk"))).isFile());
  QVERIFY(QFile::remove(executable));
  QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Stale);
  QVERIFY2(startup.setEnabled(false, &error), qPrintable(error));
  QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
}

void SettingsDialogTests::loginStartupRejectsForeignShortcut()
{
  if (!deskflow::platform::isWindows())
    QSKIP("Windows shortcuts are only available on Windows.");
  const auto folder = m_temp.filePath(QStringLiteral("foreign-startup"));
  QVERIFY(QDir().mkpath(folder));
  const auto shortcut = QDir(folder).filePath(QStringLiteral("Deskflow FileCopy.lnk"));
  QFile foreign(shortcut);
  QVERIFY(foreign.open(QIODevice::WriteOnly));
  QVERIFY(foreign.write("foreign shortcut") > 0);
  foreign.close();
  const deskflow::gui::WindowsLoginStartup startup(folder, QCoreApplication::applicationFilePath());
  QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Conflict);
  QString error;
  QVERIFY(!startup.setEnabled(false, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(foreign.open(QIODevice::ReadOnly));
  QCOMPARE(foreign.readAll(), QByteArray("foreign shortcut"));
}

void SettingsDialogTests::loginStartupEditsAreStagedUntilSave()
{
  if (!deskflow::platform::isWindows())
    QSKIP("Windows shortcuts are only available on Windows.");
  const auto folder = m_temp.filePath(QStringLiteral("dialog-startup"));
  const auto appDir = m_temp.filePath(QStringLiteral("dialog-app"));
  QVERIFY(QDir().mkpath(folder));
  QVERIFY(QDir().mkpath(appDir));
  const auto executable = QDir(appDir).filePath(QStringLiteral("deskflow.exe"));
  QFile binary(executable);
  QVERIFY(binary.open(QIODevice::WriteOnly));
  binary.close();
  const deskflow::gui::WindowsLoginStartup startup(folder, executable);
  const ServerConfig config;
  {
    SettingsDialog dialog(nullptr, config, startup);
    auto *option = dialog.findChild<QCheckBox *>(QStringLiteral("cbStartAtLogin"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(option && buttons);
    QVERIFY(!option->isChecked());
    option->setChecked(true);
    QVERIFY(buttons->button(QDialogButtonBox::Save)->isEnabled());
    QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
    buttons->button(QDialogButtonBox::Reset)->click();
    QVERIFY(!option->isChecked());
    option->setChecked(true);
    buttons->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
  }
  {
    SettingsDialog dialog(nullptr, config, startup);
    auto *option = dialog.findChild<QCheckBox *>(QStringLiteral("cbStartAtLogin"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(option && buttons);
    option->setChecked(true);
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Enabled);
  }
  {
    SettingsDialog dialog(nullptr, config, startup);
    auto *option = dialog.findChild<QCheckBox *>(QStringLiteral("cbStartAtLogin"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(option && buttons);
    QVERIFY(option->isChecked());
    option->setChecked(false);
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(startup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
  }
}

void SettingsDialogTests::loginStartupRepairsOldInstallation()
{
  if (!deskflow::platform::isWindows())
    QSKIP("Windows shortcuts are only available on Windows.");
  const auto folder = m_temp.filePath(QStringLiteral("missing-startup-folder"));
  const auto oldDir = m_temp.filePath(QStringLiteral("old-install"));
  const auto newDir = m_temp.filePath(QStringLiteral("new-install"));
  QVERIFY(QDir().mkpath(oldDir));
  QVERIFY(QDir().mkpath(newDir));
  const auto oldExe = QDir(oldDir).filePath(QStringLiteral("deskflow.exe"));
  const auto newExe = QDir(newDir).filePath(QStringLiteral("deskflow.exe"));
  QFile oldBinary(oldExe);
  QVERIFY(oldBinary.open(QIODevice::WriteOnly));
  oldBinary.close();
  QFile newBinary(newExe);
  QVERIFY(newBinary.open(QIODevice::WriteOnly));
  newBinary.close();
  QFile marker(QDir(oldDir).filePath(QStringLiteral("deskflow-filecopy.package")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  QVERIFY(marker.write("Deskflow FileCopy\n") > 0);
  marker.close();
  QFile newMarker(QDir(newDir).filePath(QStringLiteral("deskflow-filecopy.package")));
  QVERIFY(newMarker.open(QIODevice::WriteOnly));
  QVERIFY(newMarker.write("Deskflow FileCopy\n") > 0);
  newMarker.close();
  const deskflow::gui::WindowsLoginStartup oldStartup(folder, oldExe);
  const deskflow::gui::WindowsLoginStartup newStartup(folder, newExe);
  QCOMPARE(oldStartup.state(), deskflow::gui::WindowsLoginStartup::State::Disabled);
  QString error;
  QVERIFY2(oldStartup.setEnabled(true, &error), qPrintable(error));
  QCOMPARE(newStartup.state(), deskflow::gui::WindowsLoginStartup::State::OtherInstallation);
  {
    const ServerConfig config;
    SettingsDialog dialog(nullptr, config, newStartup);
    auto *option = dialog.findChild<QCheckBox *>(QStringLiteral("cbStartAtLogin"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(option && buttons);
    QVERIFY(option->isChecked());
    QVERIFY(buttons->button(QDialogButtonBox::Save)->isEnabled());
    buttons->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(newStartup.state(), deskflow::gui::WindowsLoginStartup::State::Enabled);
  }
  QVERIFY2(oldStartup.setEnabled(true, &error), qPrintable(error));
  QVERIFY(QFile::remove(oldExe));
  QCOMPARE(newStartup.state(), deskflow::gui::WindowsLoginStartup::State::Stale);
  QVERIFY2(newStartup.setEnabled(true, &error), qPrintable(error));
  QCOMPARE(newStartup.state(), deskflow::gui::WindowsLoginStartup::State::Enabled);
}

QTEST_MAIN(SettingsDialogTests)
