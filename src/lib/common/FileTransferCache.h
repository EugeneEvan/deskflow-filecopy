/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QLockFile>
#include <QStringList>
#include <functional>
#include <memory>

namespace deskflow {

// Shared by the user-session core and the cache dialog. Only marked v1 batches
// may be removed; legacy caches and unrelated files are never adopted.
class FileTransferCache
{
public:
  using Cancelled = std::function<bool()>;
  struct Usage
  {
    quint64 bytes = 0;
    quint32 entries = 0;
    quint32 managedBatches = 0;
  };
  struct Cleanup
  {
    quint32 removedBatches = 0;
    quint32 protectedBatches = 0;
    QStringList errors;
  };
  static constexpr quint64 gibibyte = 1024ULL * 1024 * 1024;
  static constexpr int defaultLimitGiB = 20;
  static constexpr int maxLimitGiB = 1024;

  static QString defaultRoot();
  static QString validatedRoot(const QString &root);
  static std::shared_ptr<QLockFile> lock(const QString &root);
  // Caller retains the root lock from batch creation through clipboard publication.
  static QString createBatch(const QString &root);
  static void removeBatch(const QString &root, const QString &batch, const Cancelled &cancelled = {});
  static Usage inspect(const QString &root, const Cancelled &cancelled = {});
  static Cleanup clean(const QString &root, const QStringList &protectedPaths = {}, const Cancelled &cancelled = {});
  static void requireCapacity(quint64 used, quint64 incoming, quint64 limit, qint64 diskAvailable);
};

} // namespace deskflow
