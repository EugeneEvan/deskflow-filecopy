/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferFormatTests.h"

#include "deskflow/FileTransferFormat.h"

#include <vector>

void FileTransferFormatTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);
}

void FileTransferFormatTests::emptyListRoundTrip()
{
  const auto refs = FileTransferFormat::marshall({});
  QCOMPARE(static_cast<uint8_t>(refs[0]), 0x01);
  QCOMPARE(refs.size(), size_t(5));

  const auto decoded = FileTransferFormat::unmarshall(refs);
  QVERIFY(decoded.empty());
}

void FileTransferFormatTests::singleFileRoundTrip()
{
  std::vector<FileRef> files{FileRef{"/home/user/document.txt", 1234}};
  const auto refs = FileTransferFormat::marshall(files);
  const auto decoded = FileTransferFormat::unmarshall(refs);

  QCOMPARE(decoded.size(), size_t(1));
  QCOMPARE(decoded[0].path, "/home/user/document.txt");
  QCOMPARE(decoded[0].size, uint64_t(1234));
}

void FileTransferFormatTests::multipleFilesRoundTrip()
{
  std::vector<FileRef> files;
  files.push_back(FileRef{"C:\\Users\\test\\file1.bin", 100});
  files.push_back(FileRef{"C:\\Users\\test\\subdir", 0});
  files.push_back(FileRef{"/very/long/unicode/путь/файл.txt", 9999999999ULL});

  const auto refs = FileTransferFormat::marshall(files);
  const auto decoded = FileTransferFormat::unmarshall(refs);

  QCOMPARE(decoded.size(), files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    QCOMPARE(decoded[i].path, files[i].path);
    QCOMPARE(decoded[i].size, files[i].size);
  }
}

void FileTransferFormatTests::unicodePathsRoundTrip()
{
  std::vector<FileRef> files{FileRef{"D:\\视频\\视频文件_测试.pdf", 42}};
  const auto refs = FileTransferFormat::marshall(files);
  const auto decoded = FileTransferFormat::unmarshall(refs);

  QCOMPARE(decoded.size(), size_t(1));
  QCOMPARE(decoded[0].path, "D:\\视频\\视频文件_测试.pdf");
}

void FileTransferFormatTests::truncatedPayload()
{
  std::vector<FileRef> files{FileRef{"/some/path/file.txt", 42}};
  const auto full = FileTransferFormat::marshall(files);
  // chop the last byte: file size field now truncated
  const std::string_view truncated(full.data(), full.size() - 1);
  const auto decoded = FileTransferFormat::unmarshall(truncated);
  QVERIFY(decoded.empty());
}

void FileTransferFormatTests::unknownVersionMarker()
{
  std::string payload(5, '\0');
  payload[0] = 0x99;
  const auto decoded = FileTransferFormat::unmarshall(payload);
  QVERIFY(decoded.empty());
}

void FileTransferFormatTests::oversizedCountRejected()
{
  // version marker + count of 0xFFFFFFFF + no entries
  std::string payload;
  payload.push_back(0x01);
  payload += std::string(4, '\xFF');
  const auto decoded = FileTransferFormat::unmarshall(payload);
  QVERIFY(decoded.empty());
}

void FileTransferFormatTests::malformedPayloadRejected()
{
  // version marker + count of 1, but no path bytes at all
  std::string payload;
  payload.push_back(0x01);
  payload += std::string("\x00\x00\x00\x01", 4);
  const auto decoded = FileTransferFormat::unmarshall(payload);
  QVERIFY(decoded.empty());
}

QTEST_MAIN(FileTransferFormatTests)
#include "FileTransferFormatTests.moc"
