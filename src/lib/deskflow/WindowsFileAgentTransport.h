/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "deskflow/FileAgentProtocol.h"
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
namespace deskflow::fileagent {
enum class ProcessIdentity
{
  User,
  System,
  Unsupported
};
ProcessIdentity processIdentity();
bool defaultDesktop();

// Both pipe I/O and process creation run off the core event thread. The bounded
// queues carry only FileAgentProtocol messages, never generic commands.
class WindowsTransport
{
public:
  using Wake = std::function<void()>;
  explicit WindowsTransport(Wake wake);
  WindowsTransport(void *input, void *output, Wake wake);
  ~WindowsTransport();
  bool send(const Message &message);
  std::vector<Message> take();
  bool failed() const;
  void stop();

private:
  struct State;
  std::shared_ptr<State> m_state;
};
} // namespace deskflow::fileagent
#endif
