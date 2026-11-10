/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class FileTransferIndicatorTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void preparingAndVerifyingDoNotClaimPercentage();
  void transferUsesRealProgressAndClampsBeforeSuccess();
  void onlyActiveStatesAnimate();
  void failureWaitsForAcknowledgementAndRetryRecovers();
  void clipboardChangeClearsSuccessWithoutResurrectingIt();
  void disconnectInvalidatesOldStatus();
  void disconnectedFailureRemainsUntilAcknowledged();
  void cancelledAndIdleRestoreBaseIcon();
  void hoverTextUsesSharedPresentationWithoutAnimationUpdates();
  void rendersAtTraySizesAndPreservesBaseIcon();
  void terminalBadgesHaveDistinctColors();
};
