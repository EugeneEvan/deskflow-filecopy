/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class CoreProcessEndpointsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void snapshotSupportsBothFamilies();
  void endpointsBeforeConnectedArePreserved();
  void reconnectReplacesOldEndpoints();
  void disconnectClearsAddresses();
  void invalidSnapshotCannotDisplayConfiguredHost();

private:
  QTemporaryDir m_temp;
  QString m_seedFile;
  QByteArray m_previousConfigHome;
  QByteArray m_previousStateHome;
  bool m_seedCreated = false;
};
