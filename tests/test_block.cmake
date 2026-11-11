# SPDX-License-Identifier: GPL-3.0-or-later
#
# What a ps2autotests program printed as its test: the text from the line
# "-- TEST BEGIN" to the line "-- TEST END", both included. Shared by
# guest_test.cmake and never_further.cmake, so that both compare the same
# text.

# Sets `result_var` to the test block of `text`, with Unix line endings and a
# final newline, or to nothing when `text` has no such block.
function(ps2n_test_block text result_var)
	string(REPLACE "\r\n" "\n" text "${text}")
	string(FIND "${text}" "-- TEST BEGIN" begin)
	string(FIND "${text}" "-- TEST END" end)
	if(begin LESS 0 OR end LESS begin)
		set(${result_var} "" PARENT_SCOPE)
		return()
	endif()
	math(EXPR length "${end} - ${begin} + 11")
	string(SUBSTRING "${text}" ${begin} ${length} block)
	set(${result_var} "${block}\n" PARENT_SCOPE)
endfunction()
