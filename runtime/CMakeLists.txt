# ps2n_runtime: everything a recompiled game links against. Generated code
# only includes ps2/recomp.h (instruction semantics + control-flow entry
# points) and ps2/runtime.h (the table types and ps2::run).

add_library(ps2n_runtime STATIC
    src/axiom.cpp
    src/clock.cpp
    src/cop0.cpp
    src/dispatch.cpp
    src/interpreter.cpp
    src/log.cpp
    src/memory.cpp
    src/runtime.cpp
    src/settings.cpp
    src/version.cpp
    src/vu.cpp
    src/vu_thread.cpp
    src/audio/audio.cpp
    src/hw/dmac.cpp
    src/hw/gs.cpp
    src/hw/hw.cpp
    src/hw/ipu.cpp
    src/hw/mpeg.cpp
    src/hw/vif.cpp
    src/input/input.cpp
    src/input/manager.cpp
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
    src/iop/spu2.cpp
    src/kernel/kernel.cpp
    src/stubs/format.cpp
    src/stubs/stubs.cpp
    src/video/video.cpp
)
add_library(ps2n::runtime ALIAS ps2n_runtime)
target_include_directories(ps2n_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ps2n_runtime PUBLIC ps2n_core)
target_compile_features(ps2n_runtime PUBLIC cxx_std_20)
# The EE FPU and the VUs round after a multiply before adding (MADD, MSUB);
# fusing `a + b * c` into one rounding would change results. Public, because
# recompiled games compile the inline instruction semantics too. clang-cl only
# takes the option through /clang:; MSVC 2022 and later only fuse with
# /fp:contract or /fp:fast.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(ps2n_runtime PUBLIC /clang:-ffp-contract=off)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ps2n_runtime PUBLIC -ffp-contract=off)
endif()
ps2n_set_warnings(ps2n_runtime)

find_package(Threads REQUIRED)
target_link_libraries(ps2n_runtime PUBLIC Threads::Threads)

# The runtime's commit, for its logs (src/version.cpp): looked up at every
# build, and the header rewritten only when it changes.
find_package(Git QUIET)
set(ps2n_version_dir "${CMAKE_CURRENT_BINARY_DIR}/version")
add_custom_target(ps2n_version
    COMMAND "${CMAKE_COMMAND}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DOUTPUT=${ps2n_version_dir}/ps2n_version.h" -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Ps2nVersion.cmake"
    BYPRODUCTS "${ps2n_version_dir}/ps2n_version.h"
    VERBATIM)
add_dependencies(ps2n_runtime ps2n_version)
target_include_directories(ps2n_runtime PRIVATE "${ps2n_version_dir}")

# Every executable linked with the runtime (a game, the runtime tests) gets
# the PS2Native icon. An interface source, so that generated game projects
# get it without a change to their CMakeLists.txt.
if(WIN32)
    target_sources(ps2n_runtime INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ps2n.rc")
endif()

# The Vulkan renderer (src/gpu). It needs no SDK: the Vulkan headers are
# vendored (third_party/vulkan) and the graphics driver's Vulkan library is
# loaded when a game starts. Here, like SDL2 below, so that generated game
# projects get it too.
option(PS2N_WITH_VULKAN "Build the Vulkan renderer (no SDK needed: the graphics driver's Vulkan is loaded when a game starts)" ON)
if(PS2N_WITH_VULKAN)
    target_include_directories(ps2n_runtime SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vulkan/include")
    target_compile_definitions(ps2n_runtime PRIVATE PS2N_HAVE_VULKAN=1 VK_NO_PROTOTYPES)
    target_sources(ps2n_runtime PRIVATE
        src/gpu/compose.cpp
        src/gpu/device.cpp
        src/gpu/loader.cpp
        src/gpu/picture.cpp
        src/gpu/renderer.cpp
        src/gpu/renderer_display.cpp
        src/gpu/renderer_setup.cpp
        src/gpu/resources.cpp)
    target_link_libraries(ps2n_runtime PRIVATE ${CMAKE_DL_LIBS})

    # The shaders, compiled to SPIR-V by glslc (the Vulkan SDK's, or a
    # distribution's shaderc package) when it is there. Without it the build
    # uses the compiled copy that comes with the sources,
    # src/gpu/shaders/ps2n_spirv.h; ps2n_update_shaders refreshes that copy.
    set(ps2n_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders")
    set(ps2n_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    # name, source file, then glslc's definitions
    set(ps2n_shaders
        "present_vert|present.vert|"
        "present_frag|present.frag|"
        "gs_vert|gs.vert|"
        "gs_frag_rtz_f64|gs.frag|ROUND_TOWARD_ZERO,FLOAT64"
        "gs_frag_rtz|gs.frag|ROUND_TOWARD_ZERO"
        "gs_frag_rte_f64|gs.frag|FLOAT64"
        "gs_frag_rte|gs.frag|"
        "resolve_comp|resolve.comp|"
        "expand_comp|expand.comp|"
        "push_comp|push.comp|"
        "copy_comp|copy.comp|"
        "display_comp|display.comp|"
        "deinterlace_comp|deinterlace.comp|")
    file(GLOB ps2n_sdk_bins "C:/VulkanSDK/*/Bin")
    find_program(PS2N_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" ${ps2n_sdk_bins})
    if(PS2N_GLSLC)
        execute_process(COMMAND "${PS2N_GLSLC}" --version OUTPUT_VARIABLE glslc_version ERROR_QUIET)
        string(REGEX MATCH "^[^\r\n]*" glslc_version "${glslc_version}")
        set(spirv_inputs "")
        set(spirv_files "")
        foreach(entry IN LISTS ps2n_shaders)
            string(REPLACE "|" ";" parts "${entry}")
            list(GET parts 0 name)
            list(GET parts 1 source)
            list(GET parts 2 defines)
            set(define_args "")
            if(defines)
                string(REPLACE "," ";" defines "${defines}")
                foreach(d IN LISTS defines)
                    list(APPEND define_args "-D${d}")
                endforeach()
            endif()
            set(out "${ps2n_spirv_dir}/${name}.inc")
            add_custom_command(
                OUTPUT "${out}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${ps2n_spirv_dir}"
                COMMAND "${PS2N_GLSLC}" --target-env=vulkan1.3 -mfmt=c ${define_args} -MD -MF "${out}.d" -o "${out}" "${ps2n_shader_dir}/${source}"
                DEPENDS "${ps2n_shader_dir}/${source}"
                DEPFILE "${out}.d"
                COMMENT "Compiling the shader ${name}"
                VERBATIM)
            list(APPEND spirv_inputs "${name}=${out}")
            list(APPEND spirv_files "${out}")
        endforeach()
        string(REPLACE ";" "|" spirv_inputs "${spirv_inputs}")
        add_custom_command(
            OUTPUT "${ps2n_spirv_dir}/ps2n_spirv.h"
            COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${ps2n_spirv_dir}/ps2n_spirv.h" "-DCOMPILER=${glslc_version}"
                    "-DINPUTS=${spirv_inputs}" -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Ps2nSpirvHeader.cmake"
            DEPENDS ${spirv_files} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Ps2nSpirvHeader.cmake"
            COMMENT "Writing ps2n_spirv.h"
            VERBATIM)
        target_sources(ps2n_runtime PRIVATE "${ps2n_spirv_dir}/ps2n_spirv.h")
        target_include_directories(ps2n_runtime PRIVATE "${ps2n_spirv_dir}")
        if(NOT TARGET ps2n_update_shaders)
            add_custom_target(ps2n_update_shaders
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ps2n_spirv_dir}/ps2n_spirv.h" "${ps2n_shader_dir}/ps2n_spirv.h"
                DEPENDS "${ps2n_spirv_dir}/ps2n_spirv.h"
                COMMENT "Copying the compiled shaders to src/gpu/shaders/ps2n_spirv.h"
                VERBATIM)
        endif()
        set_property(GLOBAL PROPERTY PS2N_SPIRV_BUILT "${ps2n_spirv_dir}/ps2n_spirv.h")
        message(STATUS "ps2n runtime: Vulkan renderer built in; shaders compiled with ${PS2N_GLSLC}")
    else()
        target_include_directories(ps2n_runtime PRIVATE "${ps2n_shader_dir}")
        message(STATUS "ps2n runtime: Vulkan renderer built in; glslc not found, so the compiled shaders that come with the sources are used")
    endif()
else()
    target_sources(ps2n_runtime PRIVATE src/gpu/gpu_stub.cpp)
    message(STATUS "ps2n runtime: Vulkan renderer not built (PS2N_WITH_VULKAN is off)")
endif()

# Here rather than in the top-level project, so that generated game projects,
# which add this directory themselves, get the window too.
option(PS2N_WITH_SDL2 "Open a window for GS output when SDL2 is available" ON)
if(PS2N_WITH_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        message(STATUS "ps2n runtime: SDL2 ${SDL2_VERSION} found, window output enabled")
        target_compile_definitions(ps2n_runtime PRIVATE PS2N_HAVE_SDL2=1)
        if(TARGET SDL2::SDL2)
            set(sdl2_target SDL2::SDL2)
            # Windows looks for SDL2.dll next to the executable (see
            # ps2n_copy_runtime_dlls); global so other directories can name it.
            get_target_property(sdl2_type SDL2::SDL2 TYPE)
            if(WIN32 AND sdl2_type STREQUAL "SHARED_LIBRARY")
                set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_GLOBAL TRUE)
                set_property(GLOBAL APPEND PROPERTY PS2N_RUNTIME_DLLS SDL2::SDL2)
            endif()
        else()
            set(sdl2_target SDL2::SDL2-static)
        endif()
        # Dear ImGui 1.92.9b (third_party/imgui, unmodified) draws the
        # settings overlay in the window. Third-party code: no project warnings.
        set(imgui_dir "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui")
        add_library(ps2n_imgui STATIC
            ${imgui_dir}/imgui.cpp
            ${imgui_dir}/imgui_draw.cpp
            ${imgui_dir}/imgui_tables.cpp
            ${imgui_dir}/imgui_widgets.cpp
            ${imgui_dir}/backends/imgui_impl_sdl2.cpp
            ${imgui_dir}/backends/imgui_impl_sdlrenderer2.cpp)
        target_include_directories(ps2n_imgui SYSTEM PUBLIC ${imgui_dir} ${imgui_dir}/backends)
        target_compile_definitions(ps2n_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DEFINE_MATH_OPERATORS)
        # clang-cl: SDL 2's SDL_endian.h defines _m_prefetch itself unless
        # <intrin.h> came first, and recent clang rejects that (it is a
        # builtin there). Public, so the runtime's files that include SDL get it too.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            target_compile_options(ps2n_imgui PUBLIC /FIintrin.h)
        endif()
        target_link_libraries(ps2n_imgui PUBLIC ${sdl2_target})
        # With the Vulkan renderer the window shows its pictures through
        # Vulkan, the overlay too: ImGui's Vulkan backend (from the same
        # release), calling Vulkan through the runtime's loader.
        if(PS2N_WITH_VULKAN)
            target_sources(ps2n_imgui PRIVATE ${imgui_dir}/backends/imgui_impl_vulkan.cpp)
            target_include_directories(ps2n_imgui SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vulkan/include")
            target_compile_definitions(ps2n_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES VK_NO_PROTOTYPES)
            target_sources(ps2n_runtime PRIVATE src/video/present_vk.cpp)
        endif()
        target_link_libraries(ps2n_runtime PRIVATE ${sdl2_target} ps2n_imgui)
        target_sources(ps2n_runtime PRIVATE src/input/sdl.cpp src/ui/overlay.cpp)
    else()
        message(STATUS "ps2n runtime: SDL2 not found, building headless (frames can still be dumped with --dump-frames)")
    endif()
endif()

# Every executable that links the runtime calls this: it copies the DLLs the
# runtime depends on (SDL2.dll from the SDL2-devel-*-VC package) next to the
# executable after each build. It does nothing where there are none.
function(ps2n_copy_runtime_dlls target)
    get_property(dlls GLOBAL PROPERTY PS2N_RUNTIME_DLLS)
    foreach(dll IN LISTS dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${dll}>" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
endfunction()
