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
# Nothing in the submodule is edited for this, nor for the changes PS2Native
# makes to PCSX2's code (below: PS2N_PCSX2_PATCHES).

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

# The runtime itself, ps2n-host (PCSX2's core and what PS2Native adds to it),
# built as PCSX2's own releases are, with link-time optimization
# (PS2N_RUNTIME_LTO: CMake's interprocedural optimization, which PCSX2's own
# builds turn on and scripts/windows/build.ps1 otherwise leaves out), and
# with a profile of play (PS2N_RUNTIME_PGO, docs/adr/0046): "generate" builds
# it to count what it runs, and a run then writes its counts where
# LLVM_PROFILE_FILE says; "use" builds it with the merged counts
# (llvm-profdata merge), PS2N_RUNTIME_PGO_DATA. The counts come from playing
# games: they stay in the build tree and never go into the repository.
#
# Both are given here, before PCSX2's own targets exist, to every target of
# the project, PCSX2's and PS2Native's. What is not the runtime (the modules
# of compiled code, the launcher, the wizard's programs) is taken out again
# where it is declared: ps2n_not_the_runtime.
option(PS2N_RUNTIME_LTO "Link-time optimization of the runtime, as PCSX2's own releases have it" OFF)
set(PS2N_RUNTIME_PGO "" CACHE STRING "The runtime built with a profile of play: generate, use, or empty for none")
set(PS2N_RUNTIME_PGO_DATA "" CACHE FILEPATH "With PS2N_RUNTIME_PGO=use: the merged profile (.profdata)")
set(PS2N_RUNTIME_COMPILE_OPTIONS "")
set(PS2N_RUNTIME_LINK_OPTIONS "")
if(PS2N_RUNTIME_LTO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()
if(PS2N_RUNTIME_PGO STREQUAL "generate")
	set(PS2N_RUNTIME_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:-fprofile-generate>")
	if(NOT WIN32)
		# The compiler's driver links the profile's runtime; on Windows the
		# linker is called directly, and host/targets.cmake names it.
		set(PS2N_RUNTIME_LINK_OPTIONS -fprofile-generate)
	endif()
elseif(PS2N_RUNTIME_PGO STREQUAL "use")
	if(NOT EXISTS "${PS2N_RUNTIME_PGO_DATA}")
		message(FATAL_ERROR "PS2N_RUNTIME_PGO=use needs PS2N_RUNTIME_PGO_DATA, a merged profile: ${PS2N_RUNTIME_PGO_DATA}")
	endif()
	# The compiler is given a copy of the profile in the build tree, named
	# after what it holds: a profile made again under the same name is then
	# another file to every compile command, and everything the runtime is
	# built from is compiled again with it. (What was compiled with two
	# profiles does not link: with link-time optimization the linker refuses
	# modules whose profile summaries differ.) The copy also has CMake run
	# again when the profile changes.
	file(SHA256 "${PS2N_RUNTIME_PGO_DATA}" ps2n_runtime_pgo_hash)
	string(SUBSTRING "${ps2n_runtime_pgo_hash}" 0 16 ps2n_runtime_pgo_hash)
	set(ps2n_runtime_pgo_copy "${CMAKE_BINARY_DIR}/ps2n-runtime-profile/${ps2n_runtime_pgo_hash}.profdata")
	file(GLOB ps2n_runtime_pgo_copies "${CMAKE_BINARY_DIR}/ps2n-runtime-profile/*.profdata")
	foreach(copy IN LISTS ps2n_runtime_pgo_copies)
		if(NOT copy STREQUAL ps2n_runtime_pgo_copy)
			file(REMOVE "${copy}")
		endif()
	endforeach()
	configure_file("${PS2N_RUNTIME_PGO_DATA}" "${ps2n_runtime_pgo_copy}" COPYONLY)
	set(PS2N_RUNTIME_COMPILE_OPTIONS
		"$<$<COMPILE_LANGUAGE:C,CXX>:-fprofile-use=${ps2n_runtime_pgo_copy}>"
		"$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-profile-instr-unprofiled>"
		"$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-profile-instr-out-of-date>")
elseif(NOT PS2N_RUNTIME_PGO STREQUAL "")
	message(FATAL_ERROR "PS2N_RUNTIME_PGO is generate, use or empty, not ${PS2N_RUNTIME_PGO}")
endif()
if(PS2N_RUNTIME_COMPILE_OPTIONS)
	if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		message(FATAL_ERROR "PS2N_RUNTIME_PGO is set up for clang only (the compiler is ${CMAKE_CXX_COMPILER_ID})")
	endif()
	add_compile_options(${PS2N_RUNTIME_COMPILE_OPTIONS})
endif()
if(PS2N_RUNTIME_LINK_OPTIONS)
	add_link_options(${PS2N_RUNTIME_LINK_OPTIONS})
endif()
if(PS2N_RUNTIME_LTO OR PS2N_RUNTIME_PGO)
	message(STATUS "PS2Native: the runtime is built with link-time optimization: ${PS2N_RUNTIME_LTO}; with a profile: ${PS2N_RUNTIME_PGO} ${PS2N_RUNTIME_PGO_DATA} ${ps2n_runtime_pgo_hash}")
endif()

# Takes a target that is not the runtime out of what the runtime is built
# with (above): compiled and linked as without it.
function(ps2n_not_the_runtime)
	foreach(ps2n_target IN LISTS ARGN)
		set_property(TARGET ${ps2n_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
		foreach(ps2n_kind IN ITEMS COMPILE LINK)
			if(PS2N_RUNTIME_${ps2n_kind}_OPTIONS)
				get_target_property(ps2n_options ${ps2n_target} ${ps2n_kind}_OPTIONS)
				if(ps2n_options)
					list(REMOVE_ITEM ps2n_options ${PS2N_RUNTIME_${ps2n_kind}_OPTIONS})
					set_property(TARGET ${ps2n_target} PROPERTY ${ps2n_kind}_OPTIONS "${ps2n_options}")
				endif()
			endif()
		endforeach()
	endforeach()
endfunction()

# PS2Native's changes to PCSX2's code (docs/adr/0050): the patches of
# patches/pcsx2, applied to copies of the files they change in the build
# tree, which PCSX2's targets then compile in the place of those files
# (cmake/ps2n-pcsx2-patches.cmake). The submodule is never edited. OFF builds
# PCSX2 as it is: the tree stock PCSX2 is timed on is built so.
option(PS2N_PCSX2_PATCHES "PCSX2's code built with PS2Native's changes to it (patches/pcsx2)" ON)

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/cmake/ps2n-pcsx2-patches.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/runtime/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/host/targets.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/tests/tests.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${PS2N_ROOT}/wizard/targets.cmake")
