/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QTest>

class FileTransferSessionTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void filesDirectoriesUnicodeAndReverseCopy();
  void checksumFailureDoesNotPublish();
  void traversalIsRejected();
  void reservedNameIsRejected();
  void duplicateNamesAreRejected();
  void senderBoundsWindowAndAcknowledgementAdvancesIt();
  void senderRejectsOutOfOrderAcknowledgement();
  void senderRejectsDuplicateAcknowledgement();
  void cancellationDuringPumpDiscardsQueuedFrames();
  void completionWaitsForFinalAcknowledgement();
  void wakeNotifiesWorkerResultsAndCoalescesUntilPump();
  void wakeCallbackCanCancelWithoutHoldingSessionMutex();
  void missingAcknowledgement_timesOutBothPeersAndRemovesPartialCache();
  void cancellationRemovesPartialCache();
  void cancellationReportsCleanupFailure_data();
  void cancellationReportsCleanupFailure();
  void cancelledWindowTailCannotInvalidateNextBatch();
  void rapidReplacementBeforeAbortDeliveryReplacesIncomingBatch_data();
  void rapidReplacementBeforeAbortDeliveryReplacesIncomingBatch();
  void duplicateBeginIsRejected();
  void completedBatchAbortBeforePumpPreventsPublication();
  void oldCompletedBatchAbortCannotInvalidateNextBatch();
  void disconnectKeepsPublishedFiles();
  void queuedCompletionCannotOverwriteNewClipboard();
  void batchLimitIsRejectedBeforeCreatingFiles();
  void simultaneousCopiesCancelBothDirections();
  void symlinkSourceIsRejected();
  void manySmallFilesAndLongPathsHaveAccurateCounts();
  void configuredCacheQuotaRejectsBeforeCreatingBatch();
  void insufficientDiskSpaceOnSmallVolume();
};
