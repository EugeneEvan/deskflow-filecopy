/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsHook.h"

#include <QTest>

class MSWindowsWheelTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void directWindowWheelIsRelayed_data();
  void directWindowWheelIsRelayed();
  void localWheelIsNotRelayed_data();
  void localWheelIsNotRelayed();

private:
  MSWindowsHook m_hook;
  HWND m_window = nullptr;
  ATOM m_windowClass = 0;
};
