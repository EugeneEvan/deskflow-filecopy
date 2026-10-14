/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "deskflow/FileAgentProtocol.h"
#include <QUuid>
#include <QtEndian>
#include <stdexcept>

namespace deskflow::fileagent {
namespace {
void require(bool value)
{
  if (!value)
    throw std::runtime_error("Invalid file agent message");
}
void put32(QByteArray &out, quint32 value)
{
  value = qToBigEndian(value);
  out.append(reinterpret_cast<char *>(&value), 4);
}
void put64(QByteArray &out, quint64 value)
{
  value = qToBigEndian(value);
  out.append(reinterpret_cast<char *>(&value), 8);
}
void putText(QByteArray &out, const QString &value)
{
  const auto bytes = value.toUtf8();
  put32(out, quint32(bytes.size()));
  out += bytes;
}
struct Reader
{
  const QByteArray &bytes;
  qsizetype offset = 0;
  QByteArray raw(qsizetype count)
  {
    require(count >= 0 && count <= bytes.size() - offset);
    auto result = bytes.mid(offset, count);
    offset += count;
    return result;
  }
  quint8 u8()
  {
    return quint8(raw(1)[0]);
  }
  quint32 u32()
  {
    auto b = raw(4);
    return qFromBigEndian<quint32>(b.constData());
  }
  quint64 u64()
  {
    auto b = raw(8);
    return qFromBigEndian<quint64>(b.constData());
  }
  QString text(qsizetype limit)
  {
    const auto size = u32();
    require(size <= limit);
    const auto b = raw(size);
    auto result = QString::fromUtf8(b);
    require(result.toUtf8() == b && !result.contains(QChar(0)));
    return result;
  }
};
bool validId(const QString &id)
{
  return !id.isEmpty() && id.size() == 36 && !QUuid(id).isNull();
}
void putRoots(QByteArray &out, const QStringList &roots)
{
  put32(out, quint32(roots.size()));
  for (const auto &root : roots)
    putText(out, root);
}
QStringList roots(Reader &in)
{
  const auto count = in.u32();
  require(count > 0 && count <= FileTransferSession::maxEntries);
  QStringList result;
  for (quint32 i = 0; i < count; ++i) {
    auto root = in.text(32768);
    require(!root.isEmpty());
    result.push_back(std::move(root));
  }
  return result;
}
} // namespace

bool validEnvelope(const QByteArray &bytes)
{
  return bytes.size() >= 26 && bytes.size() <= FileTransferSession::maxEnvelopeBytes && bytes.startsWith("DFF1") &&
         quint8(bytes[4]) == 1 && quint8(bytes[5]) >= 1 && quint8(bytes[5]) <= 7;
}

bool decode(const QByteArray &bytes, Message &message)
{
  try {
    require(bytes.size() <= maxMessageBytes);
    Reader in{bytes};
    require(in.u8() == 1);
    message = Message{static_cast<Type>(in.u8())};
    using enum Type;
    switch (message.type) {
    case Configure:
      message.text = in.text(32768);
      message.cacheLimit = in.u64();
      require(message.cacheLimit >= (1ULL << 30) && message.cacheLimit <= (1024ULL << 30));
      break;
    case StartSend:
      message.generation = in.u64();
      message.sourceRevision = in.u64();
      message.sequence = in.u32();
      message.roots = roots(in);
      break;
    case SourceClipboardFiles:
    case SourceChanged:
      message.sourceRevision = in.u64();
      message.sequence = in.u32();
      require(message.sourceRevision != 0 && message.sequence != 0);
      if (message.type == SourceClipboardFiles)
        message.roots = roots(in);
      break;
    case Receive:
    case Send:
      message.generation = in.u64();
      message.sequence = in.u32();
      message.payload = in.raw(bytes.size() - in.offset);
      require(validEnvelope(message.payload));
      break;
    case Cancel:
      message.generation = in.u64();
      message.text = in.text(4096);
      break;
    case Ready:
      break;
    case Progress: {
      message.generation = in.u64();
      auto &p = message.progress;
      p.state = static_cast<FileTransferSession::State>(in.u8());
      require(p.state >= FileTransferSession::State::Preparing && p.state <= FileTransferSession::State::Failed);
      p.doneBytes = in.u64();
      p.totalBytes = in.u64();
      p.filesDone = in.u32();
      p.filesTotal = in.u32();
      p.id = in.text(36);
      p.detail = in.text(4096);
      require(
          (p.id.isEmpty() || validId(p.id)) && p.doneBytes <= p.totalBytes &&
          p.totalBytes <= FileTransferSession::maxBatchBytes && p.filesDone <= FileTransferSession::maxEntries &&
          p.filesTotal <= FileTransferSession::maxEntries && (p.filesTotal == 0 || p.filesDone <= p.filesTotal)
      );
      break;
    }
    case Published:
      message.generation = in.u64();
      message.sequence = in.u32();
      message.id = in.text(36);
      message.roots = roots(in);
      require(validId(message.id));
      break;
    default:
      return false;
    }
    return in.offset == bytes.size();
  } catch (const std::exception &) {
    return false;
  }
}

QByteArray encode(const Message &m)
{
  QByteArray out;
  out.append(char(1));
  out.append(char(m.type));
  using enum Type;
  switch (m.type) {
  case Configure:
    putText(out, m.text);
    put64(out, m.cacheLimit);
    break;
  case StartSend:
    put64(out, m.generation);
    put64(out, m.sourceRevision);
    put32(out, m.sequence);
    putRoots(out, m.roots);
    break;
  case SourceClipboardFiles:
  case SourceChanged:
    put64(out, m.sourceRevision);
    put32(out, m.sequence);
    if (m.type == SourceClipboardFiles)
      putRoots(out, m.roots);
    break;
  case Receive:
  case Send:
    put64(out, m.generation);
    put32(out, m.sequence);
    out += m.payload;
    break;
  case Cancel:
    put64(out, m.generation);
    putText(out, m.text);
    break;
  case Ready:
    break;
  case Progress:
    put64(out, m.generation);
    out.append(char(m.progress.state));
    put64(out, m.progress.doneBytes);
    put64(out, m.progress.totalBytes);
    put32(out, m.progress.filesDone);
    put32(out, m.progress.filesTotal);
    putText(out, m.progress.id);
    putText(out, m.progress.detail);
    break;
  case Published:
    put64(out, m.generation);
    put32(out, m.sequence);
    putText(out, m.id);
    putRoots(out, m.roots);
    break;
  default:
    return {};
  }
  Message check{Ready};
  if (!decode(out, check))
    return {};
  QByteArray frame;
  put32(frame, quint32(out.size()));
  return frame + out;
}

bool Decoder::append(const QByteArray &bytes, std::vector<Message> &messages)
{
  if (m_failed || bytes.size() > maxQueuedBytes || m_buffer.size() + bytes.size() > maxQueuedBytes)
    return m_failed = true, false;
  m_buffer += bytes;
  while (m_buffer.size() >= 4) {
    const auto size = qFromBigEndian<quint32>(m_buffer.constData());
    if (size < 2 || size > maxMessageBytes)
      return m_failed = true, false;
    if (m_buffer.size() < qsizetype(size) + 4)
      return true;
    Message message{Type::Ready};
    if (messages.size() >= maxQueuedMessages || !decode(m_buffer.mid(4, size), message))
      return m_failed = true, false;
    messages.push_back(std::move(message));
    m_buffer.remove(0, qsizetype(size) + 4);
  }
  return true;
}

void SourceClipboardTracker::published(quint32 sequence)
{
  m_sequence = sequence;
  ++m_revision;
  m_roots.clear();
}

std::optional<Message> SourceClipboardTracker::observe(quint32 sequence, bool owned, const QStringList &roots)
{
  if (sequence == 0 || sequence == m_sequence)
    return std::nullopt;
  published(sequence);
  if (owned)
    return std::nullopt;
  m_roots = roots;
  Message message{roots.isEmpty() ? Type::SourceChanged : Type::SourceClipboardFiles};
  message.sourceRevision = m_revision;
  message.sequence = sequence;
  message.roots = roots;
  return message;
}

bool SourceClipboardTracker::matches(
    quint64 revision, quint32 sequence, quint32 currentSequence, const QStringList &roots
) const
{
  return !m_roots.isEmpty() && revision == m_revision && sequence == m_sequence && sequence == currentSequence &&
         roots == m_roots;
}

bool SourceNotificationLifetime::accept(
    const Message &message, quint32 currentSequence, bool active, quint32 activeSequence
)
{
  if (m_allowed && !m_allowed())
    return false;
  if ((message.type != Type::SourceClipboardFiles && message.type != Type::SourceChanged) ||
      message.sourceRevision <= m_revision)
    return false;
  m_revision = message.sourceRevision;
  // Never let a queued source event supersede a newer Begin that already
  // claimed the same native clipboard. A later local copy has a new sequence.
  return message.sequence != 0 && message.sequence == currentSequence &&
         (!active || message.sequence != activeSequence);
}
} // namespace deskflow::fileagent
