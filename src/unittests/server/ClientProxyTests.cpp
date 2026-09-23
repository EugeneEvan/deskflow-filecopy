/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Clipboard.h"
#include "deskflow/OptionTypes.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"
#include "server/ClientProxyFileTransfer.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  void push(const QByteArray &packet)
  {
    m_input.push_back(packet);
  }
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    if (m_input.empty())
      return 0;
    auto &packet = m_input.front();
    size = std::min(size, static_cast<uint32_t>(packet.size()));
    if (buffer)
      std::memcpy(buffer, packet.constData(), size);
    packet.remove(0, size);
    if (packet.isEmpty())
      m_input.pop_front();
    return size;
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return !m_input.empty();
  }

  uint32_t getSize() const override
  {
    return m_input.empty() ? 0 : static_cast<uint32_t>(m_input.front().size());
  }

private:
  QByteArray m_buffer;
  std::deque<QByteArray> m_input;
};

class ProtocolEventQueue : public MockEventQueue
{
public:
  ~ProtocolEventQueue() override
  {
    for (const auto &event : queued)
      Event::deleteData(event);
  }
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[{type, reinterpret_cast<uintptr_t>(target)}] = handler;
  }
  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase({type, reinterpret_cast<uintptr_t>(target)});
  }
  void removeHandlers(void *target) override
  {
    std::erase_if(m_handlers, [target](const auto &entry) {
      return entry.first.second == reinterpret_cast<uintptr_t>(target);
    });
  }
  bool dispatchEvent(const Event &event) override
  {
    auto found = m_handlers.find({event.getType(), reinterpret_cast<uintptr_t>(event.getTarget())});
    if (found == m_handlers.end())
      return false;
    const auto handler = found->second;
    handler(event);
    return true;
  }
  void addEvent(Event &&event) override
  {
    queued.push_back(std::move(event));
  }
  EventQueueTimer *newTimer(double, void *) override
  {
    return reinterpret_cast<EventQueueTimer *>(++m_timer);
  }
  EventQueueTimer *newOneShotTimer(double seconds, void *target) override
  {
    return newTimer(seconds, target);
  }
  bool has(EventTypes type) const
  {
    return std::any_of(queued.begin(), queued.end(), [type](const auto &event) { return event.getType() == type; });
  }
  std::vector<Event> queued;

private:
  uintptr_t m_timer = 0x100;
  std::map<std::pair<EventTypes, uintptr_t>, EventHandler> m_handlers;
};

void enableFileCopy(bool enabled)
{
  Settings::setValue(Settings::Core::FileTransferEnabled, enabled);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
}

struct FileProxyUnderTest
{
  ProtocolEventQueue events;
  CapturingStream *stream = new CapturingStream;
  ClientProxyFileTransfer proxy{"client", stream, reinterpret_cast<Server *>(0x1), &events};

  FileProxyUnderTest()
  {
    stream->take();
    // Run the real initial DINF parser, which switches handleData to
    // the normal virtual message parser before FCHL can be accepted.
    stream->push("DINF" + QByteArray::fromHex("0000 0000 0780 0438 0000 0000 0000"));
    events.dispatchEvent(Event(EventTypes::StreamInputReady, stream->getEventTarget()));
    stream->take();
  }
  void receive(const QByteArray &packet)
  {
    stream->push(packet);
    events.dispatchEvent(Event(EventTypes::StreamInputReady, stream->getEventTarget()));
  }
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
  QVERIFY(m_settingsDirectory.isValid());
  m_originalSettings = Settings::settingsFile();
  Settings::setSettingsFile(m_settingsDirectory.filePath("protocol-tests.conf"));
}

void ClientProxyTests::cleanupTestCase()
{
  Settings::setSettingsFile(m_originalSettings);
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::fileCopyNegotiatesThroughMessageParser()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  enableFileCopy(true);
  FileProxyUnderTest test;
  QVERIFY(test.events.has(EventTypes::ClientProxyReady));
  test.proxy.setOptions({});
  QCOMPARE(test.stream->take(), "DSOP" + QByteArray::fromHex("00000002 44464350 44460001"));
  test.receive("FCHL");
  QCOMPARE(test.stream->take(), QByteArray("FCHA"));
  QVERIFY(!test.events.has(EventTypes::ClientProxyDisconnected));
  // A repeated capability hello is a protocol error, not a second session.
  test.receive("FCHL");
  QVERIFY(test.events.has(EventTypes::ClientProxyDisconnected));
}

void ClientProxyTests::fileCopyDisabledDoesNotAdvertiseOrAcknowledge()
{
  enableFileCopy(false);
  FileProxyUnderTest test;
  test.proxy.setOptions({});
  QCOMPARE(test.stream->take(), "DSOP" + QByteArray::fromHex("00000000"));
  test.receive("FCHL");
  QVERIFY(!test.stream->take().contains("FCHA"));
  QVERIFY(test.events.has(EventTypes::ClientProxyDisconnected));
}

void ClientProxyTests::fileCopyRejectsDataBeforeNegotiation()
{
  enableFileCopy(false);
  FileProxyUnderTest test;
  test.receive("FCDT" + QByteArray::fromHex("00000001 01"));
  QVERIFY(test.events.has(EventTypes::ClientProxyDisconnected));
}

void ClientProxyTests::fileSelectionDoesNotSendLegacyClipboard()
{
  enableFileCopy(false);
  FileProxyUnderTest test;
  Clipboard clipboard;
  QVERIFY(clipboard.open(0));
  clipboard.add(IClipboard::Format::Files, "private source paths");
  clipboard.add(IClipboard::Format::Text, "C:\\private-source.txt");
  clipboard.close();
  test.proxy.setClipboard(kClipboardClipboard, &clipboard);
  Clipboard emptySelection;
  test.proxy.setClipboard(kClipboardSelection, &emptySelection);
  QVERIFY(!test.events.has(EventTypes::ClipboardSending));
  QVERIFY(test.stream->take().isEmpty());
}

QTEST_MAIN(ClientProxyTests)
