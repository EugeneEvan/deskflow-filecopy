# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

create_test(
  NAME CacheStatusWidgetTests
  DEPENDS gui
  SOURCE CacheStatusWidgetTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(CacheStatusWidgetTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cache-status"
)

create_test(
  NAME FileTransferWidgetTests
  DEPENDS gui
  SOURCE FileTransferWidgetTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME SettingsDialogTests
  DEPENDS gui
  SOURCE SettingsDialogTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_target_properties(SettingsDialogTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/settings-dialog"
)
add_dependencies(SettingsDialogTests app_translations)
add_custom_command(TARGET SettingsDialogTests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SettingsDialogTests>/translations"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_BINARY_DIR}/translations/deskflow_en.qm"
    "${PROJECT_BINARY_DIR}/translations/deskflow_zh_CN.qm"
    "$<TARGET_FILE_DIR:SettingsDialogTests>/translations"
)

create_test(
  NAME FileTransferProgressModelTests
  DEPENDS gui
  SOURCE FileTransferProgressModelTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME CoreIpcClientTests
  DEPENDS gui
  SOURCE CoreIpcClientTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
