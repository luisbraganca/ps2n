# SPDX-License-Identifier: GPL-3.0-or-later
#
# The static recompiler: a library (analysis and code generation), which the
# unit tests link, and its command line, ps2n-recomp.

add_library(ps2n_recomp STATIC
    src/analysis.cpp
    src/analysis.h
    src/bios.cpp
    src/bios.h
    src/codegen.cpp
    src/codegen.h
    src/vu_codegen.cpp
    src/vu_codegen.h
)
target_include_directories(ps2n_recomp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ps2n_recomp PUBLIC ps2n_core)
ps2n_set_warnings(ps2n_recomp)

add_executable(ps2n-recomp src/main.cpp)
if(WIN32)
    # The PS2Native icon (assets/branding/README.md).
    target_sources(ps2n-recomp PRIVATE ${PROJECT_SOURCE_DIR}/assets/ps2n.rc)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/assets/ps2n.rc PROPERTIES
        OBJECT_DEPENDS ${PROJECT_SOURCE_DIR}/assets/branding/icons/favicon.ico)
endif()
target_link_libraries(ps2n-recomp PRIVATE ps2n_recomp)
ps2n_set_warnings(ps2n-recomp)

# Where the run-time side's build looks for it (scripts/*/build.*).
set_target_properties(ps2n-recomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
