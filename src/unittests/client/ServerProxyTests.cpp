/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerProxyTests.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "client/Client.h"
#include "client/ServerProxy.h"
#include "client/ServerProxyFileTransfer.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Clipboard.h"
#include "deskflow/FileTransferBridge.h"
#include "deskflow/FileTransferFormat.h"
#include "deskflow/PlatformScreen.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/Screen.h"
#include "io/IStream.h"
#include "net/ISocketFactory.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTest>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

class FakeStream : public deskflow::IStream
{
public:
  void push(const std::string &bytes)
  {
    m_chunks.push_back(bytes);
  }

  void close() override
  {
    m_chunks.clear();
    m_inputShutdown = true;
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    if (m_inputShutdown || m_chunks.empty() || size == 0) {
      return 0;
    }

    auto &front = m_chunks.front();
    const size_t bytesToRead = std::min(static_cast<size_t>(size), front.size());
    if (buffer != nullptr) {
      std::memcpy(buffer, front.data(), bytesToRead);
    }

    front.erase(0, bytesToRead);
    if (front.empty()) {
      m_chunks.pop_front();
    }
    return static_cast<uint32_t>(bytesToRead);
  }

  void write(const void *data, uint32_t size) override
  {
    m_output.append(static_cast<const char *>(data), size);
  }

  std::string takeOutput()
  {
    return std::exchange(m_output, {});
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
    close();
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<FakeStream *>(this);
  }

  bool isReady() const override
  {
    return !m_inputShutdown && !m_chunks.empty();
  }

  uint32_t getSize() const override
  {
    size_t total = 0;
    for (const auto &chunk : m_chunks) {
      total += chunk.size();
    }
    return static_cast<uint32_t>(std::min<size_t>(total, UINT32_MAX));
  }

private:
  std::deque<std::string> m_chunks;
  bool m_inputShutdown = false;
  std::string m_output;
};

class RecordingEventQueue : public IEventQueue
{
public:
  ~RecordingEventQueue() override
  {
    for (const auto &event : m_addedEvents) {
      Event::deleteData(event);
    }
  }

  int loop() override
  {
    return 0;
  }

  void adoptBuffer(IEventQueueBuffer *) override
  {
  }

  bool getEvent(Event &, double = -1.0) override
  {
    return false;
  }

  bool dispatchEvent(const Event &event) override
  {
    const auto handler = m_handlers.find(HandlerKey{event.getType(), event.getTarget()});
    if (handler == m_handlers.end()) {
      return false;
    }

    const auto callback = handler->second;
    callback(event);
    return true;
  }

  void addEvent(Event &&event) override
  {
    std::lock_guard lock(m_addedMutex);
    m_addedEvents.emplace_back(std::move(event));
  }

  void dispatchPosted()
  {
    std::vector<Event> pending;
    {
      std::lock_guard lock(m_addedMutex);
      pending.swap(m_addedEvents);
    }
    for (const auto &event : pending) {
      dispatchEvent(event);
      Event::deleteData(event);
    }
  }

  EventQueueTimer *newTimer(double, void *) override
  {
    m_periodicTimer = makeTimer();
    return m_periodicTimer;
  }

  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return makeTimer();
  }

  void deleteTimer(EventQueueTimer *timer) override
  {
    if (timer == m_periodicTimer)
      m_periodicTimer = nullptr;
  }

  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[HandlerKey{type, target}] = handler;
  }

  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase(HandlerKey{type, target});
  }

  void removeHandlers(void *target) override
  {
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
      if (it->first.target == target) {
        it = m_handlers.erase(it);
      } else {
        ++it;
      }
    }
  }

  void waitForReady() const override
  {
  }

  void *getSystemTarget() override
  {
    return this;
  }

  EventQueueTimer *timer()
  {
    return m_periodicTimer ? m_periodicTimer : m_lastTimer;
  }

  const std::vector<Event> &addedEvents() const
  {
    return m_addedEvents;
  }

private:
  EventQueueTimer *makeTimer()
  {
    m_timerStorage.push_back(0);
    m_lastTimer = reinterpret_cast<EventQueueTimer *>(&m_timerStorage.back());
    return m_lastTimer;
  }
  struct HandlerKey
  {
    EventTypes type;
    void *target;

    bool operator<(const HandlerKey &other) const
    {
      if (type != other.type) {
        return static_cast<uint32_t>(type) < static_cast<uint32_t>(other.type);
      }
      return std::less<void *>{}(target, other.target);
    }
  };

  std::deque<int> m_timerStorage;
  EventQueueTimer *m_lastTimer = nullptr;
  EventQueueTimer *m_periodicTimer = nullptr;
  std::map<HandlerKey, EventHandler> m_handlers;
  std::vector<Event> m_addedEvents;
  std::mutex m_addedMutex;
};

class MemoryScreen : public PlatformScreen
{
public:
  explicit MemoryScreen(IEventQueue *events) : PlatformScreen(events)
  {
  }
  Clipboard clipboard;
  bool rejectWrites = false;
  int writes = 0;
  void *getEventTarget() const override
  {
    return const_cast<MemoryScreen *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *target) const override
  {
    return Clipboard::copy(target, &clipboard);
  }
  bool setClipboard(ClipboardID, const IClipboard *source) override
  {
    if (!source || rejectWrites)
      return false;
    ++writes;
    return Clipboard::copy(&clipboard, source);
  }
  void setText(const std::string &text)
  {
    clipboard.open(0);
    clipboard.empty();
    clipboard.add(IClipboard::Format::Text, text);
    clipboard.close();
  }
  std::string text() const
  {
    clipboard.open(0);
    const auto result = clipboard.get(IClipboard::Format::Text);
    clipboard.close();
    return result;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const override
  {
    x = y = 0;
    width = height = 100;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 0;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    x = y = 50;
  }
  void reconfigure(uint32_t) override
  {
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 0;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 0;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
  }
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setHalfDuplexMask(KeyModifierMask) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  bool isPrimary() const override
  {
    return false;
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    return nullptr;
  }
  void handleSystemEvent(const Event &) override
  {
  }
};

class UnusedSocketFactory : public ISocketFactory
{
public:
  IDataSocket *create(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
  IListenSocket *createListen(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
};

bool pumpUntil(RecordingEventQueue &events, const std::function<bool()> &finished)
{
  QElapsedTimer timer;
  timer.start();
  do {
    events.dispatchPosted();
    events.dispatchEvent(Event(EventTypes::Timer, events.timer()));
    if (finished())
      return true;
    QTest::qWait(1);
  } while (timer.elapsed() < 3000);
  return false;
}

bool containsFileFrame(const std::string &output, quint8 type, quint32 sequence)
{
  size_t offset = 0;
  while (output.size() - offset >= 8) {
    if (output.compare(offset, 4, "FCDT") != 0)
      return false;
    const auto size = qFromBigEndian<quint32>(output.data() + offset + 4);
    if (size > output.size() - offset - 8)
      return false;
    const auto *frame = output.data() + offset + 8;
    if (size >= 26 && quint8(frame[5]) == type && qFromBigEndian<quint32>(frame + 22) == sequence)
      return true;
    offset += 8 + size;
  }
  return false;
}

class TestServerProxy : public ServerProxy
{
public:
  using ServerProxy::ServerProxy;

  bool parseHandshakeMessageReturnsDisconnect(const uint8_t *code)
  {
    return parseHandshakeMessage(code) == ConnectionResult::Disconnect;
  }
};

class TestFileServerProxy : public ServerProxyFileTransfer
{
public:
  using ServerProxyFileTransfer::onOptionsChanged;

protected:
  ConnectionResult parseHandshakeMessage(const uint8_t *code) override
  {
    // The initial screen handshake is outside this test's fake Client.
    // Still use the real event-driven frame reader and extension parser.
    return ServerProxyFileTransfer::parseMessage(code);
  }
  using ServerProxyFileTransfer::ServerProxyFileTransfer;
};

Client *undereferenceableClient()
{
  // These paths must queue cleanup without calling through to Client.
  return reinterpret_cast<Client *>(0x1);
}

TestAppUtil &testAppUtil()
{
  static TestAppUtil util;
  return util;
}

const Client::DisconnectRequest *disconnectRequest(const RecordingEventQueue &events)
{
  if (events.addedEvents().size() != 1) {
    return nullptr;
  }
  return static_cast<const Client::DisconnectRequest *>(events.addedEvents().front().getDataObject());
}

} // namespace

void ServerProxyTests::initTestCase()
{
  (void)testAppUtil();
  m_log.setFilter(LogLevel::Level::Debug);
  QVERIFY(m_settingsDirectory.isValid());
  m_originalSettings = Settings::settingsFile();
  Settings::setSettingsFile(m_settingsDirectory.filePath("protocol-client-tests.conf"));
}

void ServerProxyTests::cleanupTestCase()
{
  Settings::setSettingsFile(m_originalSettings);
}

void ServerProxyTests::handleKeepAliveAlarm_timeout_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::Timer, events.timer())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server is not responding"));
}

void ServerProxyTests::handleData_incompleteMessage_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  stream.push("DF");
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("incomplete message from server"));
}

void ServerProxyTests::parseHandshakeMessage_protocolError_queuesRefusalRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  TestServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(proxy.parseHandshakeMessageReturnsDisconnect(reinterpret_cast<const uint8_t *>(kMsgEBad)));
  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Refuse);
  QVERIFY(request->refusalReason() == deskflow::core::ConnectionRefusal::ProtocolError);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server reported a protocol error"));
}

void ServerProxyTests::fileCopyOptionsRequireMatchingCapability()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
  RecordingEventQueue events;
  FakeStream stream;
  TestFileServerProxy proxy(undereferenceableClient(), &stream, &events);
  // Exercise the production capability handler used after DSOP decoding.
  // Stock 1.8 options and unknown extension versions must remain silent.
  proxy.onOptionsChanged({kOptionClipboardSharing, 1});
  QVERIFY(stream.takeOutput().empty());
  proxy.onOptionsChanged({kOptionFileCopy, 1});
  QVERIFY(stream.takeOutput().empty());
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QCOMPARE(stream.takeOutput(), std::string("FCHL"));
  QVERIFY(!proxy.canTransferFiles()); // FCHA has not arrived yet.
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QVERIFY(stream.takeOutput().empty());
}

void ServerProxyTests::fileCopyDisabledDoesNotSendHello()
{
  Settings::setValue(Settings::Core::FileTransferEnabled, false);
  RecordingEventQueue events;
  FakeStream stream;
  TestFileServerProxy proxy(undereferenceableClient(), &stream, &events);
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QVERIFY(stream.takeOutput().empty());
  QVERIFY(!proxy.canTransferFiles());
}

void ServerProxyTests::fileCopyAcknowledgmentUsesMessageParser()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
  RecordingEventQueue events;
  FakeStream stream;
  TestFileServerProxy proxy(undereferenceableClient(), &stream, &events);
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QCOMPARE(stream.takeOutput(), std::string("FCHL"));
  QVERIFY(!proxy.canTransferFiles());
  stream.push("FCHA");
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));
  QVERIFY(proxy.canTransferFiles());
  QVERIFY(events.addedEvents().empty());
  stream.push("FCHA");
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));
  QCOMPARE(events.addedEvents().size(), size_t(1));
  QCOMPARE(events.addedEvents().front().getType(), EventTypes::ClientDisconnectRequested);
}

void ServerProxyTests::withdrawnFileCopyIgnoresLateFrames()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
  RecordingEventQueue events;
  FakeStream stream;
  TestFileServerProxy proxy(undereferenceableClient(), &stream, &events);
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  stream.takeOutput();
  stream.push("FCHA");
  events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  QVERIFY(proxy.canTransferFiles());
  proxy.onOptionsChanged({});
  QVERIFY(!proxy.canTransferFiles());
  stream.push(std::string("FCDT\0\0\0\1x", 9));
  events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  QVERIFY(events.addedEvents().empty());
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QCOMPARE(stream.takeOutput(), std::string("FCHL"));
  stream.push("FCHA");
  events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  QVERIFY(proxy.canTransferFiles());
}

void ServerProxyTests::unavailableFileAgentIgnoresPendingAcknowledgment()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
  RecordingEventQueue events;
  FakeStream stream;
  TestFileServerProxy proxy(undereferenceableClient(), &stream, &events);
  proxy.onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QCOMPARE(stream.takeOutput(), std::string("FCHL"));
  Settings::setValue(Settings::Core::FileTransferEnabled, false);
  stream.push("FCHA");
  events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  const bool disconnected = !events.addedEvents().empty();
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  QVERIFY(!disconnected);
  QVERIFY(!proxy.canTransferFiles());
}

void ServerProxyTests::receivedFilesReplaceOwnedClipboard_data()
{
  QTest::addColumn<QString>("interruption");
  QTest::newRow("owned-old-clipboard") << QString();
  QTest::newRow("user-cancelled") << QStringLiteral("cancel");
  QTest::newRow("new-local-copy") << QStringLiteral("copy");
  QTest::newRow("sharing-disabled") << QStringLiteral("disabled");
  QTest::newRow("clipboard-size-zero") << QStringLiteral("zero");
  QTest::newRow("native-publication-failed") << QStringLiteral("write-failed");
}

void ServerProxyTests::receivedFilesReplaceOwnedClipboard()
{
#ifndef Q_OS_WIN
  QSKIP("File copying is currently Windows only");
#endif
  QFETCH(QString, interruption);
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(source.isValid() && cache.isValid());
  const QByteArray contents("verified incoming file");
  QFile original(source.filePath("received.txt"));
  QVERIFY(original.open(QIODevice::WriteOnly));
  QCOMPARE(original.write(contents), contents.size());
  original.close();
  Settings::setValue(Settings::Core::FileTransferEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Server::EnableClipboard, true);
  Settings::setValue(Settings::Core::FileTransferCachePath, cache.path());

  // A real sender creates the complete, checksummed FCDT batch. Clipboard
  // contents stay in MemoryScreen; Bridge only reads the OS sequence number.
  QList<QByteArray> frames;
  deskflow::FileTransferSession sender({[&](const QByteArray &frame) { frames.append(frame); }}, source.path());
  sender.startSend({original.fileName()});
  QTRY_VERIFY_WITH_TIMEOUT(
      ([&] {
        sender.pump();
        return frames.size() == 5;
      })(),
      3000
  );

  RecordingEventQueue events;
  FakeStream stream;
  auto *memory = new MemoryScreen(&events);
  deskflow::Screen screen(memory, &events);
  Client client(&events, "test-client", NetworkAddress(), new UnusedSocketFactory, &screen);
  auto *proxy = new TestFileServerProxy(&client, &stream, &events);
  client.m_server = proxy;
  client.m_active = true;
  std::fill(std::begin(client.m_ownClipboard), std::end(client.m_ownClipboard), false);
  std::fill(std::begin(client.m_sentClipboard), std::end(client.m_sentClipboard), false);
  proxy->onOptionsChanged({kOptionFileCopy, kFileCopyCapability});
  QCOMPARE(stream.takeOutput(), std::string("FCHL"));
  stream.push("FCHA");
  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));
  QVERIFY(proxy->canTransferFiles());

  memory->setText("old local clipboard");
  IScreen::ClipboardInfo grabbed{kClipboardClipboard, 0};
  const Event localCopy(
      EventTypes::ClipboardGrabbed, client.getEventTarget(), &grabbed, Event::EventFlags::DontFreeData
  );
  client.handleClipboardGrabbed(localCopy);
  QVERIFY(client.m_ownClipboard[kClipboardClipboard]);
  stream.takeOutput(); // Discard the outbound notification for the old local copy.

  const auto receive = [&](const QByteArray &frame) {
    FakeStream encoded;
    const std::string payload(frame.constData(), frame.size());
    ProtocolUtil::writef(&encoded, "FCDT%s", &payload);
    stream.push(encoded.takeOutput());
    return events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  };
  std::string replies;
  QVERIFY(receive(frames.front()));
  QVERIFY(pumpUntil(events, [&] {
    replies += stream.takeOutput();
    return containsFileFrame(replies, 6, 0);
  }));
  for (qsizetype i = 1; i < frames.size(); ++i)
    QVERIFY(receive(frames[i]));

  const bool cancelled = interruption == "cancel" || interruption == "copy";
  if (interruption == "cancel") {
    deskflow::FileTransferBridge::requestCancelAll();
    events.dispatchEvent(Event(EventTypes::Timer, events.timer()));
  } else if (interruption == "copy") {
    memory->setText("new local clipboard");
    client.handleClipboardGrabbed(localCopy);
    stream.takeOutput(); // This local CCLP is unrelated to the inbound FCDT batch.
  } else if (interruption == "disabled") {
    client.setOptions({kOptionClipboardSharing, 0});
  } else if (interruption == "zero") {
    client.setOptions({kOptionClipboardSharingSize, 0});
  } else if (interruption == "write-failed") {
    memory->rejectWrites = true;
  }
  replies.clear();
  QVERIFY(pumpUntil(events, [&] {
    replies += stream.takeOutput();
    return containsFileFrame(replies, cancelled ? 7 : 6, cancelled ? 0 : 4);
  }));

  if (!interruption.isEmpty()) {
    QCOMPARE(memory->writes, 0);
    QCOMPARE(
        memory->text(), interruption == "copy" ? std::string("new local clipboard") : std::string("old local clipboard")
    );
    QVERIFY(client.m_ownClipboard[kClipboardClipboard]);
    return;
  }
  QCOMPARE(memory->writes, 1);
  memory->clipboard.open(0);
  const auto roots = FileTransferFormat::unmarshall(memory->clipboard.get(IClipboard::Format::Files));
  memory->clipboard.close();
  QCOMPARE(roots.size(), size_t(1));
  QFile received(QString::fromStdString(roots.front().path));
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), contents);
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    QVERIFY(!client.m_ownClipboard[id]);
    QCOMPARE(client.m_dataClipboard[id], memory->clipboard.marshall());
  }
}

void ServerProxyTests::alreadyPublishedFilesReplaceOwnedClipboard()
{
  RecordingEventQueue events;
  auto *memory = new MemoryScreen(&events);
  deskflow::Screen screen(memory, &events);
  Client client(&events, "test-client", NetworkAddress(), new UnusedSocketFactory, &screen);
  // The agent has already published and Bridge accepted its generation and
  // native sequence. Legacy ownership can still describe the previous copy.
  client.m_ownClipboard[kClipboardClipboard] = true;
  const QStringList roots{m_settingsDirectory.filePath("agent-received.txt")};
  deskflow::FileTransferBridge::cacheFiles(&memory->clipboard, roots);
  QVERIFY(client.publishTransferredFiles(roots, true));
  QCOMPARE(memory->writes, 0);
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    QVERIFY(!client.m_ownClipboard[id]);
    QCOMPARE(client.m_dataClipboard[id], memory->clipboard.marshall());
  }
}

QTEST_MAIN(ServerProxyTests)
