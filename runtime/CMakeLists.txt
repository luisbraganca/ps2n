# ps2uc_runtime: everything a recompiled game links against. Generated code
# only includes ps2/recomp.h (instruction semantics + control-flow entry
# points) and ps2/runtime.h (the table types and ps2::run).

add_library(ps2uc_runtime STATIC
    src/clock.cpp
    src/cop0.cpp
    src/dispatch.cpp
    src/interpreter.cpp
    src/log.cpp
    src/memory.cpp
    src/runtime.cpp
    src/vu.cpp
    src/hw/dmac.cpp
    src/hw/gs.cpp
    src/hw/hw.cpp
    src/hw/vif.cpp
    src/iop/cdvd.cpp
    src/iop/cpu.cpp
    src/iop/disc.cpp
    src/iop/hle.cpp
    src/iop/hostfs.cpp
    src/iop/iop.cpp
    src/iop/kernel.cpp
    src/iop/libs.cpp
    src/iop/memcard.cpp
    src/iop/modules.cpp
    src/iop/pad.cpp
    src/iop/services.cpp
    src/iop/sif.cpp
    src/iop/sound.cpp
    src/kernel/kernel.cpp
    src/stubs/format.cpp
    src/stubs/stubs.cpp
    src/video/video.cpp
)
add_library(ps2uc::runtime ALIAS ps2uc_runtime)
target_include_directories(ps2uc_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ps2uc_runtime PUBLIC ps2uc_core)
target_compile_features(ps2uc_runtime PUBLIC cxx_std_20)
# The EE FPU and the VUs round after a multiply before adding (MADD, MSUB);
# fusing `a + b * c` into one rounding would change results. Public, because
# recompiled games compile the inline instruction semantics too. clang-cl only
# takes the option through /clang:; MSVC 2022 and later only fuse with
# /fp:contract or /fp:fast.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(ps2uc_runtime PUBLIC /clang:-ffp-contract=off)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ps2uc_runtime PUBLIC -ffp-contract=off)
endif()
ps2uc_set_warnings(ps2uc_runtime)

find_package(Threads REQUIRED)
target_link_libraries(ps2uc_runtime PUBLIC Threads::Threads)

if(PS2UC_WITH_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        message(STATUS "ps2uc runtime: SDL2 ${SDL2_VERSION} found, window output enabled")
        target_compile_definitions(ps2uc_runtime PRIVATE PS2UC_HAVE_SDL2=1)
        if(TARGET SDL2::SDL2)
            target_link_libraries(ps2uc_runtime PRIVATE SDL2::SDL2)
        else()
            target_link_libraries(ps2uc_runtime PRIVATE SDL2::SDL2-static)
        endif()
    else()
        message(STATUS "ps2uc runtime: SDL2 not found, building headless (frames can still be dumped with --dump-frames)")
    endif()
endif()
