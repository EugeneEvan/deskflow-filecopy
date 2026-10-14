/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "arch/Arch.h"
#include "arch/win32/ArchMiscWindows.h"
#include "base/Log.h"
#include "deskflow/FileTransferFormat.h"
#include "deskflow/WindowsFileAgentTransport.h"
#include "platform/MSWindowsClipboard.h"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <QUuid>
#include <ShlObj_core.h>
#include <optional>
#include <shellapi.h>
#include <string>

using namespace deskflow;
using namespace deskflow::fileagent;

namespace {
QString transferId(const QByteArray &bytes)
{
  return QUuid::fromRfc4122(bytes.mid(6, 16)).toString(QUuid::WithoutBraces);
}

class Agent
{
public:
  Agent(HANDLE input, HANDLE output) : transport(input, output, {})
  {
    window = CreateWindowExW(
        0, L"STATIC", L"Deskflow File Agent", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (!window)
      transport.stop();
    source.published(GetClipboardSequenceNumber());
  }
  ~Agent()
  {
    transport.stop();
    session.reset();
    if (window)
      DestroyWindow(window);
  }

  void tick()
  {
    if (transport.failed()) {
      QCoreApplication::quit();
      return;
    }
    for (const auto &message : transport.take()) {
      if (!accept(message)) {
        transport.stop();
        QCoreApplication::quit();
        return;
      }
    }
    if (!session)
      return;
    // Secure desktop hides the user clipboard from this process. Preserve the
    // original sequence until Default is accessible, then check before publish.
    if (active && defaultDesktop() && GetClipboardSequenceNumber() != sequence)
      cancel(QStringLiteral("Clipboard changed; previous transfer cancelled"));
    observeSource();
    session->pump();
    if (!pendingRoots.isEmpty())
      publishPending();
  }

private:
  QStringList sourceRoots()
  {
    // Only clipboard memory is inspected here. All filesystem validation and
    // reads remain in FileTransferSession's ordinary-user worker.
    const auto data = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    const auto size = data ? GlobalSize(data) : 0;
    if (size < sizeof(DROPFILES) || size > SIZE_T(maxMessageBytes))
      return {};
    const UINT count = DragQueryFileW(data, 0xffffffff, nullptr, 0);
    if (count == 0 || count > FileTransferSession::maxEntries)
      return {};
    QStringList roots;
    qsizetype bytes = 32;
    for (UINT i = 0; i < count; ++i) {
      const UINT length = DragQueryFileW(data, i, nullptr, 0);
      if (length == 0 || length >= 32768)
        return {};
      std::wstring path(length + 1, L'\0');
      if (DragQueryFileW(data, i, path.data(), length + 1) != length)
        return {};
      const auto root = QString::fromWCharArray(path.data(), int(length));
      const auto utf8 = root.toUtf8();
      bytes += 4 + utf8.size();
      if (!QDir::isAbsolutePath(root) || utf8.size() > 32768 || bytes > maxMessageBytes)
        return {};
      roots.push_back(root);
    }
    return roots;
  }

  void observeSource()
  {
    // The SYSTEM core can receive WM_CLIPBOARDUPDATE while seeing no formats.
    // Polling the cheap sequence in the user's existing timer also tolerates
    // dropped window notifications and retries a temporarily locked clipboard.
    if (!defaultDesktop() || GetClipboardSequenceNumber() == source.sequence())
      return;
    MSWindowsClipboard clipboard(window);
    if (!clipboard.open(0))
      return;
    const auto observedSequence = GetClipboardSequenceNumber();
    const bool owned = MSWindowsClipboard::isOwnedByDeskflow();
    const auto roots = !owned && IsClipboardFormatAvailable(CF_HDROP) ? sourceRoots() : QStringList{};
    const bool current = defaultDesktop() && GetClipboardSequenceNumber() == observedSequence;
    clipboard.close();
    if (!current)
      return;
    if (const auto message = source.observe(observedSequence, owned, roots))
      transport.send(*message);
  }

  bool currentSource(const Message &message)
  {
    if (!defaultDesktop())
      return false;
    MSWindowsClipboard clipboard(window);
    if (!clipboard.open(0))
      return false;
    const bool current =
        defaultDesktop() && !MSWindowsClipboard::isOwnedByDeskflow() &&
        source.matches(message.sourceRevision, message.sequence, GetClipboardSequenceNumber(), message.roots);
    clipboard.close();
    return current;
  }

  void sendProgress(FileTransferSession::Progress progress)
  {
    Message message{Type::Progress};
    message.generation = generation;
    message.progress = std::move(progress);
    transport.send(message);
  }
  void cancel(const QString &reason)
  {
    active = false;
    pendingRoots.clear();
    pendingReady.reset();
    publicationLease.reset();
    session->cancel(reason, generation);
  }
  bool accept(const Message &message)
  {
    if (message.type == Type::Configure) {
      if (session)
        return false;
      FileTransferSession::Callbacks callbacks;
      callbacks.acceptGeneration = [this](quint64 eventGeneration) { return eventGeneration == generation; };
      callbacks.send = [this](const QByteArray &bytes) {
        Message message{Type::Send};
        message.generation = generation;
        message.payload = bytes;
        transport.send(message);
      };
      callbacks.publishWithLease = [this](const QStringList &paths, std::shared_ptr<QLockFile> lease) {
        if (active) {
          pendingRoots = paths;
          publicationLease = std::move(lease);
        }
      };
      callbacks.progress = [this](const auto &progress) {
        if (!progress.id.isEmpty())
          id = progress.id;
        if (progress.state == FileTransferSession::State::Ready) {
          if (active && !pendingRoots.isEmpty())
            pendingReady = progress;
          else {
            auto failed = progress;
            failed.state = FileTransferSession::State::Cancelled;
            failed.detail = QStringLiteral("Clipboard changed or unavailable; files were not placed on the clipboard");
            sendProgress(failed);
          }
          return;
        }
        if (progress.state == FileTransferSession::State::Completed ||
            progress.state == FileTransferSession::State::Cancelled ||
            progress.state == FileTransferSession::State::Failed) {
          active = false;
          pendingRoots.clear();
          pendingReady.reset();
          publicationLease.reset();
        }
        sendProgress(progress);
      };
      try {
        session = std::make_unique<FileTransferSession>(std::move(callbacks), message.text, message.cacheLimit);
      } catch (const std::exception &) {
        return false;
      }
      return transport.send(Message{Type::Ready});
    }
    if (!session)
      return false;
    if (message.type == Type::Cancel) {
      if (message.generation < generation)
        return false;
      generation = message.generation;
      cancel(message.text);
      return true;
    }
    if (message.type == Type::StartSend) {
      if (message.generation <= generation)
        return false;
      generation = message.generation;
      sequence = message.sequence;
      id.clear();
      active = true;
      pendingRoots.clear();
      pendingReady.reset();
      publicationLease.reset();
      if (!currentSource(message))
        cancel(QStringLiteral("Clipboard changed or unavailable"));
      else
        session->startSend(message.roots, generation);
      return true;
    }
    if (message.type == Type::Receive) {
      if (message.generation < generation)
        return true; // Cancellation raced a queued network frame.
      if (message.generation > generation) {
        if (quint8(message.payload[5]) != 1)
          return false;
        generation = message.generation;
        sequence = message.sequence;
        id = transferId(message.payload);
        active = true;
        pendingRoots.clear();
        pendingReady.reset();
        publicationLease.reset();
      }
      session->receive(message.payload, generation);
      return true;
    }
    return false;
  }

  void publishPending()
  {
    if (!active || !pendingReady || !defaultDesktop())
      return;
    MSWindowsClipboard clipboard(window);
    if (!clipboard.open(0))
      return;
    // The sequence must be checked with the global clipboard lock held.
    // Checking before OpenClipboard permits a local Ctrl+C to be overwritten.
    if (!defaultDesktop() || GetClipboardSequenceNumber() != sequence) {
      clipboard.close();
      cancel(QStringLiteral("Clipboard changed; previous transfer cancelled"));
      return;
    }
    std::vector<FileRef> refs;
    for (const auto &path : pendingRoots)
      refs.push_back({path.toUtf8().toStdString(), 0});
    const auto bytes = FileTransferFormat::marshall(refs);
    bool published = clipboard.empty();
    if (published)
      clipboard.add(IClipboard::Format::Files, bytes);
    const auto actual = FileTransferFormat::unmarshall(clipboard.get(IClipboard::Format::Files));
    published = published && actual.size() == refs.size();
    for (size_t i = 0; published && i < refs.size(); ++i)
      published = QString::fromStdString(actual[i].path).replace('\\', '/') ==
                  QString::fromStdString(refs[i].path).replace('\\', '/');
    const auto effect = GetClipboardData(RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT));
    const auto *value = effect ? static_cast<const DWORD *>(GlobalLock(effect)) : nullptr;
    published = published && value && GlobalSize(effect) >= sizeof(DWORD) && *value == DROPEFFECT_COPY;
    if (value)
      GlobalUnlock(effect);
    const auto publishedSequence = GetClipboardSequenceNumber();
    clipboard.close();
    if (!published) {
      cancel(QStringLiteral("File clipboard publication failed"));
      return;
    }
    source.published(publishedSequence);
    Message message{Type::Published};
    message.generation = generation;
    message.sequence = publishedSequence;
    message.id = id;
    message.roots = pendingRoots;
    transport.send(message);
    sequence = publishedSequence;
    sendProgress(*pendingReady);
    pendingReady.reset();
    pendingRoots.clear();
    publicationLease.reset();
    active = false;
  }

  WindowsTransport transport;
  SourceClipboardTracker source;
  std::unique_ptr<FileTransferSession> session;
  HWND window = nullptr;
  quint64 generation = 0;
  quint32 sequence = 0;
  QString id;
  bool active = false;
  QStringList pendingRoots;
  std::optional<FileTransferSession::Progress> pendingReady;
  std::shared_ptr<QLockFile> publicationLease;
};
} // namespace

int main(int argc, char **argv)
{
  // This executable only accepts two inherited pipe handles. It has no network,
  // generic command, configuration-file or privileged launch interface.
  if (processIdentity() != ProcessIdentity::User)
    return 1;
  ArchMiscWindows::setInstanceWin32(GetModuleHandleW(nullptr));
  QCoreApplication application(argc, argv);
  const auto args = application.arguments();
  if (args.size() != 4 || args[1] != QStringLiteral("--pipes"))
    return 1;
  bool validInput = false, validOutput = false;
  const auto input = reinterpret_cast<HANDLE>(quintptr(args[2].toULongLong(&validInput)));
  const auto output = reinterpret_cast<HANDLE>(quintptr(args[3].toULongLong(&validOutput)));
  if (!validInput || !validOutput || !input || !output || input == output || GetFileType(input) != FILE_TYPE_PIPE ||
      GetFileType(output) != FILE_TYPE_PIPE)
    return 1;
  Arch arch;
  arch.init();
  Log log;
  Agent agent(input, output);
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&agent] { agent.tick(); });
  timer.start(10);
  return application.exec();
}
