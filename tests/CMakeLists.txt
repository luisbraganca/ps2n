add_executable(ps2n_core_tests core_tests.cpp)
target_link_libraries(ps2n_core_tests PRIVATE ps2n_core)
ps2n_set_warnings(ps2n_core_tests)
add_test(NAME core COMMAND ps2n_core_tests)

add_executable(ps2n_recompiler_tests recompiler_tests.cpp)
target_link_libraries(ps2n_recompiler_tests PRIVATE ps2n_recomp)
ps2n_set_warnings(ps2n_recompiler_tests)
add_test(NAME recompiler COMMAND ps2n_recompiler_tests)

add_executable(ps2n_runtime_tests runtime_tests.cpp)
target_link_libraries(ps2n_runtime_tests PRIVATE ps2n_runtime)
ps2n_copy_runtime_dlls(ps2n_runtime_tests)
# White-box access to runtime internals (GS addressing, guest printf).
target_include_directories(ps2n_runtime_tests PRIVATE ${PS2N_SOURCE_DIR}/runtime/src)
ps2n_set_warnings(ps2n_runtime_tests)
add_test(NAME runtime COMMAND ps2n_runtime_tests)

add_subdirectory(programs)
add_subdirectory(sdk)

# Recompiled VU micro programs against the interpreter (vu/check.cpp): random
# VU1 programs written at build time, recompiled by `ps2n vu`, run both ways.
add_executable(ps2n_vu_fuzz_generate vu/generate.cpp)
target_link_libraries(ps2n_vu_fuzz_generate PRIVATE ps2n_core)
ps2n_set_warnings(ps2n_vu_fuzz_generate)
set(vu_fuzz ${CMAKE_CURRENT_BINARY_DIR}/vu_fuzz)
add_custom_command(
    OUTPUT ${vu_fuzz}/code.toml ${vu_fuzz}/images.bin
    COMMAND ${CMAKE_COMMAND} -E make_directory ${vu_fuzz}
    COMMAND ps2n_vu_fuzz_generate ${vu_fuzz}/code.toml ${vu_fuzz}/images.bin
    DEPENDS ps2n_vu_fuzz_generate
    COMMENT "Writing random VU micro programs")
add_custom_command(
    OUTPUT ${vu_fuzz}/programs.cpp
    COMMAND ps2n vu ${vu_fuzz}/code.toml -o ${vu_fuzz}/programs.cpp --namespace vufuzz
    DEPENDS ps2n ${vu_fuzz}/code.toml
    COMMENT "Recompiling the random VU micro programs")
add_library(ps2n_vu_fuzz_programs STATIC ${vu_fuzz}/programs.cpp)
target_link_libraries(ps2n_vu_fuzz_programs PRIVATE ps2n_runtime)
ps2n_set_generated_warnings(ps2n_vu_fuzz_programs)
add_executable(ps2n_vu_tests vu/check.cpp)
target_link_libraries(ps2n_vu_tests PRIVATE ps2n_vu_fuzz_programs ps2n_runtime)
target_include_directories(ps2n_vu_tests PRIVATE ${PS2N_SOURCE_DIR}/runtime/src)
ps2n_copy_runtime_dlls(ps2n_vu_tests)
ps2n_set_warnings(ps2n_vu_tests)
add_test(NAME vu_compiled COMMAND ps2n_vu_tests ${vu_fuzz}/images.bin)
