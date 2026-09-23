/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>
#include <QTemporaryDir>

class ServerProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void handleKeepAliveAlarm_timeout_queuesDisconnectRequest();
  void handleData_incompleteMessage_queuesDisconnectRequest();
  void parseHandshakeMessage_protocolError_queuesRefusalRequest();
  void fileCopyOptionsRequireMatchingCapability();
  void fileCopyDisabledDoesNotSendHello();
  void fileCopyAcknowledgmentUsesMessageParser();

private:
  Log m_log;
  QTemporaryDir m_settingsDirectory;
  QString m_originalSettings;
};
