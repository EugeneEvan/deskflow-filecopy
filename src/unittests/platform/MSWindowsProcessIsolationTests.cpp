/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "common/Constants.h"
#include "platform/MSWindowsHandle.h"
#include "platform/MSWindowsProcess.h"

#include <QProcess>
#include <QTest>
#include <future>

class MSWindowsProcessIsolationTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void isolatedShutdownDoesNotBroadcastAndWaitsForExit()
  {
    // Never inspect/reset/signal a close event belonging to a real Deskflow core.
    MSWindowsHandle event(CreateEventW(nullptr, TRUE, FALSE, kCloseEventName));
    const auto eventError = GetLastError();
    if (!event.get() || eventError == ERROR_ALREADY_EXISTS) {
      QSKIP("Deskflow close event already exists or cannot be created; active cores are left untouched");
    }
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--shutdown-test-child")});
    QVERIFY(child.waitForStarted(5000));
    const auto pid = static_cast<DWORD>(child.processId());
    MSWindowsHandle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid));
    QVERIFY(process.get());
    deskflow::platform::MSWindowsProcess::shutdown(process.get(), pid, 5, true);
    QCOMPARE(WaitForSingleObject(process.get(), 0), DWORD(WAIT_OBJECT_0));
    QCOMPARE(WaitForSingleObject(event.get(), 0), DWORD(WAIT_TIMEOUT));
    QVERIFY(child.waitForFinished(5000));
  }

  void isolatedShutdownRetainsProcessAfterTerminationFailure()
  {
    MSWindowsHandle event(CreateEventW(nullptr, TRUE, FALSE, kCloseEventName));
    const auto eventError = GetLastError();
    if (!event.get() || eventError == ERROR_ALREADY_EXISTS) {
      QSKIP("Deskflow close event already exists or cannot be created; active cores are left untouched");
    }
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--shutdown-test-child")});
    QVERIFY(child.waitForStarted(5000));
    const auto pid = static_cast<DWORD>(child.processId());
    // This is a real process handle with no PROCESS_TERMINATE right: the first
    // termination attempt must fail, while waiting for process exit still works.
    MSWindowsHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    QVERIFY(process.get());
    auto shutdown = std::async(std::launch::async, [&] {
      deskflow::platform::MSWindowsProcess::shutdown(process.get(), pid, 1, true);
    });
    const auto earlyResult = shutdown.wait_for(std::chrono::milliseconds(200));
    child.kill();
    const auto childExited = child.waitForFinished(5000);
    QCOMPARE(earlyResult, std::future_status::timeout);
    QVERIFY(childExited);
    QVERIFY(shutdown.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    shutdown.get();
    QCOMPARE(WaitForSingleObject(event.get(), 0), DWORD(WAIT_TIMEOUT));
  }

private:
  Log m_log;
};

int main(int argc, char **argv)
{
  if (argc == 2 && QByteArray(argv[1]) == "--shutdown-test-child") {
    Sleep(60000);
    return 0;
  }
  QCoreApplication app(argc, argv);
  MSWindowsProcessIsolationTests test;
  return QTest::qExec(&test, argc, argv);
}

#include "MSWindowsProcessIsolationTests.moc"
