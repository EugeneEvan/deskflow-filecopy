/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferSession.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>
#include <QtEndian>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace deskflow {
namespace {

constexpr quint32 magic = 0x44464631; // DFF1, private Deskflow file-copy extension
constexpr quint8 revision = 1;
constexpr qsizetype chunkBytes = 64 * 1024;
constexpr auto timeout = std::chrono::seconds(60);
using Clock = std::chrono::steady_clock;
enum class Type : quint8
{
  Begin = 1,
  Entry,
  Data,
  FileEnd,
  Complete,
  Ack,
  Abort
};
struct CacheBudget
{
  std::mutex mutex;
  QHash<QString, quint64> reservations;
};
const auto sharedCacheBudget = std::make_shared<CacheBudget>();

void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

void put32(QByteArray &out, quint32 value)
{
  value = qToBigEndian(value);
  out.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

void put64(QByteArray &out, quint64 value)
{
  value = qToBigEndian(value);
  out.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

void putBytes(QByteArray &out, const QByteArray &value)
{
  put32(out, static_cast<quint32>(value.size()));
  out.append(value);
}

struct Reader
{
  const QByteArray &bytes;
  qsizetype pos = 0;
  QByteArray raw(qsizetype size)
  {
    require(size >= 0 && size <= bytes.size() - pos, "file transfer: truncated envelope");
    auto result = bytes.mid(pos, size);
    pos += size;
    return result;
  }
  quint8 u8()
  {
    return static_cast<quint8>(raw(1)[0]);
  }
  quint32 u32()
  {
    auto data = raw(4);
    return qFromBigEndian<quint32>(data.constData());
  }
  quint64 u64()
  {
    auto data = raw(8);
    return qFromBigEndian<quint64>(data.constData());
  }
  QByteArray field(qsizetype maximum)
  {
    auto size = u32();
    require(size <= maximum, "file transfer: oversized field");
    return raw(size);
  }
  QString path()
  {
    auto data = field(16384);
    auto result = QString::fromUtf8(data);
    require(result.toUtf8() == data, "file transfer: invalid UTF-8 path");
    return result;
  }
  void end()
  {
    require(pos == bytes.size(), "file transfer: trailing envelope data");
  }
};

QByteArray packet(Type type, const QByteArray &id, quint32 sequence, const QByteArray &body = {})
{
  QByteArray result;
  put32(result, magic);
  result.append(static_cast<char>(revision));
  result.append(static_cast<char>(type));
  result.append(id);
  put32(result, sequence);
  result.append(body);
  return result;
}

bool safeRelative(const QString &path)
{
  if (path.isEmpty() || path.size() > 4096 || path.contains('\\') || path.startsWith('/'))
    return false;
  const auto parts = path.split('/');
  if (parts.size() > 64)
    return false;
  for (const auto &part : parts) {
    if (part.isEmpty() || part == "." || part == ".." || part.size() > 255 || part.endsWith('.') || part.endsWith(' '))
      return false;
    for (const auto ch : part) {
      if (ch.unicode() < 32 || QStringLiteral(":*?\"<>|").contains(ch))
        return false;
    }
    const auto stem = part.section('.', 0, 0).toUpper();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$" ||
        stem == "CLOCK$" ||
        (stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) &&
         QStringLiteral("123456789\u00b9\u00b2\u00b3").contains(stem[3])))
      return false;
  }
  return true;
}

#ifdef Q_OS_WIN
QString nativeExtendedPath(const QString &path)
{
  auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\?\\")))
    return native;
  if (native.startsWith(QStringLiteral("\\\\")))
    return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  return QStringLiteral("\\\\?\\") + native;
}
#endif

bool linked(const QString &path)
{
  if (QFileInfo(path).isSymLink())
    return true;
#ifdef Q_OS_WIN
  const auto native = nativeExtendedPath(path);
  const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
  return false;
#endif
}

void checkAncestors(QString path)
{
  path = QFileInfo(path).absoluteFilePath();
  for (;;) {
    require(!linked(path), "file transfer: symlink or reparse point is not supported");
    const auto parent = QFileInfo(path).dir().absolutePath();
    if (parent == path)
      break;
    path = parent;
  }
}

struct Entry
{
  QString source;
  QString relative;
  quint64 size = 0;
  bool directory = false;
  QDateTime modified;
};

} // namespace

struct FileTransferSession::Impl
{
  struct Command
  {
    enum class Kind
    {
      Send,
      Receive,
      Cancel
    } kind;
    quint64 generation;
    QStringList roots;
    QByteArray bytes;
    QString reason;
  };
  struct Event
  {
    quint64 generation;
    QByteArray bytes;
    QStringList roots;
    Progress progress{State::Preparing};
    enum class Kind
    {
      Send,
      Publish,
      Progress
    } kind;
    quint64 deliveryEpoch = 0;
  };
  struct Sender
  {
    QByteArray id;
    std::vector<Entry> entries;
    size_t index = 0;
    quint64 total = 0;
    quint64 done = 0;
    quint64 offset = 0;
    quint64 manifestBytes = 0;
    quint32 sequence = 0;
    std::deque<quint32> outstanding;
    enum class Phase
    {
      Begin,
      Entry,
      Data,
      Complete,
      Finished
    } phase = Phase::Begin;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    Clock::time_point touched = Clock::now();
  };
  struct Receiver
  {
    QByteArray id;
    QString directory;
    QStringList roots;
    QSet<QString> paths;
    QSet<QString> directories;
    quint32 expectedEntries = 0;
    quint32 entries = 0;
    quint32 sequence = 0;
    quint64 total = 0;
    quint64 done = 0;
    quint64 fileSize = 0;
    quint64 offset = 0;
    quint64 reservation = 0;
    quint64 manifestBytes = 0;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    Clock::time_point touched = Clock::now();
  };

  Callbacks callbacks;
  QString cacheRoot;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Command> commands;
  std::deque<Event> events;
  std::atomic_bool stopping{false};
  std::atomic<quint64> generation{0};
  std::atomic<quint64> deliveryEpoch{0};
  bool finished = false;
  quint64 workerGeneration = 0;
  std::unique_ptr<Sender> sender;
  std::unique_ptr<Receiver> receiver;
  QByteArray completedId;
  std::shared_ptr<CacheBudget> cacheBudget = sharedCacheBudget;

  Impl(Callbacks value, QString root) : callbacks(std::move(value)), cacheRoot(std::move(root))
  {
    if (cacheRoot.isEmpty()) {
#ifdef Q_OS_WIN
      cacheRoot = qEnvironmentVariable("LOCALAPPDATA");
#else
      cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
#endif
      require(!cacheRoot.isEmpty(), "file transfer: user cache directory is unavailable");
      cacheRoot = QDir(cacheRoot).filePath("Deskflow/file-transfer");
    }
    cacheRoot = QFileInfo(cacheRoot).absoluteFilePath();
  }

  void stop()
  {
    stopping = true;
    wake.notify_all();
    std::unique_lock lock(mutex);
    // OS file I/O on disconnected/network volumes cannot always be cancelled.
    // The detached worker owns this state and performs cleanup when it resumes;
    // it never invokes application callbacks. Disconnect must not freeze keyboard input.
    wake.wait_for(lock, std::chrono::milliseconds(200), [this] { return finished; });
  }

  void enqueueEvent(Event event)
  {
    bool notifyHost = false;
    {
      std::lock_guard lock(mutex);
      notifyHost = events.empty();
      event.deliveryEpoch = deliveryEpoch;
      // The ACK window bounds transport events; coalesce progress if the UI stalls.
      if (event.kind == Event::Kind::Progress && !events.empty() && events.back().kind == event.kind &&
          events.back().progress.state == event.progress.state)
        events.back() = std::move(event);
      else
        events.push_back(std::move(event));
    }
    if (notifyHost && callbacks.wake && !stopping)
      callbacks.wake();
  }

  void invalidateEvents()
  {
    std::lock_guard lock(mutex);
    ++deliveryEpoch;
    events.clear();
  }

  void send(const QByteArray &bytes)
  {
    enqueueEvent({workerGeneration, bytes, {}, {}, Event::Kind::Send});
  }
  void progress(State state, quint64 done, quint64 total, const QString &detail = {}, QByteArray id = {})
  {
    if (id.isEmpty())
      id = sender ? sender->id : (receiver ? receiver->id : QByteArray{});
    enqueueEvent(
        {workerGeneration,
         {},
         {},
         {state, done, total, detail, id.isEmpty() ? QString{} : QUuid::fromRfc4122(id).toString(QUuid::WithoutBraces)},
         Event::Kind::Progress}
    );
  }

  void discardReceive()
  {
    if (!receiver)
      return;
    receiver->file.close();
    // Only the unique direct child created by this session can be removed.
    if (!receiver->directory.isEmpty() && QFileInfo(receiver->directory).dir().absolutePath() == cacheRoot &&
        !linked(receiver->directory))
      QDir(receiver->directory).removeRecursively();
    releaseReservation();
    receiver.reset();
  }

  void releaseReservation()
  {
    std::lock_guard lock(cacheBudget->mutex);
    if (receiver && receiver->reservation) {
      cacheBudget->reservations[cacheRoot] -= receiver->reservation;
      if (cacheBudget->reservations[cacheRoot] == 0)
        cacheBudget->reservations.remove(cacheRoot);
      receiver->reservation = 0;
    }
  }

  void abort(const QString &reason, State state)
  {
    // Malformed trailing packets must not erase an earlier batch's queued Abort.
    if (sender || receiver || !completedId.isEmpty())
      invalidateEvents();
    const auto id = sender ? sender->id : (receiver ? receiver->id : completedId);
    QByteArray body;
    putBytes(body, reason.toUtf8().left(1024));
    if (sender)
      send(packet(Type::Abort, sender->id, 0, body));
    if (receiver)
      send(packet(Type::Abort, receiver->id, 0, body));
    if (!completedId.isEmpty())
      send(packet(Type::Abort, completedId, 0, body));
    completedId.clear();
    sender.reset();
    discardReceive();
    progress(state, 0, 0, reason, id);
  }

  bool interrupted() const
  {
    return stopping || generation != workerGeneration;
  }

  void enumerate(Sender &target, const QString &source, const QString &relative, QSet<QString> &names)
  {
    require(!interrupted(), "file transfer: preparation cancelled");
    require(safeRelative(relative), "file transfer: unsupported source filename or directory depth");
    require(target.entries.size() < maxEntries, "file transfer: batch exceeds 10000 entries");
    const auto folded = relative.toCaseFolded();
    require(!names.contains(folded), "file transfer: duplicate source names");
    names.insert(folded);
    checkAncestors(source);
    QFileInfo info(source);
    require(info.exists() && (info.isFile() || info.isDir()), "file transfer: source is unavailable or unsupported");
    const auto pathBytes = static_cast<quint64>(info.absoluteFilePath().toUtf8().size() + relative.toUtf8().size());
    require(pathBytes <= maxManifestBytes - target.manifestBytes, "file transfer: manifest paths exceed 8 MiB");
    target.manifestBytes += pathBytes;
    const auto size = info.isDir() ? 0 : static_cast<quint64>(info.size());
    require(size <= maxBatchBytes - target.total, "file transfer: batch exceeds 10 GiB");
    target.total += size;
    target.entries.push_back({info.absoluteFilePath(), relative, size, info.isDir(), info.lastModified()});
    if (info.isDir()) {
      require(info.isReadable(), "file transfer: source directory is unreadable");
#ifdef Q_OS_WIN
      // QDirIterator has no error channel and Qt can skip NTFS DACL checks.
      // Distinguish an unreadable directory from a genuinely empty directory.
      const auto pattern = nativeExtendedPath(source) + QStringLiteral("\\*");
      WIN32_FIND_DATAW found{};
      const auto search = FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()), &found);
      if (search == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        require(
            error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES,
            "file transfer: source directory enumeration failed"
        );
      } else {
        FindClose(search);
      }
#endif
      QDirIterator children(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
      while (children.hasNext()) {
        children.next();
        const auto child = children.fileInfo();
        enumerate(target, child.absoluteFilePath(), relative + '/' + child.fileName(), names);
      }
    }
  }

  void start(const QStringList &roots)
  {
    if (sender || receiver || !completedId.isEmpty())
      abort(QStringLiteral("file transfer: replaced by a new copy"), State::Cancelled);
    require(!roots.isEmpty() && roots.size() <= maxEntries, "file transfer: invalid source selection");
    auto next = std::make_unique<Sender>();
    next->id = QUuid::createUuid().toRfc4122();
    progress(State::Preparing, 0, 0, {}, next->id);
    QSet<QString> names;
    for (const auto &root : roots) {
      QFileInfo info(QDir::cleanPath(root));
      require(info.isAbsolute(), "file transfer: source path must be absolute");
      enumerate(*next, info.absoluteFilePath(), info.fileName(), names);
    }
    next->touched = Clock::now();
    sender = std::move(next);
  }

  bool canProduce() const
  {
    return sender && sender->phase != Sender::Phase::Finished && sender->outstanding.size() < maxInFlightFrames &&
           !interrupted();
  }

  void produce()
  {
    if (!canProduce())
      return;
    auto &s = *sender;
    QByteArray body;
    Type type = Type::Begin;
    if (s.phase == Sender::Phase::Begin) {
      put32(body, static_cast<quint32>(s.entries.size()));
      put64(body, s.total);
      s.phase = Sender::Phase::Entry;
    } else if (s.phase == Sender::Phase::Entry) {
      if (s.index == s.entries.size()) {
        type = Type::Complete;
        s.phase = Sender::Phase::Finished;
      } else {
        const auto &entry = s.entries[s.index];
        type = Type::Entry;
        putBytes(body, entry.relative.toUtf8());
        body.append(entry.directory ? '\1' : '\0');
        put64(body, entry.size);
        if (entry.directory) {
          ++s.index;
        } else {
          checkAncestors(entry.source);
          require(
              QFileInfo(entry.source).lastModified() == entry.modified,
              "file transfer: source file changed after preparation"
          );
          s.file.setFileName(entry.source);
          require(s.file.open(QIODevice::ReadOnly), "file transfer: cannot open source file");
          require(static_cast<quint64>(s.file.size()) == entry.size, "file transfer: source file size changed");
          s.hash.reset();
          s.offset = 0;
          s.phase = Sender::Phase::Data;
        }
      }
    } else if (s.phase == Sender::Phase::Data) {
      const auto &entry = s.entries[s.index];
      if (s.offset == entry.size) {
        require(
            s.file.atEnd() && static_cast<quint64>(s.file.size()) == entry.size,
            "file transfer: source file changed while reading"
        );
        require(
            QFileInfo(entry.source).lastModified() == entry.modified,
            "file transfer: source file modification time changed while reading"
        );
        checkAncestors(entry.source);
        type = Type::FileEnd;
        body = s.hash.result();
        s.file.close();
        ++s.index;
        s.phase = Sender::Phase::Entry;
      } else {
        type = Type::Data;
        body = s.file.read(static_cast<qint64>(qMin<quint64>(chunkBytes, entry.size - s.offset)));
        require(!body.isEmpty() && s.file.error() == QFileDevice::NoError, "file transfer: source read failed");
        s.hash.addData(body);
        s.offset += body.size();
        s.done += body.size();
      }
    } else {
      return;
    }
    const auto sequence = s.sequence++;
    s.outstanding.push_back(sequence);
    s.touched = Clock::now();
    send(packet(type, s.id, sequence, body));
  }

  void accept(const QByteArray &bytes)
  {
    require(bytes.size() <= maxEnvelopeBytes, "file transfer: oversized envelope");
    Reader input{bytes};
    require(input.u32() == magic && input.u8() == revision, "file transfer: incompatible envelope");
    const auto type = static_cast<Type>(input.u8());
    const auto id = input.raw(16);
    const auto sequence = input.u32();
    if (type == Type::Abort) {
      const auto reason = QString::fromUtf8(input.field(1024));
      input.end();
      bool active = false;
      if (sender && sender->id == id) {
        sender.reset();
        active = true;
      }
      if (receiver && receiver->id == id) {
        discardReceive();
        active = true;
      }
      // Complete can be queued for publication before the host pumps its events.
      // An old completed batch must never invalidate a newer active transfer.
      if (!sender && !receiver && completedId == id) {
        completedId.clear();
        active = true;
      }
      if (active) {
        invalidateEvents();
        progress(State::Cancelled, 0, 0, reason, id);
      }
      return;
    }
    if (type == Type::Ack) {
      input.end();
      // An acknowledgement racing local cancellation is harmless.
      if (!sender || sender->id != id)
        return;
      require(
          !sender->outstanding.empty() && sender->outstanding.front() == sequence,
          "file transfer: unexpected acknowledgement"
      );
      sender->outstanding.pop_front();
      sender->touched = Clock::now();
      if (sender->phase == Sender::Phase::Finished && sender->outstanding.empty()) {
        progress(State::Completed, sender->total, sender->total);
        sender.reset();
      } else {
        progress(State::Sending, sender->done, sender->total);
      }
      return;
    }
    if (type == Type::Begin) {
      if (sender) {
        const auto reason = QStringLiteral("file transfer: simultaneous copies cancelled; copy again on one computer");
        QByteArray body;
        putBytes(body, reason.toUtf8());
        abort(reason, State::Cancelled);
        send(packet(Type::Abort, id, 0, body));
        return;
      }
      if (receiver) {
        // A rapid clipboard replacement can supersede its queued Abort before
        // the host sends it. On this ordered peer stream, a new valid Begin
        // therefore also terminates the previous unfinished incoming batch.
        require(receiver->id != id, "file transfer: duplicate incoming batch");
        require(sequence == 0, "file transfer: invalid initial sequence");
        auto replacement = input;
        const auto entries = replacement.u32();
        const auto total = replacement.u64();
        replacement.end();
        require(
            entries > 0 && entries <= maxEntries && total <= maxBatchBytes,
            "file transfer: incoming batch exceeds limits"
        );
        invalidateEvents();
        QByteArray reason;
        putBytes(reason, QByteArray("file transfer: replaced by a new copy"));
        send(packet(Type::Abort, receiver->id, 0, reason));
        discardReceive();
      }
      require(sequence == 0, "file transfer: invalid initial sequence");
      receiver = std::make_unique<Receiver>();
      auto &next = receiver;
      next->id = id;
      next->expectedEntries = input.u32();
      next->total = input.u64();
      input.end();
      require(
          next->expectedEntries > 0 && next->expectedEntries <= maxEntries && next->total <= maxBatchBytes,
          "file transfer: incoming batch exceeds limits"
      );
      checkAncestors(cacheRoot);
      require(QDir().mkpath(cacheRoot), "file transfer: cannot create cache root");
      checkAncestors(cacheRoot);
      QStorageInfo storage(cacheRoot);
      require(
          storage.isValid() && storage.isReady() && !storage.isReadOnly() &&
              storage.bytesAvailable() >= static_cast<qint64>(next->total + 16 * 1024 * 1024),
          "file transfer: insufficient cache disk space"
      );
      {
        std::lock_guard lock(cacheBudget->mutex);
        quint64 cached = 0;
        quint32 scanned = 0;
        cacheUsage(cacheRoot, cached, scanned);
        const auto reserved = cacheBudget->reservations.value(cacheRoot);
        require(
            reserved <= maxCacheBytes - cached && next->total <= maxCacheBytes - cached - reserved,
            "file transfer: cache exceeds 20 GiB; clear old received files"
        );
        const auto name = QUuid::createUuid().toString(QUuid::WithoutBraces);
        require(QDir(cacheRoot).mkdir(name), "file transfer: cannot create unique batch directory");
        next->directory = QDir(cacheRoot).filePath(name);
        if (next->total)
          cacheBudget->reservations[cacheRoot] += next->total;
        next->reservation = next->total;
      }
    } else {
      // Late data from a cancelled batch must not invalidate a new copy.
      if (!receiver || receiver->id != id)
        return;
      require(sequence == receiver->sequence, "file transfer: unexpected data sequence");
      auto &r = *receiver;
      if (type == Type::Entry) {
        require(!r.file.isOpen() && r.entries < r.expectedEntries, "file transfer: unexpected file entry");
        const auto relative = input.path();
        const auto directory = input.u8();
        const auto size = input.u64();
        input.end();
        require(
            safeRelative(relative) && directory <= 1 && (!directory || size == 0), "file transfer: unsafe incoming path"
        );
        const auto pathBytes = static_cast<quint64>(relative.toUtf8().size());
        require(pathBytes <= maxManifestBytes - r.manifestBytes, "file transfer: incoming manifest paths exceed 8 MiB");
        r.manifestBytes += pathBytes;
        require(size <= r.total - r.done, "file transfer: file size exceeds batch size");
        const auto folded = relative.toCaseFolded();
        require(!r.paths.contains(folded), "file transfer: duplicate incoming path");
        const auto slash = folded.lastIndexOf('/');
        require(slash < 0 || r.directories.contains(folded.left(slash)), "file transfer: parent directory missing");
        const auto target = QDir(r.directory).filePath(relative);
        checkAncestors(target);
        require(!QFileInfo::exists(target), "file transfer: cache target already exists");
        if (directory) {
          require(QDir().mkdir(target), "file transfer: cannot create incoming directory");
          r.directories.insert(folded);
        } else {
          r.file.setFileName(target);
          require(r.file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "file transfer: cannot create incoming file");
          r.fileSize = size;
          r.offset = 0;
          r.hash.reset();
        }
        r.paths.insert(folded);
        ++r.entries;
        if (slash < 0)
          r.roots.append(target);
      } else if (type == Type::Data) {
        const auto data = input.raw(bytes.size() - input.pos);
        require(
            r.file.isOpen() && !data.isEmpty() && data.size() <= chunkBytes &&
                static_cast<quint64>(data.size()) <= r.fileSize - r.offset,
            "file transfer: invalid file chunk"
        );
        require(r.file.write(data) == data.size(), "file transfer: destination write failed");
        r.hash.addData(data);
        r.offset += data.size();
        r.done += data.size();
      } else if (type == Type::FileEnd) {
        const auto hash = input.raw(32);
        input.end();
        require(
            r.file.isOpen() && r.offset == r.fileSize && r.hash.result() == hash,
            "file transfer: file size or SHA-256 mismatch"
        );
        require(r.file.flush(), "file transfer: destination flush failed");
        r.file.close();
      } else if (type == Type::Complete) {
        input.end();
        require(
            !r.file.isOpen() && r.entries == r.expectedEntries && r.done == r.total && !r.roots.isEmpty(),
            "file transfer: incomplete batch"
        );
        // Successful caches deliberately outlive connections: CF_HDROP readers
        // can open these paths after the peer has disconnected.
        enqueueEvent({workerGeneration, {}, r.roots, {}, Event::Kind::Publish});
        progress(State::Ready, r.done, r.total, {}, r.id);
        completedId = r.id;
        releaseReservation();
        receiver.reset();
        send(packet(Type::Ack, id, sequence));
        return;
      } else {
        require(false, "file transfer: unknown envelope type");
      }
    }
    receiver->sequence = sequence + 1;
    receiver->touched = Clock::now();
    progress(State::Receiving, receiver->done, receiver->total, {}, receiver->id);
    send(packet(Type::Ack, id, sequence));
  }

  void cacheUsage(const QString &directory, quint64 &bytes, quint32 &entries, int depth = 0)
  {
    require(depth <= 65, "file transfer: cache directory depth exceeds limit");
    QDirIterator children(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (children.hasNext()) {
      children.next();
      const auto child = children.fileInfo();
      require(!interrupted(), "file transfer: cache inspection cancelled");
      require(++entries <= 100000, "file transfer: cache contains too many entries; clear old received files");
      require(!linked(child.absoluteFilePath()), "file transfer: cache contains a symlink or reparse point");
      if (child.isDir()) {
        cacheUsage(child.absoluteFilePath(), bytes, entries, depth + 1);
      } else {
        const auto size = static_cast<quint64>(child.size());
        require(size <= maxCacheBytes - bytes, "file transfer: cache exceeds 20 GiB; clear old received files");
        bytes += size;
      }
    }
  }

  void run()
  {
    while (!stopping) {
      Command command;
      bool available = false;
      {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(25), [this] {
          return stopping || !commands.empty() || canProduce();
        });
        if (stopping)
          break;
        if (!commands.empty()) {
          command = std::move(commands.front());
          commands.pop_front();
          available = true;
        }
      }
      try {
        if (available && command.generation == generation) {
          workerGeneration = command.generation;
          if (command.kind == Command::Kind::Send)
            start(command.roots);
          else if (command.kind == Command::Kind::Receive)
            accept(command.bytes);
          else
            abort(command.reason, State::Cancelled);
        }
        if ((sender && Clock::now() - sender->touched > timeout) ||
            (receiver && Clock::now() - receiver->touched > timeout))
          abort(QStringLiteral("file transfer: peer response timed out"), State::Failed);
        produce();
      } catch (const std::exception &error) {
        abort(QString::fromUtf8(error.what()), State::Failed);
      }
    }
    sender.reset();
    discardReceive();
    {
      std::lock_guard lock(mutex);
      finished = true;
    }
    wake.notify_all();
  }

  void replace(Command::Kind kind, const QStringList &roots, const QString &reason)
  {
    std::lock_guard lock(mutex);
    const auto next = ++generation;
    commands.clear();
    events.clear();
    commands.push_back({kind, next, roots, {}, reason});
    wake.notify_one();
  }
};

FileTransferSession::FileTransferSession(Callbacks callbacks, QString cacheRoot)
    : m_impl(std::make_shared<Impl>(std::move(callbacks), std::move(cacheRoot)))
{
  std::thread([state = m_impl] { state->run(); }).detach();
}

FileTransferSession::~FileTransferSession()
{
  m_impl->stop();
}

void FileTransferSession::startSend(const QStringList &roots)
{
  m_impl->replace(Impl::Command::Kind::Send, roots, {});
}

void FileTransferSession::receive(const QByteArray &envelope)
{
  if (envelope.size() > maxEnvelopeBytes) {
    cancel(QStringLiteral("file transfer: oversized network envelope"));
    return;
  }
  std::lock_guard lock(m_impl->mutex);
  if (m_impl->commands.size() >= 32 || m_impl->events.size() >= 64) {
    const auto next = ++m_impl->generation;
    m_impl->commands.clear();
    m_impl->events.clear();
    m_impl->commands.push_back(
        {Impl::Command::Kind::Cancel, next, {}, {}, QStringLiteral("file transfer: receive queue limit exceeded")}
    );
  } else {
    m_impl->commands.push_back({Impl::Command::Kind::Receive, m_impl->generation, {}, envelope, {}});
  }
  m_impl->wake.notify_one();
}

void FileTransferSession::cancel(const QString &reason)
{
  m_impl->replace(
      Impl::Command::Kind::Cancel, {}, reason.isEmpty() ? QStringLiteral("file transfer: cancelled") : reason
  );
}

void FileTransferSession::pump()
{
  // A callback may release its owning connection; keep worker state alive
  // while stopping dispatch immediately after that owner's destruction.
  const auto state = m_impl;
  std::deque<Impl::Event> pending;
  {
    std::lock_guard lock(state->mutex);
    pending.swap(state->events);
  }
  for (const auto &event : pending) {
    if (state->stopping || event.generation != state->generation || event.deliveryEpoch != state->deliveryEpoch)
      continue;
    const auto &cb = state->callbacks;
    if (event.kind == Impl::Event::Kind::Send && cb.send)
      cb.send(event.bytes);
    else if (event.kind == Impl::Event::Kind::Publish && cb.publish)
      cb.publish(event.roots);
    else if (event.kind == Impl::Event::Kind::Progress && cb.progress)
      cb.progress(event.progress);
  }
}

} // namespace deskflow
