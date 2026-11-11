# SPDX-License-Identifier: GPL-3.0-or-later
#
# PS2Native's own test programs (tests/programs) whose output differs from
# their expected output under a CPU configuration. These tests still run;
# CTest expects them to fail and reports it when one starts to match.
#
# Variables are named PROGRAMS_DIFFERS_<configuration>.

# Stock PCSX2 v2.9.170 with its interpreters.
set(PROGRAMS_DIFFERS_stock-interp
	timeout     # only PCSX2's recompiler skips timeout loops; its interpreter runs the loop at what it costs
)
