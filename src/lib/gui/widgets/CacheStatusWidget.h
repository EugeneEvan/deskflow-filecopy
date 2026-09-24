/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;

namespace deskflow::gui {

class CacheStatusWidget : public QWidget
{
  Q_OBJECT
public:
  explicit CacheStatusWidget(QWidget *parent = nullptr);
  ~CacheStatusWidget() override;
  void refresh();

Q_SIGNALS:
  void manageRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  struct Work;
  void finishInspection();
  void updateText();
  QLabel *m_title = nullptr;
  QLabel *m_pathLabel = nullptr;
  QLabel *m_usageLabel = nullptr;
  QPushButton *m_manage = nullptr;
  QTimer *m_timer = nullptr;
  std::shared_ptr<Work> m_work;
  QString m_path;
  QString m_error;
  quint64 m_bytes = 0;
  int m_limitGiB = 20;
  bool m_pending = false;
  bool m_inspected = false;
};

} // namespace deskflow::gui
