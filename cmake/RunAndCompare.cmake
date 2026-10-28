# ctest helper: run a program and compare its stdout with an expected file.
#   cmake -DEXE=<program> -DARGS="a;b" -DEXPECTED=<file> [-DWORKDIR=<dir>] [-DCLEAN=<dir>] -P RunAndCompare.cmake
# CLEAN is deleted first (state the program writes, such as memory cards).
if(NOT WORKDIR)
    get_filename_component(WORKDIR "${EXE}" DIRECTORY)
endif()
if(CLEAN)
    file(REMOVE_RECURSE "${CLEAN}")
endif()
execute_process(
    COMMAND "${EXE}" ${ARGS}
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
file(READ "${EXPECTED}" expected)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${EXE} exited with ${rc}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()
# Compare lines, not line endings: a Windows checkout can give EXPECTED CRLF.
string(REPLACE "\r\n" "\n" out "${out}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT out STREQUAL expected)
    file(WRITE "${WORKDIR}/actual_output.txt" "${out}")
    message(FATAL_ERROR "output differs from ${EXPECTED}\n--- expected ---\n${expected}\n--- actual ---\n${out}\n--- stderr ---\n${err}")
endif()
