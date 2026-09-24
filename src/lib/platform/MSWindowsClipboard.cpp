/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboard.h"

#include "base/Log.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"
#include "platform/MSWindowsClipboardFacade.h"
#include "platform/MSWindowsClipboardFileConverter.h"
#include "platform/MSWindowsClipboardHTMLConverter.h"
#include "platform/MSWindowsClipboardUTF16Converter.h"

#include <ShlObj_core.h>

//
// MSWindowsClipboard
//

UINT MSWindowsClipboard::s_ownershipFormat = 0;

MSWindowsClipboard::MSWindowsClipboard(HWND window)
    : m_window(window),
      m_time(0),
      m_facade(new MSWindowsClipboardFacade()),
      m_deleteFacade(true)
{
  // add converters, most desired first
  m_converters.push_back(new MSWindowsClipboardUTF16Converter);
  m_converters.push_back(new MSWindowsClipboardBitmapConverter);
  m_converters.push_back(new MSWindowsClipboardBitmapConverter(CF_DIBV5));
  m_converters.push_back(new MSWindowsClipboardHTMLConverter);
  m_converters.push_back(new MSWindowsClipboardFileConverter);
}

MSWindowsClipboard::~MSWindowsClipboard()
{
  clearConverters();

  // dependency injection causes confusion over ownership, so we need
  // logic to decide whether or not we delete the facade. there must
  // be a more elegant way of doing this.
  if (m_deleteFacade)
    delete m_facade;
}

void MSWindowsClipboard::setFacade(IMSWindowsClipboardFacade &facade)
{
  delete m_facade;
  m_facade = &facade;
  m_deleteFacade = false;
}

bool MSWindowsClipboard::emptyUnowned()
{
  LOG_DEBUG("empty clipboard");

  // empty the clipboard (and take ownership)
  if (!EmptyClipboard()) {
    // unable to cause this in integ tests, but this error has never
    // actually been reported by users.
    LOG_WARN("failed to grab clipboard");
    return false;
  }

  return true;
}

bool MSWindowsClipboard::empty()
{
  if (!emptyUnowned()) {
    return false;
  }

  // mark clipboard as being owned by deskflow
  HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 1);
  if (nullptr == SetClipboardData(getOwnershipFormat(), data)) {
    LOG_WARN("failed to set clipboard data");
    GlobalFree(data);
    return false;
  }

  return true;
}

void MSWindowsClipboard::add(Format format, const std::string &data)
{
  // exit early if there is no data to prevent spurious "failed to convert clipboard data" errors
  if (data.empty()) {
    LOG_DEBUG("not adding 0 bytes to clipboard format: %d", format);
    return;
  }
  bool isSucceeded = false;
  // convert data to win32 form
  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    IMSWindowsClipboardConverter *converter = *index;

    // skip converters for other formats
    if (converter->getFormat() == format) {
      HANDLE win32Data = converter->fromIClipboard(data);
      if (win32Data != nullptr) {
        LOG_DEBUG("add %d bytes to clipboard format: %d", data.size(), format);
        m_facade->write(win32Data, converter->getWin32Format());
        if (format == Format::Files && GetClipboardData(CF_HDROP) == win32Data) {
          // Cached files are always copied; Explorer must never move them.
          const UINT effectFormat = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
          HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
          if (effect != nullptr) {
            auto *value = static_cast<DWORD *>(GlobalLock(effect));
            if (value != nullptr && effectFormat != 0) {
              *value = DROPEFFECT_COPY;
              GlobalUnlock(effect);
              m_facade->write(effect, effectFormat);
            } else {
              if (value != nullptr) {
                GlobalUnlock(effect);
              }
              GlobalFree(effect);
              LOG_WARN("failed to prepare clipboard file copy effect");
            }
          } else {
            LOG_WARN("failed to allocate clipboard file copy effect");
          }
        }
        isSucceeded = true;
        break;
      } else {
        LOG_DEBUG("failed to convert clipboard data to platform format");
      }
    }
  }

  if (!isSucceeded) {
    LOG_DEBUG("missed clipboard data convert for format: %d", format);
  }
}

bool MSWindowsClipboard::open(Time time) const
{
  LOG_DEBUG("open clipboard");

  // The clipboard is a global mutex on Windows. We aren't always going to
  // get the lock on the first try, so try a few times before giving up.
  // Based on Chromium's ScopedClipboard::Acquire() retry loop.
  static const int kMaxRetries = 5;
  static const int kRetryDelayMs = 5;

  for (int i = 0; i < kMaxRetries; ++i) {
    if (m_facade->open(m_window)) {
      std::scoped_lock lock{m_mutex};
      m_time = time;
      m_readFailed = false;
      return true;
    }

    if (i < kMaxRetries - 1) {
      LOG_DEBUG("failed to open clipboard (attempt %d/%d, error=%d), retrying", i + 1, kMaxRetries, GetLastError());
      Sleep(kRetryDelayMs);
    }
  }

  LOG_WARN("failed to open clipboard after %d attempts: %d", kMaxRetries, GetLastError());
  return false;
}

void MSWindowsClipboard::close() const
{
  LOG_DEBUG("close clipboard");
  m_facade->close();
}

IClipboard::Time MSWindowsClipboard::getTime() const
{
  std::scoped_lock lock{m_mutex};
  return m_time;
}

bool MSWindowsClipboard::has(Format format) const
{
  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    IMSWindowsClipboardConverter *converter = *index;
    if (converter->getFormat() == format) {
      if (m_facade->isFormatAvailable(converter->getWin32Format())) {
        return true;
      }
    }
  }
  return false;
}

std::string MSWindowsClipboard::get(Format format) const
{
  bool available = false;
  for (const auto *converter : m_converters) {
    if (converter->getFormat() != format || !m_facade->isFormatAvailable(converter->getWin32Format())) {
      continue;
    }
    available = true;
    HANDLE data = m_facade->read(converter->getWin32Format());
    if (data == nullptr) {
      continue;
    }
    auto converted = converter->toIClipboard(data);
    if (!converted.empty() || format != Format::Bitmap) {
      return converted;
    }
  }

  if (available) {
    m_readFailed = true;
    LOG_WARN("failed to read advertised clipboard format");
  }
  return {};
}

bool MSWindowsClipboard::readSucceeded() const
{
  return !m_readFailed;
}

void MSWindowsClipboard::clearConverters()
{
  for (ConverterList::iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    delete *index;
  }
  m_converters.clear();
}

bool MSWindowsClipboard::isOwnedByDeskflow()
{
  // create ownership format if we haven't yet
  if (s_ownershipFormat == 0) {
    s_ownershipFormat = RegisterClipboardFormat(TEXT("Deskflow Ownership"));
  }
  return (IsClipboardFormatAvailable(getOwnershipFormat()) != 0);
}

UINT MSWindowsClipboard::getOwnershipFormat()
{
  // create ownership format if we haven't yet
  if (s_ownershipFormat == 0) {
    s_ownershipFormat = RegisterClipboardFormat(TEXT("Deskflow Ownership"));
  }

  // return the format
  return s_ownershipFormat;
}
