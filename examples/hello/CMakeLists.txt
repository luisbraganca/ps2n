# The example path, as build steps:
#   hello.s --(ps2n assemble)--> hello.elf
#           --(ps2n recompile)--> C++ --(compiler)--> hello (native)
set(elf "${CMAKE_CURRENT_BINARY_DIR}/hello.elf")
ps2n_assemble(OUTPUT "${elf}" SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/hello.s")
ps2n_add_recompiled_game(
    TARGET hello
    CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/hello.toml"
    ELF "${elf}"
    FILES 2)

if(PS2N_BUILD_TESTS)
    foreach(mode recompiled interpreted)
        set(args --elf "${elf}" --virtual-clock --no-window --missing-out "${CMAKE_CURRENT_BINARY_DIR}/hello_missing_${mode}.toml")
        if(mode STREQUAL "interpreted")
            list(APPEND args --interpret)
        endif()
        add_test(NAME example_hello_${mode}
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:hello> "-DARGS=${args}"
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected.txt
                    -P ${PS2N_SOURCE_DIR}/cmake/RunAndCompare.cmake)
    endforeach()
endif()
