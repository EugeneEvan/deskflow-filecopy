/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFileConverter.h"

#include "base/Log.h"
#include "deskflow/FileTransferFormat.h"

#include <ShlObj_core.h>

#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

std::wstring toWide(std::string_view path, UINT codePage)
{
  if (path.empty() || path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int length = static_cast<int>(path.size());
  const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
  if (size == 0) {
    return {};
  }
  std::wstring result(size, L'\0');
  if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, path.data(), length, result.data(), size) != size) {
    return {};
  }
  return result;
}

std::string toUtf8(const std::wstring &path)
{
  if (path.empty() || path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int length = static_cast<int>(path.size());
  const int size =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, nullptr, 0, nullptr, nullptr);
  if (size == 0) {
    return {};
  }
  std::string result(size, '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, result.data(), size, nullptr, nullptr) !=
      size) {
    return {};
  }
  return result;
}

} // namespace

IClipboard::Format MSWindowsClipboardFileConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFileConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFileConverter::fromIClipboard(const std::string &data) const
{
  const auto refs = FileTransferFormat::unmarshall(data);
  if (refs.empty()) {
    return nullptr;
  }

  std::wstring widePaths;
  for (const auto &ref : refs) {
    const auto path = toWide(ref.path, CP_UTF8);
    if (path.empty() || path.find(L'\0') != std::wstring::npos || !std::filesystem::path(path).is_absolute()) {
      LOG_WARN("invalid file reference for CF_HDROP");
      return nullptr;
    }
    widePaths.append(path);
    widePaths.push_back(L'\0');
  }
  widePaths.push_back(L'\0');

  if (widePaths.size() > ((std::numeric_limits<size_t>::max)() - sizeof(DROPFILES)) / sizeof(wchar_t)) {
    return nullptr;
  }
  const size_t pathBytes = widePaths.size() * sizeof(wchar_t);
  HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + pathBytes);
  if (global == nullptr) {
    LOG_WARN("failed to allocate global memory for CF_HDROP data");
    return nullptr;
  }

  auto *dst = static_cast<char *>(GlobalLock(global));
  if (dst == nullptr) {
    GlobalFree(global);
    return nullptr;
  }

  DROPFILES dropFiles{};
  dropFiles.pFiles = sizeof(DROPFILES);
  dropFiles.fWide = TRUE;
  std::memcpy(dst, &dropFiles, sizeof(dropFiles));
  std::memcpy(dst + sizeof(dropFiles), widePaths.data(), pathBytes);
  GlobalUnlock(global);
  return global;
}

std::string MSWindowsClipboardFileConverter::toIClipboard(HANDLE data) const
{
  if (data == nullptr) {
    return {};
  }
  const SIZE_T size = GlobalSize(data);
  if (size < sizeof(DROPFILES)) {
    LOG_WARN("CF_HDROP header is truncated");
    return {};
  }
  const auto *raw = static_cast<const char *>(GlobalLock(data));
  if (raw == nullptr) {
    LOG_WARN("failed to lock CF_HDROP global memory");
    return {};
  }
  const auto unlock = [data](const char *) { GlobalUnlock(data); };
  const std::unique_ptr<const char, decltype(unlock)> locked(raw, unlock);

  DROPFILES header{};
  std::memcpy(&header, raw, sizeof(header));
  const size_t unitSize = header.fWide ? sizeof(wchar_t) : sizeof(char);
  if (header.pFiles < sizeof(DROPFILES) || header.pFiles > size || size - header.pFiles < 2 * unitSize ||
      (header.fWide && header.pFiles % sizeof(wchar_t) != 0)) {
    LOG_WARN("CF_HDROP file list offset is invalid");
    return {};
  }

  const char *const paths = raw + header.pFiles;
  const size_t units = (size - header.pFiles) / unitSize;
  const auto character = [paths, unitSize](size_t index) {
    wchar_t value = 0;
    std::memcpy(&value, paths + index * unitSize, unitSize);
    return value;
  };
  std::vector<FileRef> refs;
  size_t cursor = 0;
  while (cursor < units) {
    if (character(cursor) == L'\0') {
      // Even an empty list must have two NUL characters.
      if (cursor == 0 && character(1) != L'\0') {
        break;
      }
      return refs.empty() ? std::string() : FileTransferFormat::marshall(refs);
    }
    const size_t start = cursor;
    while (cursor < units && character(cursor) != L'\0') {
      ++cursor;
    }
    // Require room for the final extra NUL, within the HGLOBAL.
    if (cursor == units || cursor + 1 == units) {
      break;
    }
    std::wstring widePath;
    if (header.fWide) {
      widePath.resize(cursor - start);
      std::memcpy(widePath.data(), paths + start * unitSize, widePath.size() * unitSize);
    } else {
      widePath = toWide(std::string_view(paths + start, cursor - start), CP_ACP);
    }
    const auto utf8Path = toUtf8(widePath);
    if (utf8Path.empty() || !std::filesystem::path(widePath).is_absolute()) {
      LOG_WARN("CF_HDROP contains an invalid file path");
      return {};
    }
    FileRef ref{utf8Path, 0};
    // Narrow filesystem paths use the ANSI code page on Windows.
    const std::filesystem::path nativePath(widePath);
    std::error_code error;
    const auto status = std::filesystem::status(nativePath, error);
    if (!error && std::filesystem::is_regular_file(status)) {
      const auto fileSize = std::filesystem::file_size(nativePath, error);
      if (!error) {
        ref.size = fileSize;
      }
    }
    if (error) {
      LOG_WARN("CF_HDROP file metadata is unavailable");
    }
    refs.push_back(std::move(ref));
    ++cursor;
  }

  LOG_WARN("CF_HDROP file list is not double NUL terminated");
  return {};
}
