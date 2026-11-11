# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs one PS2 test program on ps2n-host and compares what it printed between
# "-- TEST BEGIN" and "-- TEST END" with a recorded output. Run by CTest as
#   cmake -DHOST=... -DELF=... -DWORK_DIR=... [-DEXPECTED=...] -P guest_test.cmake
#
# With -DSELFTEST=1 and no ELF it runs the host's self-test instead, which
# passes or fails by its exit code.
#
# With -DLOCKSTEP=1 it runs the program twice on PS2Native's EE provider:
# interpreted, recording every event test, and then with the code compiled
# ahead of time, compared event test by event test with that record. The test
# passes if the two runs never differ.
#
# Variables:
#   HOST       ps2n-host executable
#   ELF        the program to boot
#   SELFTEST   run ps2n-host -selftest instead of a program
#   SELFTEST_TRIALS  with SELFTEST: random cases per instruction against PCSX2's
#              recompiler (ten times as many against its interpreter)
#   LOCKSTEP   compare a compiled run with an interpreted one
#   WORK_DIR   scratch directory for this test; emptied first
#   EXPECTED   file the output must equal (a console recording)
#   EE         jit, interp or ps2n (default jit)
#   IOP VU     jit or interp (default jit)
#   STATIC     with EE=ps2n: on or off, whether compiled code is used (default on)
#   IMAGE      with EE=ps2n or LOCKSTEP: the id of the compiled image to use
#   STACK_BUDGET  with LOCKSTEP: the host's -stack-budget for the compiled run
#   CAPTURE_EXPECTED  with EE=ps2n: a capture file; what the run reports for
#              the recompiler (ps2n-host -capture) must equal it
#   REQUIRE_REPORT, REQUIRE_REPORT_2  regular expressions that what the host
#              prints about the run must match
#   SECONDS    wall-clock limit (default 60)
#   FRAMES     frame limit (default 3600)
#   UNTIL      the text of the last line the program prints, where the run
#              stops (default "-- TEST END")
#   WHOLE      compare everything the machine printed, the BIOS's lines
#              included, instead of the text between "-- TEST BEGIN" and
#              "-- TEST END": for programs that print no such lines
#   HOSTFS     let the program open files beside its executable ("host:")
#   RENDERER   the GS renderer (ps2n-host -renderer; default null). With one
#              that draws, the last displayed frame is written to frame.png in
#              the scratch directory
#   UPSCALE    internal resolution multiplier for a hardware renderer
#   SETTLE     frames to run on after the program's last line
#   TIMES      also write times.txt in the scratch directory: the program's
#              output with the frame and EE cycle count at which each line was
#              printed (ps2n-host -times), to see where two runs part in time
#   FRAME_EXPECTED  a file holding the size and fingerprint of the frame the
#              run must end on, as ps2n-host prints them ("640x448, fingerprint
#              0123456789ABCDEF")
#   FRAME_SIZE  the size the frame the run ends on must have ("640x448")
#   DLL_DIR    directory added to PATH for the host's DLLs (Windows)
#
# The BIOS comes from the PS2N_BIOS environment variable. Without it the test
# is reported as skipped, never as passed.
#
# The BIOS image is not copied. The core keeps the console's NVRAM in files
# beside the BIOS image, so the test gives the core a link to the image in its
# own scratch directory: every run then starts from the same fresh NVRAM, and
# nothing is written beside the user's own BIOS files.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/test_block.cmake")

foreach(required IN ITEMS HOST WORK_DIR)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "guest_test.cmake: ${required} is not set")
	endif()
endforeach()
foreach(flag IN ITEMS SELFTEST LOCKSTEP)
	if(NOT DEFINED ${flag})
		set(${flag} 0)
	endif()
endforeach()
if(NOT SELFTEST AND NOT DEFINED ELF)
	message(FATAL_ERROR "guest_test.cmake: ELF is not set")
endif()

foreach(provider IN ITEMS EE IOP VU)
	if(NOT DEFINED ${provider})
		set(${provider} jit)
	endif()
endforeach()
if(NOT DEFINED STATIC)
	set(STATIC on)
endif()
if(NOT DEFINED SECONDS)
	set(SECONDS 60)
endif()
if(NOT DEFINED FRAMES)
	set(FRAMES 3600)
endif()

if("$ENV{PS2N_BIOS}" STREQUAL "")
	message("PS2N_SKIP: set the PS2N_BIOS environment variable to a PS2 BIOS image to run this test")
	return()
endif()

file(TO_CMAKE_PATH "$ENV{PS2N_BIOS}" bios)
if(NOT EXISTS "${bios}" OR IS_DIRECTORY "${bios}")
	message(FATAL_ERROR "PS2N_BIOS does not name a file: ${bios}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Links `source` at `link`: a symbolic link where the platform allows one to an
# unprivileged user, otherwise a hard link. Never a copy.
function(ps2n_link source link result_var)
	file(CREATE_LINK "${source}" "${link}" RESULT result SYMBOLIC)
	if(NOT result STREQUAL "0")
		file(CREATE_LINK "${source}" "${link}" RESULT result)
	endif()
	if(result STREQUAL "0")
		set(${result_var} TRUE PARENT_SCOPE)
	else()
		set(${result_var} FALSE PARENT_SCOPE)
	endif()
endfunction()

# Gives the core a BIOS directory of this test's own, with no NVRAM in it yet,
# and sets `result_var` to the image's path there.
function(ps2n_fresh_bios result_var)
	file(REMOVE_RECURSE "${WORK_DIR}/bios")
	file(MAKE_DIRECTORY "${WORK_DIR}/bios")

	get_filename_component(bios_name "${bios}" NAME)
	get_filename_component(bios_dir "${bios}" DIRECTORY)
	get_filename_component(bios_stem "${bios}" NAME_WLE)

	ps2n_link("${bios}" "${WORK_DIR}/bios/${bios_name}" linked)
	if(linked)
		set(${result_var} "${WORK_DIR}/bios/${bios_name}" PARENT_SCOPE)

		# The optional ROM1, ROM2 and EROM images the core loads from beside the
		# BIOS image, under either naming the core accepts.
		file(GLOB siblings LIST_DIRECTORIES false "${bios_dir}/${bios_stem}.*" "${bios_dir}/${bios_name}.*")
		foreach(sibling IN LISTS siblings)
			get_filename_component(extension "${sibling}" LAST_EXT)
			string(TOLOWER "${extension}" extension)
			if(extension MATCHES "^\\.(rom1|rom2|erom)$")
				get_filename_component(sibling_name "${sibling}" NAME)
				ps2n_link("${sibling}" "${WORK_DIR}/bios/${sibling_name}" ignored)
			endif()
		endforeach()
	else()
		# For example a BIOS on another volume on Windows. The run still works, but
		# the core then reads and writes the NVRAM files beside the user's BIOS.
		message(WARNING "Could not link ${bios} into ${WORK_DIR}; using it in place, with its own NVRAM files")
		set(${result_var} "${bios}" PARENT_SCOPE)
	endif()
endfunction()

if(DEFINED DLL_DIR AND NOT "${DLL_DIR}" STREQUAL "")
	file(TO_NATIVE_PATH "${DLL_DIR}" dll_dir_native)
	if(WIN32)
		set(ENV{PATH} "${dll_dir_native};$ENV{PATH}")
	endif()
endif()

# Runs the host once, in a machine of its own: `tag` names the run's files in
# the scratch directory, the remaining arguments go to the host. Sets
# exit_code in the caller's scope and prints what the host reported.
function(ps2n_run tag)
	ps2n_fresh_bios(bios_for_run)
	execute_process(
		COMMAND "${HOST}"
			-bios "${bios_for_run}"
			-data "${WORK_DIR}/data${tag}"
			-log "${WORK_DIR}/core${tag}.log"
			${ARGN}
		RESULT_VARIABLE result
		OUTPUT_VARIABLE host_stdout
		ERROR_VARIABLE host_stderr
	)
	# Removes the links and the NVRAM the run created, not what the links point to.
	file(REMOVE_RECURSE "${WORK_DIR}/bios")

	string(STRIP "${host_stderr}" host_stderr)
	message("${host_stderr}")
	set(exit_code "${result}" PARENT_SCOPE)
	set(host_report "${host_stderr}" PARENT_SCOPE)
endfunction()

set(image "")
if(DEFINED IMAGE AND NOT "${IMAGE}" STREQUAL "")
	set(image -image ${IMAGE})
endif()

if(SELFTEST)
	set(trials "")
	if(DEFINED SELFTEST_TRIALS)
		set(trials -selftest-trials ${SELFTEST_TRIALS})
	endif()
	ps2n_run("" -selftest ${trials} -seconds ${SECONDS})
	if(NOT exit_code STREQUAL "0")
		message(FATAL_ERROR "The self-test failed (exit code ${exit_code}; 3 is the time limit, SECONDS). PCSX2's log: ${WORK_DIR}/core.log")
	endif()
	return()
endif()

set(limits -seconds ${SECONDS} -frames ${FRAMES})
if(DEFINED UNTIL AND NOT "${UNTIL}" STREQUAL "")
	list(APPEND limits -until "${UNTIL}")
endif()
if(DEFINED HOSTFS AND HOSTFS)
	list(APPEND limits -hostfs)
endif()
if(DEFINED RENDERER AND NOT "${RENDERER}" STREQUAL "")
	list(APPEND limits -renderer ${RENDERER} -frame "${WORK_DIR}/frame.png")
endif()
if(DEFINED UPSCALE)
	list(APPEND limits -upscale ${UPSCALE})
endif()
if(DEFINED SETTLE)
	list(APPEND limits -settle ${SETTLE})
endif()
if(DEFINED TIMES AND TIMES)
	list(APPEND limits -times "${WORK_DIR}/times.txt")
endif()

if(LOCKSTEP)
	message("--- interpreted ---")
	ps2n_run("-interpreted"
		-out "${WORK_DIR}/guest-interpreted.txt"
		-ee ps2n -iop ${IOP} -vu ${VU} -static off ${image}
		-trace "${WORK_DIR}/interpreted.trace"
		${limits} "${ELF}")
	if(NOT exit_code STREQUAL "0")
		message(FATAL_ERROR "The interpreted run ended with ${exit_code} (0 marker seen, 1 error, 2 frame limit, 3 time limit). Log: ${WORK_DIR}/core-interpreted.log")
	endif()

	set(stack "")
	if(DEFINED STACK_BUDGET)
		set(stack -stack-budget ${STACK_BUDGET})
	endif()

	message("--- compiled ---")
	ps2n_run(""
		-out "${WORK_DIR}/guest.txt"
		-ee ps2n -iop ${IOP} -vu ${VU} -static on ${image} ${stack}
		-trace "${WORK_DIR}/compiled.trace"
		-trace-against "${WORK_DIR}/interpreted.trace"
		-capture "${WORK_DIR}/capture.toml"
		${limits} "${ELF}")
	if(NOT exit_code STREQUAL "0")
		message(FATAL_ERROR "The compiled run ended with ${exit_code} (0 marker seen, 1 error, 2 frame limit, 3 time limit). "
			"Records of both runs: ${WORK_DIR}/interpreted.trace, ${WORK_DIR}/compiled.trace. Log: ${WORK_DIR}/core.log")
	endif()

	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/guest-interpreted.txt" "${WORK_DIR}/guest.txt"
		RESULT_VARIABLE differs)
	if(NOT differs STREQUAL "0")
		message(FATAL_ERROR "The two runs printed different output: ${WORK_DIR}/guest-interpreted.txt, ${WORK_DIR}/guest.txt")
	endif()

	# Megabytes each, and equal.
	file(REMOVE "${WORK_DIR}/interpreted.trace" "${WORK_DIR}/compiled.trace")
	message("The compiled run and the interpreted run are the same, event test by event test")
	return()
endif()

set(static "")
if(EE STREQUAL "ps2n")
	set(static -static ${STATIC} ${image})
	if(DEFINED CAPTURE_EXPECTED)
		list(APPEND static -capture "${WORK_DIR}/capture.toml")
	endif()
endif()

ps2n_run(""
	-out "${WORK_DIR}/guest.txt"
	-ee ${EE} -iop ${IOP} -vu ${VU} ${static}
	${limits} "${ELF}")

if(NOT exit_code STREQUAL "0")
	message(FATAL_ERROR "ps2n-host ended with ${exit_code} (0 marker seen, 1 error, 2 frame limit, 3 time limit). Log: ${WORK_DIR}/core.log")
endif()

foreach(requirement IN ITEMS REQUIRE_REPORT REQUIRE_REPORT_2)
	if(DEFINED ${requirement} AND NOT host_report MATCHES "${${requirement}}")
		message(FATAL_ERROR "What the host reported about the run does not match: ${${requirement}}")
	endif()
endforeach()

# The frame the run ended on.
if(DEFINED FRAME_EXPECTED OR DEFINED FRAME_SIZE)
	if(NOT host_report MATCHES "ps2n-host: frame ([0-9]+x[0-9]+), fingerprint ([0-9A-F]+)")
		message(FATAL_ERROR "The run ended with no frame on display. Log: ${WORK_DIR}/core.log")
	endif()
	set(frame_size "${CMAKE_MATCH_1}")
	set(frame "${CMAKE_MATCH_1}, fingerprint ${CMAKE_MATCH_2}")
	file(WRITE "${WORK_DIR}/frame.txt" "${frame}\n")

	if(DEFINED FRAME_SIZE AND NOT frame_size STREQUAL "${FRAME_SIZE}")
		message(FATAL_ERROR "The frame is ${frame_size}, not ${FRAME_SIZE}. Picture: ${WORK_DIR}/frame.png")
	endif()
	if(DEFINED FRAME_EXPECTED)
		if(NOT EXISTS "${FRAME_EXPECTED}")
			message(FATAL_ERROR "The recorded frame is missing: ${FRAME_EXPECTED}. This run: ${frame}")
		endif()
		file(READ "${FRAME_EXPECTED}" frame_expected)
		string(REGEX REPLACE "#[^\n]*" "" frame_expected "${frame_expected}")
		string(STRIP "${frame_expected}" frame_expected)
		if(NOT frame STREQUAL frame_expected)
			message(FATAL_ERROR "The frame differs from ${FRAME_EXPECTED}.\n  recorded: ${frame_expected}\n  this run: ${frame}\nPicture: ${WORK_DIR}/frame.png")
		endif()
		message("The frame equals ${FRAME_EXPECTED}")
	endif()
endif()

if(DEFINED CAPTURE_EXPECTED)
	# Compared without their comments and blank lines: only what the recompiler
	# reads matters.
	function(ps2n_capture_content file result_var)
		file(READ "${file}" text)
		string(REPLACE "\r\n" "\n" text "${text}")
		string(REGEX REPLACE "#[^\n]*" "" text "${text}")
		string(REGEX REPLACE "[ \t]+\n" "\n" text "${text}")
		string(REGEX REPLACE "\n\n+" "\n" text "${text}")
		string(STRIP "${text}" text)
		set(${result_var} "${text}" PARENT_SCOPE)
	endfunction()

	if(NOT EXISTS "${WORK_DIR}/capture.toml")
		message(FATAL_ERROR "The host wrote no capture file")
	endif()
	ps2n_capture_content("${WORK_DIR}/capture.toml" captured)
	ps2n_capture_content("${CAPTURE_EXPECTED}" capture_expected)
	if(NOT captured STREQUAL capture_expected)
		message(FATAL_ERROR "What this run reports for the recompiler is not what ${CAPTURE_EXPECTED} holds. This run: ${WORK_DIR}/capture.toml")
	endif()
	message("The run reports for the recompiler what ${CAPTURE_EXPECTED} holds")
endif()

if(NOT EXISTS "${WORK_DIR}/guest.txt")
	message(FATAL_ERROR "ps2n-host wrote no guest output")
endif()

# Normalizes line endings and keeps the text from the BEGIN line to the END
# line (test_block.cmake), or with WHOLE all of it.
function(ps2n_test_region text result_var)
	if(DEFINED WHOLE AND WHOLE)
		string(REPLACE "\r\n" "\n" text "${text}")
		set(${result_var} "${text}" PARENT_SCOPE)
		return()
	endif()
	ps2n_test_block("${text}" block)
	set(${result_var} "${block}" PARENT_SCOPE)
endfunction()

file(READ "${WORK_DIR}/guest.txt" guest)
ps2n_test_region("${guest}" actual)
if(actual STREQUAL "")
	message(FATAL_ERROR "The program printed no '-- TEST BEGIN' ... '-- TEST END' block. Output: ${WORK_DIR}/guest.txt")
endif()
file(WRITE "${WORK_DIR}/actual.txt" "${actual}")

if(NOT DEFINED EXPECTED OR "${EXPECTED}" STREQUAL "")
	message("No recorded output to compare with; wrote ${WORK_DIR}/actual.txt")
	return()
endif()

if(NOT EXISTS "${EXPECTED}")
	message(FATAL_ERROR "Recorded output is missing: ${EXPECTED}")
endif()
file(READ "${EXPECTED}" recorded)
ps2n_test_region("${recorded}" expected)
if(expected STREQUAL "")
	message(FATAL_ERROR "Recorded output has no '-- TEST BEGIN' ... '-- TEST END' block: ${EXPECTED}")
endif()

if(actual STREQUAL expected)
	message("Output equals ${EXPECTED}")
	return()
endif()

# Find the first line that differs, without CMake lists: output lines may
# hold semicolons and brackets.
set(rest_actual "${actual}")
set(rest_expected "${expected}")
set(line_number 0)
while(TRUE)
	math(EXPR line_number "${line_number} + 1")
	string(FIND "${rest_actual}" "\n" cut_actual)
	string(FIND "${rest_expected}" "\n" cut_expected)
	if(cut_actual LESS 0 OR cut_expected LESS 0)
		set(line_actual "${rest_actual}")
		set(line_expected "${rest_expected}")
		break()
	endif()
	string(SUBSTRING "${rest_actual}" 0 ${cut_actual} line_actual)
	string(SUBSTRING "${rest_expected}" 0 ${cut_expected} line_expected)
	if(NOT line_actual STREQUAL line_expected)
		break()
	endif()
	math(EXPR cut_actual "${cut_actual} + 1")
	math(EXPR cut_expected "${cut_expected} + 1")
	string(SUBSTRING "${rest_actual}" ${cut_actual} -1 rest_actual)
	string(SUBSTRING "${rest_expected}" ${cut_expected} -1 rest_expected)
endwhile()

message("First difference at line ${line_number} of the test block:")
message("  recorded: ${line_expected}")
message("  this run: ${line_actual}")
message(FATAL_ERROR "Output differs from ${EXPECTED}. This run: ${WORK_DIR}/actual.txt")
