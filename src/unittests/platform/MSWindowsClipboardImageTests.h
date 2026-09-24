/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class MSWindowsClipboardImageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void canonicalBitmapPreservesPixels_data();
  void canonicalBitmapPreservesPixels();
  void extendedBitfieldsUsesEmbeddedMasks_data();
  void extendedBitfieldsUsesEmbeddedMasks();
  void infoHeaderUsesExternalMasks();
  void v5ProfileDoesNotShiftPixels_data();
  void v5ProfileDoesNotShiftPixels();
  void invalidBitmapRejected_data();
  void invalidBitmapRejected();
  void failedDibFallsBackToV5_data();
  void failedDibFallsBackToV5();
  void nativeV5OnlyCanBeRead();
  void failedImageReadIsReportedAndCanRetry();
  void failedOpenKeepsDestination();
  void emptyTextRemainsValid();
  void imageIsPublishedAsCanonicalDib();

private:
  Log m_log;
};
