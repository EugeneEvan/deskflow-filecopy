/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "deskflow/FileTransferSession.h"
#include <vector>

namespace deskflow::fileagent {
enum class Type : quint8
{
  Configure = 1,
  StartSend,
  Receive,
  Cancel,
  Ready,
  Send,
  Progress,
  Published
};
constexpr qsizetype maxMessageBytes = 8 * 1024 * 1024 + 512;
constexpr size_t maxQueuedMessages = 32;
constexpr qsizetype maxQueuedBytes = 16 * 1024 * 1024;

struct Message
{
  Type type;
  quint64 generation = 0;
  quint32 sequence = 0;
  quint64 cacheLimit = 0;
  QString text;
  QString id;
  QStringList roots;
  QByteArray payload;
  FileTransferSession::Progress progress{FileTransferSession::State::Preparing};
};

QByteArray encode(const Message &message);
bool decode(const QByteArray &bytes, Message &message);
bool validEnvelope(const QByteArray &bytes);

class Decoder
{
public:
  bool append(const QByteArray &bytes, std::vector<Message> &messages);
  bool complete() const
  {
    return m_buffer.isEmpty() && !m_failed;
  }

private:
  QByteArray m_buffer;
  bool m_failed = false;
};

// An acknowledgement may update logical caches only while its exact native
// publication still exists. The pre-publication sequence is checked by the
// agent under OpenClipboard, the resulting sequence by the core on receipt.
class PublicationLifetime
{
public:
  void begin(quint64 generation, quint32 sequence, QString id)
  {
    m_generation = generation;
    m_sequence = sequence;
    m_id = std::move(id);
    m_active = true;
  }
  void cancel()
  {
    m_active = false;
  }
  bool accept(quint64 generation, quint32 publishedSequence, quint32 currentSequence, const QString &id) const
  {
    return m_active && generation == m_generation && id == m_id && publishedSequence == currentSequence &&
           publishedSequence != m_sequence;
  }

private:
  quint64 m_generation = 0;
  quint32 m_sequence = 0;
  QString m_id;
  bool m_active = false;
};
} // namespace deskflow::fileagent
