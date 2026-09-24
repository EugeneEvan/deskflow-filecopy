/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ArchMultithreadWindowsTests.h"

#include "arch/ArchException.h"
#include "arch/win32/ArchMultithreadWindows.h"

#include <QScopeGuard>

#include <atomic>

namespace {
enum class Outcome
{
  NotRun,
  Returned,
  Cancelled,
  UnexpectedException
};

struct WorkerState
{
  ArchMultithreadWindows *threads;
  HANDLE ready;
  std::atomic<Outcome> outcome{Outcome::NotRun};
  std::atomic_bool unwound{false};
};

void *runWorker(void *data)
{
  auto &state = *static_cast<WorkerState *>(data);
  if (WaitForSingleObject(state.ready, 5000) != WAIT_OBJECT_0)
    return nullptr;
  try {
    const auto unwind = qScopeGuard([&state] { state.unwound = true; });
    state.threads->testCancelThread();
    state.outcome = Outcome::Returned;
  } catch (ThreadCancelException &) {
    state.outcome = Outcome::Cancelled;
    throw;
  } catch (...) {
    // Report a lock exception as a test failure rather than letting an
    // unexpected exception escape the native thread entry point.
    state.outcome = Outcome::UnexpectedException;
  }
  return nullptr;
}
} // namespace

void ArchMultithreadWindowsTests::cancellationUnwindsWorker_data()
{
  QTest::addColumn<bool>("cancel");
  QTest::newRow("not-cancelled") << false;
  QTest::newRow("cancelled") << true;
}

void ArchMultithreadWindowsTests::cancellationUnwindsWorker()
{
  QFETCH(bool, cancel);
  ArchMultithreadWindows threads;
  WorkerState state{&threads, CreateEvent(nullptr, TRUE, FALSE, nullptr)};
  QVERIFY(state.ready != nullptr);
  const auto closeReady = qScopeGuard([&state] { CloseHandle(state.ready); });
  const auto thread = threads.newThread(&runWorker, &state);
  QVERIFY(thread != nullptr);

  // The worker is gated until we hold a native join handle. Wait for the OS
  // thread, not merely the arch exit event, before releasing its last reference.
  const auto nativeThread = OpenThread(SYNCHRONIZE, FALSE, threads.getIDOfThread(thread));
  if (!nativeThread)
    qFatal("could not open the isolated worker thread");
  const auto closeNative = qScopeGuard([nativeThread] { CloseHandle(nativeThread); });
  if (cancel)
    threads.cancelThread(thread);
  if (!SetEvent(state.ready) || WaitForSingleObject(nativeThread, 5000) != WAIT_OBJECT_0)
    qFatal("isolated worker cancellation did not terminate");
  const bool exited = threads.wait(thread, 0.0);
  threads.closeThread(thread);

  QVERIFY(exited);
  QVERIFY(state.unwound.load());
  QCOMPARE(state.outcome.load(), cancel ? Outcome::Cancelled : Outcome::Returned);
}

QTEST_GUILESS_MAIN(ArchMultithreadWindowsTests)
