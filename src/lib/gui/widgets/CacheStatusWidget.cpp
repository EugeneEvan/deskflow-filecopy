/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CacheStatusWidget.h"

#include "common/FileTransferCache.h"
#include "common/Settings.h"

#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <thread>

namespace deskflow::gui {

struct CacheStatusWidget::Work
{
  std::atomic_bool cancelled{false};
  std::atomic_bool done{false};
  FileTransferCache::Usage usage;
  QString error;
};

CacheStatusWidget::CacheStatusWidget(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("cacheStatusWidget"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto *heading = new QHBoxLayout;
  m_title = new QLabel(this);
  auto font = m_title->font();
  font.setBold(true);
  m_title->setFont(font);
  m_manage = new QPushButton(this);
  m_manage->setObjectName(QStringLiteral("manageFileCache"));
  m_usageLabel = new QLabel(this);
  m_usageLabel->setObjectName(QStringLiteral("cacheSummary"));
  m_usageLabel->setTextFormat(Qt::PlainText);
  m_usageLabel->setWordWrap(true);
  heading->addWidget(m_title);
  heading->addWidget(m_usageLabel, 1);
  heading->addWidget(m_manage);
  layout->addLayout(heading);
  m_pathLabel = new QLabel(this);
  m_pathLabel->setObjectName(QStringLiteral("cacheFolder"));
  m_pathLabel->setTextFormat(Qt::PlainText);
  m_pathLabel->setWordWrap(true);
  m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  layout->addWidget(m_pathLabel);
  m_timer = new QTimer(this);
  m_timer->setInterval(100);
  connect(m_timer, &QTimer::timeout, this, &CacheStatusWidget::finishInspection);
  connect(m_manage, &QPushButton::clicked, this, &CacheStatusWidget::manageRequested);
  refresh();
}

CacheStatusWidget::~CacheStatusWidget()
{
  if (m_work)
    m_work->cancelled = true;
}

void CacheStatusWidget::refresh()
{
  // Coalesce refreshes and discard a stale result if the folder changed during a scan.
  if (m_work) {
    m_pending = true;
    m_work->cancelled = true;
    return;
  }
  m_pending = false;
  m_inspected = false;
  m_error.clear();
  m_path = Settings::value(Settings::Core::FileTransferCachePath).toString();
  m_limitGiB =
      std::clamp(Settings::value(Settings::Core::FileTransferCacheLimitGiB).toInt(), 1, FileTransferCache::maxLimitGiB);
  try {
    if (m_path.isEmpty())
      m_path = FileTransferCache::defaultRoot();
    m_work = std::make_shared<Work>();
    updateText();
    // The worker owns only its result and path, never this widget or any Qt UI object.
    std::thread([work = m_work, path = m_path] {
      try {
        work->usage = FileTransferCache::inspect(path, [work] { return work->cancelled.load(); });
      } catch (const std::exception &error) {
        work->error = QString::fromUtf8(error.what());
      }
      work->done = true;
    }).detach();
    m_timer->start();
  } catch (const std::exception &error) {
    m_error = QString::fromUtf8(error.what());
    m_work.reset();
    updateText();
  }
}

void CacheStatusWidget::finishInspection()
{
  if (!m_work || !m_work->done)
    return;
  m_timer->stop();
  const auto work = std::move(m_work);
  if (m_pending) {
    refresh();
    return;
  }
  m_error = work->error;
  m_bytes = work->usage.bytes;
  m_inspected = m_error.isEmpty();
  updateText();
}

void CacheStatusWidget::updateText()
{
  m_title->setText(tr("Received file cache"));
  m_manage->setText(tr("Manage cache..."));
  m_pathLabel->setText(m_path.isEmpty() ? tr("Default cache folder") : QDir::toNativeSeparators(m_path));
  m_pathLabel->setToolTip(m_pathLabel->text());
  m_pathLabel->setAccessibleName(tr("Cache folder"));
  m_pathLabel->setVisible(!m_error.isEmpty());
  m_manage->setToolTip(m_pathLabel->text());
  if (m_work) {
    m_usageLabel->setText(tr("Inspecting cache..."));
  } else if (!m_error.isEmpty()) {
    m_usageLabel->setText(tr("Cannot read cache usage: %1").arg(m_error));
  } else if (m_inspected) {
    m_usageLabel->setText(
        tr("%1 used / %2 GiB limit").arg(QLocale().formattedDataSize(static_cast<qint64>(m_bytes))).arg(m_limitGiB)
    );
  }
}

void CacheStatusWidget::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    updateText();
}

} // namespace deskflow::gui
