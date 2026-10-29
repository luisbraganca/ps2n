# End-to-end programs: assembled, recompiled at build time, then run in both
# recompiled and interpreted mode and compared against a recorded output.
function(ps2n_program_test name)
    cmake_parse_arguments(P "STRIP" "FILES" "RUN_ARGS;SOURCES" ${ARGN})
    set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")
    if(P_STRIP)
        ps2n_assemble(OUTPUT "${elf}" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${name}.s" STRIP)
    else()
        ps2n_assemble(OUTPUT "${elf}" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${name}.s")
    endif()
    set(config "${CMAKE_CURRENT_SOURCE_DIR}/${name}.toml")
    if(NOT EXISTS "${config}")
        set(config "${CMAKE_CURRENT_SOURCE_DIR}/default.toml")
    endif()
    if(NOT P_FILES)
        set(P_FILES 1)
    endif()
    ps2n_add_recompiled_game(TARGET prog_${name} CONFIG "${config}" ELF "${elf}" FILES ${P_FILES} SOURCES ${P_SOURCES})
    foreach(mode recompiled interpreted)
        set(args --elf "${elf}" --virtual-clock --no-window --missing-out "${CMAKE_CURRENT_BINARY_DIR}/${name}_${mode}_missing.toml" ${P_RUN_ARGS})
        if(mode STREQUAL "interpreted")
            list(APPEND args --interpret)
        endif()
        add_test(NAME program_${name}_${mode}
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:prog_${name}> "-DARGS=${args}"
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${name}.expected
                    -P ${PS2N_SOURCE_DIR}/cmake/RunAndCompare.cmake)
    endforeach()
endfunction()

ps2n_program_test(control_flow)
ps2n_program_test(stripped STRIP)
# A polling loop that an extra entry point splits across two functions: its
# back-edge must still be a safepoint, or the vblank interrupt never comes.
ps2n_program_test(split_loop STRIP)
ps2n_program_test(kernel)
ps2n_program_test(deci2)
# Skip, stub, override and patch; --max-vsyncs turns an ignored skip into a failure instead of a hang.
ps2n_program_test(bindings SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bindings_override.cpp" RUN_ARGS --max-vsyncs 100)

# LoadExecPS2: a boot program starts a second recompiled program, which starts
# a third one that was not recompiled (interpreted), which starts the boot
# program again. All three are linked at the same address; host: is the build folder.
foreach(p exec_boot exec_main exec_extra)
    ps2n_assemble(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${p}.elf" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${p}.s")
endforeach()
ps2n_add_recompiled_game(TARGET prog_exec CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/default.toml"
                         ELF "${CMAKE_CURRENT_BINARY_DIR}/exec_boot.elf" PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/exec_main.elf"
                         FILES 1 DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/exec_extra.elf")
foreach(mode recompiled interpreted)
    set(args --elf "${CMAKE_CURRENT_BINARY_DIR}/exec_boot.elf" --host0 "${CMAKE_CURRENT_BINARY_DIR}" --virtual-clock --no-window
             --missing-out "${CMAKE_CURRENT_BINARY_DIR}/exec_${mode}_missing.toml")
    if(mode STREQUAL "interpreted")
        list(APPEND args --interpret)
    endif()
    add_test(NAME program_exec_${mode}
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:prog_exec> "-DARGS=${args}"
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/exec.expected
                -P ${PS2N_SOURCE_DIR}/cmake/RunAndCompare.cmake)
endforeach()

# A boot program that loads the next one by itself and jumps into it (no
# LoadExecPS2): the runtime recognises the recompiled program by checking its
# code against RAM, and checks again after the program rewrites a function.
ps2n_assemble(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/handoff_next.elf" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/handoff_next.s" BASE 0x00200000)
ps2n_assemble(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/handoff.elf" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/handoff.s"
              DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/handoff_next.elf")
ps2n_add_recompiled_game(TARGET prog_handoff CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/default.toml"
                         ELF "${CMAKE_CURRENT_BINARY_DIR}/handoff.elf" PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/handoff_next.elf" FILES 1)
foreach(mode recompiled interpreted)
    set(args --elf "${CMAKE_CURRENT_BINARY_DIR}/handoff.elf" --virtual-clock --no-window
             --missing-out "${CMAKE_CURRENT_BINARY_DIR}/handoff_${mode}_missing.toml")
    if(mode STREQUAL "interpreted")
        list(APPEND args --interpret)
    endif()
    add_test(NAME program_handoff_${mode}
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:prog_handoff> "-DARGS=${args}"
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/handoff.expected
                -P ${PS2N_SOURCE_DIR}/cmake/RunAndCompare.cmake)
endforeach()

# GS: DMA -> GIF -> rasterizer -> display readout, checked against a recorded frame.
ps2n_assemble(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gs.elf" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gs.s")
ps2n_add_recompiled_game(TARGET prog_gs CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/default.toml" ELF "${CMAKE_CURRENT_BINARY_DIR}/gs.elf" FILES 1)
foreach(mode recompiled interpreted)
    set(args --elf "${CMAKE_CURRENT_BINARY_DIR}/gs.elf" --virtual-clock --no-window --max-vsyncs 2
             --missing-out "${CMAKE_CURRENT_BINARY_DIR}/gs_${mode}_missing.toml")
    if(mode STREQUAL "interpreted")
        list(APPEND args --interpret)
    endif()
    add_test(NAME program_gs_${mode}
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:prog_gs> "-DARGS=${args}"
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/gs.expected
                -DFRAME_DIR=${CMAKE_CURRENT_BINARY_DIR}/gs_frames_${mode} -DFRAME=frame_000002.ppm
                -DFRAME_HASH=${CMAKE_CURRENT_SOURCE_DIR}/gs.frame.sha256
                -P ${PS2N_SOURCE_DIR}/cmake/CheckFrame.cmake)
endforeach()
