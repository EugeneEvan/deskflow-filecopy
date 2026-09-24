/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferProgressModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace deskflow::gui {
namespace {
constexpr qint64 sampleIntervalMs = 200;
constexpr qint64 minimumRateIntervalMs = 100;
constexpr qint64 sampleWindowMs = 3000;
constexpr qint64 stalledAfterMs = 2000;

bool readCounter(const QJsonObject &json, const QString &key, quint64 &counter)
{
  const auto value = json.value(key);
  if (value.isUndefined())
    return true; // Older cores do not report file counts.
  if (value.isString()) {
    bool ok = false;
    counter = value.toString().toULongLong(&ok);
    return ok;
  }
  // Qt preserves signed 64-bit integer JSON values. Strings also support the
  // full unsigned range without a conversion through floating point.
  if (!value.isDouble())
    return false;
  const auto number = value.toInteger(-1);
  if (number < 0)
    return false;
  counter = static_cast<quint64>(number);
  return true;
}
} // namespace

bool FileTransferProgressModel::Status::active() const
{
  return state == "preparing" || transferring();
}

bool FileTransferProgressModel::Status::transferring() const
{
  return state == "sending" || state == "receiving";
}

bool FileTransferProgressModel::Status::awaitingCompletion() const
{
  return transferring() && bytesTotal > 0 && bytesDone >= bytesTotal;
}

bool FileTransferProgressModel::update(const QJsonObject &json, qint64 nowMs)
{
  Status next;
  next.id = json.value(QStringLiteral("id")).toString();
  next.state = json.value(QStringLiteral("state")).toString();
  next.error = json.value(QStringLiteral("error")).toString();
  if (!next.active() && next.state != "ready" && next.state != "completed" && next.state != "cancelled" &&
      next.state != "failed")
    return false;
  if (!readCounter(json, QStringLiteral("received"), next.bytesDone) ||
      !readCounter(json, QStringLiteral("total"), next.bytesTotal) ||
      !readCounter(json, QStringLiteral("filesDone"), next.filesDone) ||
      !readCounter(json, QStringLiteral("filesTotal"), next.filesTotal))
    return false;
  next.hasFileCounts = json.contains(QStringLiteral("filesDone"));

  const bool newSampleSeries = next.id != m_status.id || next.state != m_status.state ||
                               next.bytesDone < m_status.bytesDone ||
                               (!m_samples.empty() && nowMs < m_samples.back().time);
  if (newSampleSeries || !next.transferring()) {
    m_samples.clear();
    m_lastAdvanceMs = nowMs;
  } else if (next.bytesDone > m_status.bytesDone) {
    if (nowMs - m_lastAdvanceMs >= stalledAfterMs) {
      // A resumed connection needs fresh samples rather than a pre-stall ETA.
      m_samples.clear();
    }
    m_lastAdvanceMs = nowMs;
  }

  m_status = next;
  if (next.transferring()) {
    if (m_samples.empty() || nowMs - m_samples.back().time >= sampleIntervalMs)
      m_samples.push_back({nowMs, next.bytesDone});
    while (m_samples.size() > 1 && m_samples[1].time <= nowMs - sampleWindowMs)
      m_samples.pop_front();
  }
  return true;
}

FileTransferProgressModel::Metrics FileTransferProgressModel::metrics(qint64 nowMs) const
{
  Metrics result;
  if (m_status.state == "ready" || m_status.state == "completed") {
    result.progress = 1000;
    return result;
  }
  if (!m_status.transferring())
    return result;
  if (m_status.bytesTotal > 0) {
    const auto ratio = static_cast<double>(m_status.bytesDone) / static_cast<double>(m_status.bytesTotal);
    result.progress = static_cast<int>(std::clamp(ratio, 0.0, 0.999) * 1000);
  }
  if (m_status.awaitingCompletion() || m_samples.empty() || nowMs - m_lastAdvanceMs >= stalledAfterMs)
    return result;

  const auto &first = m_samples.front();
  const auto elapsed = nowMs - first.time;
  if (elapsed < minimumRateIntervalMs || m_status.bytesDone <= first.bytes)
    return result;
  const auto rate = static_cast<double>(m_status.bytesDone - first.bytes) * 1000.0 / static_cast<double>(elapsed);
  result.bytesPerSecond = rate;
  if (m_status.bytesTotal > m_status.bytesDone) {
    const auto seconds = std::ceil(static_cast<long double>(m_status.bytesTotal - m_status.bytesDone) / rate);
    if (seconds < static_cast<long double>(std::numeric_limits<quint64>::max()))
      result.secondsRemaining = static_cast<quint64>(seconds);
  }
  return result;
}

const FileTransferProgressModel::Status &FileTransferProgressModel::status() const
{
  return m_status;
}

void FileTransferProgressModel::reset()
{
  m_status = {};
  m_samples.clear();
  m_lastAdvanceMs = 0;
}

} // namespace deskflow::gui
