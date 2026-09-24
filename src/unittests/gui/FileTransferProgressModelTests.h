/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class FileTransferProgressModelTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void firstSampleHasNoRate();
  void shortTransferRateAndEta();
  void rateUsesRecentSamples();
  void stalledTransferClearsEtaAndRecovers();
  void newTaskAndDirectionResetRate();
  void preparingAndTerminalStatesHaveNoRate();
  void countersPreserve64BitValues();
  void invalidStatusDoesNotReplaceValidStatus();
  void fileCountsAllowUnknownTotalAndEmptyFiles();
  void allBytesDoNotMeanReady();
  void counterRollbackStartsFreshSamples();
};
