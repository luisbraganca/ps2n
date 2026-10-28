# Build-system integration: assemble test programs and recompile ELFs as part
# of a CMake build, so the generated C++ is a build artifact rather than a
# checked-in blob.

# ps2uc_assemble(OUTPUT <file.elf> SOURCE <file.s> [STRIP] [BASE <address>] [DEPENDS <files>...])
#
# The assembler runs in the current binary directory, where .incbin finds
# files built there (DEPENDS names them).
function(ps2uc_assemble)
    cmake_parse_arguments(A "STRIP" "OUTPUT;SOURCE;BASE" "DEPENDS" ${ARGN})
    set(extra "")
    if(A_STRIP)
        list(APPEND extra --strip)
    endif()
    if(A_BASE)
        list(APPEND extra --base ${A_BASE})
    endif()
    add_custom_command(
        OUTPUT ${A_OUTPUT}
        COMMAND ps2-uncompiler assemble ${A_SOURCE} -o ${A_OUTPUT} ${extra}
        DEPENDS ps2-uncompiler ${A_SOURCE} ${A_DEPENDS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Assembling ${A_SOURCE}"
        VERBATIM)
endfunction()

# ps2uc_add_recompiled_game(TARGET <name> CONFIG <config.toml> ELF <file.elf>
#                           [PROGRAMS <file.elf>...] [FILES <n>] [SOURCES <extra .cpp>...]
#                           [DEPENDS <files>...])
#
# Runs `ps2-uncompiler recompile` at build time and builds the result into an
# executable linked with the runtime. FILES fixes the number of generated
# source files so the build graph is known at configure time (default 4).
# PROGRAMS are the game's other executables, started with LoadExecPS2, which
# go into the same executable (FILES source files each).
function(ps2uc_add_recompiled_game)
    cmake_parse_arguments(G "" "TARGET;CONFIG;ELF;FILES" "PROGRAMS;SOURCES;DEPENDS" ${ARGN})
    if(NOT G_FILES)
        set(G_FILES 4)
    endif()
    set(out "${CMAKE_CURRENT_BINARY_DIR}/${G_TARGET}_recomp")
    # The boot program's files are at the top, each other program's in a
    # folder named after its ELF file, as a C identifier (as the recompiler does).
    set(dirs "${out}")
    set(program_args "")
    foreach(p ${G_PROGRAMS})
        get_filename_component(name "${p}" NAME)
        string(MAKE_C_IDENTIFIER "${name}" id)
        list(APPEND dirs "${out}/${id}")
        list(APPEND program_args --program "${p}")
    endforeach()
    set(generated "${out}/recomp_main.cpp")
    math(EXPR last "${G_FILES} - 1")
    foreach(dir ${dirs})
        list(APPEND generated "${dir}/functions.h" "${dir}/recomp_table.cpp")
        foreach(i RANGE 0 ${last})
            if(i LESS 10)
                set(n "00${i}")
            elseif(i LESS 100)
                set(n "0${i}")
            else()
                set(n "${i}")
            endif()
            list(APPEND generated "${dir}/recomp_funcs_${n}.cpp")
        endforeach()
    endforeach()

    # The recompiler only rewrites files whose content changed; the stamp plus
    # BYPRODUCTS lets the build skip recompiling C++ that did not change.
    add_custom_command(
        OUTPUT "${out}/recomp.stamp"
        BYPRODUCTS ${generated}
        COMMAND ps2-uncompiler recompile ${G_CONFIG} --elf ${G_ELF} ${program_args} --out ${out} --name ${G_TARGET}
                --files ${G_FILES} --no-project --quiet
        COMMAND ${CMAKE_COMMAND} -E touch "${out}/recomp.stamp"
        DEPENDS ps2-uncompiler ${G_CONFIG} ${G_ELF} ${G_PROGRAMS} ${G_DEPENDS}
        COMMENT "Recompiling ${G_ELF}"
        VERBATIM)

    add_executable(${G_TARGET} ${generated} "${out}/recomp.stamp" ${G_SOURCES})
    target_include_directories(${G_TARGET} PRIVATE "${out}")
    target_link_libraries(${G_TARGET} PRIVATE ps2uc_runtime)
    ps2uc_set_generated_warnings(${G_TARGET})
endfunction()
