/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsWheelTests.h"
#include "platform/MSWindowsDesks.h"

void MSWindowsWheelTests::initTestCase()
{
  // Exercise the real desk window procedure without global hooks, focus changes,
  // SendInput, or any connection to the user's running Deskflow processes.
  m_hook.loadLibrary();
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = &MSWindowsDesks::primaryDeskProc;
  windowClass.hInstance = GetModuleHandle(nullptr);
  windowClass.lpszClassName = L"DeskflowWheelIsolationTest";
  m_windowClass = RegisterClassW(&windowClass);
  QVERIFY(m_windowClass != 0);
  m_window = CreateWindowExW(
      0, MAKEINTATOM(m_windowClass), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr
  );
  QVERIFY(m_window != nullptr);
}

void MSWindowsWheelTests::cleanupTestCase()
{
  m_hook.setMode(kHOOK_DISABLE);
  if (m_window)
    QVERIFY(DestroyWindow(m_window));
  if (m_windowClass)
    QVERIFY(UnregisterClassW(MAKEINTATOM(m_windowClass), GetModuleHandle(nullptr)));
}

void MSWindowsWheelTests::init()
{
  m_hook.setMode(kHOOK_DISABLE);
  MSG message{};
  while (PeekMessage(&message, nullptr, DESKFLOW_MSG_MOUSE_WHEEL, DESKFLOW_MSG_MOUSE_WHEEL, PM_REMOVE)) {
  }
}

void MSWindowsWheelTests::directWindowWheelIsRelayed_data()
{
  QTest::addColumn<int>("message");
  QTest::addColumn<int>("delta");
  for (const int message : {WM_MOUSEWHEEL, WM_MOUSEHWHEEL}) {
    for (const int delta : {-32768, -120, -40, -1, 1, 40, 120, 32767}) {
      const auto name = QByteArray::number(message) + ':' + QByteArray::number(delta);
      QTest::newRow(name.constData()) << message << delta;
    }
  }
}

void MSWindowsWheelTests::directWindowWheelIsRelayed()
{
  QFETCH(int, message);
  QFETCH(int, delta);
  m_hook.setMode(kHOOK_RELAY_EVENTS);
  SendMessage(m_window, message, MAKEWPARAM(MK_CONTROL | MK_SHIFT, static_cast<WORD>(delta)), 0);
  MSG forwarded{};
  QVERIFY(PeekMessage(&forwarded, nullptr, DESKFLOW_MSG_MOUSE_WHEEL, DESKFLOW_MSG_MOUSE_WHEEL, PM_REMOVE));
  QCOMPARE(static_cast<int32_t>(forwarded.wParam), message == WM_MOUSEWHEEL ? delta : 0);
  QCOMPARE(static_cast<int32_t>(forwarded.lParam), message == WM_MOUSEHWHEEL ? delta : 0);
  QVERIFY(!PeekMessage(&forwarded, nullptr, DESKFLOW_MSG_MOUSE_WHEEL, DESKFLOW_MSG_MOUSE_WHEEL, PM_REMOVE));
}

void MSWindowsWheelTests::localWheelIsNotRelayed_data()
{
  QTest::addColumn<int>("mode");
  QTest::newRow("disabled") << static_cast<int>(kHOOK_DISABLE);
  QTest::newRow("local-screen") << static_cast<int>(kHOOK_WATCH_JUMP_ZONE);
}

void MSWindowsWheelTests::localWheelIsNotRelayed()
{
  QFETCH(int, mode);
  m_hook.setMode(static_cast<EHookMode>(mode));
  SendMessage(m_window, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0);
  SendMessage(m_window, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-40)), 0);
  MSG forwarded{};
  QVERIFY(!PeekMessage(&forwarded, nullptr, DESKFLOW_MSG_MOUSE_WHEEL, DESKFLOW_MSG_MOUSE_WHEEL, PM_REMOVE));
}

QTEST_GUILESS_MAIN(MSWindowsWheelTests)
