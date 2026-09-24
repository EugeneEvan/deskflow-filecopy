/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTest>

class SecureSocketTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void boundedReadDrainsCurrentTlsRecord_data();
  void boundedReadDrainsCurrentTlsRecord();

private:
  Arch m_arch;
  Log m_log;
};
