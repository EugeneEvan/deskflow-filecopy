# SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
# SPDX-License-Identifier: MIT

if(WIN32)
  create_test(
    NAME ArchMultithreadWindowsTests
    DEPENDS arch
    LIBS base mt version ws2_32
    SOURCE ArchMultithreadWindowsTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/arch"
  )
  set_tests_properties(ArchMultithreadWindowsTests PROPERTIES TIMEOUT 15)
endif()
