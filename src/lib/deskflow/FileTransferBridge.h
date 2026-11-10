/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileAgentProtocol.h"
#include "deskflow/FileTransferSession.h"
#include "deskflow/WindowsFileAgentTransport.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

class IClipboard;
class IEventQueue;
class EventQueueTimer;
class FileAgentRecoveryTests;

namespace deskflow {
class IStream;
class Screen;

// All methods except requestCancelAll run on the core event thread.
// Owns one peer's worker session; never exposes source paths on the wire.
class FileTransferBridge
{
  friend class ::FileAgentRecoveryTests;

public:
  using Publish = std::function<bool(const QStringList &, bool alreadyPublished)>;
  FileTransferBridge(IEventQueue *events, IStream *stream, Publish publish, std::function<bool()> sourceAllowed = {});
  ~FileTransferBridge();
  static bool enabled();
  bool available() const;
  void setReadyCallback(std::function<void()> callback);
  static void requestCancelAll();
  static bool publishFiles(Screen *screen, const QStringList &paths);
  static void cacheFiles(IClipboard *clipboard, const QStringList &paths);
  void setNegotiated(bool negotiated);
  void offer(const IClipboard *clipboard);
  void receive(const QByteArray &payload);
  void cancel(const QString &reason = {});
  void sourceClipboardChanged();
  bool active() const
  {
    return m_active || (m_negotiated && m_pendingBegins != 0);
  }

private:
  struct WakeState;
  void tick();
  void progress(const FileTransferSession::Progress &progress);
  void publish(const QStringList &paths);
  void dispatchReceived(const QByteArray &payload, uint32_t sequence);
  void sendFrame(const QByteArray &payload);
  void cancelSession(const QString &reason);
  void pumpAgent();
  void startAgent();
  void invalidateAgent();
  void availabilityChanged();
  void clipboardChanged();
  void sourceChanged(const fileagent::Message &message);
  static uint32_t clipboardSequence();

  IEventQueue *m_events;
  IStream *m_stream;
  Publish m_publish;
  EventQueueTimer *m_timer = nullptr;
  std::unique_ptr<FileTransferSession> m_session;
#ifdef Q_OS_WIN
  std::unique_ptr<fileagent::WindowsTransport> m_agent;
  std::unique_ptr<fileagent::WindowsTransport> m_retiringAgent;
#endif
  bool m_system = false;
  bool m_agentReady = false;
  bool m_agentFailureReported = false;
  std::function<void()> m_readyCallback;
  std::chrono::steady_clock::time_point m_agentStarted{};
  std::chrono::steady_clock::time_point m_agentRetryAt{};
  std::chrono::seconds m_agentRetryDelay{1};
  QString m_lastAgentFailure;
  uint32_t m_lastAgentError = 0;
  QString m_availabilityReason;
  uint64_t m_generation = 0;
  fileagent::PublicationLifetime m_publication;
  std::function<bool()> m_sourceAllowed;
  fileagent::SourceNotificationLifetime m_sourceLifetime;
  std::optional<fileagent::Message> m_pendingSource;
  bool m_negotiated = false;
  bool m_active = false;
  bool m_publishFailed = false;
  uint32_t m_clipboardSequence = 0;
  uint64_t m_cancelGeneration = 0;
  uint64_t m_receiveToken = 0;
  uint64_t m_receiveEpoch = 0;
  std::shared_ptr<WakeState> m_wakeState;
  unsigned int m_pendingReceives = 0;
  unsigned int m_pendingBegins = 0;
  bool m_receiveOverflow = false;
  QString m_id;
  QStringList m_publishedRoots;
  FileTransferSession::State m_lastProgressState = FileTransferSession::State::Cancelled;
  QString m_lastProgressId;
  std::chrono::steady_clock::time_point m_lastProgressAt{};
};
} // namespace deskflow
