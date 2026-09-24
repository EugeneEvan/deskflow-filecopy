/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/Constants.h"

#include <QString>

namespace deskflow::gui::ipc {

CoreIpcClient::CoreIpcClient(QObject *parent) : IpcClient(parent, kCoreIpcName, QStringLiteral("core"))
{
  connect(this, &CoreIpcClient::connected, this, [this] { sendMessage(QStringLiteral("getConnectionEndpoints")); });
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  // JSON error details and Windows paths can contain '='.
  const auto args = parts.size() >= 2 ? parts.mid(1).join('=') : QString();
  Q_EMIT commandReceived(command, args);
}

void CoreIpcClient::sendCancelFileTransfer()
{
  sendMessage(QStringLiteral("cancelFileTransfer"));
}

} // namespace deskflow::gui::ipc
