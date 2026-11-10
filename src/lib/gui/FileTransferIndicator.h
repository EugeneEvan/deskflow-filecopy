/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "FileTransferProgressModel.h"

#include <QIcon>
#include <QObject>
#include <QTimer>
#include <QtGui/qwindowdefs.h>

#include <memory>

namespace deskflow::gui {

class FileTransferIndicator : public QObject
{
  Q_OBJECT
public:
  enum class VisualState
  {
    Idle,
    Preparing,
    Transferring,
    Verifying,
    Ready,
    Completed,
    Failed
  };
  enum class TaskbarState
  {
    None,
    Indeterminate,
    Normal,
    Error
  };
  Q_ENUM(VisualState)
  Q_ENUM(TaskbarState)

  explicit FileTransferIndicator(QObject *parent = nullptr);
  ~FileTransferIndicator() override;

  void apply(
      const FileTransferProgressModel::Status &status, const FileTransferProgressModel::Metrics &metrics,
      const QString &summary, const QString &detail
  );
  void setBaseIcon(const QIcon &icon);
  void setConnectionToolTip(const QString &text);
  void setConnected(bool connected);
  void clipboardChanged();
  void acknowledgeFailure();
  void clear();
  void invalidate();
  void setWindowId(WId windowId);
  void refreshTaskbar();

  VisualState visualState() const;
  TaskbarState taskbarState() const;
  int progress() const;
  bool animationRunning() const;
  QIcon icon() const;
  QString toolTip() const;
  QPixmap renderPixmap(int logicalSize, qreal devicePixelRatio = 1.0) const;

Q_SIGNALS:
  void iconChanged(const QIcon &icon);
  void toolTipChanged(const QString &text);

private:
  struct NativeTaskbar;
  void updateIcon();
  void updateToolTip();
  void updateTaskbar();

  QTimer m_timer;
  QIcon m_baseIcon;
  QIcon m_icon;
  FileTransferProgressModel::Status m_status;
  VisualState m_state = VisualState::Idle;
  QString m_connectionToolTip;
  QString m_summary;
  QString m_detail;
  QString m_toolTip;
  QString m_suppressedId;
  QString m_suppressedState;
  int m_progress = 0;
  int m_frame = 0;
  bool m_connected = true;
  bool m_suppressTask = false;
  WId m_windowId = 0;
  std::unique_ptr<NativeTaskbar> m_nativeTaskbar;
};

} // namespace deskflow::gui
