/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"

#include <QTest>

#include <chrono>
#include <cmath>
#include <memory>

void EventQueueTests::initTestCase()
{
  m_arch.init();
}

void EventQueueTests::monotonicClock_preservesSubsecondElapsedTime()
{
  const auto referenceStart = std::chrono::steady_clock::now();
  const double start = Arch::time();
  QTest::qSleep(50);
  const double elapsed = Arch::time() - start;
  const auto referenceElapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - referenceStart).count();

  QVERIFY2(elapsed > 0.0, "The monotonic clock must advance during a subsecond interval");
  QVERIFY2(
      std::abs(elapsed - referenceElapsed) < 0.1, "The monotonic clock must not round elapsed time to whole seconds"
  );
}

void EventQueueTests::shortTimer_firesRepeatedlyWithinOneSecond()
{
  EventQueue events;
  auto *timer = events.newTimer(0.01, this);
  const auto start = std::chrono::steady_clock::now();
  for (int tick = 0; tick < 5; ++tick) {
    Event event;
    QVERIFY(events.getEvent(event, 2.0));
    QCOMPARE(event.getType(), EventTypes::Timer);
    QCOMPARE(event.getTarget(), this);
  }
  events.deleteTimer(timer);

  // Generous scheduling tolerance, while five ticks cannot accidentally pass
  // an integer-second clock simply by straddling one second boundary.
  const auto elapsed = std::chrono::steady_clock::now() - start;
  QVERIFY2(elapsed < std::chrono::milliseconds(750), "A 10 ms timer must not wait for each integer-second boundary");
}

void EventQueueTests::dispatchEvent_noHandler_returnsFalse()
{
  EventQueue events;

  QVERIFY(!events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
}

void EventQueueTests::dispatchEvent_noTypeHandler_dispatchesUnknownHandler()
{
  EventQueue events;
  bool fallbackCalled = false;
  events.addHandler(EventTypes::Unknown, this, [&fallbackCalled](const Event &) { fallbackCalled = true; });

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(fallbackCalled);
}

void EventQueueTests::dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn()
{
  EventQueue events;
  auto handlerLifetime = std::make_shared<int>(1);
  std::weak_ptr<int> handlerLifetimeObserver = handlerLifetime;
  bool handlerAliveAfterRemoval = false;

  events.addHandler(
      EventTypes::ClientDisconnected, this,
      [this, &events, &handlerLifetimeObserver, &handlerAliveAfterRemoval, handlerLifetime](const Event &) {
        events.removeHandler(EventTypes::ClientDisconnected, this);
        handlerAliveAfterRemoval = handlerLifetime != nullptr && !handlerLifetimeObserver.expired();
      }
  );
  handlerLifetime.reset();

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(handlerAliveAfterRemoval);
  QVERIFY(handlerLifetimeObserver.expired());
}

QTEST_MAIN(EventQueueTests)
