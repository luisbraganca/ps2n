# ps2n_recomp: analysis + code generation (a library so tests can drive it).
# ps2n: the command line.

add_library(ps2n_recomp STATIC
    src/analysis.cpp
    src/codegen.cpp
    src/config.cpp
    src/function_map.cpp
    src/report.cpp
)
target_include_directories(ps2n_recomp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    # stub_list.inc: the recompiler validates stub bindings against the runtime's list
    PRIVATE ${PS2N_SOURCE_DIR}/runtime/include)
target_include_directories(ps2n_recomp SYSTEM PRIVATE ${PS2N_SOURCE_DIR}/third_party/tomlplusplus)
target_link_libraries(ps2n_recomp PUBLIC ps2n_core)
target_compile_definitions(ps2n_recomp PUBLIC
    PS2N_VERSION="${PROJECT_VERSION}"
    PS2N_SOURCE_DIR="${PS2N_SOURCE_DIR}")
ps2n_set_warnings(ps2n_recomp)

add_executable(ps2n src/main.cpp)
target_link_libraries(ps2n PRIVATE ps2n_recomp)
ps2n_set_warnings(ps2n)

install(TARGETS ps2n RUNTIME DESTINATION bin)
