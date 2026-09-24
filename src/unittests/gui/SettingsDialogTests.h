/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class SettingsDialogTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void cleanupTestCase();
  void transferShortcutSelectsSupportedPage();
  void serviceTogglePreservesStagedTls();
  void restoreDefaultsKeepsWindowChoicesIndependent();
  void resetRestoresStoredPreferences_data();
  void resetRestoresStoredPreferences();
  void cancelDoesNotSaveEdits();
  void saveAppliesStagedPreferences();
  void saveLanguageAppliesTranslation();
  void cacheEditsAreStagedUntilSave_data();
  void cacheEditsAreStagedUntilSave();

private:
  QTemporaryDir m_temp;
  QString m_seedFile;
  QByteArray m_previousConfigHome;
  QByteArray m_previousStateHome;
  bool m_seedCreated = false;
};
