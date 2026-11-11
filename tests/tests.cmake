# SPDX-License-Identifier: GPL-3.0-or-later
#
# The proof of concept's test programs: recompiles them ahead of time, links
# the result into ps2n-host, and registers the tests with CTest. Included by
# cmake/ps2n-inject.cmake at the end of PCSX2's top-level CMakeLists.txt.
#
# Tests are named <suite>/<configuration>/<program>:
#
#   ctest --test-dir <build> -R ps2autotests/stock-jit -j 8
#   ctest --test-dir <build> -R /lockstep/
#   ctest --test-dir <build> -R /same-as-stock/
#   ctest --test-dir <build> -R /never-further/
#
# Every test needs a BIOS image, named by the PS2N_BIOS environment variable,
# and is reported as skipped without one.

enable_testing()

set(PS2N_GUEST_TEST "${PS2N_ROOT}/tests/guest_test.cmake")
set(PS2N_TEST_WORK "${CMAKE_BINARY_DIR}/ps2n/tests")

# Where the host's DLLs are on Windows: PCSX2's prebuilt dependency package.
set(PS2N_TEST_DLL_DIR "")
if(WIN32)
	foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
		if(EXISTS "${prefix}/bin/SDL3.dll")
			set(PS2N_TEST_DLL_DIR "${prefix}/bin")
		endif()
	endforeach()
endif()

# The recompiler is a build-time tool from PS2Native's own CMake project (the
# repository root), which the build scripts build first.
if(NOT PS2N_RECOMP)
	message(FATAL_ERROR "PS2N_RECOMP is not set to the ps2n-recomp executable. Configure through scripts/windows/build.ps1 or scripts/linux/build.sh.")
endif()

# ---- code compiled ahead of time ------------------------------------------------
# Each test program becomes one C++ file in the build tree, an "image", and
# all of them are linked into the host. Nothing generated from a program is
# ever written into the repository.
set(PS2N_GENERATED "${CMAKE_BINARY_DIR}/ps2n/generated")
set(ps2n_image_sources "")
set(ps2n_image_ids "")

# ps2n_add_image(<id> <elf> [CAPTURE <file>] [COMMENT <text>] [PARTS <n>])
# CAPTURE: what an earlier run of the program reported (ps2n-host -capture).
# PARTS: for a program too large to compile as one file, how many files its
# functions are spread over (ps2n-recomp compile --parts).
macro(ps2n_add_image id elf)
	cmake_parse_arguments(image "" "CAPTURE;COMMENT;PARTS" "" ${ARGN})
	set(image_capture "")
	if(image_CAPTURE)
		set(image_capture --capture "${image_CAPTURE}")
	endif()

	# The files the recompiler writes: it names the parts after the main file.
	set(image_outputs "${PS2N_GENERATED}/${id}.cpp")
	set(image_parts "")
	if(image_PARTS AND image_PARTS GREATER 1)
		set(image_parts --parts ${image_PARTS})
		list(APPEND image_outputs "${PS2N_GENERATED}/${id}.h")
		foreach(image_part RANGE 1 ${image_PARTS})
			if(image_part LESS 10)
				set(image_part "00${image_part}")
			elseif(image_part LESS 100)
				set(image_part "0${image_part}")
			endif()
			list(APPEND image_outputs "${PS2N_GENERATED}/${id}_part${image_part}.cpp")
		endforeach()
	endif()

	add_custom_command(
		OUTPUT ${image_outputs}
		COMMAND "${PS2N_RECOMP}" compile "${elf}" --id "${id}" --out "${PS2N_GENERATED}/${id}.cpp" ${image_capture} ${image_parts}
		DEPENDS "${elf}" "${PS2N_RECOMP}" ${image_CAPTURE}
		COMMENT "Recompiling ${image_COMMENT}"
		VERBATIM
	)
	list(APPEND ps2n_image_sources ${image_outputs})
	list(APPEND ps2n_image_ids "${id}")
endmacro()

# ---- tests -------------------------------------------------------------------------
# CPU configurations a test program is run under. Each is a name, the provider
# used for the EE, the IOP and the VUs, and whether PS2Native's provider uses
# the code compiled ahead of time.
set(PS2N_TEST_CONFIGS
	"stock-jit|jit|jit|jit|on"          # PCSX2 as it ships: the reference
	"stock-interp|interp|interp|interp|on"
	"ps2n-ee|ps2n|jit|jit|off"          # PS2Native's EE provider, interpreting everything
	"ps2n-static|ps2n|jit|jit|on"       # the same with the program's code compiled ahead of time
)
# In both ps2n configurations the IOP and the VUs are on PCSX2's recompilers,
# the stand-ins of ADR 0003.

# ps2n_add_guest_test(<test name> <timeout> <labels> <arguments for guest_test.cmake>...)
function(ps2n_add_guest_test test timeout labels)
	add_test(NAME "${test}"
		COMMAND "${CMAKE_COMMAND}"
			"-DHOST=$<TARGET_FILE:ps2n-host>"
			"-DWORK_DIR=${PS2N_TEST_WORK}/${test}"
			"-DDLL_DIR=${PS2N_TEST_DLL_DIR}"
			${ARGN}
			-P "${PS2N_GUEST_TEST}"
	)
	set_tests_properties("${test}" PROPERTIES
		SKIP_REGULAR_EXPRESSION "PS2N_SKIP"
		TIMEOUT ${timeout}
		LABELS "${labels}"
	)
endfunction()

# Programs whose compiled run is known not to be the stock run, and why.
include("${PS2N_ROOT}/tests/same-as-stock-known-differences.cmake")

# ps2n_add_program_tests(<suite> <name> <elf> <expected output> <image id>
#                        [<arguments for guest_test.cmake>...])
# Registers <suite>/<configuration>/<name> for every configuration, and the
# lockstep and same-as-stock comparisons. Programs named in
# <SUITE>_DIFFERS_<configuration> are expected to print something else than
# <expected output>; those in <SUITE>_DIFFERS_same-as-stock, to part from the
# stock run.
function(ps2n_add_program_tests suite name elf expected image)
	string(TOUPPER "${suite}" suite_upper)
	foreach(config_entry IN LISTS PS2N_TEST_CONFIGS)
		string(REPLACE "|" ";" config_fields "${config_entry}")
		list(GET config_fields 0 config)
		list(GET config_fields 1 config_ee)
		list(GET config_fields 2 config_iop)
		list(GET config_fields 3 config_vu)
		list(GET config_fields 4 config_static)

		set(test "${suite}/${config}/${name}")
		ps2n_add_guest_test("${test}" 120 "${suite};${config}"
			"-DELF=${elf}" "-DEXPECTED=${expected}"
			"-DEE=${config_ee}" "-DIOP=${config_iop}" "-DVU=${config_vu}"
			"-DSTATIC=${config_static}" "-DIMAGE=${image}" ${ARGN})

		# A program whose output is known to differ from the recording under this
		# configuration still runs. It is expected to fail, so that a change in
		# either direction shows up.
		if(name IN_LIST ${suite_upper}_DIFFERS_${config})
			set_tests_properties("${test}" PROPERTIES WILL_FAIL TRUE)
		endif()
	endforeach()

	# Lockstep: the program run interpreted and then compiled, the second run
	# compared with the first at every event test.
	ps2n_add_guest_test("${suite}/lockstep/${name}" 240 "${suite};lockstep"
		"-DELF=${elf}" -DIOP=jit -DVU=jit "-DIMAGE=${image}" -DLOCKSTEP=1 ${ARGN})

	# Same as stock: the program run under stock PCSX2 with its recompilers and
	# then compiled, the second run compared with the first at every sync point
	# (ADR 0011). Together with lockstep that holds the interpreted run to the
	# stock run as well.
	set(test "${suite}/same-as-stock/${name}")
	ps2n_add_guest_test("${test}" 240 "${suite};same-as-stock"
		"-DELF=${elf}" -DIOP=jit -DVU=jit "-DIMAGE=${image}" -DSAME_AS_STOCK=1 ${ARGN})
	if(name IN_LIST ${suite_upper}_DIFFERS_same-as-stock)
		set_tests_properties("${test}" PROPERTIES WILL_FAIL TRUE)
	endif()
endfunction()

# The host's self-test: PS2Native's decoder, cycle table and instruction
# semantics against PCSX2's opcode table, interpreter and recompiler.
ps2n_add_guest_test("selftest/tables-and-semantics" 600 "selftest" -DSELFTEST=1)

# ---- ps2autotests: programs whose output was recorded on a console -----------------
include("${PS2N_ROOT}/tests/ps2autotests-known-differences.cmake")

set(PS2AUTOTESTS_ROOT "${PS2N_ROOT}/third_party/ps2autotests/tests")
set(ps2autotests_elfs "")
if(EXISTS "${PS2AUTOTESTS_ROOT}")
	file(GLOB_RECURSE ps2autotests_elfs RELATIVE "${PS2AUTOTESTS_ROOT}" "${PS2AUTOTESTS_ROOT}/*.elf")
	list(SORT ps2autotests_elfs)
else()
	message(WARNING "third_party/ps2autotests is missing; its tests are not registered. Run: git submodule update --init")
endif()

foreach(elf IN LISTS ps2autotests_elfs)
	string(REGEX REPLACE "\\.elf$" "" name "${elf}")
	string(MAKE_C_IDENTIFIER "${name}" id)
	ps2n_add_image("${id}" "${PS2AUTOTESTS_ROOT}/${elf}" COMMENT "ps2autotests/${name}")
	ps2n_add_program_tests(ps2autotests "${name}" "${PS2AUTOTESTS_ROOT}/${elf}" "${PS2AUTOTESTS_ROOT}/${name}.expected" "${id}")

	# The criterion of ADR 0005: on no line of the program's output is the
	# compiled run further from the console than stock PCSX2 with its
	# recompilers. It compares what those two tests printed, so it needs both
	# to have run first; CTest sees to that through the fixture, also when
	# only this test is asked for.
	set(never_further "ps2autotests/never-further/${name}")
	add_test(NAME "${never_further}"
		COMMAND "${CMAKE_COMMAND}"
			"-DCONSOLE=${PS2AUTOTESTS_ROOT}/${name}.expected"
			"-DREFERENCE=${PS2N_TEST_WORK}/ps2autotests/stock-jit/${name}/guest.txt"
			"-DSUBJECT=${PS2N_TEST_WORK}/ps2autotests/ps2n-static/${name}/guest.txt"
			-P "${PS2N_ROOT}/tests/never_further.cmake"
	)
	set_tests_properties("${never_further}" PROPERTIES
		SKIP_REGULAR_EXPRESSION "PS2N_SKIP"
		FIXTURES_REQUIRED "outputs/${name}"
		LABELS "ps2autotests;never-further"
	)
	set_tests_properties("ps2autotests/stock-jit/${name}" "ps2autotests/ps2n-static/${name}" PROPERTIES
		FIXTURES_SETUP "outputs/${name}"
	)
endforeach()

# The lockstep comparison again with no host stack to spare: every event test
# reached from compiled code then abandons the compiled frames, and execution
# goes on from the dispatcher through the resume points. A run must not change
# for it.
foreach(name IN ITEMS cpu/ee/alu cpu/ee_fpu/arithmetic kernel/ee/thread/terminate kernel/ee/interrupt/intc_order dma/vif/stcycl vu/lower/efu)
	if("${name}.elf" IN_LIST ps2autotests_elfs)
		string(MAKE_C_IDENTIFIER "${name}" id)
		ps2n_add_guest_test("ps2autotests/lockstep-no-stack/${name}" 240 "ps2autotests;lockstep"
			"-DELF=${PS2AUTOTESTS_ROOT}/${name}.elf" -DIOP=jit -DVU=jit "-DIMAGE=${id}" -DLOCKSTEP=1 -DSTACK_BUDGET=0)
	endif()
endforeach()

# ---- PS2Native's own test programs (tests/programs) --------------------------------
# Written for PS2Native's own assembler, so that they need no PS2 toolchain:
# the recompiler's `assemble` command makes the executables, in the build tree.
set(PS2N_PROGRAMS_ROOT "${PS2N_ROOT}/tests/programs")
set(PS2N_PROGRAMS_BUILT "${CMAKE_BINARY_DIR}/ps2n/programs")
include("${PS2N_ROOT}/tests/programs-known-differences.cmake")

file(GLOB program_sources RELATIVE "${PS2N_PROGRAMS_ROOT}" "${PS2N_PROGRAMS_ROOT}/*.s")
list(SORT program_sources)
foreach(source IN LISTS program_sources)
	string(REGEX REPLACE "\\.s$" "" name "${source}")
	set(elf "${PS2N_PROGRAMS_BUILT}/${name}.elf")
	add_custom_command(
		OUTPUT "${elf}"
		COMMAND "${PS2N_RECOMP}" assemble "${PS2N_PROGRAMS_ROOT}/${source}" --out "${elf}"
		DEPENDS "${PS2N_PROGRAMS_ROOT}/${source}" "${PS2N_RECOMP}"
		COMMENT "Assembling tests/programs/${source}"
		VERBATIM
	)

	# A program with a capture file beside it is one whose code changes while
	# it runs. It is compiled twice: from the executable alone, as a first
	# build of it would be, and with what a run of that build reported.
	set(capture "${PS2N_PROGRAMS_ROOT}/${name}.capture.toml")
	if(EXISTS "${capture}")
		ps2n_add_image("${name}" "${elf}" CAPTURE "${capture}" COMMENT "tests/programs/${name} with its capture")
		ps2n_add_image("${name}_first_build" "${elf}" COMMENT "tests/programs/${name}")
	else()
		ps2n_add_image("${name}" "${elf}" COMMENT "tests/programs/${name}")
	endif()

	ps2n_add_program_tests(programs "${name}" "${elf}" "${PS2N_PROGRAMS_ROOT}/${name}.expected" "${name}")

	if(EXISTS "${capture}")
		# The first build: correct output, with the changed code interpreted and
		# reported exactly as the capture file in the repository has it.
		ps2n_add_guest_test("programs/first-build/${name}" 120 "programs;capture"
			"-DELF=${elf}" "-DEXPECTED=${PS2N_PROGRAMS_ROOT}/${name}.expected"
			-DEE=ps2n -DIOP=jit -DVU=jit -DSTATIC=on "-DIMAGE=${name}_first_build"
			"-DCAPTURE_EXPECTED=${capture}"
			"-DREQUIRE_REPORT=in the place of [1-9][0-9]* functions memory held code that nothing was compiled from")

		# The build with the capture: nothing of the program is interpreted.
		ps2n_add_guest_test("programs/rebuilt/${name}" 120 "programs;capture"
			"-DELF=${elf}" "-DEXPECTED=${PS2N_PROGRAMS_ROOT}/${name}.expected"
			-DEE=ps2n -DIOP=jit -DVU=jit -DSTATIC=on "-DIMAGE=${name}"
			"-DREQUIRE_REPORT=instructions \\(0 inside the executable's code\\)"
			"-DREQUIRE_REPORT_2=ps2n-host: 0 block starts inside the executable's code had to be interpreted. in the place of 0 functions")
	endif()
endforeach()

# ---- programs built with ps2sdk (tests/sdk) ------------------------------------------
# C programs linked with the real EE libraries, which reach the IOP's modules
# through SIF RPC: a controller, a memory card slot, a module of our own on
# the IOP, the VUs. The binaries are in the repository (tests/sdk/README.md
# says why). They print no "-- TEST BEGIN" line, so the whole output of the
# machine is compared, and the expected output is what stock PCSX2 printed
# with its recompilers.
set(PS2N_SDK_ROOT "${PS2N_ROOT}/tests/sdk")
# name | text of the last line, where the run stops | needs "host:" files
set(PS2N_SDK_PROGRAMS
	"iop_rpc|ee: nowait sum|1"
	"pad|pad: done|0"
	"padx|pad: done|0"
	"mc|open deleted|0"
	"vu|done|0"
)
include("${PS2N_ROOT}/tests/sdk-known-differences.cmake")

foreach(entry IN LISTS PS2N_SDK_PROGRAMS)
	string(REPLACE "|" ";" fields "${entry}")
	list(GET fields 0 name)
	list(GET fields 1 until)
	list(GET fields 2 hostfs)
	set(elf "${PS2N_SDK_ROOT}/bin/${name}.elf")
	if(NOT EXISTS "${elf}")
		continue()
	endif()

	ps2n_add_image("sdk_${name}" "${elf}" COMMENT "tests/sdk/${name}")
	ps2n_add_program_tests(sdk "${name}" "${elf}" "${PS2N_SDK_ROOT}/${name}.expected" "sdk_${name}"
		"-DUNTIL=${until}" -DWHOLE=1 "-DHOSTFS=${hostfs}" -DFRAMES=900)
endforeach()

# ---- frames (step P3) --------------------------------------------------------------------
# The picture a program draws, through PCSX2's GS. With PCSX2's software
# renderer the frame the run ends on must be the recorded one, whichever CPU
# configuration ran the program: the recording is stock PCSX2's. With a
# hardware renderer the picture depends on the graphics card, so only its size
# is checked: that the renderer runs, at the console's resolution and at three
# times it.
#
# These need a graphics device (the software renderer too: PCSX2 presents
# through one). They carry the label "gpu"; leave them out with
#   ctest -LE gpu
set(vu_elf "${PS2N_SDK_ROOT}/bin/vu.elf")
if(EXISTS "${vu_elf}")
	set(frame_run "-DELF=${vu_elf}" -DIOP=jit -DVU=jit -DIMAGE=sdk_vu -DUNTIL=done -DWHOLE=1 -DSETTLE=4 -DFRAMES=900)

	foreach(config_entry IN LISTS PS2N_TEST_CONFIGS)
		string(REPLACE "|" ";" config_fields "${config_entry}")
		list(GET config_fields 0 config)
		list(GET config_fields 1 config_ee)
		list(GET config_fields 4 config_static)
		if(config STREQUAL "stock-interp")
			continue()   # PCSX2's VU interpreters; the program runs under them in sdk/stock-interp/vu
		endif()
		ps2n_add_guest_test("frames/software/${config}/vu" 120 "frames;gpu;${config}"
			${frame_run} "-DEE=${config_ee}" "-DSTATIC=${config_static}"
			-DRENDERER=sw "-DFRAME_EXPECTED=${PS2N_SDK_ROOT}/vu.software.frame")
	endforeach()

	ps2n_add_guest_test("frames/vulkan-1x/ps2n-static/vu" 120 "frames;gpu;ps2n-static"
		${frame_run} -DEE=ps2n -DSTATIC=on -DRENDERER=vulkan -DUPSCALE=1 -DFRAME_SIZE=320x448)
	ps2n_add_guest_test("frames/vulkan-3x/ps2n-static/vu" 120 "frames;gpu;ps2n-static"
		${frame_run} -DEE=ps2n -DSTATIC=on -DRENDERER=vulkan -DUPSCALE=3 -DFRAME_SIZE=960x1344)
endif()

# ---- a game (Phase 3) --------------------------------------------------------------
# A game's boot executable, compiled into the host like the test programs. The
# executable is the user's own file, outside the repository, and is read where
# it is; what is generated from it goes into the build tree with the rest and
# never into the repository. The build scripts set these (-Game, --game).
set(PS2N_GAME_ELF "" CACHE FILEPATH "A game's boot executable to compile into ps2n-host")
set(PS2N_GAME_CAPTURE "" CACHE FILEPATH "What earlier runs of the game reported for the recompiler (ps2n-host -capture)")
set(PS2N_GAME_PARTS "64" CACHE STRING "How many files the game's generated code is spread over")
if(PS2N_GAME_ELF)
	if(NOT EXISTS "${PS2N_GAME_ELF}")
		message(FATAL_ERROR "PS2N_GAME_ELF does not name a file: ${PS2N_GAME_ELF}")
	endif()
	set(game_arguments PARTS ${PS2N_GAME_PARTS} COMMENT "the game's executable, ${PS2N_GAME_ELF}")
	if(PS2N_GAME_CAPTURE)
		if(NOT EXISTS "${PS2N_GAME_CAPTURE}")
			message(FATAL_ERROR "PS2N_GAME_CAPTURE does not name a file: ${PS2N_GAME_CAPTURE}")
		endif()
		list(APPEND game_arguments CAPTURE "${PS2N_GAME_CAPTURE}")
	endif()
	ps2n_add_image(game "${PS2N_GAME_ELF}" ${game_arguments})
	message(STATUS "PS2Native: the host will hold the compiled code of ${PS2N_GAME_ELF}")
endif()

# ---- the images, linked into the host ----------------------------------------------
# The list of the images, which the host hands to PS2Native's EE provider.
add_custom_command(
	OUTPUT "${PS2N_GENERATED}/registry.cpp"
	COMMAND "${PS2N_RECOMP}" registry --out "${PS2N_GENERATED}/registry.cpp" ${ps2n_image_ids}
	DEPENDS "${PS2N_RECOMP}"
	COMMENT "Listing the recompiled test programs"
	VERBATIM
)

add_library(ps2n-test-images STATIC ${ps2n_image_sources} "${PS2N_GENERATED}/registry.cpp")
target_include_directories(ps2n-test-images PRIVATE
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}/pcsx2"
)
target_link_libraries(ps2n-test-images PRIVATE ps2n-runtime PCSX2_FLAGS)
target_link_libraries(ps2n-host PRIVATE ps2n-test-images)
