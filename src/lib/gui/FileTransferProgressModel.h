/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonObject>
#include <QString>

#include <deque>
#include <optional>

namespace deskflow::gui {

// The clock is supplied by the GUI so sampling remains monotonic and testable.
class FileTransferProgressModel
{
public:
  struct Status
  {
    QString id;
    QString state;
    QString error;
    quint64 bytesDone = 0;
    quint64 bytesTotal = 0;
    quint64 filesDone = 0;
    quint64 filesTotal = 0;
    bool hasFileCounts = false;

    bool active() const;
    bool transferring() const;
    bool awaitingCompletion() const;
  };

  struct Metrics
  {
    int progress = 0; // 0..1000; only a successful terminal state reaches 1000.
    std::optional<double> bytesPerSecond;
    std::optional<quint64> secondsRemaining;
  };

  bool update(const QJsonObject &json, qint64 nowMs);
  Metrics metrics(qint64 nowMs) const;
  const Status &status() const;
  void reset();

private:
  struct Sample
  {
    qint64 time;
    quint64 bytes;
  };

  Status m_status;
  std::deque<Sample> m_samples;
  qint64 m_lastAdvanceMs = 0;
};

} // namespace deskflow::gui
