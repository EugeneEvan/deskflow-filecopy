/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxyFileTransfer.h"

#include "client/Client.h"
#include "deskflow/FileTransferProtocol.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

ServerProxyFileTransfer::ServerProxyFileTransfer(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events),
      m_transfer(events, stream, [client](const QStringList &paths) { return client->publishTransferredFiles(paths); })
{
}

void ServerProxyFileTransfer::onOptionsChanged(const OptionsList &options)
{
  bool advertised = false;
  for (size_t i = 0; i + 1 < options.size(); i += 2) {
    if (options[i] == kOptionFileCopy && options[i + 1] == kFileCopyCapability) {
      advertised = true;
    }
  }
  if (!advertised || !deskflow::FileTransferBridge::enabled()) {
    onOptionsReset();
    return;
  }
  if (!m_requested) {
    m_requested = true;
    ProtocolUtil::writef(getStream(), kMsgCFileCopyHello);
  }
}

void ServerProxyFileTransfer::onOptionsReset()
{
  m_requested = false;
  m_negotiated = false;
  m_transfer.setNegotiated(false);
}

ServerProxy::ConnectionResult ServerProxyFileTransfer::parseMessage(const uint8_t *code)
{
  if (std::memcmp(code, kMsgCFileCopyAck, 4) == 0) {
    if (!m_requested || m_negotiated || getCurrentMessageSize() != 4 || !deskflow::FileTransferBridge::enabled()) {
      throw BadClientException();
    }
    m_negotiated = true;
    m_transfer.setNegotiated(true);
    return ConnectionResult::Okay;
  }
  if (std::memcmp(code, kMsgDFileCopy, 4) == 0) {
    if (!m_negotiated) {
      throw BadClientException();
    }
    m_transfer.receive(deskflow::filetransfer::readFrame(getStream(), getCurrentMessageSize()));
    return ConnectionResult::Okay;
  }
  return ServerProxy1_8::parseMessage(code);
}

bool ServerProxyFileTransfer::onGrabClipboard(ClipboardID id)
{
  if (id == kClipboardClipboard) {
    m_transfer.sourceClipboardChanged();
  }
  return ServerProxy1_8::onGrabClipboard(id);
}

void ServerProxyFileTransfer::onClipboardChanged(ClipboardID id, const IClipboard *clipboard)
{
  if (id == kClipboardClipboard && m_negotiated) {
    m_transfer.offer(clipboard);
  }
  ServerProxy1_8::onClipboardChanged(id, clipboard);
}

void ServerProxyFileTransfer::onRemoteClipboardChanged(ClipboardID id)
{
  if (id == kClipboardClipboard) {
    m_transfer.sourceClipboardChanged();
  }
}

bool ServerProxyFileTransfer::canTransferFiles() const
{
  return m_negotiated && deskflow::FileTransferBridge::enabled();
}
