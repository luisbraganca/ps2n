# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps2autotests programs whose output differs from the console recording, per
# CPU configuration. These tests still run; CTest expects them to fail and
# reports it when one starts to match.
#
# Each list is what a run found, not what was assumed. docs/poc-log.md records
# the run each list came from. Do not add a name to make a failure go away:
# find out why it differs first.
#
# Variables are named PS2AUTOTESTS_DIFFERS_<configuration>.

# Stock PCSX2 v2.9.170 with its recompilers, BIOS SCPH-70004 v2.00, 2026-11-10:
# 39 of 55 programs match the console.
set(PS2AUTOTESTS_DIFFERS_stock-jit
	cpu/ee/branchdelay          # a branch in a branch's delay slot
	cpu/ee/lsu                  # LWR leaves the upper half zero where the console sign-extends
	cpu/ee_cop0/performance     # performance counter control register bits
	cpu/ee_fpu/arithmetic       # the console keeps NaN operands; PCSX2 clamps them to the largest float
	cpu/ee_fpu/branchdelay      # the recompiler crashes: endless recursion in recBC1T (x86/iFPU.cpp)
	cpu/ee_fpu/compare          # comparisons of NaN operands
	cpu/ee_fpu/fcr              # FPU control registers
	cpu/ee_fpu/muldiv           # 0/0 gives NaN on the console, the largest float here
	cpu/ee_fpu/sqrt             # RSQRT of zero, as above
	cpu/vu0_macro/transfer      # CTC2 writes to masked VU control registers
	dma/vif/intstall            # VIF interrupt stall
	dma/vif/stcycl              # VIF STCYCL with CL greater than WL
	dma/vif/unpack              # UNPACK with no data
	vu/games/triace             # one result differs in its last bit
	vu/lower/efu                # EATAN
	vu/lower/integer            # a VU0 micro program starts without the current status flags: the bug in
	                            # vu0DenormalizeMicroStatus described at this program's entry further down
)

# Stock PCSX2 v2.9.170 with its interpreters, same BIOS and date:
# 38 of 55 programs match the console.
#
# Against the recompilers: cpu/ee/lsu and vu/lower/integer match the console
# here, and cpu/ee_fpu/transfer, cpu/ee_simd/funnel and cpu/ee_simd/muldiv do
# not. PCSX2's two implementations disagree on five programs.
set(PS2AUTOTESTS_DIFFERS_stock-interp
	cpu/ee/branchdelay
	cpu/ee_cop0/performance
	cpu/ee_fpu/arithmetic
	cpu/ee_fpu/branchdelay      # no crash here; the output differs
	cpu/ee_fpu/compare
	cpu/ee_fpu/fcr
	cpu/ee_fpu/muldiv
	cpu/ee_fpu/sqrt
	cpu/ee_fpu/transfer         # CFC1 of the control register
	cpu/ee_simd/funnel          # MFSA
	cpu/ee_simd/muldiv          # PMADDW with -2147483648 * -1
	cpu/vu0_macro/transfer
	dma/vif/intstall
	dma/vif/stcycl
	dma/vif/unpack
	vu/games/triace
	vu/lower/efu
)

# PS2Native's EE provider, with the IOP and the VUs on PCSX2's recompilers,
# same BIOS and date. Stage: the provider carries out the integer
# instructions, loads and stores, the FPU and the multimedia instructions
# itself; the COP0 and VU0 macro instructions call PCSX2's interpreter
# functions.
# Since 2026-11-11 CFC2 and CTC2 are PS2Native's own as well (ops_cop2.h).
# 40 of 55 programs match the console: more than either stock configuration.
#
# Against stock-jit (the output differs on four of the 54 programs it
# completes):
#  - cpu/ee/lsu matches the console here and not there;
#  - cpu/ee_fpu/arithmetic differs from the console on fewer lines here:
#    ABS.S, NEG.S, MAX.S and MIN.S are the console's, the rest is identical
#    to stock-jit's;
#  - cpu/vu0_macro/transfer differs on 7 lines here and 8 there: FBRST reads
#    back as on the console here;
#  - cpu/ee/branchdelay differs from the console in both, in different ways.
# The other FPU programs (compare, fcr, muldiv, sqrt) print exactly what
# stock-jit prints, and so do the programs below marked "the core": what
# differs there is PCSX2's hardware model or its VU recompiler, which run
# them here as well.
#
# ADR 0005: on no line of any of these is this further from the console than
# stock-jit. The ps2autotests/never-further/* tests check that.
set(PS2AUTOTESTS_DIFFERS_ps2n-ee
	cpu/ee/branchdelay          # a branch in a branch's delay slot; not what the console does either
	cpu/ee_cop0/performance     # as stock-jit: performance counter control register bits (COP0 is PCSX2's functions)
	cpu/ee_fpu/arithmetic       # ADD.S and SUB.S of values the host takes for NaN or infinity, as stock-jit
	cpu/ee_fpu/branchdelay      # an FPU branch in an FPU branch's delay slot; stock-jit crashes on it
	cpu/ee_fpu/compare          # as stock-jit: comparisons of NaN operands
	cpu/ee_fpu/fcr              # as stock-jit: FPU control registers
	cpu/ee_fpu/muldiv           # as stock-jit: 0/0 and operands the host takes for NaN or infinity
	cpu/ee_fpu/sqrt             # as stock-jit: RSQRT of zero, and of such operands
	cpu/vu0_macro/transfer      # CTC2 to registers that neither PCSX2 implementation masks as the console does
	dma/vif/intstall            # the core: VIF interrupt stall
	dma/vif/stcycl              # the core: VIF STCYCL with CL greater than WL
	dma/vif/unpack              # the core: UNPACK with no data
	vu/games/triace             # the core: one result of the VU recompiler differs in its last bit
	vu/lower/efu                # the core: EATAN and other EFU results of the VU recompiler
	# As stock-jit, one line. Cause, confirmed by experiment on 2026-11-11: PCSX2's
	# vu0DenormalizeMicroStatus (pcsx2/VU0micro.cpp) shifts right by 11 and 14
	# where mVUallocSFLAGd, which it was copied from, shifts left, so a VU0 micro
	# program starts without the current status flags. CTC2 leaves those flags
	# in the register, as on the console (cpu/vu0_macro/transfer), so the EE
	# and the micro program then read different values. This program matched
	# while CTC2 was PCSX2's interpreter function, which overwrites all 32 bits
	# and so hid the core's bug.
	vu/lower/integer
)

# The same provider running each program's code compiled ahead of time. A
# compiled run has to be the interpreted run exactly (the lockstep tests hold
# it to that), so the list is ps2n-ee's.
set(PS2AUTOTESTS_DIFFERS_ps2n-static ${PS2AUTOTESTS_DIFFERS_ps2n-ee})
