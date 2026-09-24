/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/IMSWindowsClipboardFacade.h"

class MSWindowsClipboardFacade : public IMSWindowsClipboardFacade
{
public:
  bool open(HWND window) const override;
  void close() const override;
  bool isFormatAvailable(UINT format) const override;
  HANDLE read(UINT format) const override;
  void write(HANDLE win32Data, UINT win32Format) override;
};
