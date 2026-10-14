# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

add_executable(deskflow-file-agent deskflow-file-agent.cpp)
target_link_libraries(deskflow-file-agent app arch base io mt platform Qt6::Core ${libs})
install(TARGETS deskflow-file-agent RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
