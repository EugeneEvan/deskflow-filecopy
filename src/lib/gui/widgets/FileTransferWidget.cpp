/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferWidget.h"

#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <limits>

namespace deskflow::gui {
namespace {
QString formatBytes(quint64 bytes)
{
  return QLocale().formattedDataSize(static_cast<qint64>(qMin<quint64>(bytes, (std::numeric_limits<qint64>::max)())));
}
} // namespace

FileTransferWidget::FileTransferWidget(QWidget *parent)
    : QWidget(parent),
      m_group(new QGroupBox(this)),
      m_statusLabel(new QLabel(m_group)),
      m_detailLabel(new QLabel(m_group)),
      m_progress(new QProgressBar(m_group)),
      m_bytesLabel(new QLabel(m_group)),
      m_filesLabel(new QLabel(m_group)),
      m_speedLabel(new QLabel(m_group)),
      m_remainingLabel(new QLabel(m_group)),
      m_cancelButton(new QPushButton(m_group))
{
  setObjectName(QStringLiteral("fileTransferWidget"));
  m_statusLabel->setObjectName(QStringLiteral("fileTransferStatus"));
  m_detailLabel->setObjectName(QStringLiteral("fileTransferDetail"));
  m_progress->setObjectName(QStringLiteral("fileTransferProgress"));
  m_bytesLabel->setObjectName(QStringLiteral("fileTransferBytes"));
  m_filesLabel->setObjectName(QStringLiteral("fileTransferFiles"));
  m_speedLabel->setObjectName(QStringLiteral("fileTransferSpeed"));
  m_remainingLabel->setObjectName(QStringLiteral("fileTransferRemaining"));
  m_cancelButton->setObjectName(QStringLiteral("cancelFileTransfer"));

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_group);
  auto *panelLayout = new QVBoxLayout(m_group);
  panelLayout->setSpacing(10);
  auto *header = new QHBoxLayout;
  header->addWidget(m_statusLabel, 1);
  header->addWidget(m_cancelButton, 0, Qt::AlignTop);
  panelLayout->addLayout(header);
  panelLayout->addWidget(m_progress);

  auto *metrics = new QGridLayout;
  metrics->setColumnStretch(0, 1);
  metrics->setColumnStretch(1, 1);
  metrics->setHorizontalSpacing(20);
  metrics->addWidget(m_bytesLabel, 0, 0);
  metrics->addWidget(m_filesLabel, 0, 1);
  metrics->addWidget(m_speedLabel, 1, 0);
  metrics->addWidget(m_remainingLabel, 1, 1);
  panelLayout->addLayout(metrics);
  panelLayout->addWidget(m_detailLabel);
  for (auto *label : {m_statusLabel, m_detailLabel, m_bytesLabel, m_filesLabel, m_speedLabel, m_remainingLabel}) {
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
  }
  auto statusFont = m_statusLabel->font();
  statusFont.setBold(true);
  m_statusLabel->setFont(statusFont);
  m_progress->setRange(0, 1000);
  m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_clock.start();
  m_timer.setInterval(250);
  connect(&m_timer, &QTimer::timeout, this, &FileTransferWidget::updateDisplay);
  connect(m_cancelButton, &QPushButton::clicked, this, [this] {
    if (!m_model.status().active() || m_cancelling)
      return;
    m_cancelling = true;
    updateDisplay();
    Q_EMIT cancelRequested();
  });
  updateProgressPalette();
  updateDisplay();
}

void FileTransferWidget::setAvailable(bool enabled)
{
  if (m_available == enabled)
    return;
  m_available = enabled;
  updateDisplay();
}

void FileTransferWidget::setStatus(const QString &json)
{
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    qWarning("invalid file transfer status from core ipc");
    return;
  }
  const auto previousId = m_model.status().id;
  const auto previousState = m_model.status().state;
  if (!m_model.update(document.object(), m_clock.elapsed())) {
    qWarning("invalid file transfer state or counters from core ipc");
    return;
  }

  const auto status = m_model.status();
  const bool changed = previousId != status.id || previousState != status.state;
  m_interrupted = false;
  if (changed)
    m_cancelling = false;
  if (status.active()) {
    if (!m_timer.isActive())
      m_timer.start();
  } else {
    m_timer.stop();
  }
  // Frequent ACKs update the model; the timer bounds redraws and also expires
  // stale speed estimates when the core stops reporting progress.
  if (changed || !status.active())
    updateDisplay();

  if ((status.state == "ready" || status.state == "completed" || status.state == "failed") &&
      (m_lastNotificationId != status.id || m_lastNotificationState != status.state)) {
    m_lastNotificationId = status.id;
    m_lastNotificationState = status.state;
    Q_EMIT notification(m_statusLabel->text(), m_detailLabel->text(), status.state == "failed");
  }
  if (status.state == "ready" && (!m_hasReady || m_lastReadyId != status.id)) {
    m_hasReady = true;
    m_lastReadyId = status.id;
    Q_EMIT cacheChanged();
  }
}

void FileTransferWidget::setCoreStopped()
{
  m_timer.stop();
  if (!m_model.status().active())
    return;
  m_model.reset();
  m_cancelling = false;
  m_interrupted = true;
  updateDisplay();
}

void FileTransferWidget::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    updateProgressPalette();
  if (event->type() == QEvent::FontChange) {
    auto statusFont = font();
    statusFont.setBold(true);
    m_statusLabel->setFont(statusFont);
  }
  if (event->type() == QEvent::LanguageChange || event->type() == QEvent::LocaleChange ||
      event->type() == QEvent::FontChange)
    updateDisplay();
}

void FileTransferWidget::updateProgressPalette()
{
  auto colors = palette();
  colors.setColor(QPalette::Highlight, QColor(QStringLiteral("#b2bac4")));
  colors.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#20252b")));
  m_progress->setPalette(colors);
}

QString FileTransferWidget::formatDuration(quint64 seconds) const
{
  if (seconds >= 3600)
    return tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
  if (seconds >= 60)
    return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
  return tr("%1 s").arg(seconds);
}

void FileTransferWidget::updateDisplay()
{
  const auto &status = m_model.status();
  const auto &state = status.state;
  const auto metrics = m_model.metrics(m_clock.elapsed());
  m_group->setTitle(tr("Current transfer"));
  m_cancelButton->setText(tr("Cancel transfer"));
  m_cancelButton->setVisible(status.active());
  m_cancelButton->setEnabled(status.active() && !m_cancelling);
  m_progress->setValue(metrics.progress);
  m_progress->setFormat(QStringLiteral("%p%"));
  m_progress->setTextVisible(!state.isEmpty());

  QString message;
  QString detail;
  if (m_interrupted) {
    message = tr("File transfer interrupted");
    detail = tr("The connection stopped before the transfer finished. Reconnect, then copy the files again.");
  } else if (state.isEmpty()) {
    message = m_available ? tr("No active transfer") : tr("File copying is disabled");
    detail = m_available ? tr("Copy files or folders on either computer. Paste after the receiving computer is ready.")
                         : tr("Enable file copying in Settings > File transfer on both Windows computers.");
  } else if (state == "preparing") {
    message = tr("Preparing files...");
    detail = tr("Scanning files before transfer. Speed and remaining time will appear after transfer starts.");
  } else if (status.transferring()) {
    if (status.awaitingCompletion())
      message = state == "sending" ? tr("Waiting for confirmation...") : tr("Checking received files...");
    else if (status.bytesTotal == 0)
      message =
          state == "sending" ? tr("Sending empty files and folders...") : tr("Receiving empty files and folders...");
    else
      message = state == "sending" ? tr("Sending files") : tr("Receiving files");
    detail = state == "sending" ? tr("Wait until the receiving computer is ready before pasting.")
                                : tr("Wait until the transfer finishes before pasting.");
  } else if (state == "ready") {
    message = tr("Files ready to paste");
    detail = tr("Files received. Press Ctrl+V in the destination folder to paste.");
  } else if (state == "completed") {
    message = tr("Files sent");
    detail = tr("Files sent. Paste on the receiving computer when it reports that files are ready.");
  } else if (state == "cancelled") {
    message = tr("File transfer cancelled");
    detail = status.error.isEmpty() ? tr("Copy the files again when you are ready to retry.") : status.error;
  } else {
    message = tr("File transfer failed");
    detail = status.error.isEmpty() ? tr("The transfer could not finish. Check the connection and try copying again.")
                                    : status.error;
  }
  if (m_cancelling)
    message = tr("Cancelling file transfer...");

  const bool showCounts = !state.isEmpty() && state != "preparing";
  m_bytesLabel->setVisible(showCounts);
  m_filesLabel->setVisible(showCounts && status.hasFileCounts);
  m_bytesLabel->setText(tr("Size: %1 / %2").arg(formatBytes(status.bytesDone), formatBytes(status.bytesTotal)));
  if (status.filesTotal > 0)
    m_filesLabel->setText(
        tr("Files: %1 / %2").arg(QLocale().toString(status.filesDone), QLocale().toString(status.filesTotal))
    );
  else if (state == "receiving" || state == "ready")
    m_filesLabel->setText(tr("Files received: %1").arg(QLocale().toString(status.filesDone)));
  else
    m_filesLabel->setText(tr("Files: %1").arg(QLocale().toString(status.filesDone)));

  const bool showEstimates = status.transferring() && !status.awaitingCompletion() && status.bytesTotal > 0;
  m_speedLabel->setVisible(showEstimates);
  m_remainingLabel->setVisible(showEstimates);
  m_speedLabel->setText(
      metrics.bytesPerSecond
          ? tr("Speed: %1/s")
                .arg(formatBytes(
                    static_cast<quint64>(
                        qMin(*metrics.bytesPerSecond, static_cast<double>((std::numeric_limits<qint64>::max)()))
                    )
                ))
          : tr("Speed: --")
  );
  m_remainingLabel->setText(
      metrics.secondsRemaining ? tr("%1 remaining").arg(formatDuration(*metrics.secondsRemaining)) : tr("Remaining: --")
  );
  m_statusLabel->setText(message);
  m_detailLabel->setText(detail);
  m_progress->setAccessibleName(message);
  m_progress->setAccessibleDescription(detail);
}

} // namespace deskflow::gui
