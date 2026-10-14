/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferBridge.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "common/FileTransferCache.h"
#include "common/Settings.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/FileTransferFormat.h"
#include "deskflow/IClipboard.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/Screen.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace deskflow {
namespace {
std::atomic<uint64_t> cancelGeneration{0};
std::atomic<uint64_t> receiveToken{0};

struct ReceivedEnvelope : EventData
{
  ReceivedEnvelope(uint64_t instance, uint64_t epoch, uint32_t sequence, QByteArray bytes)
      : token(instance),
        epoch(epoch),
        sequence(sequence),
        payload(std::move(bytes))
  {
  }
  uint64_t token;
  uint64_t epoch;
  uint32_t sequence;
  QByteArray payload;
};

struct WorkerNotification : EventData
{
  explicit WorkerNotification(uint64_t instance) : token(instance)
  {
  }
  uint64_t token;
};
} // namespace

struct FileTransferBridge::WakeState
{
  WakeState(IEventQueue *queue, void *owner, uint64_t instance) : events(queue), target(owner), token(instance)
  {
  }

  void notify()
  {
    // Keep the lifetime lock through addEvent: disconnect may otherwise
    // invalidate the queue between checking it and posting the notification.
    std::lock_guard lock(mutex);
    if (!events || pending)
      return;
    pending = true;
    events->addEvent(Event(EventTypes::FileTransferWake, target, new WorkerNotification(token)));
  }

  std::mutex mutex;
  IEventQueue *events;
  void *target;
  const uint64_t token;
  bool pending = false;
};

FileTransferBridge::FileTransferBridge(IEventQueue *events, IStream *stream, Publish publish)
    : m_events(events),
      m_stream(stream),
      m_publish(std::move(publish)),
      m_cancelGeneration(cancelGeneration.load()),
      m_receiveToken(++receiveToken),
      m_wakeState(std::make_shared<WakeState>(events, this, m_receiveToken))
{
  m_events->addHandler(EventTypes::FileTransferReceive, this, [this](const Event &event) {
    const auto *envelope = static_cast<const ReceivedEnvelope *>(event.getDataObject());
    if (envelope->token != m_receiveToken)
      return;
    --m_pendingReceives;
    if (envelope->payload.size() >= 26 && static_cast<quint8>(envelope->payload[5]) == 1)
      --m_pendingBegins;
    if (!m_receiveOverflow && envelope->epoch == m_receiveEpoch)
      dispatchReceived(envelope->payload, envelope->sequence);
    if (m_pendingReceives == 0)
      m_receiveOverflow = false;
  });
  m_events->addHandler(EventTypes::FileTransferWake, this, [this](const Event &event) {
    const auto *notification = static_cast<const WorkerNotification *>(event.getDataObject());
    if (notification->token == m_receiveToken)
      tick();
  });
#ifdef Q_OS_WIN
  m_system = fileagent::processIdentity() == fileagent::ProcessIdentity::System;
  if (m_system && enabled()) {
    m_agentStarted = std::chrono::steady_clock::now();
    m_agent = std::make_unique<fileagent::WindowsTransport>([state = m_wakeState] { state->notify(); });
    fileagent::Message configuration{fileagent::Type::Configure};
    configuration.text = Settings::value(Settings::Core::FileTransferCachePath).toString();
    const auto limit = Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt();
    configuration.cacheLimit = limit > 0 ? quint64(limit) * FileTransferCache::gibibyte : 0;
    m_agent->send(configuration);
    m_timer = m_events->newTimer(0.1, nullptr);
    m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { tick(); });
  }
#endif
}

FileTransferBridge::~FileTransferBridge()
{
  {
    std::lock_guard lock(m_wakeState->mutex);
    m_wakeState->events = nullptr;
    m_wakeState->target = nullptr;
  }
  m_events->removeHandler(EventTypes::FileTransferWake, this);
  m_events->removeHandler(EventTypes::FileTransferReceive, this);
  if (m_timer) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
  }
  if (m_active) {
    FileTransferSession::Progress update{FileTransferSession::State::Cancelled};
    update.id = m_id;
    update.detail = QStringLiteral("Connection closed");
    progress(update);
  }
}

bool FileTransferBridge::enabled()
{
#ifdef Q_OS_WIN
  if (fileagent::processIdentity() == fileagent::ProcessIdentity::Unsupported)
    return false;
  return Settings::value(Settings::Core::FileTransferEnabled).toBool() &&
         Settings::value(Settings::Security::TlsEnabled).toBool() &&
         Settings::value(Settings::Server::EnableClipboard).toBool();
#else
  return false;
#endif
}

bool FileTransferBridge::available() const
{
#ifdef Q_OS_WIN
  if (m_system && (!m_agent || m_agent->failed()))
    return false;
#endif
  return enabled() && (!m_system || m_agentReady);
}

void FileTransferBridge::setReadyCallback(std::function<void()> callback)
{
  m_readyCallback = std::move(callback);
}

void FileTransferBridge::cacheFiles(IClipboard *clipboard, const QStringList &paths)
{
  std::vector<FileRef> refs;
  for (const auto &path : paths)
    refs.push_back({path.toUtf8().toStdString(), 0});
  clipboard->open(0);
  clipboard->empty();
  clipboard->add(IClipboard::Format::Files, FileTransferFormat::marshall(refs));
  clipboard->close();
}

void FileTransferBridge::requestCancelAll()
{
  ++cancelGeneration;
}

bool FileTransferBridge::publishFiles(Screen *screen, const QStringList &paths)
{
  if (paths.isEmpty())
    return false;
  std::vector<FileRef> refs;
  for (const auto &path : paths)
    refs.push_back({path.toUtf8().toStdString(), 0});
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.add(IClipboard::Format::Files, FileTransferFormat::marshall(refs));
  clipboard.close();
  screen->setClipboard(kClipboardClipboard, &clipboard);
  Clipboard actual;
  if (!screen->getClipboard(kClipboardClipboard, &actual) || !actual.open(0))
    return false;
  const auto written = FileTransferFormat::unmarshall(actual.get(IClipboard::Format::Files));
  actual.close();
  if (written.size() != refs.size())
    return false;
  for (size_t i = 0; i < refs.size(); ++i) {
    if (QString::fromStdString(written[i].path).replace('\\', '/') !=
        QString::fromStdString(refs[i].path).replace('\\', '/'))
      return false;
  }
  return true;
}

uint32_t FileTransferBridge::clipboardSequence()
{
#ifdef Q_OS_WIN
  return GetClipboardSequenceNumber();
#else
  return 0;
#endif
}

void FileTransferBridge::setNegotiated(bool negotiated)
{
  m_negotiated = negotiated && available();
  if (!m_negotiated) {
    cancelSession(QStringLiteral("File sharing is unavailable"));
    m_active = false;
    return;
  }
  if (m_session || m_system)
    return;
  FileTransferSession::Callbacks callbacks;
  callbacks.send = [this](const QByteArray &bytes) { sendFrame(bytes); };
  callbacks.publish = [this](const QStringList &paths) { publish(paths); };
  callbacks.progress = [this](const auto &update) { progress(update); };
  callbacks.wake = [state = m_wakeState] { state->notify(); };
  try {
    const auto limit = Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt();
    if (limit < 1 || limit > FileTransferCache::maxLimitGiB)
      throw std::runtime_error("File cache capacity must be between 1 and 1024 GiB");
    m_session = std::make_unique<FileTransferSession>(
        std::move(callbacks), Settings::value(Settings::Core::FileTransferCachePath).toString(),
        static_cast<quint64>(limit) * FileTransferCache::gibibyte
    );
  } catch (const std::exception &error) {
    m_negotiated = false;
    FileTransferSession::Progress update{FileTransferSession::State::Failed};
    update.detail = QString::fromUtf8(error.what());
    progress(update);
    return;
  }
  // Clipboard/cancel checks and recovery if the OS drops a wake event.
  // Normal data and ACK dispatch is driven by worker notifications.
  m_timer = m_events->newTimer(0.1, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { tick(); });
}

void FileTransferBridge::offer(const IClipboard *clipboard)
{
  if (!m_negotiated || !available() || !clipboard->open(0))
    return;
  const auto refs = clipboard->has(IClipboard::Format::Files)
                        ? FileTransferFormat::unmarshall(clipboard->get(IClipboard::Format::Files))
                        : std::vector<FileRef>{};
  clipboard->close();
  QStringList paths;
  for (const auto &ref : refs)
    paths.push_back(QString::fromUtf8(ref.path.data(), ref.path.size()));
  if (paths.isEmpty() || paths == m_publishedRoots)
    return;
  m_clipboardSequence = clipboardSequence();
  m_active = true;
  m_publishFailed = false;
  ++m_generation;
#ifdef Q_OS_WIN
  if (m_system) {
    fileagent::Message message{fileagent::Type::StartSend};
    message.generation = m_generation;
    message.sequence = m_clipboardSequence;
    message.roots = paths;
    m_agent->send(message);
    return;
  }
#endif
  m_session->startSend(paths);
}

void FileTransferBridge::receive(const QByteArray &payload)
{
  if (!m_negotiated || !available())
    return;
  if (payload.size() > FileTransferSession::maxEnvelopeBytes || m_pendingReceives >= 32) {
    m_receiveOverflow = true;
    cancel(QStringLiteral("File transfer receive queue limit exceeded"));
    return;
  }
  ++m_pendingReceives;
  if (payload.size() >= 26 && static_cast<quint8>(payload[5]) == 1)
    ++m_pendingBegins;
  m_events->addEvent(Event(
      EventTypes::FileTransferReceive, this,
      new ReceivedEnvelope(m_receiveToken, m_receiveEpoch, clipboardSequence(), payload)
  ));
}

void FileTransferBridge::dispatchReceived(const QByteArray &payload, uint32_t sequence)
{
  if (!m_negotiated || !available())
    return;
  // Only Begin claims a new clipboard lifetime. Late ACK/Abort frames from a
  // cancelled sender must not re-activate the bridge.
  if (!m_active && payload.size() >= 26 && static_cast<quint8>(payload[5]) == 1) {
    // Use the arrival sequence: a queued Begin must not claim a newer Ctrl+C.
    m_clipboardSequence = sequence;
    // Ready is only valid after the matching agent publication is acknowledged.
    m_publishFailed = m_system;
    m_active = true;
    ++m_generation;
    m_publication.begin(
        m_generation, m_clipboardSequence, QUuid::fromRfc4122(payload.mid(6, 16)).toString(QUuid::WithoutBraces)
    );
  }
#ifdef Q_OS_WIN
  if (m_system) {
    fileagent::Message message{fileagent::Type::Receive};
    message.generation = m_generation;
    message.sequence = m_clipboardSequence;
    message.payload = payload;
    m_agent->send(message);
    return;
  }
#endif
  m_session->receive(payload);
}

void FileTransferBridge::cancel(const QString &reason)
{
  if (m_active || m_pendingReceives != 0) {
    cancelSession(reason);
    m_active = false;
  }
}

void FileTransferBridge::sourceClipboardChanged()
{
  cancel(QStringLiteral("Clipboard changed; previous transfer cancelled"));
}

void FileTransferBridge::tick()
{
  {
    // Clear before pump so a worker that queues more results can wake us
    // again. The timer also clears this after an unsuccessful OS post.
    std::lock_guard lock(m_wakeState->mutex);
    m_wakeState->pending = false;
  }
  const auto generation = cancelGeneration.load();
  if (generation != m_cancelGeneration) {
    m_cancelGeneration = generation;
    cancel(QStringLiteral("Transfer cancelled"));
  }
  // The agent owns sequence checks in SYSTEM mode; native publication changes
  // the sequence before its acknowledgement reaches this event thread.
  if (!m_system && m_active && m_clipboardSequence != clipboardSequence())
    sourceClipboardChanged();
  if (m_active && !enabled())
    cancel(QStringLiteral("File sharing was disabled"));
  if (m_system)
    pumpAgent();
  else if (m_session)
    m_session->pump();
}

void FileTransferBridge::cancelSession(const QString &reason)
{
  ++m_generation;
  ++m_receiveEpoch;
  m_publication.cancel();
#ifdef Q_OS_WIN
  if (m_agent) {
    fileagent::Message message{fileagent::Type::Cancel};
    message.generation = m_generation;
    message.text = reason;
    m_agent->send(message);
  }
#endif
  if (m_session)
    m_session->cancel(reason);
}

void FileTransferBridge::sendFrame(const QByteArray &bytes)
{
  const bool abortFrame = bytes.size() >= 26 && static_cast<quint8>(bytes[5]) == 7;
  if (!m_negotiated || (!available() && !abortFrame))
    return;
  const std::string payload(bytes.constData(), bytes.size());
  try {
    ProtocolUtil::writef(m_stream, "FCDT%s", &payload);
  } catch (const std::exception &) {
    m_negotiated = false;
    m_active = false;
    cancelSession(QStringLiteral("File transfer connection write failed"));
    FileTransferSession::Progress update{FileTransferSession::State::Failed};
    update.detail = QStringLiteral("File transfer connection write failed");
    progress(update);
  }
}

void FileTransferBridge::pumpAgent()
{
#ifdef Q_OS_WIN
  if (!m_agent)
    return;
  if (!m_agentReady && std::chrono::steady_clock::now() - m_agentStarted > std::chrono::seconds(10))
    m_agent->stop();
  for (const auto &message : m_agent->take()) {
    if (m_agent->failed())
      break;
    using enum fileagent::Type;
    if (message.type == Ready) {
      if (m_agentReady) {
        m_agent->stop();
        break;
      }
      m_agentReady = true;
      try {
        if (m_readyCallback)
          m_readyCallback();
      } catch (const std::exception &) {
        qWarning() << "File agent capability update failed";
        m_agent->stop();
      }
      continue;
    }
    if (!m_agentReady || (message.type != Send && message.type != Progress && message.type != Published)) {
      m_agent->stop();
      break;
    }
    if (message.generation != m_generation)
      continue;
    if (message.type == Send)
      sendFrame(message.payload);
    else if (message.type == Progress)
      progress(message.progress);
    else if (message.type == Published) {
      if (m_active && m_negotiated && available() &&
          m_publication.accept(message.generation, message.sequence, clipboardSequence(), message.id) &&
          m_publish(message.roots, true)) {
        m_publishedRoots = message.roots;
        m_clipboardSequence = message.sequence;
        m_publishFailed = false;
        m_publication.cancel();
      } else {
        m_publishFailed = true;
      }
    }
  }
  if (m_agent->failed() && !m_agentFailureReported) {
    m_agentFailureReported = true;
    m_agentReady = false;
    m_negotiated = false;
    m_publication.cancel();
    FileTransferSession::Progress update{FileTransferSession::State::Failed};
    update.id = m_id;
    update.detail = QStringLiteral("User file agent unavailable; file sharing disabled");
    progress(update);
    try {
      if (m_readyCallback)
        m_readyCallback();
    } catch (const std::exception &) {
      qWarning() << "File agent capability withdrawal failed";
    }
  }
#endif
}

void FileTransferBridge::publish(const QStringList &paths)
{
  // This check runs immediately before touching the native clipboard, even
  // when an earlier callback in the same pump cancelled the batch.
  if (!m_active || m_clipboardSequence != clipboardSequence() || !enabled() || !m_publish(paths, false)) {
    m_publishFailed = true;
    return;
  }
  m_publishedRoots = paths;
  m_clipboardSequence = clipboardSequence();
}

void FileTransferBridge::progress(const FileTransferSession::Progress &update)
{
  using enum FileTransferSession::State;
  if ((update.state == Preparing || update.state == Sending || update.state == Receiving) &&
      (!m_negotiated || !available())) {
    cancelSession(QStringLiteral("File sharing was disabled"));
    m_active = false;
    return;
  }
  QString state;
  QString detail = update.detail;
  switch (update.state) {
  case Preparing:
    state = QStringLiteral("preparing");
    m_active = true;
    break;
  case Sending:
    state = QStringLiteral("sending");
    m_active = true;
    break;
  case Receiving:
    state = QStringLiteral("receiving");
    m_active = true;
    break;
  case Ready:
    state = m_publishFailed ? QStringLiteral("cancelled") : QStringLiteral("ready");
    if (m_publishFailed)
      detail = QStringLiteral("Clipboard changed or unavailable; files were not placed on the clipboard");
    m_active = false;
    break;
  case Completed:
    state = QStringLiteral("completed");
    m_active = false;
    break;
  case Cancelled:
    state = QStringLiteral("cancelled");
    m_active = false;
    break;
  case Failed:
    state = QStringLiteral("failed");
    m_active = false;
    break;
  }
  if (!update.id.isEmpty())
    m_id = update.id;
  const auto now = std::chrono::steady_clock::now();
  const bool byteProgress = update.state == Sending || update.state == Receiving;
  if (byteProgress && update.state == m_lastProgressState && m_id == m_lastProgressId &&
      now - m_lastProgressAt < std::chrono::milliseconds(150))
    return;
  m_lastProgressState = update.state;
  m_lastProgressId = m_id;
  m_lastProgressAt = now;
  if (!core::ipc::CoreIpcServer::available())
    return;
  const QJsonObject object{
      {QStringLiteral("id"), m_id},
      {QStringLiteral("state"), state},
      {QStringLiteral("received"), static_cast<qint64>(update.doneBytes)},
      {QStringLiteral("total"), static_cast<qint64>(update.totalBytes)},
      {QStringLiteral("filesDone"), QString::number(update.filesDone)},
      {QStringLiteral("filesTotal"), QString::number(update.filesTotal)},
      {QStringLiteral("error"), detail}
  };
  ipcSendToClient(
      QStringLiteral("fileTransfer"), QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)), true
  );
}
} // namespace deskflow
