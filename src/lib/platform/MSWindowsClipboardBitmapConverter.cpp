/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardBitmapConverter.h"

#include "base/Log.h"

#include <QScopeGuard>
#include <QtEndian>

#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
bool normaliseMalformedMacDib(const std::string &data, std::string &normalisedData)
{
  if (data.size() < sizeof(BITMAPINFOHEADER)) {
    return false;
  }

  const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(data.data());
  if (header->biWidth <= 0) {
    return false;
  }

  const auto width = static_cast<size_t>(header->biWidth);
  const auto height = static_cast<size_t>(std::abs(static_cast<int64_t>(header->biHeight)));
  if (height == 0 || width > (std::numeric_limits<size_t>::max() - sizeof(BITMAPINFOHEADER)) / 4 / height) {
    return false;
  }
  const auto expectedSize = sizeof(BITMAPINFOHEADER) + width * height * 4;

  // macOS can describe an INFOHEADER-sized 32-bit pixel payload as a V5 DIB.
  // Windows then interprets the first pixels as V5 colour masks. The pixel
  // bytes are ordinary BGRA, so publish a canonical BI_RGB DIB instead.
  if (header->biSize <= sizeof(BITMAPINFOHEADER) || header->biPlanes != 1 || header->biBitCount != 32 ||
      header->biCompression != BI_BITFIELDS || expectedSize != data.size()) {
    return false;
  }

  normalisedData = data.substr(0, sizeof(BITMAPINFOHEADER));
  qToLittleEndian<quint32>(sizeof(BITMAPINFOHEADER), reinterpret_cast<quint8 *>(&normalisedData[0]));
  qToLittleEndian<quint32>(BI_RGB, reinterpret_cast<quint8 *>(&normalisedData[0]) + 16);
  normalisedData += data.substr(sizeof(BITMAPINFOHEADER));
  LOG_INFO("normalised malformed macOS clipboard image to BI_RGB");
  return true;
}
} // namespace

//
// MSWindowsClipboardBitmapConverter
//

IClipboard::Format MSWindowsClipboardBitmapConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

UINT MSWindowsClipboardBitmapConverter::getWin32Format() const
{
  return m_format;
}

HANDLE
MSWindowsClipboardBitmapConverter::fromIClipboard(const std::string &data) const
{
  // V5 is a native read fallback. Received canonical DIBs use an INFOHEADER,
  // so must never be published under the CF_DIBV5 identifier.
  if (m_format != CF_DIB) {
    return nullptr;
  }
  std::string normalisedData;
  const auto *clipboardData = &data;
  if (normaliseMalformedMacDib(data, normalisedData)) {
    clipboardData = &normalisedData;
  }

  // copy to memory handle
  HGLOBAL gData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, clipboardData->size());
  if (gData != nullptr) {
    // get a pointer to the allocated memory
    char *dst = (char *)GlobalLock(gData);
    if (dst != nullptr) {
      memcpy(dst, clipboardData->data(), clipboardData->size());
      GlobalUnlock(gData);
    } else {
      GlobalFree(gData);
      gData = nullptr;
    }
  }

  return gData;
}

std::string MSWindowsClipboardBitmapConverter::toIClipboard(HANDLE data) const
{
  const size_t srcSize = GlobalSize(data);
  if (srcSize < sizeof(BITMAPINFOHEADER) || srcSize > (std::numeric_limits<uint32_t>::max)()) {
    LOG_WARN("invalid clipboard bitmap allocation size");
    return {};
  }
  const auto *src = static_cast<const char *>(GlobalLock(data));
  if (src == nullptr) {
    return {};
  }
  const auto unlock = qScopeGuard([data] { GlobalUnlock(data); });

  BITMAPINFOHEADER header{};
  std::memcpy(&header, src, sizeof(header));
  if ((header.biSize != sizeof(BITMAPINFOHEADER) && header.biSize != 52 && header.biSize != 56 &&
       header.biSize != sizeof(BITMAPV4HEADER) && header.biSize != sizeof(BITMAPV5HEADER)) ||
      header.biSize > srcSize || header.biWidth <= 0 || header.biHeight == 0 ||
      header.biHeight == (std::numeric_limits<LONG>::min)() || header.biPlanes != 1) {
    LOG_WARN("invalid clipboard bitmap header");
    return {};
  }

  const bool rgb =
      header.biCompression == BI_RGB && (header.biBitCount == 1 || header.biBitCount == 4 || header.biBitCount == 8 ||
                                         header.biBitCount == 16 || header.biBitCount == 24 || header.biBitCount == 32);
  const bool bitfields = header.biCompression == BI_BITFIELDS && (header.biBitCount == 16 || header.biBitCount == 32);
  const bool rle = header.biHeight > 0 && header.biSizeImage != 0 &&
                   ((header.biCompression == BI_RLE4 && header.biBitCount == 4) ||
                    (header.biCompression == BI_RLE8 && header.biBitCount == 8));
  if (!rgb && !bitfields && !rle) {
    LOG_WARN("unsupported clipboard bitmap encoding");
    return {};
  }

  // INFOHEADER stores masks after its header; V4/V5 already contain them.
  // Advancing over external masks for V5 would skip the first three pixels.
  uint64_t offset = header.biSize;
  if (bitfields && header.biSize == sizeof(BITMAPINFOHEADER)) {
    offset += 3 * sizeof(DWORD);
  }
  uint64_t colours = header.biClrUsed;
  if (header.biBitCount <= 8) {
    const uint64_t maximumColours = uint64_t{1} << header.biBitCount;
    if (colours > maximumColours) {
      LOG_WARN("invalid clipboard bitmap colour table");
      return {};
    }
    if (colours == 0) {
      colours = maximumColours;
    }
  }
  offset += colours * sizeof(RGBQUAD);
  if (offset > srcSize) {
    LOG_WARN("truncated clipboard bitmap colour table");
    return {};
  }

  const auto width = static_cast<uint64_t>(header.biWidth);
  const auto height = static_cast<uint64_t>(std::abs(static_cast<int64_t>(header.biHeight)));
  const uint64_t rowSize = ((width * header.biBitCount + 31) / 32) * 4;
  const uint64_t pixelSize = rle ? header.biSizeImage : rowSize * height;
  if (header.biSize == sizeof(BITMAPV5HEADER)) {
    BITMAPV5HEADER v5{};
    std::memcpy(&v5, src, sizeof(v5));
    if ((v5.bV5CSType == PROFILE_EMBEDDED || v5.bV5CSType == PROFILE_LINKED) && v5.bV5ProfileSize != 0) {
      const uint64_t profileEnd = uint64_t{v5.bV5ProfileData} + v5.bV5ProfileSize;
      if (v5.bV5ProfileData < offset || profileEnd > srcSize) {
        LOG_WARN("invalid clipboard bitmap colour profile");
        return {};
      }
      if (v5.bV5ProfileData == offset) {
        offset = profileEnd;
      } else if (v5.bV5ProfileData < offset + pixelSize) {
        LOG_WARN("clipboard bitmap colour profile overlaps pixels");
        return {};
      }
    }
  }
  if (pixelSize > srcSize - offset) {
    LOG_WARN("truncated clipboard bitmap pixels");
    return {};
  }

  LOG_INFO("bitmap: %dx%d %d", header.biWidth, header.biHeight, header.biBitCount);
  if (rgb && (header.biBitCount == 24 || header.biBitCount == 32)) {
    return std::string(src, srcSize);
  }

  const uint64_t outputSize = width * height * 4;
  if (outputSize > (std::numeric_limits<uint32_t>::max)() - sizeof(BITMAPINFOHEADER)) {
    LOG_WARN("clipboard bitmap is too large to convert");
    return {};
  }
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(BITMAPINFOHEADER);
  info.biWidth = header.biWidth;
  info.biHeight = header.biHeight;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biXPelsPerMeter = 1000;
  info.biYPelsPerMeter = 1000;

  const char *srcBits = src + static_cast<size_t>(offset);
  if (bitfields && header.biBitCount == 32) {
    DWORD masks[3]{};
    std::memcpy(masks, src + sizeof(BITMAPINFOHEADER), sizeof(masks));
    if (masks[0] == 0x00ff0000 && masks[1] == 0x0000ff00 && masks[2] == 0x000000ff) {
      // Common screenshot/browser DIBV5 data is already BGRA. Preserve alpha
      // and avoid a GDI round-trip that can discard it.
      std::string image(reinterpret_cast<const char *>(&info), sizeof(info));
      image.append(srcBits, static_cast<size_t>(outputSize));
      return image;
    }
  }

  LOG_INFO("convert image from: depth=%d comp=%d", header.biBitCount, header.biCompression);
  HDC dc = GetDC(nullptr);
  if (dc == nullptr) {
    LOG_WARN("failed to acquire device context for clipboard image");
    return {};
  }
  const auto releaseDC = qScopeGuard([dc] { ReleaseDC(nullptr, dc); });
  void *raw = nullptr;
  HBITMAP dst = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &raw, nullptr, 0);
  const auto deleteBitmap = qScopeGuard([dst] {
    if (dst != nullptr)
      DeleteObject(dst);
  });
  if (dst == nullptr || raw == nullptr) {
    LOG_WARN("failed to allocate destination bitmap for clipboard image");
    return {};
  }

  HDC dstDC = CreateCompatibleDC(dc);
  if (dstDC == nullptr) {
    LOG_WARN("failed to allocate clipboard image device context");
    return {};
  }
  const auto deleteDC = qScopeGuard([dstDC] { DeleteDC(dstDC); });
  HGDIOBJ oldBitmap = SelectObject(dstDC, dst);
  if (oldBitmap == nullptr || oldBitmap == HGDI_ERROR) {
    LOG_WARN("failed to select clipboard image bitmap");
    return {};
  }
  const auto restoreBitmap = qScopeGuard([dstDC, oldBitmap] { SelectObject(dstDC, oldBitmap); });
  const auto lines = static_cast<UINT>(height);
  const int copied = SetDIBitsToDevice(
      dstDC, 0, 0, header.biWidth, lines, 0, 0, 0, lines, srcBits, reinterpret_cast<const BITMAPINFO *>(src),
      DIB_RGB_COLORS
  );
  if (copied != static_cast<int>(lines)) {
    LOG_WARN("failed to render clipboard image pixels");
    return {};
  }
  GdiFlush();

  std::string image(reinterpret_cast<const char *>(&info), sizeof(info));
  image.append(static_cast<const char *>(raw), static_cast<size_t>(outputSize));
  return image;
}
