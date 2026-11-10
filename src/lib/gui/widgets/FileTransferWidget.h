/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/FileTransferProgressModel.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace deskflow::gui {

class FileTransferWidget : public QWidget
{
  Q_OBJECT
public:
  explicit FileTransferWidget(QWidget *parent = nullptr);
  void setStatus(const QString &json);
  void setCoreStopped();
  // Availability reflects the configured feature, not the connection state.
  // Existing transfer results remain visible when the setting changes.
  void setAvailable(bool enabled);
  void setRuntimeAvailability(const QString &json);
  void clearClipboardResult();
  const FileTransferProgressModel::Status &transferStatus() const;
  FileTransferProgressModel::Metrics transferMetrics() const;
  QString statusText() const;
  QString detailText() const;
  bool runtimeUnavailable() const;
  bool interrupted() const
  {
    return m_interrupted;
  }

Q_SIGNALS:
  void cancelRequested();
  void presentationChanged();
  void cacheChanged();

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateDisplay();
  void updateProgressPalette();
  QString formatDuration(quint64 seconds) const;

  QGroupBox *m_group;
  QLabel *m_statusLabel;
  QLabel *m_detailLabel;
  QProgressBar *m_progress;
  QLabel *m_bytesLabel;
  QLabel *m_filesLabel;
  QLabel *m_speedLabel;
  QLabel *m_remainingLabel;
  QPushButton *m_cancelButton;
  FileTransferProgressModel m_model;
  QElapsedTimer m_clock;
  QTimer m_timer;
  QString m_runtimeReason;
  QString m_invalidatedResultId;
  QString m_invalidatedResultState;
  QString m_lastReadyId;
  bool m_hasReady = false;
  bool m_available = false;
  bool m_cancelling = false;
  bool m_interrupted = false;
};

} // namespace deskflow::gui
