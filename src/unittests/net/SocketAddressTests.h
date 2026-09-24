/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class SocketAddressTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void connectedEndpoints_data();
  void connectedEndpoints();
  void listeningSocketHasNoPeer();
};
