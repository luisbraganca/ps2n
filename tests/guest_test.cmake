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
# With -DSAME_AS_STOCK=1 it runs the program twice as well: on stock PCSX2
# with its recompilers, recording every sync point (the moments the core lets
# the IOP catch up with the EE), and then on PS2Native's EE provider with the
# code compiled ahead of time, compared sync point by sync point with that
# record: where the EE is, its cycle count, its registers. The test passes if
# the two runs never differ (ADR 0011).
#
# Variables:
#   HOST       ps2n-host executable
#   ELF        the program to boot
#   SELFTEST   run ps2n-host -selftest instead of a program
#   SELFTEST_TRIALS  with SELFTEST: random cases per instruction against PCSX2's
#              recompiler (ten times as many against its interpreter)
#   LOCKSTEP   compare a compiled run with an interpreted one
#   SAME_AS_STOCK  compare a compiled run with a run of stock PCSX2
#   REGISTERS  with SAME_AS_STOCK: "report" lets the runs differ in registers
#              as long as place and time are the same at every sync point, and
#              prints which registers differ and how often: for a game, where
#              an instruction that follows the console on purpose (ADR 0005)
#              leaves something else in a register than stock PCSX2 does.
#              Without it the registers must be the same as well
#   WORK_DIR   scratch directory for this test; emptied first
#   EXPECTED   file the output must equal (a console recording)
#   EE         jit, interp or ps2n (default jit)
#   IOP VU     jit or interp (default jit)
#   STATIC     with EE=ps2n: on or off, whether compiled code is used (default on)
#   BIOS_STATIC  with EE=ps2n: off to leave the BIOS to the interpreter even
#              where the host has it compiled (ps2n-host -bios-static off)
#   CALL_SITES  with EE=ps2n: off to send every call through a register to the
#              runtime's dispatcher (ps2n-host -call-sites off)
#   IMAGE      with EE=ps2n or LOCKSTEP: the id of the compiled image to use
#   STACK_BUDGET  with LOCKSTEP: the host's -stack-budget for the compiled run
#   CAPTURE_EXPECTED  with EE=ps2n: a capture file; what the run reports for
#              the recompiler (ps2n-host -capture) must equal it
#   CAPTURE    with EE=ps2n: write what the run reports for the recompiler to
#              capture.toml in the scratch directory, without comparing it.
#              (What a run with compiled code had to interpret of the BIOS
#              goes to bios-capture.toml there whatever this says.)
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
#   PAD        a script of controller input for port 1 (ps2n-host -pad)
#   FRAME_EVERY  with a renderer that draws: also fingerprint the frame on
#              display every so many frames, and write each as
#              frame-<number>.png in the scratch directory
#   EXPECT_EXIT  how the host is expected to end (default 0, the program's last
#              line was seen). 2: at the frame limit, for a program that
#              prints no last line, such as a game run for a number of frames
#   TIMES      also write times.txt in the scratch directory: the program's
#              output with the frame and EE cycle count at which each line was
#              printed (ps2n-host -times), to see where two runs part in time
#   PROFILE    also write profile.txt in the scratch directory: where the CPU
#              thread's time went (ps2n-host -profile; Windows only)
#   PROFILE_FROM  with PROFILE: the frame from which to look (ps2n-host
#              -profile-from), to leave a game's startup out
#   MARKS      frames at which the wall-clock time is noted and reported
#              (ps2n-host -mark, once each), a list: to time parts of a run
#   FAST_CORES keep the run on the processor's fastest cores, so that its time
#              repeats (ps2n-host -fast-cores): for runs that are timed
#   SYNC       also write sync.txt in the scratch directory: the run's record of
#              sync points (ps2n-host -sync), which a stock run can write too
#   SYNC_AGAINST  the sync.txt of an earlier run, to compare this run with; what
#              differs first is reported, and does not fail the run
#   SYNC_DETAIL  with SYNC: the sync point from which the registers are written
#   TRACE      with EE=ps2n: also write run.trace in the scratch directory, the
#              record of event tests (ps2n-host -trace)
#   TRACE_DETAIL  with TRACE: the event test from which the registers are written
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
foreach(flag IN ITEMS SELFTEST LOCKSTEP SAME_AS_STOCK)
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
if(NOT DEFINED EXPECT_EXIT)
	set(EXPECT_EXIT 0)
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

# Holds the compiled run to the stock run in what both reports say of one kind
# of thing: the lines that match `pattern` in stock_report and in host_report
# must be the same, in the same order. Fails with `complaint` and the first
# pair that differs; otherwise sets `count_var` to how many lines there were.
function(ps2n_same_lines pattern complaint count_var)
	string(REGEX MATCHALL "${pattern}" stock_lines "${stock_report}")
	string(REGEX MATCHALL "${pattern}" compiled_lines "${host_report}")
	list(LENGTH stock_lines stock_count)
	list(LENGTH compiled_lines compiled_count)
	if(NOT stock_lines STREQUAL compiled_lines)
		set(first_stock "(none)")
		set(first_compiled "(none)")
		set(index 0)
		while(index LESS stock_count OR index LESS compiled_count)
			set(first_stock "(none)")
			set(first_compiled "(none)")
			if(index LESS stock_count)
				list(GET stock_lines ${index} first_stock)
			endif()
			if(index LESS compiled_count)
				list(GET compiled_lines ${index} first_compiled)
			endif()
			if(NOT first_stock STREQUAL first_compiled)
				break()
			endif()
			math(EXPR index "${index} + 1")
		endwhile()
		message(FATAL_ERROR "${complaint}. The first that differs:\n  stock:    ${first_stock}\n  compiled: ${first_compiled}")
	endif()
	set(${count_var} ${stock_count} PARENT_SCOPE)
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
if(DEFINED FRAME_EVERY AND NOT "${FRAME_EVERY}" STREQUAL "0")
	list(APPEND limits -frame-every ${FRAME_EVERY})
endif()
if(DEFINED PAD AND NOT "${PAD}" STREQUAL "")
	list(APPEND limits -pad "${PAD}")
endif()
if(DEFINED TIMES AND TIMES)
	list(APPEND limits -times "${WORK_DIR}/times.txt")
endif()
if(DEFINED PROFILE AND PROFILE)
	list(APPEND limits -profile "${WORK_DIR}/profile.txt")
	if(DEFINED PROFILE_FROM AND NOT "${PROFILE_FROM}" STREQUAL "")
		list(APPEND limits -profile-from "${PROFILE_FROM}")
	endif()
endif()
if(DEFINED MARKS)
	foreach(mark IN LISTS MARKS)
		list(APPEND limits -mark "${mark}")
	endforeach()
endif()
if(DEFINED FAST_CORES AND FAST_CORES)
	list(APPEND limits -fast-cores)
endif()

if(DEFINED SYNC AND SYNC)
	list(APPEND limits -sync "${WORK_DIR}/sync.txt")
endif()
if(DEFINED SYNC_AGAINST AND NOT "${SYNC_AGAINST}" STREQUAL "")
	list(APPEND limits -sync-against "${SYNC_AGAINST}")
endif()
if(DEFINED SYNC_DETAIL AND NOT "${SYNC_DETAIL}" STREQUAL "")
	list(APPEND limits -sync-detail ${SYNC_DETAIL})
endif()

if(LOCKSTEP)
	message("--- interpreted ---")
	ps2n_run("-interpreted"
		-out "${WORK_DIR}/guest-interpreted.txt"
		-ee ps2n -iop ${IOP} -vu ${VU} -static off ${image}
		-trace "${WORK_DIR}/interpreted.trace"
		${limits} "${ELF}")
	if(NOT exit_code STREQUAL "${EXPECT_EXIT}")
		message(FATAL_ERROR "The interpreted run ended with ${exit_code}, not ${EXPECT_EXIT} (0 marker seen, 1 error, 2 frame limit, 3 time limit). Log: ${WORK_DIR}/core-interpreted.log")
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
	if(NOT exit_code STREQUAL "${EXPECT_EXIT}")
		message(FATAL_ERROR "The compiled run ended with ${exit_code}, not ${EXPECT_EXIT} (0 marker seen, 1 error, 2 frame limit, 3 time limit). "
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

if(SAME_AS_STOCK)
	message("--- stock PCSX2 ---")
	ps2n_run("-stock"
		-out "${WORK_DIR}/guest-stock.txt"
		-ee jit -iop ${IOP} -vu ${VU}
		-sync "${WORK_DIR}/stock.sync"
		${limits} "${ELF}")
	if(NOT exit_code STREQUAL "${EXPECT_EXIT}")
		message(FATAL_ERROR "The stock run ended with ${exit_code}, not ${EXPECT_EXIT} (0 marker seen, 1 error, 2 frame limit, 3 time limit). Log: ${WORK_DIR}/core-stock.log")
	endif()
	set(stock_report "${host_report}")

	message("--- PS2Native, compiled ---")
	ps2n_run(""
		-out "${WORK_DIR}/guest.txt"
		-ee ps2n -iop ${IOP} -vu ${VU} -static on ${image}
		-sync-against "${WORK_DIR}/stock.sync"
		${limits} "${ELF}")
	if(NOT exit_code STREQUAL "${EXPECT_EXIT}")
		message(FATAL_ERROR "The compiled run ended with ${exit_code}, not ${EXPECT_EXIT} (0 marker seen, 1 error, 2 frame limit, 3 time limit). "
			"The stock run's record: ${WORK_DIR}/stock.sync. Log: ${WORK_DIR}/core.log")
	endif()
	if(host_report MATCHES "ps2n-host: sync: the same as the reference run at every sync point")
		message("The compiled run and the stock run are the same, sync point by sync point")
	elseif(DEFINED REGISTERS AND REGISTERS STREQUAL "report"
			AND host_report MATCHES "ps2n-host: sync: place and time are the same as the reference run's at every sync point")
		# A game long enough to meet an instruction where PS2Native follows the
		# console and stock PCSX2 does not (ADR 0005): the registers then differ
		# on purpose, and the host says which and how often.
		message("The compiled run and the stock run are at the same place at the same time at every sync point; "
			"their registers differ as reported above")
	else()
		message(FATAL_ERROR "The compiled run is not the stock run (see \"ps2n-host: sync:\" above). The stock run's record: "
			"${WORK_DIR}/stock.sync; to see the registers, run both again with SYNC and SYNC_DETAIL. Log: ${WORK_DIR}/core.log")
	endif()

	# Megabytes, and compared.
	file(REMOVE "${WORK_DIR}/stock.sync")

	# With a renderer that draws: the frame each run ended on, and those
	# fingerprinted along the way (FRAME_EVERY), by what the host printed of
	# them. The pictures in the scratch directory are the compiled run's.
	ps2n_same_lines("ps2n-host: frame[^\n]*fingerprint [0-9A-F]+"
		"The compiled run does not show the pictures the stock run shows" frame_count)
	if(frame_count GREATER 0)
		message("The compiled run and the stock run show the same picture at each of the ${frame_count} frames fingerprinted")
	endif()

	# The sound hardware, fingerprinted when each run ended and at the same
	# frames along the way: its registers and its memory, which holds what it
	# last mixed.
	ps2n_same_lines("ps2n-host: sound at[^\n]*fingerprint [0-9A-F]+"
		"The compiled run does not leave the sound hardware as the stock run leaves it" sound_count)
	if(sound_count GREATER 0)
		message("The compiled run and the stock run have the sound hardware in the same state at each of the ${sound_count} moments fingerprinted")
	endif()
	return()
endif()

set(static "")
if(EE STREQUAL "ps2n")
	set(static -static ${STATIC} ${image})
	if(DEFINED CAPTURE_EXPECTED OR (DEFINED CAPTURE AND CAPTURE))
		list(APPEND static -capture "${WORK_DIR}/capture.toml")
	endif()
	# What the BIOS's compiled code did not cover, for the next build of it
	# (build.ps1 -BiosCapture): every run with compiled code reports it.
	if(NOT STATIC STREQUAL "off")
		list(APPEND static -bios-capture "${WORK_DIR}/bios-capture.toml")
	endif()
	if(DEFINED BIOS_STATIC AND BIOS_STATIC STREQUAL "off")
		list(APPEND static -bios-static off)
	endif()
	if(DEFINED CALL_SITES AND CALL_SITES STREQUAL "off")
		list(APPEND static -call-sites off)
	endif()
	if(DEFINED TRACE AND TRACE)
		list(APPEND static -trace "${WORK_DIR}/run.trace")
		if(DEFINED TRACE_DETAIL AND NOT "${TRACE_DETAIL}" STREQUAL "")
			list(APPEND static -trace-detail ${TRACE_DETAIL})
		endif()
	endif()
endif()

ps2n_run(""
	-out "${WORK_DIR}/guest.txt"
	-ee ${EE} -iop ${IOP} -vu ${VU} ${static}
	${limits} "${ELF}")

if(NOT exit_code STREQUAL "${EXPECT_EXIT}")
	message(FATAL_ERROR "ps2n-host ended with ${exit_code}, not ${EXPECT_EXIT} (0 marker seen, 1 error, 2 frame limit, 3 time limit). Log: ${WORK_DIR}/core.log")
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
