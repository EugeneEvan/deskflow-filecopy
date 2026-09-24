/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QDialog>
#include <memory>

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QDialogButtonBox;
class QTimer;

class FileTransferCacheDialog : public QDialog
{
  Q_OBJECT
public:
  FileTransferCacheDialog(QString path, int limitGiB, QWidget *parent = nullptr);
  ~FileTransferCacheDialog() override;
  QString cachePath() const;
  int limitGiB() const;

private:
  struct Work;
  void accept() override;
  void startInspection(bool clean = false);
  void finishWork();
  void setBusy(bool busy);
  QLineEdit *m_path;
  QSpinBox *m_limit;
  QLabel *m_usage;
  QPushButton *m_browse;
  QPushButton *m_open;
  QPushButton *m_refresh;
  QPushButton *m_clean;
  QDialogButtonBox *m_buttons;
  QTimer *m_timer;
  std::shared_ptr<Work> m_work;
};
