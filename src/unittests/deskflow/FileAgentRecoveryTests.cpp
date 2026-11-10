/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FileAgentRecoveryTests.h"
#include "MockEventQueue.h"
#include "common/Settings.h"
#include "deskflow/FileTransferBridge.h"

#include <QElapsedTimer>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

using namespace deskflow;
using namespace deskflow::fileagent;

namespace {
class RecoveryEventQueue : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    Event::deleteData(event);
  }
};

#ifdef Q_OS_WIN
// Exercise the real asynchronous transport without requiring a SYSTEM test
// runner or creating a user process. The opposite pipe ends belong to the test.
struct AgentPipes
{
  HANDLE agentInput = nullptr;
  HANDLE agentOutput = nullptr;
  std::unique_ptr<WindowsTransport> core;

  AgentPipes()
  {
    HANDLE input = nullptr, output = nullptr;
    if (CreatePipe(&input, &agentOutput, nullptr, 4096) && CreatePipe(&agentInput, &output, nullptr, 4096))
      core = std::make_unique<WindowsTransport>(input, output, WindowsTransport::Wake{});
    else if (input)
      CloseHandle(input);
  }
  ~AgentPipes()
  {
    if (agentInput)
      CloseHandle(agentInput);
    if (agentOutput)
      CloseHandle(agentOutput);
  }
};
#endif
} // namespace

void FileAgentRecoveryTests::initTestCase()
{
  QVERIFY(m_settings.isValid());
  m_originalSettings = Settings::settingsFile();
  Settings::setSettingsFile(m_settings.filePath("agent-recovery.conf"));
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
}

void FileAgentRecoveryTests::cleanupTestCase()
{
  Settings::setSettingsFile(m_originalSettings);
}

void FileAgentRecoveryTests::failureRevokesOldTransferLifetime()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  AgentPipes pipes;
  QVERIFY(pipes.core);
  bridge.m_agent = std::move(pipes.core);
  bridge.m_system = true;
  bridge.m_agentReady = true;
  bridge.m_negotiated = true;
  bridge.m_active = true;
  bridge.m_generation = 7;
  bridge.m_receiveEpoch = 9;
  bridge.m_publication.begin(7, 40, QStringLiteral("old-transfer"));
  int withdrawals = 0;
  bridge.setReadyCallback([&] { ++withdrawals; });
  bridge.m_agent->stop();
  bridge.pumpAgent();
  QVERIFY(!bridge.available());
  QVERIFY(!bridge.m_active);
  QVERIFY(bridge.m_generation > 7);
  QVERIFY(bridge.m_receiveEpoch > 9);
  QVERIFY(!bridge.m_publication.accept(7, 41, 41, QStringLiteral("old-transfer")));
  QCOMPARE(withdrawals, 1);
  bridge.pumpAgent();
  QCOMPARE(withdrawals, 1);
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::failedTransportIsReleasedBeforeRetry()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  AgentPipes pipes;
  QVERIFY(pipes.core);
  bridge.m_system = true;
  bridge.m_agent = std::move(pipes.core);
  bridge.m_agent->stop();
  QElapsedTimer elapsed;
  elapsed.start();
  bridge.pumpAgent();
  QVERIFY(elapsed.elapsed() < 200);
  QVERIFY(!bridge.m_agent);
  // Timer/wake bursts cannot immediately start another process attempt.
  for (int i = 0; i < 50; ++i)
    bridge.pumpAgent();
  QVERIFY(!bridge.m_agent);
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::unavailableAgentDoesNotReplayPendingClipboard()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  AgentPipes pipes;
  QVERIFY(pipes.core);
  bridge.m_system = true;
  bridge.m_agent = std::move(pipes.core);
  Message oldCopy{Type::SourceClipboardFiles};
  oldCopy.sourceRevision = 9;
  oldCopy.sequence = 42;
  oldCopy.roots = {QStringLiteral("C:/fixture/old-copy")};
  bridge.m_pendingSource = oldCopy;
  QVERIFY(bridge.m_sourceLifetime.accept(oldCopy, 42, false, 0));
  bridge.m_agent->stop();
  bridge.pumpAgent();
  QVERIFY(!bridge.m_pendingSource);
  Message newCopy{Type::SourceClipboardFiles};
  newCopy.sourceRevision = 1;
  newCopy.sequence = 43;
  newCopy.roots = {QStringLiteral("C:/fixture/new-copy")};
  QVERIFY(bridge.m_sourceLifetime.accept(newCopy, 43, false, 0));
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::retryWaitsForDeadlineAndBacksOff()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  bridge.m_system = true;
  for (int attempt = 0; attempt < 8; ++attempt) {
    AgentPipes pipes;
    QVERIFY(pipes.core);
    bridge.m_agent = std::move(pipes.core);
    bridge.m_agentStarted = std::chrono::steady_clock::now();
    bridge.m_agent->stop();
    const auto before = std::chrono::steady_clock::now();
    bridge.pumpAgent();
    QVERIFY(!bridge.m_agent);
    const auto delay = bridge.m_agentRetryAt - before;
    QVERIFY(delay >= std::chrono::seconds(attempt == 0 ? 1 : 2));
    QVERIFY(delay < std::chrono::seconds(31));
    bridge.pumpAgent();
    QVERIFY(!bridge.m_agent);
  }
  // Advance the deadline without sleeps. The real launch still happens on its
  // worker and will reject this ordinary-user test process without spawning.
  const auto retryAt = std::chrono::steady_clock::now();
  bridge.m_agentRetryAt = retryAt;
  bridge.pumpAgent();
  QVERIFY(bridge.m_agentStarted >= retryAt);
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::replacementReadyRestoresCapability()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  bridge.m_system = true;
  AgentPipes failed;
  QVERIFY(failed.core);
  bridge.m_agent = std::move(failed.core);
  bridge.m_agent->stop();
  bridge.pumpAgent();
  QVERIFY(!bridge.available());
  const auto generation = bridge.m_generation;
  const auto epoch = bridge.m_receiveEpoch;
  AgentPipes replacement;
  QVERIFY(replacement.core);
  bridge.m_agent = std::move(replacement.core);
  bridge.m_agentStarted = std::chrono::steady_clock::now();
  int ready = 0;
  bridge.setReadyCallback([&] {
    if (bridge.available())
      ++ready;
  });
  const auto message = encode(Message{Type::Ready});
  DWORD written = 0;
  QVERIFY(WriteFile(replacement.agentOutput, message.constData(), DWORD(message.size()), &written, nullptr));
  QCOMPARE(written, DWORD(message.size()));
  QTRY_VERIFY_WITH_TIMEOUT(
      ([&] {
        bridge.pumpAgent();
        return bridge.available();
      })(),
      1000
  );
  QCOMPARE(ready, 1);
  QVERIFY(!bridge.m_negotiated);
  QVERIFY(!bridge.active());
  QCOMPARE(bridge.m_generation, generation);
  QCOMPARE(bridge.m_receiveEpoch, epoch);
  QVERIFY(!bridge.m_pendingSource);
  bridge.setNegotiated(true);
  QVERIFY(bridge.m_negotiated);
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::readyTimeoutRevokesAndSchedulesRetry()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  AgentPipes pipes;
  QVERIFY(pipes.core);
  bridge.m_system = true;
  bridge.m_agent = std::move(pipes.core);
  bridge.m_agentStarted = std::chrono::steady_clock::now() - std::chrono::seconds(11);
  bridge.pumpAgent();
  QVERIFY(!bridge.m_agent);
  QVERIFY(!bridge.available());
  QVERIFY(bridge.m_agentRetryAt > std::chrono::steady_clock::now());
#else
  QSKIP("Windows agent recovery");
#endif
}

void FileAgentRecoveryTests::disabledSharingStopsRetry()
{
#ifdef Q_OS_WIN
  RecoveryEventQueue events;
  FileTransferBridge bridge(&events, nullptr, [](const auto &, bool) { return true; });
  AgentPipes pipes;
  QVERIFY(pipes.core);
  bridge.m_system = true;
  bridge.m_agent = std::move(pipes.core);
  bridge.m_agentReady = true;
  Settings::setValue(Settings::Core::FileTransferEnabled, false);
  bridge.pumpAgent();
  const bool stopped = !bridge.m_agent && !bridge.available();
  bridge.m_agentRetryAt = std::chrono::steady_clock::now();
  bridge.pumpAgent();
  const bool remainsStopped = !bridge.m_agent;
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  QVERIFY(stopped);
  QVERIFY(remainsStopped);
#else
  QSKIP("Windows agent recovery");
#endif
}

QTEST_GUILESS_MAIN(FileAgentRecoveryTests)
