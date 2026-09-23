/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"
#include "deskflow/FileTransferBridge.h"

// Protocol 1.8 with an explicitly negotiated, optional file-copy extension.
class ServerProxyFileTransfer : public ServerProxy1_8
{
public:
  ServerProxyFileTransfer(Client *client, deskflow::IStream *stream, IEventQueue *events);
  bool onGrabClipboard(ClipboardID id) override;
  void onClipboardChanged(ClipboardID id, const IClipboard *clipboard) override;
  bool canTransferFiles() const;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;
  void onOptionsChanged(const OptionsList &options) override;
  void onOptionsReset() override;
  void onRemoteClipboardChanged(ClipboardID id) override;

private:
  deskflow::FileTransferBridge m_transfer;
  bool m_requested = false;
  bool m_negotiated = false;
};
