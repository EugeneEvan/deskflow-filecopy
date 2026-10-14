/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <optional>

namespace deskflow::gui {

class FullControlManager : public QObject
{
  Q_OBJECT
public:
  enum class Action
  {
    Install,
    Apply,
    Start,
    Stop,
    Status,
    Uninstall
  };
  Q_ENUM(Action)
  struct Status
  {
    bool valid = false;
    bool installed = false;
    bool running = false;
    QString directory;
    QString error;
  };

  explicit FullControlManager(QObject *parent = nullptr);
  ~FullControlManager() override;
  const Status &status() const
  {
    return m_status;
  }
  bool busy() const
  {
    return m_busy;
  }
  bool actionRunning() const
  {
    return m_action.has_value();
  }
  void refresh();
  void run(Action action);

  static QString protectedDirectory(const QString &applicationDirectory);
  static QStringList arguments(Action action, const QString &script, const QString &source, const QString &settings);
  static QString quoteArguments(const QStringList &arguments);
  static Status parseStatus(const QByteArray &json, const QString &expectedDirectory);
  static std::optional<bool> confirmedMode(Action action, bool succeeded, const Status &status);
  static bool isProtectedDirectory(const QString &directory);

Q_SIGNALS:
  void statusChanged();
  void busyChanged(bool busy);
  void finished(deskflow::gui::FullControlManager::Action action, bool succeeded, const QString &error);

private:
  void readStatus(bool completingAction);
  void finish(bool succeeded, const QString &error);
  QString scriptPath(Action action) const;
  QString powerShellPath() const;
  void setBusy(bool busy);
  Status m_status;
  QProcess m_query;
  QTimer m_poll;
  QTimer m_queryTimeout;
  void *m_processHandle = nullptr;
  std::optional<Action> m_action;
  bool m_busy = false;
  bool m_completingAction = false;
};

} // namespace deskflow::gui
