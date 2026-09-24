/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardImageTests.h"

#include "deskflow/Clipboard.h"
#include "platform/IMSWindowsClipboardFacade.h"
#include "platform/MSWindowsClipboard.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"

#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace {

using GlobalMemory = std::unique_ptr<void, decltype(&GlobalFree)>;

GlobalMemory memoryFor(const std::string &bytes)
{
  GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes.size()), &GlobalFree);
  if (memory) {
    if (auto *raw = GlobalLock(memory.get())) {
      std::memcpy(raw, bytes.data(), bytes.size());
      GlobalUnlock(memory.get());
    }
  }
  return memory;
}

BITMAPINFOHEADER infoHeader(LONG width = 2, LONG height = 2, WORD depth = 32)
{
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = height;
  header.biPlanes = 1;
  header.biBitCount = depth;
  header.biCompression = BI_RGB;
  return header;
}

std::string bytesFor(const BITMAPINFOHEADER &header, size_t pixelBytes = 16)
{
  std::string bytes(reinterpret_cast<const char *>(&header), sizeof(header));
  bytes.append(pixelBytes, '\0');
  return bytes;
}

std::string v5Image()
{
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(header);
  header.bV5Width = 2;
  header.bV5Height = 2;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00ff0000;
  header.bV5GreenMask = 0x0000ff00;
  header.bV5BlueMask = 0x000000ff;
  header.bV5AlphaMask = 0xff000000;
  header.bV5CSType = LCS_sRGB;
  const DWORD pixels[] = {0x7fff0000, 0xff00ff00, 0x110000ff, 0xffddeeff};
  std::string bytes(reinterpret_cast<const char *>(&header), sizeof(header));
  bytes.append(reinterpret_cast<const char *>(pixels), sizeof(pixels));
  return bytes;
}

// No native clipboard calls: formats/handles belong entirely to the test.
// Conversion may use GDI, but never opens, reads, clears or writes the user's clipboard.
class MemoryClipboardFacade : public IMSWindowsClipboardFacade
{
public:
  bool open(HWND) const override
  {
    ++openCount;
    return canOpen;
  }
  void close() const override
  {
    ++closeCount;
  }
  bool isFormatAvailable(UINT format) const override
  {
    return formats.contains(format);
  }
  HANDLE read(UINT format) const override
  {
    reads.push_back(format);
    const auto found = formats.find(format);
    return found == formats.end() ? nullptr : found->second;
  }
  void write(HANDLE memory, UINT format) override
  {
    writes.push_back(format);
    GlobalFree(memory);
  }

  bool canOpen = true;
  std::map<UINT, HANDLE> formats;
  mutable std::vector<UINT> reads;
  std::vector<UINT> writes;
  mutable int openCount = 0;
  mutable int closeCount = 0;
};

} // namespace

void MSWindowsClipboardImageTests::canonicalBitmapPreservesPixels_data()
{
  QTest::addColumn<int>("depth");
  QTest::addColumn<int>("height");
  QTest::newRow("rgb24-bottom-up") << 24 << 2;
  QTest::newRow("rgb24-top-down") << 24 << -2;
  QTest::newRow("rgb32-bottom-up") << 32 << 2;
  QTest::newRow("rgb32-top-down") << 32 << -2;
}

void MSWindowsClipboardImageTests::canonicalBitmapPreservesPixels()
{
  QFETCH(int, depth);
  QFETCH(int, height);
  auto bytes = bytesFor(infoHeader(2, height, static_cast<WORD>(depth)));
  for (size_t i = sizeof(BITMAPINFOHEADER); i < bytes.size(); ++i) {
    bytes[i] = static_cast<char>(i);
  }
  const auto memory = memoryFor(bytes);
  QVERIFY(memory);
  const auto converted = MSWindowsClipboardBitmapConverter().toIClipboard(memory.get());
  // GlobalAlloc may add trailing padding, so compare the complete actual DIB.
  QCOMPARE(converted.substr(0, bytes.size()), bytes);
}

void MSWindowsClipboardImageTests::extendedBitfieldsUsesEmbeddedMasks_data()
{
  QTest::addColumn<int>("headerSize");
  QTest::addColumn<int>("height");
  QTest::newRow("v4-bottom-up") << int(sizeof(BITMAPV4HEADER)) << 2;
  QTest::newRow("v4-top-down") << int(sizeof(BITMAPV4HEADER)) << -2;
  QTest::newRow("v5-bottom-up") << int(sizeof(BITMAPV5HEADER)) << 2;
  QTest::newRow("v5-top-down") << int(sizeof(BITMAPV5HEADER)) << -2;
}

void MSWindowsClipboardImageTests::extendedBitfieldsUsesEmbeddedMasks()
{
  QFETCH(int, headerSize);
  QFETCH(int, height);
  const auto v5 = v5Image();
  auto bytes = v5.substr(0, static_cast<size_t>(headerSize));
  bytes.append(v5.substr(sizeof(BITMAPV5HEADER)));
  const DWORD nativeSize = headerSize;
  const LONG nativeHeight = height;
  std::memcpy(bytes.data(), &nativeSize, sizeof(nativeSize));
  std::memcpy(bytes.data() + offsetof(BITMAPINFOHEADER, biHeight), &nativeHeight, sizeof(nativeHeight));
  const auto memory = memoryFor(bytes);
  QVERIFY(memory);
  const auto converted = MSWindowsClipboardBitmapConverter(CF_DIBV5).toIClipboard(memory.get());
  QCOMPARE(converted.size(), sizeof(BITMAPINFOHEADER) + size_t{16});
  QCOMPARE(converted.substr(sizeof(BITMAPINFOHEADER)), v5.substr(sizeof(BITMAPV5HEADER)));
  BITMAPINFOHEADER output{};
  std::memcpy(&output, converted.data(), sizeof(output));
  QCOMPARE(output.biSize, DWORD(sizeof(output)));
  QCOMPARE(output.biCompression, DWORD(BI_RGB));
  QCOMPARE(output.biHeight, nativeHeight);
}

void MSWindowsClipboardImageTests::infoHeaderUsesExternalMasks()
{
  auto header = infoHeader(2, 1, 16);
  header.biCompression = BI_BITFIELDS;
  auto bytes = bytesFor(header, 0);
  const DWORD masks[] = {0xf800, 0x07e0, 0x001f};
  const WORD pixels[] = {0xf800, 0x07e0};
  bytes.append(reinterpret_cast<const char *>(masks), sizeof(masks));
  bytes.append(reinterpret_cast<const char *>(pixels), sizeof(pixels));
  const auto memory = memoryFor(bytes);
  QVERIFY(memory);
  const auto converted = MSWindowsClipboardBitmapConverter().toIClipboard(memory.get());
  QCOMPARE(converted.size(), sizeof(BITMAPINFOHEADER) + size_t{8});
  const auto *output = reinterpret_cast<const unsigned char *>(converted.data() + sizeof(BITMAPINFOHEADER));
  QCOMPARE(output[0], 0);
  QCOMPARE(output[1], 0);
  QCOMPARE(output[2], 255);
  QCOMPARE(output[4], 0);
  QCOMPARE(output[5], 255);
  QCOMPARE(output[6], 0);
}

void MSWindowsClipboardImageTests::v5ProfileDoesNotShiftPixels_data()
{
  QTest::addColumn<int>("placement");
  QTest::newRow("profile-before-pixels") << 0;
  QTest::newRow("profile-after-pixels") << 1;
  QTest::newRow("srgb-ignores-profile-fields") << 2;
}

void MSWindowsClipboardImageTests::v5ProfileDoesNotShiftPixels()
{
  QFETCH(int, placement);
  auto bytes = v5Image();
  const DWORD profileSize = 16;
  const DWORD profileOffset = placement == 0 ? sizeof(BITMAPV5HEADER) : DWORD(bytes.size());
  if (placement == 0) {
    bytes.insert(sizeof(BITMAPV5HEADER), profileSize, '\x7f');
  } else if (placement == 1) {
    bytes.append(profileSize, '\x7f');
  }
  const DWORD colourSpace = placement == 2 ? LCS_sRGB : PROFILE_EMBEDDED;
  std::memcpy(bytes.data() + offsetof(BITMAPV5HEADER, bV5CSType), &colourSpace, sizeof(colourSpace));
  std::memcpy(bytes.data() + offsetof(BITMAPV5HEADER, bV5ProfileData), &profileOffset, sizeof(profileOffset));
  std::memcpy(bytes.data() + offsetof(BITMAPV5HEADER, bV5ProfileSize), &profileSize, sizeof(profileSize));
  const auto memory = memoryFor(bytes);
  QVERIFY(memory);
  const auto converted = MSWindowsClipboardBitmapConverter(CF_DIBV5).toIClipboard(memory.get());
  QCOMPARE(converted.size(), sizeof(BITMAPINFOHEADER) + size_t{16});
  QCOMPARE(converted.substr(sizeof(BITMAPINFOHEADER)), v5Image().substr(sizeof(BITMAPV5HEADER)));
}

void MSWindowsClipboardImageTests::invalidBitmapRejected_data()
{
  QTest::addColumn<QByteArray>("bytes");
  const auto row = [](const char *name, const std::string &bytes) {
    QTest::newRow(name) << QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
  };
  row("short-header", std::string(4, '\0'));
  auto header = infoHeader();
  header.biSize = sizeof(BITMAPV5HEADER);
  row("truncated-v5-header", bytesFor(header));
  row("negative-width", bytesFor(infoHeader(-2)));
  row("zero-height", bytesFor(infoHeader(2, 0)));
  row("minimum-height", bytesFor(infoHeader(2, (std::numeric_limits<LONG>::min)())));
  row("missing-pixels", bytesFor(infoHeader(20, 20), 0));
  row("invalid-depth", bytesFor(infoHeader(2, 2, 12)));
  row("truncated-colour-table", bytesFor(infoHeader(2, 2, 8), 0));
  header = infoHeader();
  header.biPlanes = 2;
  row("invalid-planes", bytesFor(header));
  header = infoHeader(20, 20);
  header.biCompression = BI_BITFIELDS;
  row("missing-external-masks", bytesFor(header, 0));
  auto profile = v5Image();
  const DWORD profileOffset = 1000;
  const DWORD profileSize = 100;
  const DWORD colourSpace = PROFILE_EMBEDDED;
  std::memcpy(profile.data() + offsetof(BITMAPV5HEADER, bV5CSType), &colourSpace, sizeof(colourSpace));
  std::memcpy(profile.data() + offsetof(BITMAPV5HEADER, bV5ProfileData), &profileOffset, sizeof(profileOffset));
  std::memcpy(profile.data() + offsetof(BITMAPV5HEADER, bV5ProfileSize), &profileSize, sizeof(profileSize));
  row("profile-outside-allocation", profile);
}

void MSWindowsClipboardImageTests::invalidBitmapRejected()
{
  QFETCH(QByteArray, bytes);
  const auto memory = memoryFor(bytes.toStdString());
  QVERIFY(memory);
  QVERIFY(MSWindowsClipboardBitmapConverter().toIClipboard(memory.get()).empty());
}

void MSWindowsClipboardImageTests::failedDibFallsBackToV5_data()
{
  QTest::addColumn<bool>("invalidDib");
  QTest::newRow("delayed-dib-unavailable") << false;
  QTest::newRow("dib-invalid") << true;
}

void MSWindowsClipboardImageTests::failedDibFallsBackToV5()
{
  QFETCH(bool, invalidDib);
  const auto invalid = memoryFor(std::string(4, '\0'));
  const auto v5 = memoryFor(v5Image());
  QVERIFY(invalid);
  QVERIFY(v5);
  MemoryClipboardFacade facade;
  facade.formats[CF_DIB] = invalidDib ? invalid.get() : nullptr;
  facade.formats[CF_DIBV5] = v5.get();
  MSWindowsClipboard source(nullptr);
  source.setFacade(facade);
  Clipboard destination;
  QVERIFY(Clipboard::copy(&destination, &source));
  QVERIFY(source.readSucceeded());
  QCOMPARE(facade.reads, (std::vector<UINT>{CF_DIB, CF_DIBV5}));
  QVERIFY(destination.open(0));
  QVERIFY(destination.has(IClipboard::Format::Bitmap));
  QCOMPARE(
      destination.get(IClipboard::Format::Bitmap).substr(sizeof(BITMAPINFOHEADER)),
      v5Image().substr(sizeof(BITMAPV5HEADER))
  );
  destination.close();
  QCOMPARE(facade.closeCount, 1);
}

void MSWindowsClipboardImageTests::nativeV5OnlyCanBeRead()
{
  const auto v5 = memoryFor(v5Image());
  QVERIFY(v5);
  MemoryClipboardFacade facade;
  facade.formats[CF_DIBV5] = v5.get();
  MSWindowsClipboard source(nullptr);
  source.setFacade(facade);
  QVERIFY(source.open(0));
  QVERIFY(source.has(IClipboard::Format::Bitmap));
  QVERIFY(!source.get(IClipboard::Format::Bitmap).empty());
  QVERIFY(source.readSucceeded());
  source.close();
  QCOMPARE(facade.reads, (std::vector<UINT>{CF_DIBV5}));
}

void MSWindowsClipboardImageTests::failedImageReadIsReportedAndCanRetry()
{
  MemoryClipboardFacade facade;
  facade.formats[CF_DIB] = nullptr;
  MSWindowsClipboard source(nullptr);
  source.setFacade(facade);
  Clipboard destination;
  // The generic copy API cannot express conversion errors; the native screen
  // must combine its result with readSucceeded before publishing a snapshot.
  QVERIFY(!(Clipboard::copy(&destination, &source) && source.readSucceeded()));
  const auto image = memoryFor(bytesFor(infoHeader()));
  QVERIFY(image);
  facade.formats[CF_DIB] = image.get();
  QVERIFY(Clipboard::copy(&destination, &source) && source.readSucceeded());
}

void MSWindowsClipboardImageTests::failedOpenKeepsDestination()
{
  MemoryClipboardFacade facade;
  facade.canOpen = false;
  MSWindowsClipboard source(nullptr);
  source.setFacade(facade);
  Clipboard destination;
  QVERIFY(destination.open(1));
  QVERIFY(destination.empty());
  destination.add(IClipboard::Format::Text, "previous clipboard");
  destination.close();
  QVERIFY(!Clipboard::copy(&destination, &source));
  QVERIFY(destination.open(1));
  QCOMPARE(destination.get(IClipboard::Format::Text), std::string("previous clipboard"));
  destination.close();
  QCOMPARE(facade.closeCount, 0);
}

void MSWindowsClipboardImageTests::emptyTextRemainsValid()
{
  const auto text = memoryFor(std::string(sizeof(wchar_t), '\0'));
  QVERIFY(text);
  MemoryClipboardFacade facade;
  facade.formats[CF_UNICODETEXT] = text.get();
  MSWindowsClipboard source(nullptr);
  source.setFacade(facade);
  QVERIFY(source.open(0));
  QVERIFY(source.has(IClipboard::Format::Text));
  QVERIFY(source.get(IClipboard::Format::Text).empty());
  QVERIFY(source.readSucceeded());
  source.close();
}

void MSWindowsClipboardImageTests::imageIsPublishedAsCanonicalDib()
{
  MemoryClipboardFacade facade;
  MSWindowsClipboard destination(nullptr);
  destination.setFacade(facade);
  QVERIFY(destination.open(0));
  destination.add(IClipboard::Format::Bitmap, bytesFor(infoHeader()));
  destination.close();
  QCOMPARE(facade.writes, (std::vector<UINT>{CF_DIB}));
  QCOMPARE(MSWindowsClipboardBitmapConverter(CF_DIBV5).fromIClipboard(bytesFor(infoHeader())), nullptr);
}

QTEST_GUILESS_MAIN(MSWindowsClipboardImageTests)
