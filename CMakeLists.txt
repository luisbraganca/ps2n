cmake_minimum_required(VERSION 3.20)

project(PS2Native
    VERSION 0.1.0
    DESCRIPTION "Static recompiler that turns PlayStation 2 ELF executables into C++"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PS2N_BUILD_TESTS    "Build the unit and end-to-end tests" ON)
option(PS2N_BUILD_EXAMPLES "Build the example games (recompiled at build time)" ON)
option(PS2N_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project code" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Ps2nWarnings)

# Lets the recompiler point generated projects back at this source tree.
set(PS2N_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(core)
add_subdirectory(recompiler)
add_subdirectory(runtime)

include(Ps2nRecompile)

if(PS2N_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PS2N_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
