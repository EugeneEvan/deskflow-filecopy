/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ArchMultithreadWindowsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void cancellationUnwindsWorker_data();
  void cancellationUnwindsWorker();
};
