/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/DeskflowException.h"
#include "deskflow/IClipboard.h"
#include "io/IStream.h"

#include <QByteArray>

namespace deskflow::filetransfer {

// Called after the four-byte FCDT code has been consumed. PacketStreamFilter
// exposes the remaining bytes in this packet, not the following messages.
inline QByteArray readFrame(IStream *stream, uint32_t messageSize)
{
  constexpr uint32_t maxFrameSize = 128 * 1024;
  const uint32_t packetSize = stream->getSize();
  if (messageSize < 8 || messageSize > maxFrameSize + 8 || packetSize != messageSize - 4) {
    throw BadClientException();
  }
  unsigned char bytes[4];
  if (stream->read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    throw BadClientException();
  }
  const uint32_t size =
      (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
  if (size == 0 || size > maxFrameSize || size != packetSize - 4) {
    throw BadClientException();
  }
  QByteArray payload(static_cast<qsizetype>(size), Qt::Uninitialized);
  if (stream->read(payload.data(), size) != size) {
    throw BadClientException();
  }
  return payload;
}

inline bool containsFiles(const IClipboard *clipboard)
{
  if (!clipboard->open(0)) {
    return false;
  }
  // Shell file selections can also expose source paths as text. They must
  // travel only through the negotiated transfer, even for mixed formats.
  const bool files = clipboard->has(IClipboard::Format::Files);
  clipboard->close();
  return files;
}

} // namespace deskflow::filetransfer
