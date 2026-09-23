/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Converts between the "Files" clipboard format and the Windows CF_HDROP
//! clipboard format (standard file/folder list used by Explorer)
class MSWindowsClipboardFileConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardFileConverter() = default;
  ~MSWindowsClipboardFileConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &data) const override;
  std::string toIClipboard(HANDLE data) const override;
};
