/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FileAgentProtocolTests.h"
#include "deskflow/FileAgentProtocol.h"
#include "deskflow/WindowsFileAgentTransport.h"
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

using namespace deskflow::fileagent;

void FileAgentProtocolTests::fragmentedFrames()
{
  Message message{Type::Cancel};
  message.generation = 17;
  message.text = QStringLiteral("Clipboard changed");
  const auto wire = encode(message);
  Decoder decoder;
  std::vector<Message> messages;
  for (const auto byte : wire)
    QVERIFY(decoder.append(QByteArray(1, byte), messages));
  QCOMPARE(messages.size(), size_t(1));
  QCOMPARE(messages.front().generation, quint64(17));
  QCOMPARE(messages.front().text, QStringLiteral("Clipboard changed"));
  QVERIFY(decoder.complete());
  messages.clear();
  QVERIFY(decoder.append(wire + wire, messages));
  QCOMPARE(messages.size(), size_t(2));
}

void FileAgentProtocolTests::rejectsUnboundedAndUnknownMessages()
{
  for (const auto &bytes :
       {QByteArray::fromHex("ffffffff"), QByteArray::fromHex("00000000"), QByteArray::fromHex("0000000201ff")}) {
    Decoder decoder;
    std::vector<Message> messages;
    QVERIFY(!decoder.append(bytes, messages));
  }
  Decoder decoder;
  std::vector<Message> messages;
  QVERIFY(decoder.append(QByteArray::fromHex("0000000201"), messages));
  QVERIFY(!decoder.complete()); // EOF in a frame is not a clean shutdown.
}

void FileAgentProtocolTests::rejectsInvalidProgress()
{
  Message message{Type::Progress};
  message.progress.state = static_cast<deskflow::FileTransferSession::State>(255);
  QVERIFY(encode(message).isEmpty());
  message.progress.state = deskflow::FileTransferSession::State::Sending;
  message.progress.doneBytes = 2;
  message.progress.totalBytes = 1;
  QVERIFY(encode(message).isEmpty());
}

void FileAgentProtocolTests::publicationLifetime()
{
  PublicationLifetime lifetime;
  lifetime.begin(7, 41, QStringLiteral("transfer"));
  QVERIFY(!lifetime.accept(6, 41, 42, QStringLiteral("transfer")));
  QVERIFY(!lifetime.accept(7, 41, 42, QStringLiteral("another")));
  QVERIFY(!lifetime.accept(7, 41, 43, QStringLiteral("transfer")));
  QVERIFY(lifetime.accept(7, 42, 42, QStringLiteral("transfer")));
  lifetime.cancel();
  QVERIFY(!lifetime.accept(7, 42, 42, QStringLiteral("transfer")));
  lifetime.begin(8, 55, QStringLiteral("new"));
  QVERIFY(!lifetime.accept(7, 42, 42, QStringLiteral("transfer")));
}

void FileAgentProtocolTests::pipeEofDisablesTransport()
{
#ifdef Q_OS_WIN
  HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
  QVERIFY(CreatePipe(&inputRead, &inputWrite, nullptr, 4096));
  QVERIFY(CreatePipe(&outputRead, &outputWrite, nullptr, 4096));
  WindowsTransport transport(inputRead, outputWrite, {});
  CloseHandle(inputWrite);
  QTRY_VERIFY_WITH_TIMEOUT(transport.failed(), 1000);
  QVERIFY(!transport.send(Message{Type::Ready}));
  CloseHandle(outputRead);
#else
  QSKIP("Windows pipe transport");
#endif
}

void FileAgentProtocolTests::stalledPipeHasBoundedQueueAndStop()
{
#ifdef Q_OS_WIN
  HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
  QVERIFY(CreatePipe(&inputRead, &inputWrite, nullptr, 4096));
  QVERIFY(CreatePipe(&outputRead, &outputWrite, nullptr, 4096));
  WindowsTransport transport(inputRead, outputWrite, {});
  Message message{Type::StartSend};
  message.generation = 1;
  for (int i = 0; i < 128; ++i)
    message.roots.push_back(QString(16000, 'x'));
  QVERIFY(transport.send(message));
  QTest::qWait(20); // A 2 MiB write cannot fit the unread 4 KiB pipe.
  bool rejected = false;
  for (int i = 0; i < 12 && !rejected; ++i)
    rejected = !transport.send(message);
  QVERIFY(rejected);
  QVERIFY(transport.failed());
  QElapsedTimer timer;
  timer.start();
  transport.stop();
  QVERIFY(timer.elapsed() < 200);
  CloseHandle(inputWrite);
  CloseHandle(outputRead); // Releases the blocked worker; the event thread never joins it.
#else
  QSKIP("Windows pipe transport");
#endif
}

QTEST_GUILESS_MAIN(FileAgentProtocolTests)
