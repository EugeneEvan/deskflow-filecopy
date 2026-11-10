/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "base/Log.h"
#include <QTemporaryDir>
#include <QTest>

class FileAgentRecoveryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void failureRevokesOldTransferLifetime();
  void failedTransportIsReleasedBeforeRetry();
  void unavailableAgentDoesNotReplayPendingClipboard();
  void retryWaitsForDeadlineAndBacksOff();
  void replacementReadyRestoresCapability();
  void readyTimeoutRevokesAndSchedulesRetry();
  void disabledSharingStopsRetry();

private:
  Log m_log;
  QTemporaryDir m_settings;
  QString m_originalSettings;
};
