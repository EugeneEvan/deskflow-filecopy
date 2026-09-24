/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include <QObject>
#include <QSet>

class QLocalSocket;

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent);
  ~CoreIpcServer() override;

  static CoreIpcServer &instance();
  static bool available();
  void setConnectionEndpoints(const QString &json);

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
  QString m_connectionEndpoints = QStringLiteral("[]");
};

} // namespace deskflow::core::ipc
