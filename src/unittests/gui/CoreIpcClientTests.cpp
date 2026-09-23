/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClientTests.h"

#include "gui/ipc/CoreIpcClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

namespace {
class TestCoreIpcClient : public deskflow::gui::ipc::CoreIpcClient
{
public:
  using CoreIpcClient::processCommand;
};
} // namespace

void CoreIpcClientTests::fileTransferJsonPreservesEqualsAndUnicode()
{
  TestCoreIpcClient client;
  QSignalSpy spy(&client, &TestCoreIpcClient::commandReceived);
  const QJsonObject status{
      {QStringLiteral("id"), QStringLiteral("batch-1")},
      {QStringLiteral("state"), QStringLiteral("failed")},
      {QStringLiteral("received"), QStringLiteral("4294967296")},
      {QStringLiteral("total"), QStringLiteral("8589934592")},
      {QStringLiteral("error"), QStringLiteral("缓存=a=b\n文件不可写")}
  };
  const auto payload = QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));
  const auto wireMessage = QStringLiteral("fileTransfer=") + payload;
  client.processCommand(QStringLiteral("fileTransfer"), wireMessage.split('='));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.first().at(0).toString(), QStringLiteral("fileTransfer"));
  QCOMPARE(spy.first().at(1).toString(), payload);
  QCOMPARE(QJsonDocument::fromJson(spy.first().at(1).toString().toUtf8()).object(), status);
}

void CoreIpcClientTests::legacyCommand()
{
  TestCoreIpcClient client;
  QSignalSpy spy(&client, &TestCoreIpcClient::commandReceived);
  client.processCommand(
      QStringLiteral("connectedClients"), {QStringLiteral("connectedClients"), QStringLiteral("A,B")}
  );
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.first().at(1).toString(), QStringLiteral("A,B"));
}

void CoreIpcClientTests::commandWithoutArguments()
{
  TestCoreIpcClient client;
  QSignalSpy spy(&client, &TestCoreIpcClient::commandReceived);
  client.processCommand(QStringLiteral("secureSocket"), {QStringLiteral("secureSocket")});
  QCOMPARE(spy.count(), 1);
  QVERIFY(spy.first().at(1).toString().isEmpty());
}

QTEST_GUILESS_MAIN(CoreIpcClientTests)
