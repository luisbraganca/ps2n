# SPDX-License-Identifier: GPL-3.0-or-later
#
# The runtime's reading of a disc through PCSX2's readers (host/disc.h; step
# P4.8, docs/adr/0036): a small disc of our own (tests/disc/make_images.cpp)
# as an .iso, a raw .bin and a .cso, each listed and its boot executable
# extracted, the same from each; files named on the disc extracted, one in a
# folder; a file that is no disc refused, with why.
#
#   cmake -DHOST=<ps2n-host> -DMAKER=<ps2n-disc-images> -DWORK_DIR=<dir> [-DDLL_DIR=<dir>] -P disc_test.cmake

if(DEFINED DLL_DIR AND NOT "${DLL_DIR}" STREQUAL "")
	file(TO_NATIVE_PATH "${DLL_DIR}" dll_dir_native)
	if(WIN32)
		set(ENV{PATH} "${dll_dir_native};$ENV{PATH}")
	else()
		set(ENV{LD_LIBRARY_PATH} "${dll_dir_native}:$ENV{LD_LIBRARY_PATH}")
	endif()
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(COMMAND "${MAKER}" "${WORK_DIR}" RESULT_VARIABLE made)
if(NOT made EQUAL 0)
	message(FATAL_ERROR "the disc images could not be written (${made})")
endif()
file(READ "${WORK_DIR}/boot.expected" expected HEX)

foreach(format IN ITEMS iso bin cso)
	set(image "${WORK_DIR}/disc.${format}")
	execute_process(COMMAND "${HOST}" -disc-executables "${image}"
		RESULT_VARIABLE code OUTPUT_VARIABLE listed ERROR_VARIABLE said)
	string(REPLACE "\r\n" "\n" listed "${listed}")
	if(NOT code EQUAL 0 OR NOT listed STREQUAL "boot SLES_000.00\nloads SUB/GAME.ELF\n")
		message(FATAL_ERROR "disc.${format}: -disc-executables exited ${code} and printed:\n${listed}${said}")
	endif()
	execute_process(COMMAND "${HOST}" -disc-extract "${image}" "${WORK_DIR}/${format}"
		RESULT_VARIABLE code OUTPUT_VARIABLE written ERROR_VARIABLE said)
	if(NOT code EQUAL 0 OR NOT EXISTS "${WORK_DIR}/${format}/SLES_000.00")
		message(FATAL_ERROR "disc.${format}: -disc-extract exited ${code}:\n${written}${said}")
	endif()
	file(READ "${WORK_DIR}/${format}/SLES_000.00" got HEX)
	if(NOT got STREQUAL expected)
		message(FATAL_ERROR "disc.${format}: the boot executable extracted is not the disc's")
	endif()
	message(STATUS "disc.${format}: its executables listed, the boot executable extracted")
endforeach()

# Files named, one in a folder, from the .cso (its boot executable's block is kept plain, the others stored).
execute_process(COMMAND "${HOST}" -disc-extract "${WORK_DIR}/disc.cso" "${WORK_DIR}/named" SUB/GAME.ELF sles_000.00 SUB/BIG.BIN
	RESULT_VARIABLE code OUTPUT_VARIABLE written ERROR_VARIABLE said)
if(NOT code EQUAL 0)
	message(FATAL_ERROR "-disc-extract of named files exited ${code}:\n${written}${said}")
endif()
foreach(file IN ITEMS GAME.ELF SLES_000.00 BIG.BIN)
	if(NOT EXISTS "${WORK_DIR}/named/${file}")
		message(FATAL_ERROR "-disc-extract of named files wrote no ${file}:\n${written}${said}")
	endif()
endforeach()
file(SIZE "${WORK_DIR}/named/BIG.BIN" big)
if(NOT big EQUAL 2058)
	message(FATAL_ERROR "BIG.BIN, over two sectors, came out ${big} bytes, not 2058")
endif()

# A file the disc has not got, and a file that is no disc.
execute_process(COMMAND "${HOST}" -disc-extract "${WORK_DIR}/disc.iso" "${WORK_DIR}/none" NOPE.ELF
	RESULT_VARIABLE code OUTPUT_VARIABLE written ERROR_VARIABLE said)
if(code EQUAL 0 OR NOT said MATCHES "the disc has no file NOPE.ELF")
	message(FATAL_ERROR "a file the disc has not got: exited ${code}:\n${written}${said}")
endif()
execute_process(COMMAND "${HOST}" -disc-executables "${WORK_DIR}/boot.expected"
	RESULT_VARIABLE code OUTPUT_VARIABLE listed ERROR_VARIABLE said)
if(code EQUAL 0 OR NOT said MATCHES "cannot read the disc image")
	message(FATAL_ERROR "a file that is no disc: exited ${code}:\n${listed}${said}")
endif()
message(STATUS "a file the disc has not got, and one that is no disc: refused")
