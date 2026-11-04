# Writes OUTPUT, a header that defines PS2N_BUILD_COMMIT as
# `git describe --always --dirty` gives it for SOURCE_DIR ("unknown" without
# git), when that differs from what the file holds. The runtime's build runs
# it every time; an unchanged file recompiles nothing.
#   cmake -DGIT_EXECUTABLE=<git> -DSOURCE_DIR=<dir> -DOUTPUT=<header> -P Ps2nVersion.cmake
set(commit "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" describe --always --dirty
        OUTPUT_VARIABLE described
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rc
        ERROR_QUIET)
    if(rc EQUAL 0 AND described)
        set(commit "${described}")
    endif()
endif()
set(text "#define PS2N_BUILD_COMMIT \"${commit}\"\n")
set(old "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUTPUT}" "${text}")
endif()
