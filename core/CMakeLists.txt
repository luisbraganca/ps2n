# ps2uc_core: ELF parsing, R5900 decoding/disassembly and the test assembler.
# Shared by the recompiler and by the runtime (whose fallback interpreter
# decodes instructions at run time).
add_library(ps2uc_core STATIC
    src/assembler.cpp
    src/decoder.cpp
    src/disasm.cpp
    src/elf.cpp
    src/irx.cpp
    src/util.cpp
)
add_library(ps2uc::core ALIAS ps2uc_core)
target_include_directories(ps2uc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ps2uc_core PUBLIC cxx_std_20)
set_target_properties(ps2uc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
ps2uc_set_warnings(ps2uc_core)
