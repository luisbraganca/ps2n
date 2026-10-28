# End-to-end programs: assembled, recompiled at build time, then run in both
# recompiled and interpreted mode and compared against a recorded output.
function(ps2uc_program_test name)
    cmake_parse_arguments(P "STRIP" "FILES" "RUN_ARGS;SOURCES" ${ARGN})
    set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")
    if(P_STRIP)
        ps2uc_assemble(OUTPUT "${elf}" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${name}.s" STRIP)
    else()
        ps2uc_assemble(OUTPUT "${elf}" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${name}.s")
    endif()
    set(config "${CMAKE_CURRENT_SOURCE_DIR}/${name}.toml")
    if(NOT EXISTS "${config}")
        set(config "${CMAKE_CURRENT_SOURCE_DIR}/default.toml")
    endif()
    if(NOT P_FILES)
        set(P_FILES 1)
    endif()
    ps2uc_add_recompiled_game(TARGET prog_${name} CONFIG "${config}" ELF "${elf}" FILES ${P_FILES} SOURCES ${P_SOURCES})
    foreach(mode recompiled interpreted)
        set(args --elf "${elf}" --virtual-clock --no-window --missing-out "${CMAKE_CURRENT_BINARY_DIR}/${name}_${mode}_missing.toml" ${P_RUN_ARGS})
        if(mode STREQUAL "interpreted")
            list(APPEND args --interpret)
        endif()
        add_test(NAME program_${name}_${mode}
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:prog_${name}> "-DARGS=${args}"
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/${name}.expected
                    -P ${PS2UC_SOURCE_DIR}/cmake/RunAndCompare.cmake)
    endforeach()
endfunction()

ps2uc_program_test(control_flow)
ps2uc_program_test(stripped STRIP)
ps2uc_program_test(kernel)
# Skip, stub, override and patch; --max-vsyncs turns an ignored skip into a failure instead of a hang.
ps2uc_program_test(bindings SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bindings_override.cpp" RUN_ARGS --max-vsyncs 100)

# GS: DMA -> GIF -> rasterizer -> display readout, checked against a recorded frame.
ps2uc_assemble(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gs.elf" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gs.s")
ps2uc_add_recompiled_game(TARGET prog_gs CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/default.toml" ELF "${CMAKE_CURRENT_BINARY_DIR}/gs.elf" FILES 1)
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
                -P ${PS2UC_SOURCE_DIR}/cmake/CheckFrame.cmake)
endforeach()
