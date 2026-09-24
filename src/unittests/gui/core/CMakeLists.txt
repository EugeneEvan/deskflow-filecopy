# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME CoreProcessEndpointsTests
  DEPENDS gui
  SOURCE CoreProcessEndpointsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(CoreProcessEndpointsTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/core-process-endpoints"
)

create_test(
  NAME NetworkMonitorTests
  DEPENDS gui
  SOURCE NetworkMonitorTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
