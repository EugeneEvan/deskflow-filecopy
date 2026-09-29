/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui {

class WindowsLoginStartup
{
public:
  enum class State
  {
    Disabled,
    Enabled,
    OtherInstallation,
    Stale,
    Conflict,
    Error
  };

  WindowsLoginStartup(QString startupDirectory, QString executablePath);
  static WindowsLoginStartup current();

  State state(QString *error = nullptr) const;
  bool setEnabled(bool enabled, QString *error = nullptr) const;

private:
  QString shortcutPath() const;
  QString m_startupDirectory;
  QString m_executablePath;
};

} // namespace deskflow::gui
