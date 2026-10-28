add_executable(ps2uc_core_tests core_tests.cpp)
target_link_libraries(ps2uc_core_tests PRIVATE ps2uc_core)
ps2uc_set_warnings(ps2uc_core_tests)
add_test(NAME core COMMAND ps2uc_core_tests)

add_executable(ps2uc_recompiler_tests recompiler_tests.cpp)
target_link_libraries(ps2uc_recompiler_tests PRIVATE ps2uc_recomp)
ps2uc_set_warnings(ps2uc_recompiler_tests)
add_test(NAME recompiler COMMAND ps2uc_recompiler_tests)

add_executable(ps2uc_runtime_tests runtime_tests.cpp)
target_link_libraries(ps2uc_runtime_tests PRIVATE ps2uc_runtime)
# White-box access to runtime internals (GS addressing, guest printf).
target_include_directories(ps2uc_runtime_tests PRIVATE ${PS2UC_SOURCE_DIR}/runtime/src)
ps2uc_set_warnings(ps2uc_runtime_tests)
add_test(NAME runtime COMMAND ps2uc_runtime_tests)

add_subdirectory(programs)
add_subdirectory(sdk)
