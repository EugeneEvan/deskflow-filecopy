/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CacheStatusWidgetTests.h"
#include "common/Settings.h"
#include "gui/widgets/CacheStatusWidget.h"

#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QScopeGuard>

void CacheStatusWidgetTests::initTestCase()
{
  QVERIFY(m_temp.isValid());
  qputenv("XDG_STATE_HOME", m_temp.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", m_temp.path().toUtf8());
#ifdef Q_OS_WIN
  // Settings discovers its initial file before setSettingsFile can redirect it.
  // This test has its own executable directory; never touch a pre-existing seed.
  const auto seedPath = QCoreApplication::applicationDirPath() + "/settings/Deskflow.conf";
  QVERIFY(QDir().mkpath(QFileInfo(seedPath).absolutePath()));
  QFile seed(seedPath);
  QVERIFY2(seed.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(seed.errorString()));
  const QByteArray seedContents("[core]\ncomputerName=cache-widget-test\n");
  QCOMPARE(seed.write(seedContents), qint64(seedContents.size()));
  seed.close();
#endif
  Settings::setSettingsFile(m_temp.filePath("settings.conf"));
  Settings::setStateFile(m_temp.filePath("state.conf"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#ifdef Q_OS_WIN
  QVERIFY(QFile::remove(seedPath));
#endif
}

void CacheStatusWidgetTests::folderChangeDiscardsStaleScan()
{
  const auto first = m_temp.filePath("first");
  const auto second = m_temp.filePath("second");
  QVERIFY(QDir().mkpath(first));
  QVERIFY(QDir().mkpath(second));
  QFile payload(QDir(second).filePath("payload"));
  QVERIFY(payload.open(QIODevice::WriteOnly));
  QCOMPARE(payload.write(QByteArray(2048, 'x')), qint64(2048));
  payload.close();
  Settings::setValue(Settings::Core::FileTransferCachePath, first);
  Settings::setValue(Settings::Core::FileTransferCacheLimitGiB, 20);
  deskflow::gui::CacheStatusWidget widget;
  // Replace the configured path before the first result can be delivered to the UI.
  Settings::setValue(Settings::Core::FileTransferCachePath, second);
  Settings::setValue(Settings::Core::FileTransferCacheLimitGiB, 7);
  widget.refresh();
  const auto *folder = widget.findChild<QLabel *>("cacheFolder");
  const auto *summary = widget.findChild<QLabel *>("cacheSummary");
  QVERIFY(folder);
  QVERIFY(summary);
  QTRY_COMPARE(folder->text(), QDir::toNativeSeparators(second));
  QTRY_COMPARE(summary->text(), QString("%1 used / 7 GiB limit").arg(QLocale().formattedDataSize(2048)));
}

void CacheStatusWidgetTests::invalidFolderShowsError()
{
  Settings::setValue(Settings::Core::FileTransferCachePath, QDir::rootPath());
  deskflow::gui::CacheStatusWidget widget;
  const auto *summary = widget.findChild<QLabel *>("cacheSummary");
  QVERIFY(summary);
  QTRY_VERIFY(summary->text().startsWith("Cannot read cache usage:"));
  QVERIFY(!summary->text().contains(" used / "));
}

void CacheStatusWidgetTests::unavailableDefaultFolderShowsError()
{
#ifdef Q_OS_WIN
  const auto previous = qgetenv("LOCALAPPDATA");
  const bool existed = qEnvironmentVariableIsSet("LOCALAPPDATA");
  const auto restore = qScopeGuard([&] {
    if (existed)
      qputenv("LOCALAPPDATA", previous);
    else
      qunsetenv("LOCALAPPDATA");
  });
  qunsetenv("LOCALAPPDATA");
  Settings::setValue(Settings::Core::FileTransferCachePath, QString());
  deskflow::gui::CacheStatusWidget widget;
  const auto *summary = widget.findChild<QLabel *>("cacheSummary");
  QVERIFY(summary);
  QVERIFY(summary->text().startsWith("Cannot read cache usage:"));
#else
  QSKIP("LOCALAPPDATA is a Windows setting");
#endif
}

QTEST_MAIN(CacheStatusWidgetTests)
