/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreProcessEndpointsTests.h"
#include "common/Settings.h"
#include "gui/core/CoreProcess.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>

using deskflow::gui::CoreProcess;

namespace {
const QString kEndpoints = QStringLiteral(
    R"([{"name":"client=a","localAddress":"192.0.2.1","peerAddress":"192.0.2.2"},)"
    R"({"name":"","localAddress":"fe80::1%3","peerAddress":"fe80::2%3"}])"
);

bool command(CoreProcess &process, const QString &name, const QString &args)
{
  return QMetaObject::invokeMethod(
      &process, "onCoreIpcMessageReceived", Qt::DirectConnection, Q_ARG(QString, name), Q_ARG(QString, args)
  );
}
} // namespace

void CoreProcessEndpointsTests::initTestCase()
{
  QVERIFY(m_temp.isValid());
  m_seedFile = QDir(QCoreApplication::applicationDirPath()).filePath("settings/Deskflow.conf");
  QVERIFY(!QFile::exists(m_seedFile));
  QVERIFY(QDir().mkpath(QFileInfo(m_seedFile).absolutePath()));
  QFile seed(m_seedFile);
  QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  m_seedCreated = true;
  seed.close();
  m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
  m_previousStateHome = qgetenv("XDG_STATE_HOME");
  qputenv("XDG_CONFIG_HOME", m_temp.filePath("initial-config").toUtf8());
  qputenv("XDG_STATE_HOME", m_temp.filePath("initial-state").toUtf8());
  Settings::setSettingsFile(m_temp.filePath("Deskflow.conf"));
  Settings::setStateFile(m_temp.filePath("state.conf"));
  // Dispose the original QSettings before removing its portable seed.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void CoreProcessEndpointsTests::cleanupTestCase()
{
  if (m_seedCreated) {
    Settings::save(false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
}

void CoreProcessEndpointsTests::snapshotSupportsBothFamilies()
{
  const ServerConfig config;
  CoreProcess process(config);
  QSignalSpy spy(&process, &CoreProcess::connectionEndpointsChanged);
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QCOMPARE(process.connectionEndpoints().size(), 2);
  QCOMPARE(process.connectionEndpoints().first().name, QStringLiteral("client=a"));
  QCOMPARE(process.connectionEndpoints().first().localAddress, QStringLiteral("192.0.2.1"));
  QCOMPARE(process.connectionEndpoints().last().peerAddress, QStringLiteral("fe80::2%3"));
  QCOMPARE(spy.size(), 1);
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QCOMPARE(spy.size(), 1);
  QVERIFY(command(process, "connectionEndpoints", "[]"));
  QVERIFY(process.connectionEndpoints().isEmpty());
  QCOMPARE(spy.size(), 2);
}

void CoreProcessEndpointsTests::endpointsBeforeConnectedArePreserved()
{
  const ServerConfig config;
  CoreProcess process(config);
  QVERIFY(command(process, "connectionState", "Connecting"));
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QVERIFY(command(process, "connectionState", "Connected"));
  QCOMPARE(process.connectionState(), deskflow::core::ConnectionState::Connected);
  QCOMPARE(process.connectionEndpoints().size(), 2);
  QCOMPARE(process.connectionEndpoints().first().localAddress, QStringLiteral("192.0.2.1"));
  QCOMPARE(process.connectionEndpoints().first().peerAddress, QStringLiteral("192.0.2.2"));
  QCOMPARE(process.connectionEndpoints().last().peerAddress, QStringLiteral("fe80::2%3"));
}

void CoreProcessEndpointsTests::reconnectReplacesOldEndpoints()
{
  const ServerConfig config;
  CoreProcess process(config);
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QVERIFY(command(process, "connectionState", "Connected"));
  QVERIFY(command(process, "connectionState", "Disconnected"));
  QVERIFY(process.connectionEndpoints().isEmpty());
  QVERIFY(command(process, "connectionState", "Connecting"));
  const auto reconnected =
      QStringLiteral(R"([{"name":"","localAddress":"198.51.100.10","peerAddress":"198.51.100.20"}])");
  QVERIFY(command(process, "connectionEndpoints", reconnected));
  QVERIFY(command(process, "connectionState", "Connected"));
  QCOMPARE(process.connectionState(), deskflow::core::ConnectionState::Connected);
  QCOMPARE(process.connectionEndpoints().size(), 1);
  QCOMPARE(process.connectionEndpoints().first().localAddress, QStringLiteral("198.51.100.10"));
  QCOMPARE(process.connectionEndpoints().first().peerAddress, QStringLiteral("198.51.100.20"));
}

void CoreProcessEndpointsTests::disconnectClearsAddresses()
{
  const ServerConfig config;
  CoreProcess process(config);
  QVERIFY(command(process, "connectionState", "Connected"));
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QVERIFY(command(process, "connectionState", "Disconnected"));
  QVERIFY(process.connectionEndpoints().isEmpty());
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QVERIFY(command(process, "connectionState", "Listening"));
  QVERIFY(process.connectionEndpoints().isEmpty());
}

void CoreProcessEndpointsTests::invalidSnapshotCannotDisplayConfiguredHost()
{
  const ServerConfig config;
  CoreProcess process(config);
  process.setAddress("configured-server.example");
  QVERIFY(command(process, "connectionEndpoints", kEndpoints));
  QVERIFY(command(process, "connectionEndpoints", "invalid JSON"));
  QVERIFY(process.connectionEndpoints().isEmpty());
  QVERIFY(command(
      process, "connectionEndpoints",
      QStringLiteral(
          R"([{"localAddress":"0.0.0.0","peerAddress":"192.0.2.2"},)"
          R"({"localAddress":"192.0.2.1","peerAddress":"configured-server.example"}])"
      )
  ));
  QVERIFY(process.connectionEndpoints().isEmpty());
}

QTEST_MAIN(CoreProcessEndpointsTests)
