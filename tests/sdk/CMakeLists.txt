# Programs built with the ps2dev toolchain and ps2sdk. The binaries in bin/
# are checked in so the tests run without the toolchain; Makefile rebuilds them.
# FRAME names a dumped frame whose SHA-256 must match ${name}.frame.sha256.
# MEMCARDS gives each run an empty memory card directory.
function(ps2uc_sdk_test name)
    cmake_parse_arguments(T "MEMCARDS" "CONFIG;EXPECTED;FRAME" "RUN_ARGS" ${ARGN})
    if(NOT T_CONFIG)
        set(T_CONFIG ${name}.toml)
    endif()
    if(NOT T_EXPECTED)
        set(T_EXPECTED ${name}.expected)
    endif()
    set(elf "${CMAKE_CURRENT_SOURCE_DIR}/bin/${name}.elf")
    ps2uc_add_recompiled_game(TARGET sdk_${name} CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/${T_CONFIG}" ELF "${elf}" FILES 4)
    foreach(mode recompiled interpreted)
        set(args --elf "${elf}" --virtual-clock --no-window --host0 "${CMAKE_CURRENT_SOURCE_DIR}/bin"
                 --missing-out "${CMAKE_CURRENT_BINARY_DIR}/${name}_${mode}_missing.toml" ${T_RUN_ARGS})
        if(mode STREQUAL "interpreted")
            list(APPEND args --interpret)
        endif()
        set(cards "")
        if(T_MEMCARDS)
            set(cards "${CMAKE_CURRENT_BINARY_DIR}/${name}_${mode}_memcards")
            list(APPEND args --memcards "${cards}")
        endif()
        if(T_FRAME)
            add_test(NAME sdk_${name}_${mode}
                COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:sdk_${name}> "-DARGS=${args}"
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${T_EXPECTED}
                        -DFRAME_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}_frames_${mode} -DFRAME=${T_FRAME}
                        -DFRAME_HASH=${CMAKE_CURRENT_SOURCE_DIR}/${name}.frame.sha256
                        -P ${PS2UC_SOURCE_DIR}/cmake/CheckFrame.cmake)
        else()
            add_test(NAME sdk_${name}_${mode}
                COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:sdk_${name}> "-DARGS=${args}"
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${T_EXPECTED} "-DCLEAN=${cards}"
                        -P ${PS2UC_SOURCE_DIR}/cmake/RunAndCompare.cmake)
        endif()
    endforeach()
endfunction()

# An IOP module (rpctest.irx) run on the IOP interpreter: loaded through
# LOADFILE, threads, semaphores, event flags, message boxes, alarms, vblank
# waits, and an RPC server called in wait and no-wait mode.
ps2uc_sdk_test(iop_rpc)

# Sound: an IOP module (sdtest.irx) driving libsd, emulated without audio
# output: registers, voice transfers and their interrupts, a looping block
# transfer ticking at the playback rate, batches.
ps2uc_sdk_test(iop_sd)

# Controllers through PADMAN: libpad and libpadx against the HLE pad service,
# with input from a script: modes, pressure mode, buttons and sticks.
set(pad_script "${CMAKE_CURRENT_SOURCE_DIR}/pad.script")
ps2uc_sdk_test(pad RUN_ARGS --pad-script "${pad_script}")
ps2uc_sdk_test(padx CONFIG pad.toml EXPECTED pad.expected RUN_ARGS --pad-script "${pad_script}")

# Vector units: VU0 micro programs (VCALLMS, VCALLMSR) uploaded through VIF0,
# a VU1 program on a VIF1 TOP/TOPS double buffer drawing with XGKICK, flags,
# Q/P timing, branches and the E bit, and VIF1 UNPACK formats and modes.
ps2uc_sdk_test(vu RUN_ARGS --max-vsyncs 2 FRAME frame_000002.ppm)

# CD/DVD through libcdvd: a disc built from the disc/ directory; file search,
# reads into aligned and unaligned buffers, streaming, the clock, cdrom0:.
ps2uc_sdk_test(cdvd RUN_ARGS --cdrom "${CMAKE_CURRENT_SOURCE_DIR}/disc")

# Memory cards through libmc (XMCSERV) on an empty card directory.
ps2uc_sdk_test(mc MEMCARDS)
