/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransferSession.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

class IClipboard;
class IEventQueue;
class EventQueueTimer;

namespace deskflow {
class IStream;
class Screen;

// All methods except requestCancelAll run on the core event thread.
// Owns one peer's worker session; never exposes source paths on the wire.
class FileTransferBridge
{
public:
  using Publish = std::function<bool(const QStringList &)>;
  FileTransferBridge(IEventQueue *events, IStream *stream, Publish publish);
  ~FileTransferBridge();
  static bool enabled();
  static void requestCancelAll();
  static bool publishFiles(Screen *screen, const QStringList &paths);
  void setNegotiated(bool negotiated);
  void offer(const IClipboard *clipboard);
  void receive(const QByteArray &payload);
  void cancel(const QString &reason = {});
  void sourceClipboardChanged();
  bool active() const
  {
    return m_active || m_pendingBegins != 0;
  }

private:
  struct WakeState;
  void tick();
  void progress(const FileTransferSession::Progress &progress);
  void publish(const QStringList &paths);
  void dispatchReceived(const QByteArray &payload);
  static uint32_t clipboardSequence();

  IEventQueue *m_events;
  IStream *m_stream;
  Publish m_publish;
  EventQueueTimer *m_timer = nullptr;
  std::unique_ptr<FileTransferSession> m_session;
  bool m_negotiated = false;
  bool m_active = false;
  bool m_publishFailed = false;
  uint32_t m_clipboardSequence = 0;
  uint64_t m_cancelGeneration = 0;
  uint64_t m_receiveToken = 0;
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
