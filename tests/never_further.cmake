# SPDX-License-Identifier: GPL-3.0-or-later
#
# The criterion of ADR 0005 for one ps2autotests program: PS2Native is never
# further from the console than stock PCSX2. On no line of the program's test
# block may stock PCSX2 print what the console printed while PS2Native prints
# something else. Run by CTest after the two runs it compares, as
#   cmake -DCONSOLE=... -DREFERENCE=... -DSUBJECT=... -P never_further.cmake
#
# Variables:
#   CONSOLE    the program's output as recorded on a console
#   REFERENCE  what the program printed under stock PCSX2 (the guest.txt of
#              its stock-jit test)
#   SUBJECT    what it printed under PS2Native (the guest.txt of its
#              ps2n-static test)
#
# It passes when PS2Native printed exactly what stock PCSX2 or the console
# printed, when stock PCSX2 printed no test block at all, and otherwise when
# the comparison line by line finds no such line. When the outputs differ and
# do not have the same number of lines, they cannot be compared line by line:
# the test then fails, and somebody has to look.
#
# Like the runs it compares, it is reported as skipped without a BIOS.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/test_block.cmake")

foreach(required IN ITEMS CONSOLE REFERENCE SUBJECT)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "never_further.cmake: ${required} is not set")
	endif()
endforeach()

if("$ENV{PS2N_BIOS}" STREQUAL "")
	message("PS2N_SKIP: set the PS2N_BIOS environment variable to a PS2 BIOS image to run this test")
	return()
endif()

# Sets `result_var` to the test block of `file`, or to nothing when the file
# is missing or holds no block.
function(ps2n_block_of file result_var)
	set(${result_var} "" PARENT_SCOPE)
	if(EXISTS "${file}")
		file(READ "${file}" text)
		ps2n_test_block("${text}" block)
		set(${result_var} "${block}" PARENT_SCOPE)
	endif()
endfunction()

# Takes the first line off the text in `text_var` and puts it in `line_var`.
# Without CMake lists: a line may hold semicolons and brackets.
function(ps2n_pop_line text_var line_var)
	set(text "${${text_var}}")
	string(FIND "${text}" "\n" cut)
	if(cut LESS 0)
		set(${line_var} "${text}" PARENT_SCOPE)
		set(${text_var} "" PARENT_SCOPE)
		return()
	endif()
	string(SUBSTRING "${text}" 0 ${cut} line)
	math(EXPR cut "${cut} + 1")
	string(SUBSTRING "${text}" ${cut} -1 text)
	set(${line_var} "${line}" PARENT_SCOPE)
	set(${text_var} "${text}" PARENT_SCOPE)
endfunction()

function(ps2n_count_lines text result_var)
	string(REGEX MATCHALL "\n" newlines "${text}")
	list(LENGTH newlines count)
	set(${result_var} ${count} PARENT_SCOPE)
endfunction()

ps2n_block_of("${CONSOLE}" console)
ps2n_block_of("${REFERENCE}" reference)
ps2n_block_of("${SUBJECT}" subject)

if(console STREQUAL "")
	message(FATAL_ERROR "The console recording is missing or has no test block: ${CONSOLE}")
endif()
if(subject STREQUAL "")
	message(FATAL_ERROR "PS2Native's run printed no test block: ${SUBJECT}")
endif()
if(reference STREQUAL "")
	message("Stock PCSX2 printed no test block (${REFERENCE}): nothing to be further from")
	return()
endif()
if(subject STREQUAL reference)
	message("PS2Native printed what stock PCSX2 printed")
	return()
endif()
if(subject STREQUAL console)
	message("PS2Native printed what the console printed; stock PCSX2 did not")
	return()
endif()

ps2n_count_lines("${console}" console_lines)
ps2n_count_lines("${reference}" reference_lines)
ps2n_count_lines("${subject}" subject_lines)
if(NOT console_lines EQUAL reference_lines OR NOT console_lines EQUAL subject_lines)
	message(FATAL_ERROR "The three outputs differ and cannot be compared line by line: the console printed "
		"${console_lines} lines, stock PCSX2 ${reference_lines}, PS2Native ${subject_lines}. Compare them by hand:\n"
		"  ${CONSOLE}\n  ${REFERENCE}\n  ${SUBJECT}")
endif()

set(further 0)
set(closer 0)
set(line_number 0)
set(report "")
while(line_number LESS console_lines)
	math(EXPR line_number "${line_number} + 1")
	ps2n_pop_line(console console_line)
	ps2n_pop_line(reference reference_line)
	ps2n_pop_line(subject subject_line)

	if(reference_line STREQUAL console_line AND NOT subject_line STREQUAL console_line)
		math(EXPR further "${further} + 1")
		if(further LESS_EQUAL 5)
			string(APPEND report "  line ${line_number}: the console and stock PCSX2: ${console_line}\n"
				"  line ${line_number}: PS2Native:                   ${subject_line}\n")
		endif()
	elseif(subject_line STREQUAL console_line AND NOT reference_line STREQUAL console_line)
		math(EXPR closer "${closer} + 1")
	endif()
endwhile()

if(further GREATER 0)
	message("${report}")
	message(FATAL_ERROR "PS2Native is further from the console than stock PCSX2 on ${further} lines of the test block "
		"(and closer on ${closer}). The outputs:\n  ${CONSOLE}\n  ${REFERENCE}\n  ${SUBJECT}")
endif()

message("On no line is PS2Native further from the console than stock PCSX2; on ${closer} lines it is closer")
