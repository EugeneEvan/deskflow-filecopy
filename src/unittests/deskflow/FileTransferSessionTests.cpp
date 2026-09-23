/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferSessionTests.h"
#include "deskflow/FileTransferSession.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

using deskflow::FileTransferSession;
using State = FileTransferSession::State;

namespace {
bool write(const QString &path, const QByteArray &contents)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray read(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

bool spin(
    const std::function<bool()> &finished, FileTransferSession &first, FileTransferSession *second = nullptr,
    qint64 timeoutMs = 10000
)
{
  QElapsedTimer timer;
  timer.start();
  do {
    first.pump();
    if (second)
      second->pump();
    if (finished())
      return true;
    QTest::qWait(1);
  } while (timer.elapsed() < timeoutMs);
  return false;
}

void append32(QByteArray &bytes, quint32 value)
{
  value = qToBigEndian(value);
  bytes.append(reinterpret_cast<const char *>(&value), 4);
}

quint32 sequenceOf(const QByteArray &packet)
{
  return qFromBigEndian<quint32>(packet.constData() + 22);
}

QByteArray acknowledgementOf(const QByteArray &packet)
{
  auto ack = packet.left(26);
  ack[5] = '\6';
  return ack;
}

QByteArray abortOf(const QByteArray &packet)
{
  auto abort = packet.left(26);
  abort[5] = '\7';
  const QByteArray reason("test cancellation");
  append32(abort, static_cast<quint32>(reason.size()));
  return abort + reason;
}

QByteArray maliciousEntry(const QByteArray &original, const QString &path)
{
  QByteArray result = original.left(26);
  const auto utf8 = path.toUtf8();
  append32(result, static_cast<quint32>(utf8.size()));
  result.append(utf8);
  result.append(original.right(9)); // directory flag and expected size
  return result;
}
} // namespace

void FileTransferSessionTests::filesDirectoriesUnicodeAndReverseCopy()
{
  QTemporaryDir source;
  QTemporaryDir cacheA;
  QTemporaryDir cacheB;
  QVERIFY(source.isValid() && cacheA.isValid() && cacheB.isValid());
  const auto root = source.filePath(QString::fromUtf8("目录"));
  QVERIFY(QDir().mkpath(root + "/empty"));
  const QByteArray contents(2 * 1024 * 1024 + 19, '\x9a');
  const auto name = QString::fromUtf8("文件.bin");
  QVERIFY(write(root + '/' + name, contents));
  QVERIFY(write(root + "/zero", {}));
  QStringList rootsA;
  QStringList rootsB;
  bool failed = false;
  FileTransferSession *a = nullptr;
  FileTransferSession *b = nullptr;
  FileTransferSession first(
      {[&](const QByteArray &packet) { b->receive(packet); }, [&](const QStringList &paths) { rootsA = paths; },
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cacheA.path()
  );
  FileTransferSession second(
      {[&](const QByteArray &packet) { a->receive(packet); }, [&](const QStringList &paths) { rootsB = paths; },
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cacheB.path()
  );
  a = &first;
  b = &second;
  first.startSend({root});
  QVERIFY(spin([&] { return !rootsB.isEmpty() || failed; }, first, &second));
  QVERIFY(!failed);
  QCOMPARE(rootsB.size(), 1);
  QCOMPARE(read(rootsB[0] + '/' + name), contents);
  QVERIFY(QFileInfo(rootsB[0] + "/zero").isFile());
  QCOMPARE(QFileInfo(rootsB[0] + "/zero").size(), 0);
  QVERIFY(QFileInfo(rootsB[0] + "/empty").isDir());
  second.startSend(rootsB);
  QVERIFY(spin([&] { return !rootsA.isEmpty() || failed; }, first, &second));
  QVERIFY(!failed);
  QCOMPARE(read(rootsA[0] + '/' + name), contents);
}

void FileTransferSessionTests::checksumFailureDoesNotPublish()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), QByteArray(4 * 65536, 'x')));
  bool failed = false;
  bool cancelled = false;
  bool corrupt = true;
  QStringList published;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); }, [&](const QStringList &paths) { published = paths; },
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](QByteArray packet) {
         if (corrupt && static_cast<quint8>(packet[5]) == 3)
           packet[26] = static_cast<char>(packet[26] ^ 1);
         receiver.receive(packet);
       },
       {},
       [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; }},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return failed && cancelled; }, sourceSession, &receiver));
  QVERIFY(published.isEmpty());
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  // FileEnd and Complete can share one window. The late Complete must not
  // erase the checksum Abort, and the same connection must accept another copy.
  corrupt = false;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return !published.isEmpty(); }, sourceSession, &receiver));
  QCOMPARE(read(published[0]), QByteArray(4 * 65536, 'x'));
}

void FileTransferSessionTests::traversalIsRejected()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), "data"));
  bool failed = false;
  bool published = false;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); }, [&](const QStringList &) { published = true; },
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](QByteArray packet) {
         if (static_cast<quint8>(packet[5]) == 2)
           packet = maliciousEntry(packet, "../escaped");
         receiver.receive(packet);
       },
       {},
       {}},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return failed; }, sourceSession, &receiver));
  QVERIFY(!published);
  QVERIFY(!QFileInfo::exists(cache.filePath("escaped")));
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferSessionTests::reservedNameIsRejected()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), "data"));
  bool failed = false;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); },
       {},
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](QByteArray packet) {
         if (static_cast<quint8>(packet[5]) == 2)
           packet = maliciousEntry(packet, "CON.txt");
         receiver.receive(packet);
       },
       {},
       {}},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return failed; }, sourceSession, &receiver));
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferSessionTests::duplicateNamesAreRejected()
{
  QTemporaryDir source;
  QVERIFY(QDir().mkpath(source.filePath("a")));
  QVERIFY(QDir().mkpath(source.filePath("b")));
  QVERIFY(write(source.filePath("a/file"), "a"));
  QVERIFY(write(source.filePath("b/file"), "b"));
  bool failed = false;
  int envelopes = 0;
  FileTransferSession session(
      {[&](const QByteArray &) { ++envelopes; },
       {},
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      source.path()
  );
  session.startSend({source.filePath("a/file"), source.filePath("b/file")});
  QVERIFY(spin([&] { return failed; }, session));
  QCOMPARE(envelopes, 0);
}

void FileTransferSessionTests::senderBoundsWindowAndAcknowledgementAdvancesIt()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  QList<QByteArray> envelopes;
  FileTransferSession session({[&](const QByteArray &packet) { envelopes.push_back(packet); }, {}, {}}, source.path());
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return envelopes.size() >= 8; }, session, nullptr, 1000));
  QTest::qWait(150);
  session.pump();
  QCOMPARE(envelopes.size(), 8);
  for (qsizetype index = 0; index < envelopes.size(); ++index)
    QCOMPARE(sequenceOf(envelopes[index]), static_cast<quint32>(index));
  session.receive(acknowledgementOf(envelopes[0]));
  QVERIFY(spin([&] { return envelopes.size() >= 9; }, session));
  QTest::qWait(150);
  session.pump();
  QCOMPARE(envelopes.size(), 9);
  QCOMPARE(sequenceOf(envelopes[8]), 8U);
}

void FileTransferSessionTests::senderRejectsOutOfOrderAcknowledgement()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  QList<QByteArray> envelopes;
  QString failure;
  FileTransferSession session(
      {[&](const QByteArray &packet) { envelopes.push_back(packet); },
       {},
       [&](const auto &progress) {
         if (progress.state == State::Failed)
           failure = progress.detail;
       }},
      source.path()
  );
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return envelopes.size() >= 8; }, session));
  session.receive(acknowledgementOf(envelopes[1]));
  QVERIFY(spin([&] { return !failure.isEmpty(); }, session));
  QCOMPARE(failure, QStringLiteral("file transfer: unexpected acknowledgement"));
}

void FileTransferSessionTests::senderRejectsDuplicateAcknowledgement()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  QList<QByteArray> envelopes;
  QString failure;
  FileTransferSession session(
      {[&](const QByteArray &packet) { envelopes.push_back(packet); },
       {},
       [&](const auto &progress) {
         if (progress.state == State::Failed)
           failure = progress.detail;
       }},
      source.path()
  );
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return envelopes.size() >= 8; }, session));
  const auto ack = acknowledgementOf(envelopes[0]);
  session.receive(ack);
  session.receive(ack);
  QVERIFY(spin([&] { return !failure.isEmpty(); }, session));
  QCOMPARE(failure, QStringLiteral("file transfer: unexpected acknowledgement"));
}

void FileTransferSessionTests::cancellationDuringPumpDiscardsQueuedFrames()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  FileTransferSession *current = nullptr;
  int dataFrames = 0;
  bool cancelled = false;
  FileTransferSession session(
      {[&](const QByteArray &packet) {
         const auto type = static_cast<quint8>(packet[5]);
         if (type == 3) {
           ++dataFrames;
           current->cancel();
         } else if (type != 7) {
           current->receive(acknowledgementOf(packet));
         }
       },
       {},
       [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; }},
      source.path()
  );
  current = &session;
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return cancelled; }, session));
  QTest::qWait(100);
  session.pump();
  QCOMPARE(dataFrames, 1);
}

void FileTransferSessionTests::completionWaitsForFinalAcknowledgement()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), "x"));
  QList<QByteArray> envelopes;
  bool completed = false;
  FileTransferSession session(
      {[&](const QByteArray &packet) { envelopes.push_back(packet); },
       {},
       [&](const auto &progress) { completed |= progress.state == State::Completed; }},
      source.path()
  );
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return envelopes.size() == 5; }, session));
  QVERIFY(!completed);
  QCOMPARE(static_cast<quint8>(envelopes.back()[5]), quint8(5));
  for (qsizetype index = 0; index < envelopes.size() - 1; ++index)
    session.receive(acknowledgementOf(envelopes[index]));
  QTest::qWait(100);
  session.pump();
  QVERIFY(!completed);
  session.receive(acknowledgementOf(envelopes.back()));
  QVERIFY(spin([&] { return completed; }, session));
}

void FileTransferSessionTests::wakeNotifiesWorkerResultsAndCoalescesUntilPump()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  const auto hostThread = std::this_thread::get_id();
  std::atomic<int> wakes{0};
  std::atomic<int> applicationCallbacks{0};
  std::atomic_bool wrongWakeThread{false};
  std::atomic_bool wrongApplicationThread{false};
  QList<QByteArray> envelopes;
  FileTransferSession::Callbacks callbacks;
  callbacks.send = [&](const QByteArray &packet) {
    ++applicationCallbacks;
    wrongApplicationThread = std::this_thread::get_id() != hostThread;
    envelopes.push_back(packet);
  };
  callbacks.progress = [&](const auto &) {
    ++applicationCallbacks;
    wrongApplicationThread = std::this_thread::get_id() != hostThread;
  };
  callbacks.wake = [&] {
    wrongWakeThread = std::this_thread::get_id() == hostThread;
    ++wakes;
  };
  FileTransferSession session(std::move(callbacks), source.path());
  session.startSend({source.filePath("file")});
  QTRY_VERIFY_WITH_TIMEOUT(wakes.load() > 0, 2000);
  QTest::qWait(100);
  QCOMPARE(wakes.load(), 1);
  QCOMPARE(applicationCallbacks.load(), 0);
  session.pump();
  QCOMPARE(envelopes.size(), 8);
  session.receive(acknowledgementOf(envelopes[0]));
  QTRY_VERIFY_WITH_TIMEOUT(wakes.load() >= 2, 2000);
  QTest::qWait(100);
  QCOMPARE(wakes.load(), 2);
  session.pump();
  QCOMPARE(envelopes.size(), 9);
  QVERIFY(!wrongWakeThread.load());
  QVERIFY(!wrongApplicationThread.load());
}

void FileTransferSessionTests::wakeCallbackCanCancelWithoutHoldingSessionMutex()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("file"), "x"));
  FileTransferSession *current = nullptr;
  std::atomic<int> wakes{0};
  bool cancelled = false;
  FileTransferSession::Callbacks callbacks;
  callbacks.progress = [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; };
  callbacks.wake = [&] {
    if (++wakes == 1)
      current->cancel();
  };
  FileTransferSession session(std::move(callbacks), source.path());
  current = &session;
  session.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return cancelled; }, session));
  QVERIFY(wakes.load() >= 2);
}

void FileTransferSessionTests::missingAcknowledgement_timesOutBothPeersAndRemovesPartialCache()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(source.isValid() && cache.isValid());
  const QByteArray contents(1000000, 'x');
  QVERIFY(write(source.filePath("file"), contents));
  std::optional<quint32> firstDataSequence;
  bool acknowledgementDropped = false;
  bool published = false;
  quint64 receivedBytes = 0;
  QString senderFailure;
  QString receiverFailure;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) {
         const auto type = static_cast<quint8>(packet[5]);
         if (firstDataSequence && type == 6 && sequenceOf(packet) >= *firstDataSequence) {
           acknowledgementDropped = true;
           return;
         }
         // Isolate the watchdogs: neither peer's timeout Abort may cause the
         // other peer to report cancellation instead of its own timeout.
         if (type != 7)
           sender->receive(packet);
       },
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) {
         if (progress.state == State::Receiving)
           receivedBytes = progress.doneBytes;
         if (progress.state == State::Failed)
           receiverFailure = progress.detail;
       }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](const QByteArray &packet) {
         const auto type = static_cast<quint8>(packet[5]);
         if (type == 3 && !firstDataSequence)
           firstDataSequence = sequenceOf(packet);
         if (type != 7)
           receiver.receive(packet);
       },
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) {
         if (progress.state == State::Failed)
           senderFailure = progress.detail;
       }},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return acknowledgementDropped && receivedBytes > 0; }, sourceSession, &receiver));
  QVERIFY(receivedBytes < static_cast<quint64>(contents.size()));
  const auto batches = QDir(cache.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  QCOMPARE(batches.size(), 1);
  const auto partialPath = cache.filePath(batches[0] + "/file");
  QVERIFY(QFileInfo(partialPath).size() > 0);
  QVERIFY(QFileInfo(partialPath).size() < contents.size());

  // Intentionally exercise the unchanged production timeout, not a shortened
  // test timeout. This case adds about 60 seconds to FileTransferSessionTests.
  QElapsedTimer waiting;
  waiting.start();
  QVERIFY(spin([&] { return !senderFailure.isEmpty() && !receiverFailure.isEmpty(); }, sourceSession, &receiver, 65000)
  );
  QVERIFY(waiting.elapsed() >= 55000);
  QCOMPARE(senderFailure, QStringLiteral("file transfer: peer response timed out"));
  QCOMPARE(receiverFailure, QStringLiteral("file transfer: peer response timed out"));
  QVERIFY(!published);
  QVERIFY(!QFileInfo::exists(partialPath));
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  QCOMPARE(read(source.filePath("file")), contents);
}

void FileTransferSessionTests::cancellationRemovesPartialCache()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), QByteArray(1000000, 'x')));
  bool cancelled = false;
  bool published = false;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); }, [&](const QStringList &) { published = true; },
       [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](const QByteArray &packet) {
         receiver.receive(packet);
         if (static_cast<quint8>(packet[5]) == 3)
           receiver.cancel();
       },
       {},
       {}},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return cancelled; }, sourceSession, &receiver));
  QVERIFY(!published);
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferSessionTests::cancelledWindowTailCannotInvalidateNextBatch()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("old"), QByteArray(1000000, 'x')));
  QVERIFY(write(source.filePath("new"), "new contents"));
  bool cancelOnce = true;
  bool senderCancelled = false;
  bool receiverCancelled = false;
  bool failed = false;
  bool injectOldTail = false;
  QByteArray oldData;
  QStringList published;
  FileTransferSession *sender = nullptr;
  FileTransferSession *destination = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); }, [&](const QStringList &paths) { published = paths; },
       [&](const auto &progress) {
         failed |= progress.state == State::Failed;
         receiverCancelled |= progress.state == State::Cancelled;
         if (cancelOnce && progress.state == State::Receiving && progress.doneBytes > 0) {
           cancelOnce = false;
           destination->cancel();
         }
       }},
      cache.path()
  );
  destination = &receiver;
  FileTransferSession sourceSession(
      {[&](const QByteArray &packet) {
         const auto type = static_cast<quint8>(packet[5]);
         if (!injectOldTail && type == 3 && oldData.isEmpty())
           oldData = packet;
         receiver.receive(packet);
         if (injectOldTail && type == 1) {
           receiver.receive(oldData);
           receiver.receive(abortOf(oldData));
         }
       },
       {},
       [&](const auto &progress) {
         senderCancelled |= progress.state == State::Cancelled;
         failed |= progress.state == State::Failed;
       }},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("old")});
  QVERIFY(spin([&] { return receiverCancelled && senderCancelled; }, sourceSession, &receiver));
  QVERIFY(!failed);
  QVERIFY(!oldData.isEmpty());
  QVERIFY(published.isEmpty());
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  injectOldTail = true;
  sourceSession.startSend({source.filePath("new")});
  QVERIFY(spin([&] { return !published.isEmpty() || failed; }, sourceSession, &receiver));
  QVERIFY(!failed);
  QCOMPARE(read(published[0]), QByteArray("new contents"));
}

void FileTransferSessionTests::rapidReplacementBeforeAbortDeliveryReplacesIncomingBatch_data()
{
  QTest::addColumn<bool>("intermediateCopy");
  QTest::newRow("copy-copy-copy") << true;
  QTest::newRow("copy-cancel-copy") << false;
}

void FileTransferSessionTests::rapidReplacementBeforeAbortDeliveryReplacesIncomingBatch()
{
  QFETCH(bool, intermediateCopy);
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("old"), QByteArray(1000000, 'x')));
  QVERIFY(write(source.filePath("intermediate"), "never delivered"));
  QVERIFY(write(source.filePath("new"), "latest clipboard"));
  QVERIFY(QDir().mkpath(cache.filePath("completed")));
  const auto retainedPath = cache.filePath("completed/retained");
  QVERIFY(write(retainedPath, "previously completed contents"));
  FileTransferSession *current = nullptr;
  bool acknowledge = false;
  quint64 receivedBytes = 0;
  QString failure;
  QStringList published;
  QList<QByteArray> deliveredBegins;
  QList<QByteArray> deliveredAborts;
  int deliveredFrames = 0;
  std::atomic_bool replaceOnWake{false};
  std::atomic_bool replacedBeforePump{false};
  FileTransferSession receiver(
      {[&](const QByteArray &packet) {
         if (acknowledge)
           current->receive(packet);
       },
       [&](const QStringList &paths) { published = paths; },
       [&](const auto &progress) {
         if (progress.state == State::Receiving)
           receivedBytes = progress.doneBytes;
         if (progress.state == State::Failed)
           failure = progress.detail;
       }},
      cache.path()
  );
  FileTransferSession::Callbacks callbacks;
  callbacks.send = [&](const QByteArray &packet) {
    ++deliveredFrames;
    const auto type = static_cast<quint8>(packet[5]);
    if (type == 1)
      deliveredBegins.append(packet.mid(6, 16));
    if (type == 7)
      deliveredAborts.append(packet.mid(6, 16));
    receiver.receive(packet);
  };
  callbacks.wake = [&] {
    // Reproduce another clipboard replacement after Abort A is queued but
    // before the host can deliver it. This callback runs outside the lock.
    if (replaceOnWake.exchange(false)) {
      current->startSend({source.filePath("new")});
      replacedBeforePump = true;
    }
  };
  FileTransferSession sender(std::move(callbacks), source.path());
  current = &sender;
  sender.startSend({source.filePath("old")});
  QVERIFY(spin([&] { return deliveredFrames == 8 && receivedBytes > 0; }, sender, &receiver));
  QCOMPARE(deliveredBegins.size(), 1);
  auto oldDirectories = QDir(cache.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  oldDirectories.removeAll("completed");
  QCOMPARE(oldDirectories.size(), 1);
  const auto oldCache = cache.filePath(oldDirectories[0]);
  acknowledge = true;
  replaceOnWake = true;
  if (intermediateCopy)
    sender.startSend({source.filePath("intermediate")});
  else
    sender.cancel();
  QTRY_VERIFY_WITH_TIMEOUT(replacedBeforePump.load(), 2000);
  QVERIFY(spin([&] { return !published.isEmpty() || !failure.isEmpty(); }, sender, &receiver));
  QVERIFY2(failure.isEmpty(), qPrintable(failure));
  QCOMPARE(deliveredBegins.size(), 2);
  QVERIFY(!deliveredAborts.contains(deliveredBegins.front()));
  QVERIFY(!QFileInfo::exists(oldCache));
  QCOMPARE(QDir(cache.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);
  QCOMPARE(read(retainedPath), QByteArray("previously completed contents"));
  QCOMPARE(read(published[0]), QByteArray("latest clipboard"));
}

void FileTransferSessionTests::duplicateBeginIsRejected()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), "contents"));
  QByteArray begin;
  FileTransferSession sender(
      {[&](const QByteArray &packet) {
         if (static_cast<quint8>(packet[5]) == 1)
           begin = packet;
       },
       {},
       {}},
      source.path()
  );
  sender.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return !begin.isEmpty(); }, sender));
  QString failure;
  bool published = false;
  FileTransferSession receiver(
      {{},
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) {
         if (progress.state == State::Failed)
           failure = progress.detail;
       }},
      cache.path()
  );
  receiver.receive(begin);
  receiver.receive(begin);
  QVERIFY(spin([&] { return !failure.isEmpty(); }, receiver));
  QCOMPARE(failure, QStringLiteral("file transfer: duplicate incoming batch"));
  QVERIFY(!published);
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferSessionTests::completedBatchAbortBeforePumpPreventsPublication()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), "contents"));
  QList<QByteArray> frames;
  FileTransferSession sender({[&](const QByteArray &packet) { frames.append(packet); }, {}, {}}, source.path());
  sender.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return frames.size() == 5; }, sender));
  bool published = false;
  bool cancelled = false;
  FileTransferSession receiver(
      {{},
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; }},
      cache.path()
  );
  for (const auto &frame : frames)
    receiver.receive(frame);
  receiver.receive(abortOf(frames.front()));
  // Let the ordered worker process Complete then Abort without any host pump.
  QTest::qWait(100);
  QVERIFY(spin([&] { return cancelled; }, receiver));
  QVERIFY(!published);
}

void FileTransferSessionTests::oldCompletedBatchAbortCannotInvalidateNextBatch()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("old"), "old contents"));
  QVERIFY(write(source.filePath("new"), "new contents"));
  QList<QByteArray> frames;
  FileTransferSession sender(
      {[&](const QByteArray &packet) {
         if (static_cast<quint8>(packet[5]) != 7)
           frames.append(packet);
       },
       {},
       {}},
      source.path()
  );
  sender.startSend({source.filePath("old")});
  QVERIFY(spin([&] { return frames.size() == 5; }, sender));
  const auto oldFrames = frames;
  frames.clear();
  sender.startSend({source.filePath("new")});
  QVERIFY(spin([&] { return frames.size() == 5; }, sender));
  QStringList published;
  bool failed = false;
  FileTransferSession receiver(
      {{},
       [&](const QStringList &paths) { published = paths; },
       [&](const auto &progress) { failed |= progress.state == State::Failed || progress.state == State::Cancelled; }},
      cache.path()
  );
  for (const auto &frame : oldFrames)
    receiver.receive(frame);
  receiver.receive(frames.front());
  receiver.receive(abortOf(oldFrames.front()));
  for (qsizetype index = 1; index < frames.size(); ++index)
    receiver.receive(frames[index]);
  QTest::qWait(100);
  QVERIFY(spin([&] { return !published.isEmpty() && QFileInfo(published[0]).fileName() == "new"; }, receiver));
  QVERIFY(!failed);
  QCOMPARE(read(published[0]), QByteArray("new contents"));
}

void FileTransferSessionTests::disconnectKeepsPublishedFiles()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("file"), "retained"));
  QStringList published;
  {
    FileTransferSession *sender = nullptr;
    FileTransferSession receiver(
        {[&](const QByteArray &packet) { sender->receive(packet); },
         [&](const QStringList &roots) { published = roots; },
         {}},
        cache.path()
    );
    FileTransferSession sourceSession(
        {[&](const QByteArray &packet) { receiver.receive(packet); }, {}, {}}, source.path()
    );
    sender = &sourceSession;
    sourceSession.startSend({source.filePath("file")});
    QVERIFY(spin([&] { return !published.isEmpty(); }, sourceSession, &receiver));
  }
  QCOMPARE(read(published[0]), QByteArray("retained"));
}

void FileTransferSessionTests::queuedCompletionCannotOverwriteNewClipboard()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(write(source.filePath("old"), "old contents"));
  QVERIFY(write(source.filePath("new"), "new contents"));
  bool completeOnWire = false;
  bool cancelled = false;
  QStringList published;
  FileTransferSession *sender = nullptr;
  FileTransferSession receiver(
      {[&](const QByteArray &packet) { sender->receive(packet); }, [&](const QStringList &paths) { published = paths; },
       [&](const auto &progress) { cancelled |= progress.state == State::Cancelled; }},
      cache.path()
  );
  FileTransferSession sourceSession(
      {[&](const QByteArray &packet) {
         receiver.receive(packet);
         completeOnWire |= static_cast<quint8>(packet[5]) == 5;
       },
       {},
       {}},
      source.path()
  );
  sender = &sourceSession;
  sourceSession.startSend({source.filePath("old")});
  QElapsedTimer timer;
  timer.start();
  while (!completeOnWire && timer.elapsed() < 10000) {
    sourceSession.pump();
    if (!completeOnWire)
      receiver.pump();
    QTest::qWait(1);
  }
  QVERIFY(completeOnWire);
  // Simulate a newer local clipboard while the completed worker result is
  // still waiting for the event thread to publish it.
  receiver.cancel();
  QVERIFY(spin([&] { return cancelled; }, sourceSession, &receiver));
  QVERIFY(published.isEmpty());
  sourceSession.startSend({source.filePath("new")});
  QVERIFY(spin([&] { return !published.isEmpty(); }, sourceSession, &receiver));
  QCOMPARE(read(published[0]), QByteArray("new contents"));
}

void FileTransferSessionTests::batchLimitIsRejectedBeforeCreatingFiles()
{
  QTemporaryDir cache;
  bool failed = false;
  bool published = false;
  FileTransferSession receiver(
      {{},
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      cache.path()
  );
  QByteArray packet;
  append32(packet, 0x44464631);
  packet.append('\1');
  packet.append('\1');
  packet.append(QUuid::createUuid().toRfc4122());
  append32(packet, 0);
  append32(packet, FileTransferSession::maxEntries + 1);
  packet.append(QByteArray(8, '\0'));
  receiver.receive(packet);
  QVERIFY(spin([&] { return failed; }, receiver));
  QVERIFY(!published);
  QVERIFY(QDir(cache.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void FileTransferSessionTests::simultaneousCopiesCancelBothDirections()
{
  QTemporaryDir source;
  QTemporaryDir cacheA;
  QTemporaryDir cacheB;
  QVERIFY(write(source.filePath("file"), "contents"));
  QByteArray beginA;
  QByteArray beginB;
  bool cancelledA = false;
  bool cancelledB = false;
  bool published = false;
  FileTransferSession first(
      {[&](const QByteArray &packet) {
         if (static_cast<quint8>(packet[5]) == 1)
           beginA = packet;
       },
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) { cancelledA |= progress.state == State::Cancelled; }},
      cacheA.path()
  );
  FileTransferSession second(
      {[&](const QByteArray &packet) {
         if (static_cast<quint8>(packet[5]) == 1)
           beginB = packet;
       },
       [&](const QStringList &) { published = true; },
       [&](const auto &progress) { cancelledB |= progress.state == State::Cancelled; }},
      cacheB.path()
  );
  first.startSend({source.filePath("file")});
  second.startSend({source.filePath("file")});
  QVERIFY(spin([&] { return !beginA.isEmpty() && !beginB.isEmpty(); }, first, &second));
  first.receive(beginB);
  second.receive(beginA);
  QVERIFY(spin([&] { return cancelledA && cancelledB; }, first, &second));
  QVERIFY(!published);
}

void FileTransferSessionTests::symlinkSourceIsRejected()
{
  QTemporaryDir source;
  QVERIFY(write(source.filePath("target"), "contents"));
  const auto link = source.filePath("link");
#ifdef Q_OS_WIN
  const auto nativeLink = QDir::toNativeSeparators(link);
  const auto nativeTarget = QDir::toNativeSeparators(source.filePath("target"));
  if (!CreateSymbolicLinkW(
          reinterpret_cast<LPCWSTR>(nativeLink.utf16()), reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
          SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
      ))
    QSKIP("This Windows account cannot create a symbolic link; reparse rejection was not exercised");
#else
  QVERIFY(QFile::link(source.filePath("target"), link));
#endif
  bool failed = false;
  bool sent = false;
  FileTransferSession session(
      {[&](const QByteArray &) { sent = true; },
       {},
       [&](const auto &progress) { failed |= progress.state == State::Failed; }},
      source.path()
  );
  session.startSend({link});
  QVERIFY(spin([&] { return failed; }, session));
  QVERIFY(!sent);
  QCOMPARE(read(source.filePath("target")), QByteArray("contents"));
}

QTEST_GUILESS_MAIN(FileTransferSessionTests)
