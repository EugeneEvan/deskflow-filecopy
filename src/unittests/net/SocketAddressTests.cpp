/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SocketAddressTests.h"
#include "arch/Arch.h"

#include <QTcpServer>
#include <QTcpSocket>

namespace {
std::pair<std::string, std::string> addresses(qintptr descriptor)
{
  // Borrow the descriptor only for this query; Qt retains ownership.
  ArchSocketImpl socket{};
#ifdef Q_OS_WIN
  socket.m_socket = static_cast<SOCKET>(descriptor);
#else
  socket.m_fd = static_cast<int>(descriptor);
#endif
  ARCH_NETWORK network;
  return network.getSocketAddresses(&socket);
}
} // namespace

void SocketAddressTests::connectedEndpoints_data()
{
  QTest::addColumn<QString>("loopback");
  QTest::newRow("IPv4") << QStringLiteral("127.0.0.1");
  QTest::newRow("IPv6") << QStringLiteral("::1");
}

void SocketAddressTests::connectedEndpoints()
{
  QFETCH(QString, loopback);
  const QHostAddress expected(loopback);
  QTcpServer server;
  if (!server.listen(expected, 0) && expected.protocol() == QAbstractSocket::IPv6Protocol) {
    QSKIP("IPv6 loopback is unavailable on this host");
  }
  QVERIFY(server.isListening());
  QTcpSocket client;
  client.connectToHost(expected, server.serverPort());
  QVERIFY(client.waitForConnected(5000));
  QVERIFY(server.waitForNewConnection(5000));
  const auto peer = server.nextPendingConnection();
  QVERIFY(peer);

  const auto outgoing = addresses(client.socketDescriptor());
  const auto accepted = addresses(peer->socketDescriptor());
  QCOMPARE(QHostAddress(QString::fromStdString(outgoing.first)), client.localAddress());
  QCOMPARE(QHostAddress(QString::fromStdString(outgoing.second)), client.peerAddress());
  QCOMPARE(QHostAddress(QString::fromStdString(accepted.first)), peer->localAddress());
  QCOMPARE(QHostAddress(QString::fromStdString(accepted.second)), peer->peerAddress());
  QCOMPARE(outgoing.first, accepted.second);
  QCOMPARE(outgoing.second, accepted.first);

  client.abort();
  const auto closed = addresses(client.socketDescriptor());
  QVERIFY(closed.first.empty());
  QVERIFY(closed.second.empty());
}

void SocketAddressTests::listeningSocketHasNoPeer()
{
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::AnyIPv4, 0));
  const auto endpoints = addresses(server.socketDescriptor());
  QVERIFY(endpoints.first.empty());
  QVERIFY(endpoints.second.empty());
}

QTEST_GUILESS_MAIN(SocketAddressTests)
