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

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/runtime/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/host/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/tests/tests.cmake")
