# ctest helper: run a program that dumps frames, then compare stdout and the
# SHA-256 of one dumped frame with recorded values.
#   cmake -DEXE=<program> -DARGS="a;b" -DEXPECTED=<stdout file> -DFRAME_DIR=<dir>
#         -DFRAME=<file name> -DFRAME_HASH=<hash file> -P CheckFrame.cmake
file(REMOVE_RECURSE "${FRAME_DIR}")
execute_process(
    COMMAND "${EXE}" ${ARGS} --dump-frames "${FRAME_DIR}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${EXE} exited with ${rc}\n${err}")
endif()
file(READ "${EXPECTED}" expected)
# Compare lines, not line endings: a Windows checkout can give EXPECTED CRLF.
string(REPLACE "\r\n" "\n" out "${out}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT out STREQUAL expected)
    message(FATAL_ERROR "stdout differs\n--- expected ---\n${expected}\n--- actual ---\n${out}")
endif()
if(NOT EXISTS "${FRAME_DIR}/${FRAME}")
    message(FATAL_ERROR "no frame ${FRAME} was dumped\n${err}")
endif()
file(SHA256 "${FRAME_DIR}/${FRAME}" actual)
file(STRINGS "${FRAME_HASH}" wanted LIMIT_COUNT 1)
if(NOT actual STREQUAL wanted)
    message(FATAL_ERROR "frame ${FRAME_DIR}/${FRAME} differs from the recorded image (sha256 ${actual}, expected ${wanted})")
endif()
