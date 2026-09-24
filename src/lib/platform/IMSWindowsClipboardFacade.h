/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class IMSWindowsClipboardConverter;

class IMSWindowsClipboardFacade
{
public:
  virtual bool open(HWND window) const = 0;
  virtual void close() const = 0;
  virtual bool isFormatAvailable(UINT format) const = 0;
  virtual HANDLE read(UINT format) const = 0;
  virtual void write(HANDLE win32Data, UINT win32Format) = 0;
  virtual ~IMSWindowsClipboardFacade() = default;
};
