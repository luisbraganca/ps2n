# SPDX-License-Identifier: GPL-3.0-or-later
#
# Unit tests of the build-time tools, and of the host's rules that need
# nothing of PCSX2. They need no BIOS and no PCSX2.

add_executable(ps2n_core_tests core_tests.cpp)
target_link_libraries(ps2n_core_tests PRIVATE ps2n_core)
ps2n_set_warnings(ps2n_core_tests)
add_test(NAME unit/core COMMAND ps2n_core_tests)

add_executable(ps2n_recomp_tests recomp_tests.cpp)
target_link_libraries(ps2n_recomp_tests PRIVATE ps2n_recomp)
ps2n_set_warnings(ps2n_recomp_tests)
add_test(NAME unit/recompiler COMMAND ps2n_recomp_tests)

# When the pointer hides over a game's picture (host/pointer_hiding.h, a
# header of its own that needs nothing else).
add_executable(ps2n_host_tests host_tests.cpp)
target_include_directories(ps2n_host_tests PRIVATE "${PROJECT_SOURCE_DIR}/host")
ps2n_set_warnings(ps2n_host_tests)
add_test(NAME unit/host COMMAND ps2n_host_tests)
