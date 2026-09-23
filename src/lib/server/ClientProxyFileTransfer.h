/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransferBridge.h"
#include "server/ClientProxy1_8.h"

// Protocol 1.8 with an explicitly negotiated, optional file-copy extension.
class ClientProxyFileTransfer : public ClientProxy1_8
{
public:
  ClientProxyFileTransfer(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events);
  void setOptions(const OptionsList &options) override;
  void resetOptions() override;
  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  void sourceClipboardChanged();
  bool canTransferFiles() const;
  bool hasActiveFileTransfer() const;

protected:
  bool parseMessage(const uint8_t *code) override;
  void onRemoteClipboardChanged(ClipboardID id) override;

private:
  deskflow::FileTransferBridge m_transfer;
  bool m_advertised = false;
  bool m_negotiated = false;
};
