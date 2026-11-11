# SPDX-License-Identifier: GPL-3.0-or-later
#
# Unit tests of the build-time tools. They need no BIOS and no PCSX2.

add_executable(ps2n_core_tests core_tests.cpp)
target_link_libraries(ps2n_core_tests PRIVATE ps2n_core)
ps2n_set_warnings(ps2n_core_tests)
add_test(NAME unit/core COMMAND ps2n_core_tests)

add_executable(ps2n_recomp_tests recomp_tests.cpp)
target_link_libraries(ps2n_recomp_tests PRIVATE ps2n_recomp)
ps2n_set_warnings(ps2n_recomp_tests)
add_test(NAME unit/recompiler COMMAND ps2n_recomp_tests)
