# SPDX-License-Identifier: GPL-3.0-or-later
#
# Programs whose compiled run is not the run of stock PCSX2 with its
# recompilers, sync point by sync point (the <suite>/same-as-stock/* tests,
# ADR 0011). These tests still run; they are expected to fail by parting from
# the stock run, and CTest reports it when one starts to pass, or fails in
# another way (tests/tests.cmake, ps2n_expect_failure).
#
# Time is counted as PCSX2's recompiler counts it, so a run only parts from
# the stock run where something is computed differently on purpose (ADR 0005:
# the console is followed where a recording shows the recompiler wrong), or
# where the stock run itself goes wrong. Each entry says which, and where the
# runs were seen to part. Do not add a name to make a failure go away: find
# out why it differs first.
#
# Found on 2026-11-11 with PCSX2 v2.9.170 and BIOS SCPH-70004 v2.00: 59 of the
# 63 programs are the same as stock at every sync point.
#
# Variables are named <SUITE>_DIFFERS_same-as-stock.

set(PS2AUTOTESTS_DIFFERS_same-as-stock
	# LWR sign-extends here as on the console, where PCSX2's recompiler leaves
	# the upper half zero (ops_int.h). The program prints what it computed:
	# 4 of its 171 lines differ from stock's, and the runs part at sync point
	# 33,024.
	cpu/ee/lsu

	# ABS.S, NEG.S, MAX.S and MIN.S are the console's here (ops_fpu.h). 48 of
	# the program's 293 lines differ from stock's; the FPU registers differ
	# from sync point 25,857 and the runs part at 26,066.
	cpu/ee_fpu/arithmetic

	# Stock PCSX2 does not get through this program: its recompiler crashes on
	# an FPU branch in an FPU branch's delay slot (endless recursion in recBC1T,
	# x86/iFPU.cpp), so there is no whole stock run to compare with.
	cpu/ee_fpu/branchdelay

	# CTC2 to FBRST reads back here as on the console (ops_cop2.h). 1 of the
	# program's 36 lines differs from stock's, and the runs part at sync point
	# 26,961.
	cpu/vu0_macro/transfer
)

set(SDK_DIFFERS_same-as-stock)
set(PROGRAMS_DIFFERS_same-as-stock)
