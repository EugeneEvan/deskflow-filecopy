/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//! A single file reference carried inside the "Files" clipboard format
struct FileRef
{
  std::string path;  // absolute path, UTF-8 encoded
  uint64_t size = 0; // file size in bytes, 0 if unknown/not a regular file
};

//! Encodes/decodes a list of file references into the binary payload
//! stored under IClipboard::Format::Files
class FileTransferFormat
{
public:
  //! Serialize a list of file references
  //!
  //! Layout (big-endian):
  //!   [1 byte] 0x01 (version marker)
  //!   [4 bytes] file count N
  //!   repeated N times:
  //!     [4 bytes] path length in bytes
  //!     [path-length bytes] path (UTF-8)
  //!     [8 bytes] file size in bytes
  static std::string marshall(const std::vector<FileRef> &files);

  //! Decode a payload produced by marshall().
  //! Returns an empty vector if the payload is malformed (bad version marker,
  //! truncated fields, or counts that exceed the remaining buffer).
  static std::vector<FileRef> unmarshall(std::string_view data);

private:
  static uint64_t readUInt64(std::string_view &data);
};
