# SPDX-License-Identifier: GPL-3.0-or-later
#
# PS2Native's build-time tools and their unit tests: the instruction decoder,
# disassembler and assembler, and the executable readers (core/).
#
# This project does not depend on PCSX2 and builds in seconds.
#
# The run-time side (runtime/, host/) is NOT built from here. It is added to
# PCSX2's own CMake project, which has to be the top-level one; see
# cmake/ps2n-inject.cmake and the scripts under scripts/.

cmake_minimum_required(VERSION 3.20)

project(PS2Native
	VERSION 0.2.0
	DESCRIPTION "Converts PlayStation 2 games into native applications by static recompilation"
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PS2N_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project code" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Ps2nWarnings)

add_subdirectory(core)
add_subdirectory(recompiler)

enable_testing()
add_subdirectory(tests/unit)
