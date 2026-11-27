# SPDX-License-Identifier: GPL-3.0-or-later
#
# PS2Native's changes to PCSX2's code (docs/adr/0050): the patches of
# patches/pcsx2, applied in the order of their names. The submodule is never
# edited. Each source file a patch changes is copied into the build tree
# (ps2n-pcsx2/src), the patches are applied to the copies, and PCSX2's own
# targets are given the copies to compile in the place of the files they came
# from. Nothing else of PCSX2's build changes: a tree compiles two files again
# when a patch changes two files.
#
# Included once PCSX2's own CMakeLists.txt has finished (cmake/ps2n-inject.cmake),
# when its targets exist. PS2N_PCSX2_PATCHES=OFF builds PCSX2 as it is: the
# tree stock PCSX2 is timed on is built so. Only a change that lets PCSX2's
# source compile here and changes nothing a run does is applied there too, one
# whose header says so on a line of its own: "Stock PCSX2's tree is built with
# it too." Without it that tree would not build (since PCSX2 v2.9.198, on
# Linux).
#
# What this carries is a change to a file PCSX2's targets compile (.cpp, .c).
# A change to a header would not reach the files beside it that include it by
# name, so a patch that touches anything else stops the configuration here;
# such a change needs PCSX2's source copied whole (build.ps1 -Pcsx2Source,
# build.sh --pcsx2-source).
#
# It leaves, for the programs and the kit's licences:
#   PS2N_PCSX2_CHANGES     the names of the patches applied (a list; empty for none)
#   ps2n_pcsx2_changes.h   the same as a string, in PS2N_VERSION_DIR

set(PS2N_PCSX2_CHANGES "")
set(ps2n_pcsx2_dir "${CMAKE_BINARY_DIR}/ps2n-pcsx2")
set(ps2n_pcsx2_work "${ps2n_pcsx2_dir}/work")
set(ps2n_pcsx2_src "${ps2n_pcsx2_dir}/src")
set(ps2n_pcsx2_files "")

# The list is looked at again at every build: a patch added or taken away
# runs CMake again.
file(GLOB ps2n_pcsx2_patches CONFIGURE_DEPENDS "${PS2N_ROOT}/patches/pcsx2/*.patch")
list(SORT ps2n_pcsx2_patches)

# PCSX2 as it is: only the changes whose header says that stock PCSX2's tree
# is built with them too (the text before the patch's first file).
if(NOT PS2N_PCSX2_PATCHES)
	set(ps2n_pcsx2_kept "")
	foreach(ps2n_patch IN LISTS ps2n_pcsx2_patches)
		file(READ "${ps2n_patch}" ps2n_text)
		string(REPLACE "\r\n" "\n" ps2n_text "${ps2n_text}")
		string(FIND "${ps2n_text}" "\n--- a/" ps2n_diff_at)
		if(ps2n_diff_at GREATER -1)
			string(SUBSTRING "${ps2n_text}" 0 ${ps2n_diff_at} ps2n_text)
		endif()
		if(ps2n_text MATCHES "(^|\n)Stock PCSX2's tree is built with it too\\.(\n|$)")
			list(APPEND ps2n_pcsx2_kept "${ps2n_patch}")
		endif()
	endforeach()
	set(ps2n_pcsx2_patches "${ps2n_pcsx2_kept}")
endif()

if(ps2n_pcsx2_patches)
	find_package(Git REQUIRED)

	# The copies are patched in a repository of their own, made anew each time:
	# git then takes a patch's paths from that folder, whatever repository the
	# build tree is inside. Its one attribute has git read each copy with Unix
	# line endings, as PCSX2's repository and the patches have them, and write
	# it so, whatever git gave the checkout on this system and whatever the
	# user's own settings are: the patched copies are the same bytes everywhere.
	file(REMOVE_RECURSE "${ps2n_pcsx2_work}")
	file(MAKE_DIRECTORY "${ps2n_pcsx2_work}")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -c init.defaultBranch=main init -q
		WORKING_DIRECTORY "${ps2n_pcsx2_work}"
		RESULT_VARIABLE ps2n_result OUTPUT_QUIET ERROR_VARIABLE ps2n_error)
	if(ps2n_result)
		message(FATAL_ERROR "PS2Native's changes to PCSX2: git init failed in ${ps2n_pcsx2_work}: ${ps2n_error}")
	endif()
	file(WRITE "${ps2n_pcsx2_work}/.gitattributes" "* text eol=lf\n")

	foreach(ps2n_patch IN LISTS ps2n_pcsx2_patches)
		get_filename_component(ps2n_patch_name "${ps2n_patch}" NAME_WE)
		set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ps2n_patch}")

		# The files it changes: "<lines added> <lines taken out> <file>" each.
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" apply --numstat "${ps2n_patch}"
			WORKING_DIRECTORY "${ps2n_pcsx2_work}"
			RESULT_VARIABLE ps2n_result OUTPUT_VARIABLE ps2n_numstat ERROR_VARIABLE ps2n_error)
		if(ps2n_result)
			message(FATAL_ERROR "PS2Native's change to PCSX2 ${ps2n_patch_name} is not a patch git reads: ${ps2n_error}")
		endif()
		string(REGEX MATCHALL "[^\r\n]+" ps2n_lines "${ps2n_numstat}")
		if(NOT ps2n_lines)
			message(FATAL_ERROR "PS2Native's change to PCSX2 ${ps2n_patch_name} changes no file.")
		endif()
		foreach(ps2n_line IN LISTS ps2n_lines)
			if(NOT ps2n_line MATCHES "^[0-9]+\t[0-9]+\t(.+)$")
				message(FATAL_ERROR "PS2Native's change to PCSX2 ${ps2n_patch_name}: not understood: ${ps2n_line}")
			endif()
			set(ps2n_file "${CMAKE_MATCH_1}")
			get_filename_component(ps2n_extension "${ps2n_file}" LAST_EXT)
			if(NOT ps2n_extension MATCHES "^\\.(cpp|c|cc|cxx)$")
				message(FATAL_ERROR
					"PS2Native's change to PCSX2 ${ps2n_patch_name} changes ${ps2n_file}, which is not a file PCSX2's targets compile. "
					"Changes are carried to compiled sources only (cmake/ps2n-pcsx2-patches.cmake says why).")
			endif()
			if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${ps2n_file}")
				message(FATAL_ERROR "PS2Native's change to PCSX2 ${ps2n_patch_name} changes ${ps2n_file}, which PCSX2's source in ${CMAKE_SOURCE_DIR} does not have.")
			endif()
			if(NOT ps2n_file IN_LIST ps2n_pcsx2_files)
				list(APPEND ps2n_pcsx2_files "${ps2n_file}")
				set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${ps2n_file}")
				get_filename_component(ps2n_work_dir "${ps2n_pcsx2_work}/${ps2n_file}" DIRECTORY)
				file(MAKE_DIRECTORY "${ps2n_work_dir}")
				file(COPY_FILE "${CMAKE_SOURCE_DIR}/${ps2n_file}" "${ps2n_pcsx2_work}/${ps2n_file}")
			endif()
		endforeach()

		execute_process(
			COMMAND "${GIT_EXECUTABLE}" -c core.safecrlf=false apply --whitespace=nowarn "${ps2n_patch}"
			WORKING_DIRECTORY "${ps2n_pcsx2_work}"
			RESULT_VARIABLE ps2n_result OUTPUT_QUIET ERROR_VARIABLE ps2n_error)
		if(ps2n_result)
			message(FATAL_ERROR
				"PS2Native's change to PCSX2 ${ps2n_patch_name} does not apply to PCSX2's source in ${CMAKE_SOURCE_DIR}:\n${ps2n_error}\n"
				"PS2N_PCSX2_PATCHES=OFF (build.ps1 -Pcsx2Patches off, build.sh --pcsx2-patches off) builds PCSX2 as it is.")
		endif()
		list(APPEND PS2N_PCSX2_CHANGES "${ps2n_patch_name}")
	endforeach()

	# The copies the targets compile: written only where their text changed, so
	# that nothing is compiled again for a configuration that changed nothing.
	foreach(ps2n_file IN LISTS ps2n_pcsx2_files)
		file(READ "${CMAKE_SOURCE_DIR}/${ps2n_file}" ps2n_before)
		string(REPLACE "\r\n" "\n" ps2n_before "${ps2n_before}")
		file(READ "${ps2n_pcsx2_work}/${ps2n_file}" ps2n_after)
		string(REPLACE "\r\n" "\n" ps2n_after "${ps2n_after}")
		if("${ps2n_before}" STREQUAL "${ps2n_after}")
			message(FATAL_ERROR "PS2Native's changes to PCSX2 left ${ps2n_file} as it was: nothing was applied to it.")
		endif()
		get_filename_component(ps2n_copy_dir "${ps2n_pcsx2_src}/${ps2n_file}" DIRECTORY)
		file(MAKE_DIRECTORY "${ps2n_copy_dir}")
		file(COPY_FILE "${ps2n_pcsx2_work}/${ps2n_file}" "${ps2n_pcsx2_src}/${ps2n_file}" ONLY_IF_DIFFERENT)
	endforeach()
endif()

# Copies of files no patch changes any more are taken away.
file(GLOB_RECURSE ps2n_pcsx2_copies RELATIVE "${ps2n_pcsx2_src}" "${ps2n_pcsx2_src}/*")
foreach(ps2n_copy IN LISTS ps2n_pcsx2_copies)
	if(NOT ps2n_copy IN_LIST ps2n_pcsx2_files)
		file(REMOVE "${ps2n_pcsx2_src}/${ps2n_copy}")
	endif()
endforeach()

# Every target of PCSX2's project, the directory's and those below it.
function(ps2n_targets_below directory result)
	get_property(ps2n_found DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	get_property(ps2n_below DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(ps2n_sub IN LISTS ps2n_below)
		ps2n_targets_below("${ps2n_sub}" ps2n_more)
		list(APPEND ps2n_found ${ps2n_more})
	endforeach()
	set(${result} "${ps2n_found}" PARENT_SCOPE)
endfunction()

if(ps2n_pcsx2_files)
	ps2n_targets_below("${CMAKE_SOURCE_DIR}" ps2n_all_targets)
	set(ps2n_pcsx2_given "")
	foreach(ps2n_target IN LISTS ps2n_all_targets)
		get_target_property(ps2n_type ${ps2n_target} TYPE)
		if(ps2n_type STREQUAL "INTERFACE_LIBRARY")
			continue()
		endif()
		get_target_property(ps2n_sources ${ps2n_target} SOURCES)
		get_target_property(ps2n_target_dir ${ps2n_target} SOURCE_DIR)
		if(NOT ps2n_sources)
			continue()
		endif()
		set(ps2n_new_sources "")
		set(ps2n_replaced FALSE)
		foreach(ps2n_source IN LISTS ps2n_sources)
			set(ps2n_keep "${ps2n_source}")
			if(NOT ps2n_source MATCHES "\\$<")
				cmake_path(ABSOLUTE_PATH ps2n_source BASE_DIRECTORY "${ps2n_target_dir}" NORMALIZE OUTPUT_VARIABLE ps2n_absolute)
				foreach(ps2n_file IN LISTS ps2n_pcsx2_files)
					cmake_path(ABSOLUTE_PATH ps2n_file BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE ps2n_original)
					if(ps2n_absolute STREQUAL ps2n_original)
						get_source_file_property(ps2n_header_only "${ps2n_original}" TARGET_DIRECTORY ${ps2n_target} HEADER_FILE_ONLY)
						if(ps2n_header_only)
							message(FATAL_ERROR "PS2Native's changes to PCSX2: ${ps2n_file} is not compiled by ${ps2n_target} (HEADER_FILE_ONLY).")
						endif()
						set(ps2n_keep "${ps2n_pcsx2_src}/${ps2n_file}")
						# The copy is compiled as the file it came from: with
						# what PCSX2's build says of that file alone, and with
						# the folder it came from searched first for what it
						# includes by name.
						get_filename_component(ps2n_original_dir "${ps2n_original}" DIRECTORY)
						set(ps2n_include_dirs "${ps2n_original_dir}")
						foreach(ps2n_property IN ITEMS COMPILE_FLAGS COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES SKIP_PRECOMPILE_HEADERS LANGUAGE)
							get_source_file_property(ps2n_value "${ps2n_original}" TARGET_DIRECTORY ${ps2n_target} ${ps2n_property})
							if(ps2n_value AND NOT ps2n_value STREQUAL "NOTFOUND")
								if(ps2n_property STREQUAL "INCLUDE_DIRECTORIES")
									list(APPEND ps2n_include_dirs ${ps2n_value})
								else()
									set_source_files_properties("${ps2n_keep}" TARGET_DIRECTORY ${ps2n_target} PROPERTIES ${ps2n_property} "${ps2n_value}")
								endif()
							endif()
						endforeach()
						set_source_files_properties("${ps2n_keep}" TARGET_DIRECTORY ${ps2n_target} PROPERTIES INCLUDE_DIRECTORIES "${ps2n_include_dirs}")
						list(APPEND ps2n_pcsx2_given "${ps2n_file}")
						set(ps2n_replaced TRUE)
					endif()
				endforeach()
			endif()
			list(APPEND ps2n_new_sources "${ps2n_keep}")
		endforeach()
		if(ps2n_replaced)
			set_property(TARGET ${ps2n_target} PROPERTY SOURCES "${ps2n_new_sources}")
		endif()
	endforeach()
	foreach(ps2n_file IN LISTS ps2n_pcsx2_files)
		if(NOT ps2n_file IN_LIST ps2n_pcsx2_given)
			message(FATAL_ERROR "PS2Native's changes to PCSX2: no target of PCSX2's compiles ${ps2n_file}, so the change to it would not be built.")
		endif()
	endforeach()
endif()

# For the programs (ps2n-host -help) and the kit's licences.
list(JOIN PS2N_PCSX2_CHANGES ", " ps2n_pcsx2_changes_text)
set(ps2n_pcsx2_changes_h "// Written by cmake/ps2n-pcsx2-patches.cmake: PS2Native's changes to PCSX2's code this tree is built with (patches/pcsx2).\n#pragma once\n#define PS2N_PCSX2_CHANGES \"${ps2n_pcsx2_changes_text}\"\n")
set(ps2n_pcsx2_changes_h_old "")
if(EXISTS "${PS2N_VERSION_DIR}/ps2n_pcsx2_changes.h")
	file(READ "${PS2N_VERSION_DIR}/ps2n_pcsx2_changes.h" ps2n_pcsx2_changes_h_old)
endif()
if(NOT "${ps2n_pcsx2_changes_h}" STREQUAL "${ps2n_pcsx2_changes_h_old}")
	file(WRITE "${PS2N_VERSION_DIR}/ps2n_pcsx2_changes.h" "${ps2n_pcsx2_changes_h}")
endif()
file(WRITE "${ps2n_pcsx2_dir}/changes.txt" "${ps2n_pcsx2_changes_text}\n")
if(PS2N_PCSX2_CHANGES AND NOT PS2N_PCSX2_PATCHES)
	message(STATUS "PS2Native: PCSX2's code is built as it is, but for the changes that only let it compile here: ${ps2n_pcsx2_changes_text} (${ps2n_pcsx2_files})")
elseif(PS2N_PCSX2_CHANGES)
	message(STATUS "PS2Native: PCSX2's code is built with PS2Native's changes: ${ps2n_pcsx2_changes_text} (${ps2n_pcsx2_files})")
else()
	message(STATUS "PS2Native: PCSX2's code is built as it is, with none of PS2Native's changes")
endif()
