/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class FileTransferWidgetTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void idleAndDisabledShowInstructions();
  void cancelWaitsForStateChange();
  void terminalNotificationsAreNotRepeated();
  void stoppedCoreInterruptsActiveTransfer();
  void unknownFileTotalIsNotShownAsZero();
  void confirmationMustArriveBeforeFullProgress();
  void stoppedProgressExpiresSpeedEstimate();
  void invalidStatusKeepsCurrentTransfer();
  void errorsArePlainText();
};
