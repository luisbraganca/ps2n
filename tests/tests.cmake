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
# With it, the code the program ran outside its executable (code it copied
# into kernel memory, for one) becomes an image of its own, <id>_outside
# (ps2n-recomp compile --outside), perhaps empty.
# PARTS: for a program too large to compile as one file, how many files its
# functions are spread over (ps2n-recomp compile --parts).
macro(ps2n_add_image id elf)
	cmake_parse_arguments(image "" "CAPTURE;COMMENT;PARTS" "" ${ARGN})
	set(image_capture "")
	set(image_outside "")
	if(image_CAPTURE)
		set(image_capture --capture "${image_CAPTURE}" --outside "${PS2N_GENERATED}/${id}_outside.cpp")
		set(image_outside "${PS2N_GENERATED}/${id}_outside.cpp")
	endif()

	# The files the recompiler writes: it names the parts after the main file.
	set(image_outputs "${PS2N_GENERATED}/${id}.cpp" ${image_outside})
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
	if(image_CAPTURE)
		list(APPEND ps2n_image_ids "${id}_outside")
	endif()
endmacro()

# ---- tests -------------------------------------------------------------------------
# CPU configurations a test program is run under. Each is a name, the provider
# used for the EE, the IOP and the VUs, and whether PS2Native's providers use
# the code compiled ahead of time.
set(PS2N_TEST_CONFIGS
	"stock-jit|jit|jit|jit|on"          # PCSX2 as it ships: the reference
	"stock-interp|interp|interp|interp|on"
	"ps2n-ee|ps2n|jit|ps2n|off"         # PS2Native's EE and VU providers, interpreting everything
	"ps2n-static|ps2n|jit|ps2n|on"      # the same with the code compiled ahead of time where a build has it
)
# In both ps2n configurations the VUs' micro programs run on PS2Native's VU
# provider too (step G11, ADR 0022): on the blocks a build has compiled
# (ps2n-recomp vu), which for a test program are few or none, and on its
# interpreter. The IOP is on PCSX2's recompiler, the stand-in of ADR 0003.

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

# ps2n_expect_failure(<test> <words>)
# A test that is expected to fail, and in one way: the driver has to say
# <words>, its words for that failure (tests/guest_test.cmake). The test
# passes when it does. It fails when the program passes, so that a difference
# that went away shows, and it fails when the program fails in any other way:
# a run that stops at an error, for one. (CTest's WILL_FAIL takes every
# failure for the expected one: on 2026-11-14 three tests passed with it for
# a run that had stopped at an error.)
function(ps2n_expect_failure test words)
	set_tests_properties("${test}" PROPERTIES PASS_REGULAR_EXPRESSION "${words}")
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
		# configuration still runs. It is expected to fail, by printing something
		# else than the recording, so that a change in either direction shows up.
		# (One program stock PCSX2's recompiler does not get through:
		# <SUITE>_NO_STOCK_RUN.)
		if(name IN_LIST ${suite_upper}_DIFFERS_${config})
			if(config STREQUAL "stock-jit" AND name IN_LIST ${suite_upper}_NO_STOCK_RUN)
				ps2n_expect_failure("${test}" "ps2n-host ended with")
			else()
				ps2n_expect_failure("${test}" "Output differs from")
			endif()
		endif()
	endforeach()

	# Lockstep: the program run interpreted and then compiled, the second run
	# compared with the first at every event test.
	ps2n_add_guest_test("${suite}/lockstep/${name}" 240 "${suite};lockstep"
		"-DELF=${elf}" -DIOP=jit -DVU=ps2n "-DIMAGE=${image}" -DLOCKSTEP=1 ${ARGN})

	# Same as stock: the program run under stock PCSX2 with its recompilers and
	# then compiled, the second run compared with the first at every sync point
	# (ADR 0011). Together with lockstep that holds the interpreted run to the
	# stock run as well.
	set(test "${suite}/same-as-stock/${name}")
	ps2n_add_guest_test("${test}" 240 "${suite};same-as-stock"
		"-DELF=${elf}" -DIOP=jit -DVU=ps2n "-DIMAGE=${image}" -DSAME_AS_STOCK=1 ${ARGN})
	if(name IN_LIST ${suite_upper}_DIFFERS_same-as-stock)
		if(name IN_LIST ${suite_upper}_NO_STOCK_RUN)
			ps2n_expect_failure("${test}" "The stock run ended with")
		else()
			ps2n_expect_failure("${test}" "The compiled run is not the stock run")
		endif()
	endif()
endfunction()

# The host's self-test: PS2Native's decoder, cycle table and instruction
# semantics against PCSX2's opcode table, interpreter and recompiler.
ps2n_add_guest_test("selftest/tables-and-semantics" 600 "selftest" -DSELFTEST=1)

# The same random micro programs carried out by code compiled ahead of time
# (step G11): the first so many for each VU and setting, whose blocks a build
# has compiled when it was given their capture (scripts/windows/vu-selftest.ps1
# makes it and builds it in). Skipped, not passed, by a host built without;
# failed when a block of them had to be interpreted, which is what a change
# to how blocks are planned does until the capture is made again.
set(PS2N_VU_SELFTEST_PROGRAMS 150)
ps2n_add_guest_test("selftest/vu-compiled" 600 "selftest;ps2n-vu" -DSELFTEST=1
	-DSELFTEST_VU=${PS2N_VU_SELFTEST_PROGRAMS} -DVU_REQUIRE_COMPILED=1)

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

# PS2Native's configurations without its VU provider (step G11,
# docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md): the console-
# recorded VU tests with PS2Native's EE provider and PCSX2's VU recompiler
# under it, as every test ran before G11. They tell a fault of the VU
# provider from one of the EE's when a VU test fails: held to the recording
# as ps2n-static is, with the same known differences.
foreach(elf IN LISTS ps2autotests_elfs)
	if(NOT elf MATCHES "^vu/")
		continue()
	endif()
	string(REGEX REPLACE "\\.elf$" "" name "${elf}")
	string(MAKE_C_IDENTIFIER "${name}" id)
	set(test "ps2autotests/ps2n-static-microvu/${name}")
	ps2n_add_guest_test("${test}" 120 "ps2autotests;ps2n-static-microvu"
		"-DELF=${PS2AUTOTESTS_ROOT}/${elf}" "-DEXPECTED=${PS2AUTOTESTS_ROOT}/${name}.expected"
		-DEE=ps2n -DIOP=jit -DVU=jit -DSTATIC=on "-DIMAGE=${id}")
	if(name IN_LIST PS2AUTOTESTS_DIFFERS_ps2n-static)
		ps2n_expect_failure("${test}" "Output differs from")
	endif()
endforeach()

# The lockstep comparison again with no host stack to spare: every event test
# reached from compiled code then abandons the compiled frames, and execution
# goes on from the dispatcher through the resume points. A run must not change
# for it.
foreach(name IN ITEMS cpu/ee/alu cpu/ee_fpu/arithmetic kernel/ee/thread/terminate kernel/ee/interrupt/intc_order dma/vif/stcycl vu/lower/efu)
	if("${name}.elf" IN_LIST ps2autotests_elfs)
		string(MAKE_C_IDENTIFIER "${name}" id)
		ps2n_add_guest_test("ps2autotests/lockstep-no-stack/${name}" 240 "ps2autotests;lockstep"
			"-DELF=${PS2AUTOTESTS_ROOT}/${name}.elf" -DIOP=jit -DVU=ps2n "-DIMAGE=${id}" -DLOCKSTEP=1 -DSTACK_BUDGET=0)
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
	set(frame_run "-DELF=${vu_elf}" -DIOP=jit -DIMAGE=sdk_vu -DUNTIL=done -DWHOLE=1 -DSETTLE=4 -DFRAMES=900)

	foreach(config_entry IN LISTS PS2N_TEST_CONFIGS)
		string(REPLACE "|" ";" config_fields "${config_entry}")
		list(GET config_fields 0 config)
		list(GET config_fields 1 config_ee)
		list(GET config_fields 3 config_vu)
		list(GET config_fields 4 config_static)
		if(config STREQUAL "stock-interp")
			continue()   # PCSX2's VU interpreters; the program runs under them in sdk/stock-interp/vu
		endif()
		ps2n_add_guest_test("frames/software/${config}/vu" 120 "frames;gpu;${config}"
			${frame_run} "-DEE=${config_ee}" "-DVU=${config_vu}" "-DSTATIC=${config_static}"
			-DRENDERER=sw "-DFRAME_EXPECTED=${PS2N_SDK_ROOT}/vu.software.frame")
	endforeach()

	# The same picture with PCSX2's VU recompiler under PS2Native's EE provider,
	# as before G11: to tell a fault of the VU provider from another.
	ps2n_add_guest_test("frames/software/ps2n-static-microvu/vu" 120 "frames;gpu;ps2n-static-microvu"
		${frame_run} -DEE=ps2n -DVU=jit -DSTATIC=on
		-DRENDERER=sw "-DFRAME_EXPECTED=${PS2N_SDK_ROOT}/vu.software.frame")

	ps2n_add_guest_test("frames/vulkan-1x/ps2n-static/vu" 120 "frames;gpu;ps2n-static"
		${frame_run} -DEE=ps2n -DVU=ps2n -DSTATIC=on -DRENDERER=vulkan -DUPSCALE=1 -DFRAME_SIZE=320x448)
	ps2n_add_guest_test("frames/vulkan-3x/ps2n-static/vu" 120 "frames;gpu;ps2n-static"
		${frame_run} -DEE=ps2n -DVU=ps2n -DSTATIC=on -DRENDERER=vulkan -DUPSCALE=3 -DFRAME_SIZE=960x1344)
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

# ---- the BIOS's EE code (step G8) ------------------------------------------------------
# Compiled from the user's own BIOS dump, read where it is, as four images
# (ps2n-recomp bios). What is generated from it goes into the build tree and
# never into the repository (docs/adr/0007, 0017). The build scripts set these
# (-Bios, --bios). Without a dump the BIOS's code is interpreted.
set(PS2N_BIOS_IMAGE "" CACHE FILEPATH "A BIOS dump whose EE code is compiled into ps2n-host")
set(PS2N_BIOS_CAPTURE "" CACHE FILEPATH "What earlier runs reported of the BIOS for the recompiler (ps2n-host -bios-capture)")
if(PS2N_BIOS_IMAGE)
	if(NOT EXISTS "${PS2N_BIOS_IMAGE}")
		message(FATAL_ERROR "PS2N_BIOS_IMAGE does not name a file: ${PS2N_BIOS_IMAGE}")
	endif()
	set(bios_capture "")
	set(bios_depends "${PS2N_BIOS_IMAGE}" "${PS2N_RECOMP}")
	if(PS2N_BIOS_CAPTURE)
		if(NOT EXISTS "${PS2N_BIOS_CAPTURE}")
			message(FATAL_ERROR "PS2N_BIOS_CAPTURE does not name a file: ${PS2N_BIOS_CAPTURE}")
		endif()
		set(bios_capture --capture "${PS2N_BIOS_CAPTURE}")
		list(APPEND bios_depends "${PS2N_BIOS_CAPTURE}")
	endif()
	set(bios_ids bios_rom0 bios_rom1 bios_kernel bios_loader)
	set(bios_outputs "")
	foreach(bios_id IN LISTS bios_ids)
		list(APPEND bios_outputs "${PS2N_GENERATED}/bios/${bios_id}.cpp")
	endforeach()
	add_custom_command(
		OUTPUT ${bios_outputs}
		COMMAND "${PS2N_RECOMP}" bios "${PS2N_BIOS_IMAGE}" --out-dir "${PS2N_GENERATED}/bios" ${bios_capture}
		DEPENDS ${bios_depends}
		COMMENT "Recompiling the BIOS's EE code, ${PS2N_BIOS_IMAGE}"
		VERBATIM
	)
	list(APPEND ps2n_image_sources ${bios_outputs})
	list(APPEND ps2n_image_ids ${bios_ids})
	message(STATUS "PS2Native: the host will hold the compiled EE code of the BIOS ${PS2N_BIOS_IMAGE}")
endif()

# ---- the vector units' micro code (step G11) ---------------------------------------------
# Compiled from the plans of the blocks runs had to interpret (ps2n-host
# -vu-capture, ps2n-recomp vu; docs/adr/0022). A plan is found by its key,
# whatever program its code was in, so every image of them is used by every
# run. What is generated is derived from the programs whose micro code it
# was: it goes into the build tree with the rest. The build scripts set these
# (-VuCapture, -VuSelftestCapture).
set(PS2N_VU_CAPTURE "" CACHE FILEPATH "What runs of a game reported of the vector units' micro code (ps2n-host -vu-capture), to compile into ps2n-host")
set(PS2N_VU_SELFTEST_CAPTURE "" CACHE FILEPATH "The same from the host's self-test, whose random micro programs then run compiled as well")
set(PS2N_VU_PARTS "16" CACHE STRING "How many files a capture's compiled micro code is spread over")
set(ps2n_vu_arguments "")

# ps2n_add_vu_image(<id> <capture file> <comment>)
macro(ps2n_add_vu_image id capture comment)
	if(NOT EXISTS "${capture}")
		message(FATAL_ERROR "No such capture of micro code: ${capture}")
	endif()
	set(vu_outputs "${PS2N_GENERATED}/vu/vu_${id}.cpp" "${PS2N_GENERATED}/vu/vu_${id}.h")
	foreach(vu_part RANGE 1 ${PS2N_VU_PARTS})
		if(vu_part LESS 10)
			set(vu_part "00${vu_part}")
		elseif(vu_part LESS 100)
			set(vu_part "0${vu_part}")
		endif()
		list(APPEND vu_outputs "${PS2N_GENERATED}/vu/vu_${id}_part${vu_part}.cpp")
	endforeach()
	add_custom_command(
		OUTPUT ${vu_outputs}
		COMMAND "${PS2N_RECOMP}" vu "${capture}" --id "${id}" --out "${PS2N_GENERATED}/vu/vu_${id}.cpp" --parts ${PS2N_VU_PARTS}
		DEPENDS "${capture}" "${PS2N_RECOMP}"
		COMMENT "Recompiling ${comment}"
		VERBATIM
	)
	list(APPEND ps2n_image_sources ${vu_outputs})
	list(APPEND ps2n_vu_arguments --vu "${id}")
endmacro()

if(PS2N_VU_PARTS LESS 2)
	message(FATAL_ERROR "PS2N_VU_PARTS is 2 or more, not ${PS2N_VU_PARTS}")
endif()
if(PS2N_VU_CAPTURE)
	ps2n_add_vu_image(game "${PS2N_VU_CAPTURE}" "the micro code runs of a game captured, ${PS2N_VU_CAPTURE}")
	message(STATUS "PS2Native: the host will hold the compiled micro code of ${PS2N_VU_CAPTURE}")
endif()
if(PS2N_VU_SELFTEST_CAPTURE)
	ps2n_add_vu_image(selftest "${PS2N_VU_SELFTEST_CAPTURE}" "the self-test's micro programs, ${PS2N_VU_SELFTEST_CAPTURE}")
	message(STATUS "PS2Native: the host will hold the compiled micro code of the self-test, ${PS2N_VU_SELFTEST_CAPTURE}")
endif()

# ---- the images, linked into the host ----------------------------------------------
# The list of the images, which the host hands to PS2Native's EE provider,
# and of the images of compiled micro code, which it hands to the VU provider.
add_custom_command(
	OUTPUT "${PS2N_GENERATED}/registry.cpp"
	COMMAND "${PS2N_RECOMP}" registry --out "${PS2N_GENERATED}/registry.cpp" ${ps2n_vu_arguments} ${ps2n_image_ids}
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
# Generated code needs no debugging information but the functions' names and
# line tables, which is what the profiler reads (ps2n-host -profile). With all
# of it, the game's code made the host's PDB too large for the system's
# symbol handler to read (578 MB); it also takes longer to compile and link.
# The machine code is the same either way.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_options(ps2n-test-images PRIVATE -gline-tables-only)
endif()

# Profile-guided optimization of the generated code (docs/adr/0019). With
# PS2N_PGO=generate the generated code is built to count where it goes, and
# the host writes what it counted when it exits, where LLVM_PROFILE_FILE
# says; with PS2N_PGO=use the merged counts (llvm-profdata merge),
# PS2N_PGO_DATA, guide the compiler. The counts come from running a game:
# they stay in the build tree and never go into the repository.
set(PS2N_PGO "" CACHE STRING "Profile-guided optimization of the generated code: generate, use, or empty for none")
set(PS2N_PGO_DATA "" CACHE FILEPATH "With PS2N_PGO=use: the merged profile (.profdata)")
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if(PS2N_PGO STREQUAL "generate")
		target_compile_options(ps2n-test-images PRIVATE -fprofile-instr-generate)
		if(WIN32)
			# The linker is called directly, so the profiling runtime has to be named.
			execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
				OUTPUT_VARIABLE ps2n_clang_resources OUTPUT_STRIP_TRAILING_WHITESPACE)
			target_link_libraries(ps2n-host PRIVATE "${ps2n_clang_resources}/lib/windows/clang_rt.profile-x86_64.lib")
		else()
			target_link_options(ps2n-host PRIVATE -fprofile-instr-generate)
		endif()
	elseif(PS2N_PGO STREQUAL "use")
		if(NOT EXISTS "${PS2N_PGO_DATA}")
			message(FATAL_ERROR "PS2N_PGO=use needs PS2N_PGO_DATA, a merged profile: ${PS2N_PGO_DATA}")
		endif()
		target_compile_options(ps2n-test-images PRIVATE "-fprofile-instr-use=${PS2N_PGO_DATA}"
			-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
	elseif(NOT PS2N_PGO STREQUAL "")
		message(FATAL_ERROR "PS2N_PGO is generate, use or empty, not ${PS2N_PGO}")
	endif()
endif()

# Link-time optimization of PS2Native's own code (docs/adr/0019). With
# PS2N_LTO=thin the generated code and the runtime are compiled to LLVM
# bitcode and optimized together when the host is linked (ThinLTO): a
# compiled function can take in one of another part, or one of the
# runtime's, which the 64 parts compiled one by one cannot. PCSX2's own code
# is compiled as before. The linker keeps what it made of each part in the
# build tree, so a link that changes few parts does little work again.
set(PS2N_LTO "" CACHE STRING "Link-time optimization of PS2Native's code: thin, or empty for none")
if(PS2N_LTO STREQUAL "thin")
	# Set up so far for clang-cl linking with lld-link, as on Windows.
	if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC AND CMAKE_LINKER MATCHES "lld-link"))
		message(FATAL_ERROR "PS2N_LTO=thin is set up for clang-cl with lld-link only so far (CMAKE_LINKER is ${CMAKE_LINKER})")
	endif()
	target_compile_options(ps2n-test-images PRIVATE -flto=thin)
	target_compile_options(ps2n-runtime PRIVATE -flto=thin)
	target_link_options(ps2n-host PRIVATE "/lldltocache:${CMAKE_BINARY_DIR}/ps2n/lto-cache"
		"/lldltocachepolicy:cache_size_bytes=8g:prune_after=168h")
elseif(NOT PS2N_LTO STREQUAL "")
	message(FATAL_ERROR "PS2N_LTO is thin or empty, not ${PS2N_LTO}")
endif()
