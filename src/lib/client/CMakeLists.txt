# SPDX-FileCopyrightText: (C) 2024, 2026 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-FileCopyrightText: (C) 2012 - 2024 Symless Ltd
# SPDX-FileCopyrightText: (C) 2009 - 2012 Nick Bolton
# SPDX-License-Identifier: MIT

add_library(client STATIC
  Client.cpp
  Client.h
  ServerProxy.cpp
  ServerProxy.h
  ServerProxy1_7.cpp
  ServerProxy1_7.h
  ServerProxy1_8.cpp
  ServerProxy1_8.h
  ServerProxyFileTransfer.cpp
  ServerProxyFileTransfer.h
)

target_link_libraries(client PUBLIC common)

# The optional file-copy bridge is supplied by the app library.
target_link_libraries(client PUBLIC app)
