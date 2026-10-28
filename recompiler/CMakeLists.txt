# ps2uc_recomp: analysis + code generation (a library so tests can drive it).
# ps2-uncompiler: the command line.

add_library(ps2uc_recomp STATIC
    src/analysis.cpp
    src/codegen.cpp
    src/config.cpp
    src/function_map.cpp
    src/report.cpp
)
target_include_directories(ps2uc_recomp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    # stub_list.inc: the recompiler validates stub bindings against the runtime's list
    PRIVATE ${PS2UC_SOURCE_DIR}/runtime/include)
target_include_directories(ps2uc_recomp SYSTEM PRIVATE ${PS2UC_SOURCE_DIR}/third_party/tomlplusplus)
target_link_libraries(ps2uc_recomp PUBLIC ps2uc_core)
target_compile_definitions(ps2uc_recomp PUBLIC
    PS2UC_VERSION="${PROJECT_VERSION}"
    PS2UC_SOURCE_DIR="${PS2UC_SOURCE_DIR}")
ps2uc_set_warnings(ps2uc_recomp)

add_executable(ps2-uncompiler src/main.cpp)
target_link_libraries(ps2-uncompiler PRIVATE ps2uc_recomp)
ps2uc_set_warnings(ps2-uncompiler)

install(TARGETS ps2-uncompiler RUNTIME DESTINATION bin)
