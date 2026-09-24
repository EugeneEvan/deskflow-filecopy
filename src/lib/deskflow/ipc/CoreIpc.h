/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QJsonArray>
#include <QString>

void ipcSendToClient(const QString &command, const QString &args = "", bool replacePending = false);
void ipcSendConnectionState(deskflow::core::ConnectionState state);
void ipcSendConnectionEndpoints(const QJsonArray &endpoints);
