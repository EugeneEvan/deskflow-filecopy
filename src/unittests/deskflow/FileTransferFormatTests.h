/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class FileTransferFormatTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void emptyListRoundTrip();
  void singleFileRoundTrip();
  void multipleFilesRoundTrip();
  void unicodePathsRoundTrip();
  void truncatedPayload();
  void unknownVersionMarker();
  void oversizedCountRejected();
  void malformedPayloadRejected();

private:
  Log m_log;
};
