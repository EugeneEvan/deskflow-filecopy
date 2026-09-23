/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardTests.h"

#include "platform/MSWindowsClipboard.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"
#include "platform/MSWindowsClipboardFileConverter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <ShlObj_core.h>
#include <cstring>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

#include "deskflow/Clipboard.h"
#include "deskflow/FileTransferFormat.h"

namespace {

using GlobalMemory = std::unique_ptr<void, decltype(&GlobalFree)>;

std::string dropPayload(const std::string &paths, bool wide, DWORD offset = sizeof(DROPFILES))
{
  DROPFILES header{};
  header.pFiles = offset;
  header.fWide = wide;
  std::string result(offset, '\x7f');
  std::memcpy(result.data(), &header, sizeof(header));
  result.append(paths);
  return result;
}

std::string convertDrop(const std::string &bytes)
{
  GlobalMemory data(GlobalAlloc(GMEM_MOVEABLE, bytes.size()), &GlobalFree);
  if (!data) {
    return {};
  }
  void *raw = GlobalLock(data.get());
  if (raw == nullptr) {
    return {};
  }
  // Do not let allocator padding accidentally supply missing terminators.
  std::memset(raw, 0x7f, GlobalSize(data.get()));
  std::memcpy(raw, bytes.data(), bytes.size());
  GlobalUnlock(data.get());
  return MSWindowsClipboardFileConverter().toIClipboard(data.get());
}

std::string wideBytes(const std::wstring &text)
{
  return {reinterpret_cast<const char *>(text.data()), text.size() * sizeof(wchar_t)};
}

} // namespace

void MSWindowsClipboardTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);

  MSWindowsClipboard clipboard(NULL);

  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::cleanupTestCase()
{
  initTestCase();
}

void MSWindowsClipboardTests::emptyUnusedClipboard()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.emptyUnowned());
}

void MSWindowsClipboardTests::emptyOpenCalled()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::emptySingleFormat()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.empty());
  QVERIFY(!clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::addValue()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::replaceValue()
{
  using enum IClipboard::Format;

  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(Text, m_testString);
  clipboard.add(Text, m_testString2);

  QCOMPARE(clipboard.get(Text), m_testString2);
}

void MSWindowsClipboardTests::openTimeIsOne()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
}

void MSWindowsClipboardTests::closeIsOpen()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  clipboard.close();
}

void MSWindowsClipboardTests::getTimeOpenWithNoEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  // this behavior is different to that of Clipboard which only
  // returns the value passed into open(t) after empty() is called.
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::getTimeOpenAndEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::has_withFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::has_withNoFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.get(IClipboard::Format::Text), "");
}

void MSWindowsClipboardTests::getNonEmptyText()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::isOwnedByDeskflow()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.isOwnedByDeskflow());
}

void MSWindowsClipboardTests::normalisesMalformedMacBitmap()
{
  // A 1x1 top-down macOS DIB that incorrectly declares a V5 header.
  constexpr qsizetype headerSize = sizeof(BITMAPINFOHEADER);
  std::string dib(headerSize + 4, '\0');
  auto *raw = reinterpret_cast<quint8 *>(&dib[0]);
  qToLittleEndian<quint32>(sizeof(BITMAPV5HEADER), raw); // claims V5 but only has an INFOHEADER
  qToLittleEndian<quint32>(1, raw + 4);
  qToLittleEndian<quint32>(-1, raw + 8);
  qToLittleEndian<quint16>(1, raw + 12);
  qToLittleEndian<quint16>(32, raw + 14);
  qToLittleEndian<quint32>(BI_BITFIELDS, raw + 16);
  raw[headerSize + 3] = 0xff;

  MSWindowsClipboardBitmapConverter converter;
  const auto handle = converter.fromIClipboard(dib);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), SIZE_T(headerSize + 4));
  const auto *result = static_cast<const quint8 *>(GlobalLock(handle));
  QVERIFY(result != nullptr);
  QCOMPARE(qFromLittleEndian<quint32>(result), quint32(headerSize));
  QCOMPARE(qFromLittleEndian<quint32>(result + 16), quint32(BI_RGB));
  QCOMPARE(result[headerSize + 3], quint8(0xff));
  GlobalUnlock(handle);
  GlobalFree(handle);
}

void MSWindowsClipboardTests::preservesHealthyMacV5Bitmap()
{
  // A complete 1x1 top-down macOS V5 DIB with BGRA colour masks and one pixel.
  constexpr qsizetype headerSize = sizeof(BITMAPV5HEADER);
  std::string dib(headerSize + 4, '\0');
  auto *raw = reinterpret_cast<quint8 *>(&dib[0]);
  qToLittleEndian<quint32>(headerSize, raw);
  qToLittleEndian<quint32>(1, raw + 4);
  qToLittleEndian<quint32>(-1, raw + 8);
  qToLittleEndian<quint16>(1, raw + 12);
  qToLittleEndian<quint16>(32, raw + 14);
  qToLittleEndian<quint32>(BI_BITFIELDS, raw + 16);
  qToLittleEndian<quint32>(0x00ff0000, raw + 40);
  qToLittleEndian<quint32>(0x0000ff00, raw + 44);
  qToLittleEndian<quint32>(0x000000ff, raw + 48);
  qToLittleEndian<quint32>(0xff000000, raw + 52);
  raw[headerSize] = 0x12;
  raw[headerSize + 1] = 0x34;
  raw[headerSize + 2] = 0x56;
  raw[headerSize + 3] = 0x78;

  MSWindowsClipboardBitmapConverter converter;
  const auto handle = converter.fromIClipboard(dib);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), SIZE_T(dib.size()));
  const auto *result = static_cast<const char *>(GlobalLock(handle));
  QVERIFY(result != nullptr);
  QCOMPARE(std::string(result, GlobalSize(handle)), dib);
  GlobalUnlock(handle);
  GlobalFree(handle);
}

void MSWindowsClipboardTests::filesRoundTrip()
{
  MSWindowsClipboardFileConverter converter;

  // Round-trip through the converter directly (not the system clipboard),
  // avoiding the platform-specific CF_HDROP registration requirement that
  // the full MSWindowsClipboard::add() path exercises.
  const std::string tempDir = QDir::toNativeSeparators(QDir::tempPath()).toUtf8().toStdString();
  const std::vector<FileRef> refs{{tempDir, 0}};
  const std::string encoded = FileTransferFormat::marshall(refs);

  const HANDLE handle = converter.fromIClipboard(encoded);
  QVERIFY(handle != nullptr);

  const std::string roundTrip = converter.toIClipboard(handle);
  QVERIFY(!roundTrip.empty());

  const auto decoded = FileTransferFormat::unmarshall(roundTrip);
  QCOMPARE(decoded.size(), size_t(1));
  QCOMPARE(decoded[0].path, tempDir);
  QCOMPARE(decoded[0].size, uint64_t(0));

  // Converters do not own the handle passed to fromIClipboard() — the
  // caller (MSWindowsClipboard) is responsible for freeing it.
  GlobalFree(handle);
}

void MSWindowsClipboardTests::filesGetEmptyWhenAbsent()
{
  MSWindowsClipboard clipboard(nullptr);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.emptyUnowned());
  // When no CF_HDROP format is present in the system clipboard, get() must
  // return an empty string rather than crash or return garbage.
  QCOMPARE(clipboard.get(IClipboard::Format::Files), "");
}

void MSWindowsClipboardTests::filesUnicodeAndDirectories()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString filePath = QDir::toNativeSeparators(directory.filePath(QStringLiteral("\u6587\u4ef6-\U0001f600.txt")));
  const QString folderPath = QDir::toNativeSeparators(directory.filePath(QStringLiteral("\u7a7a\u76ee\u5f55")));
  QVERIFY(QDir().mkdir(folderPath));
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write("payload"), qint64(7));
  file.close();

  const std::vector<FileRef> refs{{filePath.toUtf8().toStdString(), 0}, {folderPath.toUtf8().toStdString(), 123}};
  MSWindowsClipboardFileConverter converter;
  GlobalMemory handle(converter.fromIClipboard(FileTransferFormat::marshall(refs)), &GlobalFree);
  QVERIFY(handle != nullptr);
  const auto decoded = FileTransferFormat::unmarshall(converter.toIClipboard(handle.get()));
  QCOMPARE(decoded.size(), size_t(2));
  QCOMPARE(decoded[0].path, refs[0].path);
  QCOMPARE(decoded[0].size, uint64_t(7));
  QCOMPARE(decoded[1].path, refs[1].path);
  QCOMPARE(decoded[1].size, uint64_t(0));
}

void MSWindowsClipboardTests::filesWithOffsetAndAnsiPaths()
{
  const std::wstring widePath = L"C:\\deskflow-test\\missing-file.txt";
  std::wstring widePaths = widePath;
  widePaths.append(2, L'\0');
  const auto wideRefs = FileTransferFormat::unmarshall(convertDrop(dropPayload(wideBytes(widePaths), true, 32)));
  QCOMPARE(wideRefs.size(), size_t(1));
  QCOMPARE(wideRefs[0].path, "C:\\deskflow-test\\missing-file.txt");
  QCOMPARE(wideRefs[0].size, uint64_t(0));

  std::string ansiPaths = "C:\\deskflow-test\\first.txt";
  ansiPaths.push_back('\0');
  ansiPaths.append("C:\\deskflow-test\\second.txt");
  ansiPaths.append(2, '\0');
  const auto ansiRefs = FileTransferFormat::unmarshall(convertDrop(dropPayload(ansiPaths, false, 24)));
  QCOMPARE(ansiRefs.size(), size_t(2));
  QCOMPARE(ansiRefs[0].path, "C:\\deskflow-test\\first.txt");
  QCOMPARE(ansiRefs[1].path, "C:\\deskflow-test\\second.txt");
}

void MSWindowsClipboardTests::filesRejectMalformedDrop_data()
{
  QTest::addColumn<QByteArray>("bytes");
  const auto add = [](const char *name, const std::string &value) {
    QTest::newRow(name) << QByteArray(value.data(), static_cast<qsizetype>(value.size()));
  };
  add("short-header", std::string(1, 'x'));
  std::wstring list = L"C:\\test.txt";
  list.append(2, L'\0');
  auto good = dropPayload(wideBytes(list), true);
  auto invalid = good;
  DWORD offset = 0;
  std::memcpy(invalid.data(), &offset, sizeof(offset));
  add("offset-in-header", invalid);
  offset = 0xffffffff;
  std::memcpy(invalid.data(), &offset, sizeof(offset));
  add("offset-outside-allocation", invalid);
  add("misaligned-wide-offset", dropPayload(wideBytes(list), true, 21));
  add("unterminated-wide-path", dropPayload(wideBytes(L"C:\\test.txt"), true));
  list.pop_back();
  add("missing-wide-list-terminator", dropPayload(wideBytes(list), true));
  list.append(L"C:\\truncated-second.txt");
  add("truncated-second-wide-path", dropPayload(wideBytes(list), true));
  add("unterminated-ansi-path", dropPayload("C:\\test.txt", false));
  std::string ansiPath = "C:\\test.txt";
  ansiPath.push_back('\0');
  add("missing-ansi-list-terminator", dropPayload(ansiPath, false));
  std::wstring invalidUtf16 = L"C:\\";
  invalidUtf16.push_back(static_cast<wchar_t>(0xd800));
  invalidUtf16.append(2, L'\0');
  add("invalid-utf16", dropPayload(wideBytes(invalidUtf16), true));
}

void MSWindowsClipboardTests::filesRejectMalformedDrop()
{
  QFETCH(QByteArray, bytes);
  QVERIFY(convertDrop(std::string(bytes.constData(), bytes.size())).empty());
}

void MSWindowsClipboardTests::filesRejectInvalidReferences()
{
  MSWindowsClipboardFileConverter converter;
  QVERIFY(converter.fromIClipboard("invalid") == nullptr);
  QVERIFY(converter.fromIClipboard(FileTransferFormat::marshall({{"relative.txt", 0}})) == nullptr);
  std::string embeddedNull = "C:\\one.txt";
  embeddedNull.push_back('\0');
  embeddedNull.append("C:\\two.txt");
  QVERIFY(converter.fromIClipboard(FileTransferFormat::marshall({{embeddedNull, 0}})) == nullptr);
  QVERIFY(converter.fromIClipboard(FileTransferFormat::marshall({{"C:\\\xff", 0}})) == nullptr);
  QVERIFY(converter.toIClipboard(nullptr).empty());
}

void MSWindowsClipboardTests::filesCopyEffect()
{
  // Earlier tests opened the clipboard with a null owner and do not close
  // it. Release that test-owned handle before switching to a real owner.
  CloseClipboard();
  // OpenClipboard(NULL) cannot publish data after EmptyClipboard because
  // it has no owner; use a real hidden window just as the application does.
  HWND window = CreateWindowExW(
      0, L"STATIC", L"Deskflow clipboard test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr
  );
  QVERIFY(window != nullptr);
  const auto destroy = [](HWND handle) { DestroyWindow(handle); };
  const std::unique_ptr<std::remove_pointer_t<HWND>, decltype(destroy)> owner(window, destroy);
  MSWindowsClipboard clipboard(window);
  Clipboard savedClipboard;
  QVERIFY(IClipboard::copy(&savedClipboard, &clipboard));
  const auto restore = [&clipboard, &savedClipboard](void *) {
    if (!IClipboard::copy(&clipboard, &savedClipboard)) {
      LOG_WARN("failed to restore clipboard after file copy effect test");
    }
    clipboard.close();
  };
  const std::unique_ptr<void, decltype(restore)> restoreClipboard(reinterpret_cast<void *>(1), restore);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.add(IClipboard::Format::Files, FileTransferFormat::marshall({{"C:\\deskflow-test\\file.txt", 0}}));
  QVERIFY(clipboard.has(IClipboard::Format::Files));
  HANDLE effect = GetClipboardData(RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT));
  QVERIFY(effect != nullptr);
  QVERIFY(GlobalSize(effect) >= sizeof(DWORD));
  const auto *value = static_cast<const DWORD *>(GlobalLock(effect));
  QVERIFY(value != nullptr);
  const DWORD actual = *value;
  GlobalUnlock(effect);
  QCOMPARE(actual, DWORD(DROPEFFECT_COPY));
  clipboard.close();
}

QTEST_MAIN(MSWindowsClipboardTests)
