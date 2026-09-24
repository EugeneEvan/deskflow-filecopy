/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FileTransferCacheDialog.h"
#include "common/FileTransferCache.h"
#include "common/Settings.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <atomic>
#include <stdexcept>
#include <thread>

using deskflow::FileTransferCache;

struct FileTransferCacheDialog::Work
{
  std::atomic_bool cancelled{false};
  std::atomic_bool done{false};
  FileTransferCache::Usage usage;
  FileTransferCache::Cleanup cleanup;
  QString error;
  bool cleaning = false;
};

FileTransferCacheDialog::FileTransferCacheDialog(QString path, int limitGiB, QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("File cache"));
  setMinimumWidth(560);
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  m_path = new QLineEdit(path, this);
  m_path->setObjectName(QStringLiteral("fileCachePath"));
  m_path->setPlaceholderText(QDir::toNativeSeparators(FileTransferCache::defaultRoot()));
  m_browse = new QPushButton(tr("Browse..."), this);
  auto *pathRow = new QHBoxLayout;
  pathRow->addWidget(m_path);
  pathRow->addWidget(m_browse);
  form->addRow(tr("Cache folder"), pathRow);
  m_limit = new QSpinBox(this);
  m_limit->setObjectName(QStringLiteral("fileCacheLimitGiB"));
  m_limit->setRange(1, FileTransferCache::maxLimitGiB);
  m_limit->setSuffix(QStringLiteral(" GiB"));
  m_limit->setValue(limitGiB);
  form->addRow(tr("Capacity limit"), m_limit);
  layout->addLayout(form);
  auto *info = new QLabel(
      tr("Choose a dedicated local folder. Leave the path empty to use the default folder. "
         "Save Preferences and reconnect to apply changes. Existing files are not moved or automatically deleted."),
      this
  );
  info->setWordWrap(true);
  layout->addWidget(info);
  m_usage = new QLabel(this);
  m_usage->setObjectName(QStringLiteral("fileCacheUsage"));
  m_usage->setWordWrap(true);
  m_usage->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_usage);
  auto *actions = new QHBoxLayout;
  m_open = new QPushButton(tr("Open folder"), this);
  m_refresh = new QPushButton(tr("Refresh usage"), this);
  m_clean = new QPushButton(tr("Clean received files..."), this);
  actions->addWidget(m_open);
  actions->addWidget(m_refresh);
  actions->addWidget(m_clean);
  layout->addLayout(actions);
  auto *cleanupInfo = new QLabel(
      tr("Cleanup removes only batches marked by this version. Current clipboard files are kept. "
         "Cleanup is unavailable while receiving. Older unmarked caches and unrelated files are kept."),
      this
  );
  cleanupInfo->setWordWrap(true);
  layout->addWidget(cleanupInfo);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(m_buttons);
  m_timer = new QTimer(this);
  m_timer->setInterval(50);
  connect(m_timer, &QTimer::timeout, this, &FileTransferCacheDialog::finishWork);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &FileTransferCacheDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_browse, &QPushButton::clicked, this, [this] {
    const auto directory = QFileDialog::getExistingDirectory(this, tr("Choose cache folder"), m_path->text());
    if (!directory.isEmpty()) {
      m_path->setText(QDir::toNativeSeparators(directory));
      startInspection();
    }
  });
  connect(m_open, &QPushButton::clicked, this, [this] {
    try {
      const auto root = FileTransferCache::validatedRoot(m_path->text().trimmed());
      if (!QDir().mkpath(root) || !QDesktopServices::openUrl(QUrl::fromLocalFile(root)))
        throw std::runtime_error("file cache: cannot open folder");
    } catch (const std::exception &error) {
      QMessageBox::warning(this, tr("File cache"), QString::fromUtf8(error.what()));
    }
  });
  connect(m_refresh, &QPushButton::clicked, this, [this] { startInspection(); });
  connect(m_clean, &QPushButton::clicked, this, [this] {
    if (QMessageBox::question(
            this, tr("Clean received files"),
            tr("Confirm that all pastes have finished and no application needs old received files. "
               "Remove this version's cached batches except those referenced by the current clipboard? "
               "This cannot be undone. Files already pasted elsewhere are unaffected."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No
        ) == QMessageBox::Yes)
      startInspection(true);
  });
  connect(m_path, &QLineEdit::textEdited, this, [this] { m_usage->setText(tr("Refresh to inspect this folder.")); });
  startInspection();
}

FileTransferCacheDialog::~FileTransferCacheDialog()
{
  if (m_work)
    m_work->cancelled = true;
}

QString FileTransferCacheDialog::cachePath() const
{
  return QDir::fromNativeSeparators(m_path->text().trimmed());
}

int FileTransferCacheDialog::limitGiB() const
{
  return m_limit->value();
}

void FileTransferCacheDialog::accept()
{
  try {
    const auto root = FileTransferCache::validatedRoot(cachePath());
    if (!QDir().mkpath(root))
      throw std::runtime_error("file cache: cannot create chosen folder");
    QTemporaryFile probe(QDir(root).filePath(QStringLiteral(".deskflow-write-check-XXXXXX")));
    if (!probe.open() || probe.write("cache") != 5 || !probe.flush())
      throw std::runtime_error("file cache: chosen folder is not writable");
    QDialog::accept();
  } catch (const std::exception &error) {
    QMessageBox::warning(this, tr("File cache"), QString::fromUtf8(error.what()));
  }
}

void FileTransferCacheDialog::setBusy(bool busy)
{
  const bool writable = Settings::isWritable();
  m_path->setEnabled(!busy && writable);
  m_limit->setEnabled(!busy && writable);
  m_browse->setEnabled(!busy && writable);
  m_open->setEnabled(!busy);
  m_refresh->setEnabled(!busy);
  m_clean->setEnabled(!busy && writable);
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy && writable);
}

void FileTransferCacheDialog::startInspection(bool clean)
{
  if (m_work)
    return;
  m_work = std::make_shared<Work>();
  m_work->cleaning = clean;
  setBusy(true);
  m_usage->setText(clean ? tr("Cleaning cache...") : tr("Inspecting cache..."));
  const auto path = cachePath();
  std::thread([work = m_work, path] {
    try {
      const auto cancelled = [work] { return work->cancelled.load(); };
      if (work->cleaning)
        work->cleanup = FileTransferCache::clean(path, {}, cancelled);
      work->usage = FileTransferCache::inspect(path, cancelled);
    } catch (const std::exception &error) {
      work->error = QString::fromUtf8(error.what());
    }
    work->done = true;
  }).detach();
  m_timer->start();
}

void FileTransferCacheDialog::finishWork()
{
  if (!m_work || !m_work->done)
    return;
  m_timer->stop();
  const auto work = std::move(m_work);
  setBusy(false);
  QString text;
  if (!work->error.isEmpty()) {
    text = tr("Cache operation failed: %1").arg(work->error);
  } else {
    text = tr("Folder usage: %1. Managed batches: %2.")
               .arg(QLocale().formattedDataSize(static_cast<qint64>(work->usage.bytes)))
               .arg(work->usage.managedBatches);
  }
  if (work->cleaning) {
    text += '\n' + tr("Removed %1 batches; kept %2 clipboard batches.")
                       .arg(work->cleanup.removedBatches)
                       .arg(work->cleanup.protectedBatches);
    if (!work->cleanup.errors.isEmpty())
      text += '\n' + tr("Some batches could not be removed:") + '\n' + work->cleanup.errors.mid(0, 5).join('\n');
  }
  m_usage->setText(text);
}
