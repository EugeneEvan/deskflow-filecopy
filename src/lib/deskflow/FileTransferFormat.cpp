/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferFormat.h"

#include "base/Log.h"

#include <cstring>

namespace {

constexpr uint8_t kVersionMarker = 0x01;

void writeUInt32(std::string *buffer, uint32_t value)
{
  *buffer += static_cast<uint8_t>((value >> 24) & 0xff);
  *buffer += static_cast<uint8_t>((value >> 16) & 0xff);
  *buffer += static_cast<uint8_t>((value >> 8) & 0xff);
  *buffer += static_cast<uint8_t>(value & 0xff);
}

void writeUInt64(std::string *buffer, uint64_t value)
{
  writeUInt32(buffer, static_cast<uint32_t>(value >> 32));
  writeUInt32(buffer, static_cast<uint32_t>(value & 0xffffffff));
}

} // namespace

std::string FileTransferFormat::marshall(const std::vector<FileRef> &files)
{
  std::string data;
  data.reserve(5 + files.size() * (4 + 8 + 256)); // rough estimate, path len + path + size

  data += static_cast<char>(kVersionMarker);
  writeUInt32(&data, static_cast<uint32_t>(files.size()));

  for (const auto &file : files) {
    writeUInt32(&data, static_cast<uint32_t>(file.path.size()));
    data += file.path;
    writeUInt64(&data, file.size);
  }

  return data;
}

std::vector<FileRef> FileTransferFormat::unmarshall(std::string_view data)
{
  std::vector<FileRef> result;

  if (data.size() < 5) {
    return result;
  }
  if (static_cast<uint8_t>(data[0]) != kVersionMarker) {
    LOG_WARN("file transfer payload: unknown version marker 0x%02x", static_cast<uint8_t>(data[0]));
    return result;
  }

  const uint8_t *cursor = reinterpret_cast<const uint8_t *>(data.data()) + 1;
  const uint8_t *const end = reinterpret_cast<const uint8_t *>(data.data()) + data.size();

  const uint64_t count = (static_cast<uint64_t>(cursor[0]) << 24) | (static_cast<uint64_t>(cursor[1]) << 16) |
                         (static_cast<uint64_t>(cursor[2]) << 8) | static_cast<uint64_t>(cursor[3]);
  cursor += 4;

  // Guard against absurd counts (e.g. 0xFFFFFFFF) that would attempt a
  // multi-gigabyte reserve and throw std::bad_alloc.
  if (count > 100000) {
    LOG_WARN("file transfer payload: file count %llu exceeds limit", static_cast<unsigned long long>(count));
    return result;
  }

  result.reserve(static_cast<size_t>(count));

  for (uint64_t i = 0; i < count; ++i) {
    // need at least 4 bytes for the path length
    if (end - cursor < 4) {
      LOG_WARN("file transfer payload: truncated path length at file %llu", static_cast<unsigned long long>(i));
      result.clear();
      return result;
    }

    const uint64_t pathLen = (static_cast<uint64_t>(cursor[0]) << 24) | (static_cast<uint64_t>(cursor[1]) << 16) |
                             (static_cast<uint64_t>(cursor[2]) << 8) | static_cast<uint64_t>(cursor[3]);
    cursor += 4;

    if (end - cursor < static_cast<int64_t>(pathLen + 8)) {
      LOG_WARN(
          "file transfer payload: truncated entry at file %llu (need %llu, have %lld)",
          static_cast<unsigned long long>(i), static_cast<unsigned long long>(pathLen + 8),
          static_cast<long long>(end - cursor)
      );
      result.clear();
      return result;
    }

    FileRef file;
    file.path.assign(reinterpret_cast<const char *>(cursor), static_cast<size_t>(pathLen));
    cursor += pathLen;

    file.size = (static_cast<uint64_t>(cursor[0]) << 56) | (static_cast<uint64_t>(cursor[1]) << 48) |
                (static_cast<uint64_t>(cursor[2]) << 40) | (static_cast<uint64_t>(cursor[3]) << 32) |
                (static_cast<uint64_t>(cursor[4]) << 24) | (static_cast<uint64_t>(cursor[5]) << 16) |
                (static_cast<uint64_t>(cursor[6]) << 8) | static_cast<uint64_t>(cursor[7]);
    cursor += 8;

    result.push_back(std::move(file));
  }

  return result;
}
