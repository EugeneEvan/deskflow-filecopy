/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>
#include <QTemporaryDir>

class ClientProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void keyDown_data();
  void keyDown();
  void keyRepeat_data();
  void keyRepeat();
  void keyUp_data();
  void keyUp();
  void fileCopyNegotiatesThroughMessageParser();
  void fileCopyDisabledDoesNotAdvertiseOrAcknowledge();
  void fileCopyRejectsDataBeforeNegotiation();
  void fileSelectionDoesNotSendLegacyClipboard();

private:
  Log m_log;
  QTemporaryDir m_settingsDirectory;
  QString m_originalSettings;
};
