/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "deskflow/WindowsFileAgentTransport.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <QDebug>
#include <UserEnv.h>
#include <Windows.h>
#include <WtsApi32.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace deskflow::fileagent {
namespace {
struct Handle
{
  HANDLE value = nullptr;
  ~Handle()
  {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};
ProcessIdentity identity(HANDLE token)
{
  alignas(TOKEN_USER) std::array<unsigned char, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> data{};
  DWORD length = 0;
  if (!GetTokenInformation(token, TokenUser, data.data(), DWORD(data.size()), &length))
    return ProcessIdentity::Unsupported;
  const auto user = reinterpret_cast<const TOKEN_USER *>(data.data());
  if (IsWellKnownSid(user->User.Sid, WinLocalSystemSid))
    return ProcessIdentity::System;
  if (IsWellKnownSid(user->User.Sid, WinLocalServiceSid) || IsWellKnownSid(user->User.Sid, WinNetworkServiceSid))
    return ProcessIdentity::Unsupported;
  return ProcessIdentity::User;
}
} // namespace

ProcessIdentity processIdentity()
{
  DWORD session = 0;
  Handle token;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0 ||
      !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
    return ProcessIdentity::Unsupported;
  return identity(token.value);
}

bool defaultDesktop()
{
  const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop)
    return false;
  std::array<wchar_t, 256> name{};
  DWORD size = 0;
  const bool result = GetUserObjectInformationW(desktop, UOI_NAME, name.data(), DWORD(sizeof(name)), &size) &&
                      _wcsicmp(name.data(), L"Default") == 0;
  CloseDesktop(desktop);
  return result;
}

struct WindowsTransport::State : std::enable_shared_from_this<State>
{
  explicit State(Wake callback) : wake(std::move(callback))
  {
  }
  ~State()
  {
    if (input)
      CloseHandle(input);
    if (output)
      CloseHandle(output);
    if (process)
      CloseHandle(process);
    if (job)
      CloseHandle(job);
  }
  std::mutex mutex;
  std::condition_variable changed;
  std::atomic_bool stopping{false};
  Wake wake;
  HANDLE input = nullptr;
  HANDLE output = nullptr;
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  std::deque<QByteArray> outgoing;
  qsizetype outgoingBytes = 0;
  std::vector<Message> incoming;
  qsizetype incomingBytes = 0;

  void stop()
  {
    if (stopping.exchange(true))
      return;
    {
      std::lock_guard lock(mutex);
      // Termination closes the opposite pipe ends, releasing blocked writes.
      // No join or process wait is performed by the keyboard event thread.
      if (process)
        TerminateProcess(process, 1);
      outgoing.clear();
      outgoingBytes = 0;
    }
    changed.notify_all();
    if (wake)
      wake();
  }

  void runPipes()
  {
    const auto self = shared_from_this();
    std::thread([self] {
      while (!self->stopping) {
        QByteArray bytes;
        {
          std::unique_lock lock(self->mutex);
          self->changed.wait(lock, [&] { return self->stopping || !self->outgoing.empty(); });
          if (self->stopping)
            break;
          bytes = std::move(self->outgoing.front());
          self->outgoing.pop_front();
          self->outgoingBytes -= bytes.size();
        }
        qsizetype offset = 0;
        while (!self->stopping && offset < bytes.size()) {
          DWORD written = 0;
          if (!WriteFile(self->output, bytes.constData() + offset, DWORD(bytes.size() - offset), &written, nullptr) ||
              !written) {
            self->stop();
            return;
          }
          offset += written;
        }
      }
    }).detach();
    std::thread([self] {
      Decoder decoder;
      while (!self->stopping) {
        DWORD available = 0;
        if (!PeekNamedPipe(self->input, nullptr, 0, nullptr, &available, nullptr)) {
          self->stop();
          return;
        }
        if (!available) {
          Sleep(5);
          continue;
        }
        QByteArray bytes(qMin<DWORD>(available, 64 * 1024), Qt::Uninitialized);
        DWORD read = 0;
        if (!ReadFile(self->input, bytes.data(), DWORD(bytes.size()), &read, nullptr) || !read) {
          self->stop();
          return;
        }
        bytes.resize(read);
        std::vector<Message> messages;
        if (!decoder.append(bytes, messages)) {
          self->stop();
          return;
        }
        bool overflow = false;
        {
          std::lock_guard lock(self->mutex);
          for (auto &message : messages) {
            const auto size = encode(message).size();
            if (self->incoming.size() >= maxQueuedMessages || self->incomingBytes + size > maxQueuedBytes) {
              overflow = true;
              break;
            }
            self->incomingBytes += size;
            self->incoming.push_back(std::move(message));
          }
        }
        if (overflow) {
          self->stop();
          return;
        }
        if (!messages.empty() && self->wake)
          self->wake();
      }
    }).detach();
  }

  bool launch()
  {
    DWORD session = 0;
    Handle token;
    DWORD tokenSession = 0, length = 0;
    if (processIdentity() != ProcessIdentity::System || !ProcessIdToSessionId(GetCurrentProcessId(), &session) ||
        !session) {
      qWarning() << "File agent requires an interactive SYSTEM core" << GetLastError();
      return false;
    }
    if (!WTSQueryUserToken(session, &token.value)) {
      qWarning() << "File agent WTSQueryUserToken failed" << GetLastError();
      return false;
    }
    if (identity(token.value) != ProcessIdentity::User ||
        !GetTokenInformation(token.value, TokenSessionId, &tokenSession, sizeof(tokenSession), &length) ||
        tokenSession != session) {
      qWarning() << "File agent user token session validation failed" << GetLastError();
      return false;
    }
    std::array<wchar_t, 32768> module{};
    const auto count = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
    if (!count || count >= module.size())
      return false;
    std::wstring executable(module.data(), count);
    const auto slash = executable.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
      return false;
    const auto directory = executable.substr(0, slash);
    executable = directory + L"\\deskflow-file-agent.exe";
    Handle parentRead, parentWrite, childRead, childWrite;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&childRead.value, &parentWrite.value, &attributes, 64 * 1024) ||
        !CreatePipe(&parentRead.value, &childWrite.value, &attributes, 64 * 1024) ||
        !SetHandleInformation(parentRead.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parentWrite.value, HANDLE_FLAG_INHERIT, 0))
      return false;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> storage(attributeBytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes))
      return false;
    const HANDLE handles[]{childRead.value, childWrite.value};
    const bool handlesSet = UpdateProcThreadAttribute(
        list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<HANDLE *>(handles), sizeof(handles), nullptr, nullptr
    );
    void *environment = nullptr;
    if (!handlesSet || !CreateEnvironmentBlock(&environment, token.value, FALSE)) {
      qWarning() << "File agent handle list or CreateEnvironmentBlock failed" << GetLastError();
      DeleteProcThreadAttributeList(list);
      return false;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\Default";
    startup.StartupInfo.lpDesktop = desktop;
    startup.lpAttributeList = list;
    auto command = L"\"" + executable + L"\" --pipes " + std::to_wstring(reinterpret_cast<uintptr_t>(childRead.value)) +
                   L" " + std::to_wstring(reinterpret_cast<uintptr_t>(childWrite.value));
    PROCESS_INFORMATION created{};
    Handle newJob;
    newJob.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    bool ok = newJob.value &&
              SetInformationJobObject(newJob.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
              CreateProcessAsUserW(
                  token.value, executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                  CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                  environment, directory.c_str(), &startup.StartupInfo, &created
              );
    const auto createError = GetLastError();
    DestroyEnvironmentBlock(environment);
    DeleteProcThreadAttributeList(list);
    if (!ok) {
      qWarning() << "File agent job setup or CreateProcessAsUser failed" << createError;
      return false;
    }
    Handle childProcess{created.hProcess}, childThread{created.hThread};
    ok = AssignProcessToJobObject(newJob.value, created.hProcess) && ResumeThread(created.hThread) != DWORD(-1);
    if (!ok)
      qWarning() << "File agent AssignProcessToJobObject or ResumeThread failed" << GetLastError();
    {
      std::lock_guard lock(mutex);
      if (!ok || stopping) {
        TerminateProcess(created.hProcess, 1);
        return false;
      }
      input = std::exchange(parentRead.value, nullptr);
      output = std::exchange(parentWrite.value, nullptr);
      process = std::exchange(childProcess.value, nullptr);
      job = std::exchange(newJob.value, nullptr);
    }
    return true;
  }
};

WindowsTransport::WindowsTransport(Wake wake) : m_state(std::make_shared<State>(std::move(wake)))
{
  std::thread([state = m_state] {
    if (!state->launch())
      state->stop();
    else
      state->runPipes();
  }).detach();
}
WindowsTransport::WindowsTransport(HANDLE input, HANDLE output, Wake wake)
    : m_state(std::make_shared<State>(std::move(wake)))
{
  m_state->input = input;
  m_state->output = output;
  SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
  m_state->runPipes();
}
WindowsTransport::~WindowsTransport()
{
  stop();
}
void WindowsTransport::stop()
{
  m_state->stop();
}
bool WindowsTransport::failed() const
{
  return m_state->stopping;
}
bool WindowsTransport::send(const Message &message)
{
  const auto bytes = encode(message);
  if (bytes.isEmpty()) {
    stop();
    return false;
  }
  {
    std::lock_guard lock(m_state->mutex);
    if (m_state->stopping)
      return false;
    if (m_state->outgoing.size() < maxQueuedMessages && m_state->outgoingBytes + bytes.size() <= maxQueuedBytes) {
      m_state->outgoingBytes += bytes.size();
      m_state->outgoing.push_back(bytes);
      m_state->changed.notify_one();
      return true;
    }
  }
  stop();
  return false;
}
std::vector<Message> WindowsTransport::take()
{
  std::lock_guard lock(m_state->mutex);
  std::vector<Message> result;
  result.swap(m_state->incoming);
  m_state->incomingBytes = 0;
  return result;
}
} // namespace deskflow::fileagent
#endif
