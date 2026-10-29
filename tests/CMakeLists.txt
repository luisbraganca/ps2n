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
