/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxyFileTransfer.h"

#include "deskflow/FileTransferProtocol.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/Server.h"

#include <cstring>

ClientProxyFileTransfer::ClientProxyFileTransfer(
    const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, stream, server, events),
      m_transfer(events, stream, [this, server](const QStringList &paths) {
        return server->publishTransferredFiles(this, paths);
      })
{
}

void ClientProxyFileTransfer::setOptions(const OptionsList &options)
{
  if (options.size() % 2 != 0) {
    ClientProxy1_8::setOptions(options);
    return;
  }
  OptionsList advertised;
  for (size_t i = 0; i < options.size(); i += 2) {
    if (options[i] != kOptionFileCopy) {
      advertised.push_back(options[i]);
      advertised.push_back(options[i + 1]);
    }
  }
  m_advertised = deskflow::FileTransferBridge::enabled();
  if (m_advertised) {
    advertised.push_back(kOptionFileCopy);
    advertised.push_back(kFileCopyCapability);
  } else {
    m_negotiated = false;
    m_transfer.setNegotiated(false);
  }
  ClientProxy1_8::setOptions(advertised);
}

void ClientProxyFileTransfer::resetOptions()
{
  m_advertised = false;
  m_negotiated = false;
  m_transfer.setNegotiated(false);
  ClientProxy1_8::resetOptions();
}

bool ClientProxyFileTransfer::parseMessage(const uint8_t *code)
{
  if (std::memcmp(code, kMsgCFileCopyHello, 4) == 0) {
    if (!m_advertised || m_negotiated || getCurrentMessageSize() != 4 || !deskflow::FileTransferBridge::enabled()) {
      throw BadClientException();
    }
    ProtocolUtil::writef(getStream(), kMsgCFileCopyAck);
    m_negotiated = true;
    m_transfer.setNegotiated(true);
    return true;
  }
  if (std::memcmp(code, kMsgDFileCopy, 4) == 0) {
    if (!m_negotiated) {
      throw BadClientException();
    }
    const auto payload = deskflow::filetransfer::readFrame(getStream(), getCurrentMessageSize());
    if (m_server->canTransferFiles()) {
      m_transfer.receive(payload);
    } else {
      m_transfer.cancel(QStringLiteral("File copying requires exactly two computers with clipboard sharing enabled"));
    }
    return true;
  }
  return ClientProxy1_8::parseMessage(code);
}

void ClientProxyFileTransfer::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
#ifdef Q_OS_WIN
  // The secondary's Selection aliases its Windows clipboard. Once Files
  // are offered on ID 0, ID 1 must not send an empty legacy DCLP over them.
  if (id == kClipboardSelection &&
      deskflow::filetransfer::containsFiles(&m_clipboard[kClipboardClipboard].m_clipboard)) {
    m_clipboard[id].m_dirty = false;
    return;
  }
#endif
  if (id == kClipboardClipboard && m_clipboard[id].m_dirty && m_negotiated && m_server->canTransferFiles()) {
    m_transfer.offer(clipboard);
  }
  ClientProxy1_8::setClipboard(id, clipboard);
}

void ClientProxyFileTransfer::sourceClipboardChanged()
{
  m_transfer.sourceClipboardChanged();
}

void ClientProxyFileTransfer::onRemoteClipboardChanged(ClipboardID id)
{
  if (id == kClipboardClipboard) {
    sourceClipboardChanged();
  }
}

bool ClientProxyFileTransfer::canTransferFiles() const
{
  return m_negotiated && deskflow::FileTransferBridge::enabled();
}

bool ClientProxyFileTransfer::hasActiveFileTransfer() const
{
  return m_transfer.active();
}
