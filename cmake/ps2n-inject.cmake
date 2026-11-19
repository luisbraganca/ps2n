# SPDX-License-Identifier: GPL-3.0-or-later
#
# PCSX2's build is driven unmodified, as the top-level CMake project of the
# pinned submodule. This file is handed to it through
#   -DCMAKE_PROJECT_Pcsx2_INCLUDE=<this file> -DPS2N_ROOT=<repository root>
# so CMake includes it at the end of PCSX2's project() call. It defers adding
# PS2Native's own targets until PCSX2's top-level CMakeLists.txt has finished,
# when the PCSX2 and PCSX2_FLAGS targets exist to link against.
#
# CMake does not allow add_subdirectory() in a deferred call, so the targets
# are declared by including a file instead, in the top-level directory's
# scope. That file names its sources by absolute path.
#
# Nothing in the submodule is edited for this.

if(NOT DEFINED PS2N_ROOT)
	message(FATAL_ERROR "PS2N_ROOT is not set. Configure through scripts/windows/build.ps1 or scripts/linux/build.sh.")
endif()

# PS2Native's version, which its programs show: the file version.txt at the
# repository's root (docs/adr/0042), given to them as a header of the build's,
# ps2n_version.h in PS2N_VERSION_DIR, written when it changes. (A file named
# VERSION at the root, which is on the include path, would be found for the
# standard header <version> on Windows.)
file(STRINGS "${PS2N_ROOT}/version.txt" PS2N_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS2N_ROOT}/version.txt")
set(PS2N_VERSION_DIR "${CMAKE_BINARY_DIR}/ps2n-version")
set(ps2n_version_h "// Written by cmake/ps2n-inject.cmake from version.txt.\n#pragma once\n#define PS2N_VERSION \"${PS2N_VERSION}\"\n")
if(EXISTS "${PS2N_VERSION_DIR}/ps2n_version.h")
	file(READ "${PS2N_VERSION_DIR}/ps2n_version.h" ps2n_version_h_old)
endif()
if(NOT "${ps2n_version_h}" STREQUAL "${ps2n_version_h_old}")
	file(WRITE "${PS2N_VERSION_DIR}/ps2n_version.h" "${ps2n_version_h}")
endif()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/runtime/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/host/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/tests/tests.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/wizard/targets.cmake")
