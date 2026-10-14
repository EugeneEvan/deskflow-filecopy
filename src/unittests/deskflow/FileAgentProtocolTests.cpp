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
  // A replacement Begin can arrive before the previous Cancelled callback,
  // with no intervening change to the user's native clipboard sequence.
  lifetime.begin(9, 55, QStringLiteral("replacement"));
  QVERIFY(!lifetime.accept(8, 56, 56, QStringLiteral("new")));
  QVERIFY(lifetime.accept(9, 56, 56, QStringLiteral("replacement")));
}

void FileAgentProtocolTests::sourceNotifications()
{
  SourceClipboardTracker tracker;
  tracker.published(40);
  const QStringList files{QStringLiteral("D:/fixture/source")};
  auto update = tracker.observe(41, false, files);
  QVERIFY(update);
  QCOMPARE(update->type, Type::SourceClipboardFiles);
  QCOMPARE(update->sequence, quint32(41));
  QCOMPARE(update->roots, files);
  QVERIFY(tracker.matches(update->sourceRevision, 41, 41, files));
  QVERIFY(!tracker.observe(41, false, files));
  Message decoded{Type::Ready};
  QVERIFY(decode(encode(*update).mid(4), decoded));
  QCOMPARE(decoded.sourceRevision, update->sourceRevision);
  QCOMPARE(decoded.roots, files);
  // A received batch is marked before its native notification is delivered.
  tracker.published(42);
  QVERIFY(!tracker.observe(42, true, files));
  QVERIFY(!tracker.observe(43, true, files));
  QVERIFY(!tracker.matches(update->sourceRevision, 41, 43, files));
  QVERIFY(!tracker.observe(0, false, files));
  auto next = tracker.observe(44, false, files);
  QVERIFY(next);
  QVERIFY(next->sourceRevision > update->sourceRevision);
}

void FileAgentProtocolTests::sourceChangesInvalidatePendingStart()
{
  SourceClipboardTracker tracker;
  tracker.published(70);
  const QStringList a{QStringLiteral("D:/fixture/a")};
  const QStringList b{QStringLiteral("D:/fixture/b")};
  const auto first = tracker.observe(71, false, a);
  QVERIFY(first);
  // Selecting text or clearing the clipboard revokes a queued file start.
  const auto cleared = tracker.observe(72, false, {});
  QVERIFY(cleared);
  QCOMPARE(cleared->type, Type::SourceChanged);
  QVERIFY(cleared->roots.isEmpty());
  QVERIFY(!tracker.matches(first->sourceRevision, 71, 72, a));
  const auto second = tracker.observe(73, false, b);
  QVERIFY(second);
  QVERIFY(!tracker.matches(first->sourceRevision, 71, 73, a));
  QVERIFY(!tracker.matches(second->sourceRevision, 73, 74, b));
  QVERIFY(!tracker.matches(second->sourceRevision, 73, 73, a));
  QVERIFY(tracker.matches(second->sourceRevision, 73, 73, b));
}

void FileAgentProtocolTests::sourceLifetimeRejectsLateNotifications()
{
  SourceNotificationLifetime lifetime;
  Message files{Type::SourceClipboardFiles};
  files.sourceRevision = 1;
  files.sequence = 101;
  files.roots = {QStringLiteral("D:/fixture/a")};
  QVERIFY(lifetime.accept(files, 101, false, 0));
  QVERIFY(!lifetime.accept(files, 101, false, 0));
  Message clear{Type::SourceChanged};
  clear.sourceRevision = 3;
  clear.sequence = 103;
  QVERIFY(lifetime.accept(clear, 103, true, 101));
  files.sourceRevision = 2;
  files.sequence = 102;
  QVERIFY(!lifetime.accept(files, 103, false, 0));
  files.sourceRevision = 4;
  files.sequence = 104;
  QVERIFY(!lifetime.accept(files, 105, false, 0));
  // A newer incoming Begin already claimed this exact native clipboard.
  files.sourceRevision = 5;
  files.sequence = 105;
  QVERIFY(!lifetime.accept(files, 105, true, 105));
  // A genuinely newer local copy cancels that receiving generation.
  files.sourceRevision = 6;
  files.sequence = 106;
  QVERIFY(lifetime.accept(files, 106, true, 105));
}

void FileAgentProtocolTests::sourceMessagesAreBounded()
{
  Message files{Type::SourceClipboardFiles};
  files.sourceRevision = 1;
  files.sequence = 15;
  QVERIFY(encode(files).isEmpty());
  files.roots = {QStringLiteral("D:/fixture/a")};
  QVERIFY(!encode(files).isEmpty());
  files.sourceRevision = 0;
  QVERIFY(encode(files).isEmpty());
  files.sourceRevision = 1;
  files.sequence = 0;
  QVERIFY(encode(files).isEmpty());
  files.sequence = 15;
  files.roots = {QString(32769, QChar('x'))};
  QVERIFY(encode(files).isEmpty());
  files.roots = QStringList(deskflow::FileTransferSession::maxEntries + 1, QStringLiteral("x"));
  QVERIFY(encode(files).isEmpty());
  Message changed{Type::SourceChanged};
  changed.sourceRevision = 2;
  changed.sequence = 16;
  Message decoded{Type::Ready};
  const auto frame = encode(changed);
  QVERIFY(!frame.isEmpty());
  QVERIFY(decode(frame.mid(4), decoded));
  QVERIFY(!decode(frame.mid(4) + QByteArray(1, '\0'), decoded));
  Message start{Type::StartSend};
  start.generation = 8;
  start.sourceRevision = 7;
  start.sequence = 17;
  start.roots = {QStringLiteral("D:/fixture/a")};
  QVERIFY(decode(encode(start).mid(4), decoded));
  QCOMPARE(decoded.sourceRevision, quint64(7));
}

void FileAgentProtocolTests::sourcePermissionIsCheckedBeforeLifetimeChanges()
{
  bool allowed = false;
  SourceNotificationLifetime lifetime([&] { return allowed; });
  Message files{Type::SourceClipboardFiles};
  files.sourceRevision = 2;
  files.sequence = 202;
  files.roots = {QStringLiteral("D:/fixture/a")};
  // This gate precedes Bridge's cancelSession and StartSend, so a server with
  // multiple peers cannot cancel an active receive or emit new network data.
  QVERIFY(!lifetime.accept(files, 202, true, 201));
  Message changed{Type::SourceChanged};
  changed.sourceRevision = 3;
  changed.sequence = 203;
  QVERIFY(!lifetime.accept(changed, 203, true, 201));
  allowed = true;
  // Rejection did not even advance the observation lifetime.
  QVERIFY(lifetime.accept(files, 202, true, 201));
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
