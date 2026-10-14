/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FullControlManagerTests.h"
#include "gui/core/FullControlManager.h"
#include <QFile>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <AclAPI.h>
#include <Windows.h>
#include <sddl.h>
#endif

using deskflow::gui::FullControlManager;
using Action = FullControlManager::Action;

void FullControlManagerTests::fixedDirectoryAndLiteralArguments()
{
  QCOMPARE(FullControlManager::protectedDirectory("F:/Apps/中文 portable"), "F:/Deskflow FileCopy Control");
  const auto args = FullControlManager::arguments(
      Action::Install, "F:/Apps/中文 portable/full-control-manager.ps1", "F:/Apps/中文 portable",
      "F:/Users/user's $data/settings.conf"
  );
  QVERIFY(!args.isEmpty());
  QVERIFY(args.contains(QStringLiteral("-File")));
  QVERIFY(!args.contains(QStringLiteral("-Command")));
  QCOMPARE(args.last(), QStringLiteral("F:/Users/user's $data/settings.conf"));
  const auto quoted = FullControlManager::quoteArguments({QStringLiteral("F:\\path with space\\")});
  QCOMPARE(quoted, QStringLiteral("\"F:\\path with space\\\\\""));
}

void FullControlManagerTests::invalidPathsAreRejected()
{
  for (const auto &path :
       {QStringLiteral("relative/path"), QStringLiteral("\\\\server\\share"), QStringLiteral("F:/bad\" -Command evil"),
        QStringLiteral("F:/bad\npath"), QStringLiteral("F:/x/../y"), QStringLiteral("F:/x:stream")}) {
    QVERIFY(FullControlManager::protectedDirectory(path).isEmpty());
    QVERIFY(FullControlManager::arguments(Action::Apply, "F:/manager.ps1", "F:/app", path).isEmpty());
  }
}

void FullControlManagerTests::statusMustMatchProtectedInstallation()
{
  const auto dir = QStringLiteral("F:/Deskflow FileCopy Control");
  auto state = FullControlManager::parseStatus(
      R"({"installed":true,"running":true,"directory":"F:/Deskflow FileCopy Control","settings":"F:/Deskflow FileCopy Control/settings/Deskflow.conf"})",
      dir
  );
  QVERIFY(state.valid);
  QVERIFY(state.installed && state.running);
  QVERIFY(!FullControlManager::parseStatus("{}", dir).valid);
  QVERIFY(!FullControlManager::parseStatus("log\n{}", dir).valid);
  QVERIFY(!FullControlManager::parseStatus(
               R"({"installed":true,"running":true,"directory":"F:/user-writable","settings":"F:/user-writable/a"})",
               dir
  )
               .valid);
  QVERIFY(
      !FullControlManager::parseStatus(
           R"({"installed":false,"running":true,"directory":"F:/Deskflow FileCopy Control","settings":"F:/Deskflow FileCopy Control/settings/Deskflow.conf"})",
           dir
      )
           .valid
  );
}

void FullControlManagerTests::failedOrCancelledActionPreservesMode()
{
  FullControlManager::Status running{true, true, true, "F:/Deskflow FileCopy Control", {}};
  const auto unchanged = FullControlManager::confirmedMode(Action::Install, false, running);
  QVERIFY(!unchanged.has_value());
  QVERIFY(!FullControlManager::confirmedMode(Action::Uninstall, false, running).has_value());
  QVERIFY(!FullControlManager::confirmedMode(Action::Install, true, {}).has_value());
  QVERIFY(FullControlManager::confirmedMode(Action::Install, true, running).value());
  running.running = false;
  QVERIFY(!FullControlManager::confirmedMode(Action::Install, true, running).has_value());
  QVERIFY(!FullControlManager::confirmedMode(Action::Stop, true, running).has_value());
  QVERIFY(!FullControlManager::confirmedMode(Action::Apply, true, running).has_value());
  running.installed = false;
  QVERIFY(!FullControlManager::confirmedMode(Action::Uninstall, true, running).value());
}

void FullControlManagerTests::userWritableComponentIsRejected()
{
#ifdef Q_OS_WIN
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  QFile script(temp.filePath("full-control-manager.ps1"));
  QVERIFY(script.open(QIODevice::WriteOnly));
  script.write("# Never execute test scripts\n");
  script.close();
  QFile marker(temp.filePath("deskflow-full-control.component"));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("Deskflow FileCopy Full Control v1");
  marker.close();
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(
      L"D:P(A;OICI;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr
  ));
  PACL acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  QVERIFY(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
  auto path = temp.path().toStdWString();
  const auto result = SetNamedSecurityInfoW(
      path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
      acl, nullptr
  );
  LocalFree(descriptor);
  QCOMPARE(result, DWORD(ERROR_SUCCESS));
  QVERIFY(!FullControlManager::isProtectedDirectory(temp.path()));
#else
  QSKIP("Windows ACL validation is only available on Windows.");
#endif
}

QTEST_GUILESS_MAIN(FullControlManagerTests)
