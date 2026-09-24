/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QTemporaryDir>
#include <QTest>

class CacheStatusWidgetTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void folderChangeDiscardsStaleScan();
  void invalidFolderShowsError();
  void unavailableDefaultFolderShowsError();

private:
  QTemporaryDir m_temp;
};
