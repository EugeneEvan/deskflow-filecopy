/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QTest>

class FileAgentProtocolTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void fragmentedFrames();
  void rejectsUnboundedAndUnknownMessages();
  void rejectsInvalidProgress();
  void publicationLifetime();
  void sourceNotifications();
  void sourceChangesInvalidatePendingStart();
  void sourceLifetimeRejectsLateNotifications();
  void sourcePermissionIsCheckedBeforeLifetimeChanges();
  void sourceMessagesAreBounded();
  void pipeEofDisablesTransport();
  void stalledPipeHasBoundedQueueAndStop();
};
