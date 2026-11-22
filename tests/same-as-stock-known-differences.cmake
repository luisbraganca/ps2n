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

# Programs whose run does not repeat from one run to the next when the runtime
# is built with link-time optimization (PS2N_RUNTIME_LTO, cmake/ps2n-inject.cmake),
# under stock PCSX2 as under PS2Native. The comparisons of two runs of them
# (same-as-stock, lockstep, lockstep-no-stack) are then disabled, and CTest
# lists them as not run; without link-time optimization they run and pass.
# Found on 2026-11-21 (docs/app-log.md, "The runtime itself").
#
# dma/vif/stcycl: a defect of PCSX2's (v2.9.170, x86). An UNPACK with masks
# that would run past the end of a VU's memory is carried out by PCSX2's
# interpreted unpack routines (dVifUnpack, pcsx2/x86/Vif_Dynarec.cpp, falls
# back to them). Their masked write (VifUnpackSSE_Simple::doMaskWrite,
# pcsx2/x86/Vif_UnpackSSE.cpp) loads the address of nVifMask and counts the
# places of its three tables from nVifMask[2]: it takes the data mask and the
# write-protect mask from the 128 bytes before nVifMask, and the row and column
# values from the data mask's table. (PCSX2's arm64 routine counts from
# nVifMask itself.) What those 128 bytes hold is the linker's to say. Without
# link-time optimization it is the end of nVifUpk, pointers to the unpack
# routines, whose low bits are the same on every run: the program prints the
# same wrong values every time, and the comparisons pass. With it, it is the
# end of nVif, pointers into the heap, whose low bits are not: the program's
# VIF0 cases with a CL of 253 to 255 come out one of two ways, under stock
# PCSX2 too, and a comparison of two runs fails whenever its two runs differ.
# The program's output is listed as differing from the console's in every
# configuration for these cases (tests/ps2autotests-known-differences.cmake).
# The maintainer told PCSX2 of it on 2026-11-22 (PCSX2's issue 15223): when
# the PCSX2 pinned here has it corrected, the program comes off this list.
set(PS2AUTOTESTS_UNSTEADY_WITH_RUNTIME_LTO
	dma/vif/stcycl
)
