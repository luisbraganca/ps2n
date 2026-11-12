# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps2n-runtime: what PS2Native adds to the PCSX2 core at run time: its EE
# provider and the instruction semantics the provider and statically compiled
# code share.
#
# Included by cmake/ps2n-inject.cmake at the end of PCSX2's top-level
# CMakeLists.txt, so CMAKE_SOURCE_DIR is the PCSX2 submodule and this file's
# own directory is CMAKE_CURRENT_LIST_DIR.

add_library(ps2n-runtime STATIC
	"${CMAKE_CURRENT_LIST_DIR}/ee_provider.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/ee_provider.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ee_memory.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ee_state.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_cop0.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_cop2.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_fpu.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_int.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_mmi.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/ops_vu0.h"
	"${CMAKE_CURRENT_LIST_DIR}/include/ps2n/recomp.h"
	"${CMAKE_CURRENT_LIST_DIR}/ops_cop2.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/ops_vu0.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/rec_blocks.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/rec_blocks.h"

	# The instruction decoder, shared with the build-time tools (core/). Only
	# the parts that use no C++ exceptions are compiled here: PCSX2's build
	# switches exceptions off.
	"${PS2N_ROOT}/core/src/cycles.cpp"
	"${PS2N_ROOT}/core/src/decoder.cpp"
	"${PS2N_ROOT}/core/src/disasm.cpp"
	"${PS2N_ROOT}/core/src/strings.cpp"
)

# Users include "runtime/<header>", "ps2n/<header>" for the semantics
# (runtime/include) and for the decoder (core/include). The PCSX2 directories
# are the ones pcsx2-gsrunner uses.
target_include_directories(ps2n-runtime
	PUBLIC
		"${PS2N_ROOT}"
		"${PS2N_ROOT}/core/include"
		"${CMAKE_CURRENT_LIST_DIR}/include"
	PRIVATE
		"${CMAKE_BINARY_DIR}/common/include"
		"${CMAKE_SOURCE_DIR}/pcsx2"
)

target_link_libraries(ps2n-runtime PRIVATE PCSX2_FLAGS)
