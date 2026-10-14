/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <memory>

class QLockFile;

namespace deskflow {

// One authenticated peer connection. Disk work runs on a private worker;
// pump() dispatches application callbacks on the caller's event thread. Never share a
// session between peers. Call cancel() when the connection/clipboard expires.
class FileTransferSession
{
public:
  enum class State
  {
    Preparing,
    Sending,
    Receiving,
    Ready,
    Completed,
    Cancelled,
    Failed
  };
  struct Progress
  {
    State state;
    quint64 doneBytes = 0;
    quint64 totalBytes = 0;
    QString detail;
    QString id;
    quint32 filesDone = 0;
    // 0 means unknown while receiving with the v1 streaming manifest.
    quint32 filesTotal = 0;
  };
  struct Callbacks
  {
    std::function<void(const QByteArray &)> send;
    std::function<void(const QStringList &)> publish;
    std::function<void(const Progress &)> progress;
    // Worker-thread notification only. Post a safe host event; send, publish
    // and progress continue to run exclusively from pump(). Called unlocked.
    std::function<void()> wake;
    // A user agent may defer publication until Default desktop is accessible.
    // Retain the cache lease through that deferred publication or cancellation.
    std::function<void(const QStringList &, std::shared_ptr<QLockFile>)> publishWithLease;
    // Optional caller lifetime, independent of the worker's cancellation
    // generation. Checked before every application callback from pump().
    std::function<bool(quint64)> acceptGeneration;
  };

  explicit FileTransferSession(
      Callbacks callbacks, QString cacheRoot = {}, quint64 cacheLimitBytes = 20ULL * 1024 * 1024 * 1024
  );
  ~FileTransferSession();
  FileTransferSession(const FileTransferSession &) = delete;
  FileTransferSession &operator=(const FileTransferSession &) = delete;

  void startSend(const QStringList &roots, quint64 callerGeneration = 0);
  void receive(const QByteArray &envelope, quint64 callerGeneration = 0);
  void cancel(const QString &reason = {}, quint64 callerGeneration = 0);
  void pump();

  static constexpr quint64 maxBatchBytes = 10ULL * 1024 * 1024 * 1024;
  static constexpr quint64 maxCacheBytes = 20ULL * 1024 * 1024 * 1024;
  static constexpr quint32 maxEntries = 10000;
  static constexpr quint64 maxManifestBytes = 8ULL * 1024 * 1024;
  static constexpr qsizetype maxEnvelopeBytes = 128 * 1024;
  static constexpr std::size_t maxInFlightFrames = 8;

private:
  struct Impl;
  std::shared_ptr<Impl> m_impl;
};

} // namespace deskflow
