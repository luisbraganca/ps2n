// SPDX-License-Identifier: GPL-3.0-or-later
//
// Self-test: PS2Native's decoder, cycle table and instruction semantics
// against the PCSX2 core.
//
// Two oracles carry out the same instruction on the same registers and
// memory as PS2Native's own function:
//
//  - PCSX2's interpreter function for the instruction;
//  - PCSX2's EE recompiler, made to compile and run a block holding just that
//    instruction. This is the one that matters most: games are tested against
//    the recompiler, and its behaviour is written nowhere but in the code it
//    generates.
//
// Where PS2Native deliberately differs from one of them (the ops_*.h headers
// say where and why), the instruction or the case is listed below and checked
// against the other.

#include "selftest.h"

#include "runtime/ee_provider.h"

#include "ps2n/r5900.h"
#include "ps2n/rec_rules.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/R5900OpcodeTables.h"
#include "pcsx2/VU.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace r5900 = ps2n::r5900;
using r5900::Op;

namespace
{
	// Guest addresses used by the tests, in kseg0, which is mapped to RAM from
	// reset. Nothing has run yet, so this memory is free.
	constexpr u32 kCodeAddr = 0x80100000u; // the instruction under test, for the recompiler
	constexpr u32 kIdleAddr = 0x80100040u; // where its block jumps to
	constexpr u32 kScratchAddr = 0x80200000u; // what loads and stores touch
	constexpr u32 kScratchSize = 256;

	struct Rng
	{
		u64 state;

		u64 Next()
		{
			// xorshift64*
			state ^= state >> 12;
			state ^= state << 25;
			state ^= state >> 27;
			return state * 0x2545F4914F6CDD1Dull;
		}
		u32 Next32() { return static_cast<u32>(Next() >> 32); }
		u32 Below(u32 n) { return Next32() % n; }

		// Values that exercise carries, sign extension and saturation.
		u32 Interesting32()
		{
			switch (Below(10))
			{
				case 0: return 0;
				case 1: return 1;
				case 2: return 0xFFFFFFFFu;
				case 3: return 0x80000000u;
				case 4: return 0x7FFFFFFFu;
				case 5: return Below(64);
				case 6: return 0u - Below(64);
				default: return Next32();
			}
		}

		GPR_reg Gpr()
		{
			GPR_reg r;
			r.UL[0] = Interesting32();
			// Compiled code keeps 32-bit values sign-extended; other upper halves
			// must work as well.
			r.UL[1] = Below(2) ? (static_cast<s32>(r.UL[0]) < 0 ? 0xFFFFFFFFu : 0u) : Interesting32();
			r.UL[2] = Interesting32();
			r.UL[3] = Interesting32();
			return r;
		}

		// The bits of a float: every class the EE and the host treat
		// differently, and ordinary values of every magnitude.
		u32 Float()
		{
			const u32 sign = Below(2) << 31;
			switch (Below(16))
			{
				case 0: return sign; // zero
				case 1: return sign | 0x3F800000u; // one
				case 2: return sign | (1 + Below(0x7FFFFF)); // denormal
				case 3: return sign | 0x00800000u | Below(4); // the smallest normals
				case 4: return sign | 0x7F7FFFFFu - Below(4); // the largest floats
				case 5: return sign | 0x7F800000u; // an infinity to the host
				case 6: return sign | 0x7F800000u | (1 + Below(0x7FFFFF)); // a NaN to the host
				case 7: return sign | 0x7FFFFFFFu;
				case 8: return sign | 0x4F000000u - Below(3) + 1; // around 2^31, where conversion saturates
				case 9: return sign | ((0x7F + Below(40) - 20) << 23) | (Below(2) ? 0 : Below(0x800000)); // near one
				case 10: return sign | ((0x7F + Below(60) - 30) << 23) | (Next32() & 0x7FFFFF & ~((1u << Below(23)) - 1)); // short mantissas
				case 11: return sign | (Below(0xFF) << 23) | 0x7FFFFF; // all mantissa bits, to round
				default: return Next32();
			}
		}
	};

	// The registers and memory an instruction of the families tested so far
	// can read or write.
	struct State
	{
		GPR_reg gpr[32];
		GPR_reg hi, lo;
		u32 sa;
		u32 fpr[32];
		u32 acc;
		u32 fcr31;
		u32 vi[32]; // VU0's integer registers (0 to 15) and control registers (16 to 31)
		u32 vf[32][4]; // VU0's floating-point registers: x, y, z, w. The first is constant
		u32 vu_acc[4]; // VU0's accumulator
		u32 clip_copy; // the copy of the clip flag that PCSX2's VU interpreter works on
		u32 micro_status[4]; // the copies of the status flags that PCSX2's VU recompiler works on
		u32 micro0; // the first word of VU0's micro memory, where the I-bit game fix finds VIADDI's immediate
		u8 vu0_mem[0x1000]; // VU0's data memory
		u8 vu1_regs[1024]; // VU1's floating-point and integer registers, which VU0 reaches from address 0x400
		// COP0. The count register is kept as what a read of it would have given
		// at the moment the state was put into the machine: the core brings the
		// register itself up to date only now and then.
		u32 cp0[32];
		u32 count_age; // cycles since the count register was last brought up to date
		u32 pccr, pcr0, pcr1; // the performance counters: control register, the two counters
		u8 scratch[kScratchSize];
	};

	// The cycle count when a state was last put into the machine.
	u64 s_apply_cycle = 0;

	void Apply(const State& s)
	{
		std::memcpy(cpuRegs.GPR.r, s.gpr, sizeof(s.gpr));
		cpuRegs.HI = s.hi;
		cpuRegs.LO = s.lo;
		cpuRegs.sa = s.sa;
		for (u32 i = 0; i < 32; i++)
			fpuRegs.fpr[i].UL = s.fpr[i];
		fpuRegs.ACC.UL = s.acc;
		fpuRegs.fprc[31] = s.fcr31;
		for (u32 i = 0; i < 32; i++)
			VU0.VI[i].UL = s.vi[i];
		for (u32 i = 0; i < 32; i++)
			std::memcpy(VU0.VF[i].UL, s.vf[i], sizeof(s.vf[i]));
		std::memcpy(VU0.ACC.UL, s.vu_acc, sizeof(s.vu_acc));
		VU0.clipflag = s.clip_copy;
		std::memcpy(VU0.micro_statusflags, s.micro_status, sizeof(s.micro_status));
		std::memcpy(VU0.Micro, &s.micro0, sizeof(s.micro0));
		std::memcpy(VU0.Mem, s.vu0_mem, sizeof(s.vu0_mem));
		static_assert(sizeof(VU1.VF) + sizeof(VU1.VI) == sizeof(s.vu1_regs));
		std::memcpy(VU1.VF, s.vu1_regs, sizeof(VU1.VF));
		std::memcpy(VU1.VI, s.vu1_regs + sizeof(VU1.VF), sizeof(VU1.VI));
		std::memcpy(cpuRegs.CP0.r, s.cp0, sizeof(s.cp0));
		s_apply_cycle = cpuRegs.cycle;
		cpuRegs.lastCOP0Cycle = cpuRegs.cycle - s.count_age;
		cpuRegs.CP0.n.Count = s.cp0[9] - s.count_age;
		cpuRegs.PERF.n.pccr.val = s.pccr;
		cpuRegs.PERF.n.pcr0 = s.pcr0;
		cpuRegs.PERF.n.pcr1 = s.pcr1;
		cpuRegs.lastPERFCycle[0] = cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
		std::memcpy(PSM(kScratchAddr), s.scratch, kScratchSize);
	}

	void Capture(State* s)
	{
		std::memcpy(s->gpr, cpuRegs.GPR.r, sizeof(s->gpr));
		s->hi = cpuRegs.HI;
		s->lo = cpuRegs.LO;
		s->sa = cpuRegs.sa;
		for (u32 i = 0; i < 32; i++)
			s->fpr[i] = fpuRegs.fpr[i].UL;
		s->acc = fpuRegs.ACC.UL;
		s->fcr31 = fpuRegs.fprc[31];
		for (u32 i = 0; i < 32; i++)
			s->vi[i] = VU0.VI[i].UL;
		for (u32 i = 0; i < 32; i++)
			std::memcpy(s->vf[i], VU0.VF[i].UL, sizeof(s->vf[i]));
		std::memcpy(s->vu_acc, VU0.ACC.UL, sizeof(s->vu_acc));
		s->clip_copy = VU0.clipflag;
		std::memcpy(s->micro_status, VU0.micro_statusflags, sizeof(s->micro_status));
		std::memcpy(&s->micro0, VU0.Micro, sizeof(s->micro0));
		std::memcpy(s->vu0_mem, VU0.Mem, sizeof(s->vu0_mem));
		std::memcpy(s->vu1_regs, VU1.VF, sizeof(VU1.VF));
		std::memcpy(s->vu1_regs + sizeof(VU1.VF), VU1.VI, sizeof(VU1.VI));
		std::memcpy(s->cp0, cpuRegs.CP0.r, sizeof(s->cp0));
		// What a read would have given when the state was put in: the register
		// counts cycles, and each oracle has let a different number pass since.
		s->cp0[9] = cpuRegs.CP0.n.Count - static_cast<u32>(cpuRegs.lastCOP0Cycle - s_apply_cycle);
		s->count_age = 0;
		s->pccr = cpuRegs.PERF.n.pccr.val;
		s->pcr0 = cpuRegs.PERF.n.pcr0;
		s->pcr1 = cpuRegs.PERF.n.pcr1;
		std::memcpy(s->scratch, PSM(kScratchAddr), kScratchSize);
	}

	std::string Hex128(const GPR_reg& r)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%08x_%08x_%08x_%08x", r.UL[3], r.UL[2], r.UL[1], r.UL[0]);
		return buf;
	}

	bool IsNaN(u32 bits)
	{
		return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
	}

	// What a comparison leaves out.
	struct Tolerance
	{
		u32 fcr31_ignored = 0; // bits of FCR31 that are not compared
		bool any_nan_equal = false; // two NaNs are the same FPU result whatever their sign and payload
		bool clip_copy_ignored = false; // PCSX2's VU interpreter's copy of the clip flag is not compared
		bool micro_status_ignored = false; // PCSX2's VU recompiler's copies of the status flags are not compared
	};

	// Describes how `got` differs from `want`, or returns an empty string.
	std::string Difference(const State& want, const State& got, const Tolerance& tolerance)
	{
		std::string out;
		char buf[192];
		const bool any_nan_equal = tolerance.any_nan_equal;
		const u32 fcr31_ignored = tolerance.fcr31_ignored;
		const auto same_float = [any_nan_equal](u32 a, u32 b) { return a == b || (any_nan_equal && IsNaN(a) && IsNaN(b)); };
		for (u32 i = 0; i < 32; i++)
		{
			if (std::memcmp(&want.gpr[i], &got.gpr[i], sizeof(GPR_reg)) != 0)
				out += "      $" + std::string(r5900::gprName(i)) + ": oracle " + Hex128(want.gpr[i]) + ", PS2Native " + Hex128(got.gpr[i]) + "\n";
		}
		if (std::memcmp(&want.hi, &got.hi, sizeof(GPR_reg)) != 0)
			out += "      HI: oracle " + Hex128(want.hi) + ", PS2Native " + Hex128(got.hi) + "\n";
		if (std::memcmp(&want.lo, &got.lo, sizeof(GPR_reg)) != 0)
			out += "      LO: oracle " + Hex128(want.lo) + ", PS2Native " + Hex128(got.lo) + "\n";
		if (want.sa != got.sa)
		{
			std::snprintf(buf, sizeof(buf), "      SA: oracle %08x, PS2Native %08x\n", want.sa, got.sa);
			out += buf;
		}
		for (u32 i = 0; i < 32; i++)
		{
			if (!same_float(want.fpr[i], got.fpr[i]))
			{
				std::snprintf(buf, sizeof(buf), "      $f%u: oracle %08x, PS2Native %08x\n", i, want.fpr[i], got.fpr[i]);
				out += buf;
			}
		}
		if (!same_float(want.acc, got.acc))
		{
			std::snprintf(buf, sizeof(buf), "      ACC: oracle %08x, PS2Native %08x\n", want.acc, got.acc);
			out += buf;
		}
		if ((want.fcr31 ^ got.fcr31) & ~fcr31_ignored)
		{
			std::snprintf(buf, sizeof(buf), "      FCR31: oracle %08x, PS2Native %08x\n", want.fcr31, got.fcr31);
			out += buf;
		}
		for (u32 i = 0; i < 32; i++)
		{
			if (want.vi[i] != got.vi[i])
			{
				std::snprintf(buf, sizeof(buf), "      $vi%u: oracle %08x, PS2Native %08x\n", i, want.vi[i], got.vi[i]);
				out += buf;
			}
		}
		for (u32 i = 0; i < 32; i++)
		{
			if (std::memcmp(want.vf[i], got.vf[i], sizeof(want.vf[i])) != 0)
			{
				std::snprintf(buf, sizeof(buf), "      $vf%u (x y z w): oracle %08x %08x %08x %08x, PS2Native %08x %08x %08x %08x\n", i, want.vf[i][0],
					want.vf[i][1], want.vf[i][2], want.vf[i][3], got.vf[i][0], got.vf[i][1], got.vf[i][2], got.vf[i][3]);
				out += buf;
			}
		}
		if (std::memcmp(want.vu_acc, got.vu_acc, sizeof(want.vu_acc)) != 0)
		{
			std::snprintf(buf, sizeof(buf), "      VU0's ACC (x y z w): oracle %08x %08x %08x %08x, PS2Native %08x %08x %08x %08x\n", want.vu_acc[0],
				want.vu_acc[1], want.vu_acc[2], want.vu_acc[3], got.vu_acc[0], got.vu_acc[1], got.vu_acc[2], got.vu_acc[3]);
			out += buf;
		}
		if (want.micro0 != got.micro0)
		{
			std::snprintf(buf, sizeof(buf), "      VU0's micro memory at 0: oracle %08x, PS2Native %08x\n", want.micro0, got.micro0);
			out += buf;
		}
		for (u32 i = 0; i < sizeof(want.vu0_mem); i++)
		{
			if (want.vu0_mem[i] != got.vu0_mem[i])
			{
				std::snprintf(buf, sizeof(buf), "      VU0's data memory at %03x: oracle %02x, PS2Native %02x (first differing byte)\n", i,
					want.vu0_mem[i], got.vu0_mem[i]);
				out += buf;
				break;
			}
		}
		for (u32 i = 0; i < sizeof(want.vu1_regs); i++)
		{
			if (want.vu1_regs[i] != got.vu1_regs[i])
			{
				std::snprintf(buf, sizeof(buf), "      VU1's registers at quadword %u, byte %u: oracle %02x, PS2Native %02x (first differing byte)\n",
					i / 16, i % 16, want.vu1_regs[i], got.vu1_regs[i]);
				out += buf;
				break;
			}
		}
		if (!tolerance.clip_copy_ignored && want.clip_copy != got.clip_copy)
		{
			std::snprintf(buf, sizeof(buf), "      the VU interpreter's clip flag: oracle %08x, PS2Native %08x\n", want.clip_copy,
				got.clip_copy);
			out += buf;
		}
		if (!tolerance.micro_status_ignored && std::memcmp(want.micro_status, got.micro_status, sizeof(want.micro_status)) != 0)
		{
			std::snprintf(buf, sizeof(buf), "      the VU recompiler's status flags: oracle %08x %08x %08x %08x, PS2Native %08x %08x %08x %08x\n",
				want.micro_status[0], want.micro_status[1], want.micro_status[2], want.micro_status[3], got.micro_status[0],
				got.micro_status[1], got.micro_status[2], got.micro_status[3]);
			out += buf;
		}
		for (u32 i = 0; i < 32; i++)
		{
			if (want.cp0[i] != got.cp0[i])
			{
				const char* name = r5900::cop0Name(i);
				std::snprintf(buf, sizeof(buf), "      COP0 register %u (%s): oracle %08x, PS2Native %08x\n", i, name ? name : "?", want.cp0[i],
					got.cp0[i]);
				out += buf;
			}
		}
		if (want.pccr != got.pccr || want.pcr0 != got.pcr0 || want.pcr1 != got.pcr1)
		{
			std::snprintf(buf, sizeof(buf), "      performance counters (control, 0, 1): oracle %08x %08x %08x, PS2Native %08x %08x %08x\n",
				want.pccr, want.pcr0, want.pcr1, got.pccr, got.pcr0, got.pcr1);
			out += buf;
		}
		for (u32 i = 0; i < kScratchSize; i++)
		{
			if (want.scratch[i] != got.scratch[i])
			{
				std::snprintf(buf, sizeof(buf), "      memory at %08x: oracle %02x, PS2Native %02x (first differing byte)\n",
					kScratchAddr + i, want.scratch[i], got.scratch[i]);
				out += buf;
				break;
			}
		}
		return out;
	}

	// The bits of an instruction word that are operands, from the operand
	// syntax of its row in r5900_ops.inc.
	u32 OperandMask(const r5900::OpInfo& oi)
	{
		u32 mask = 0;
		for (const char* p = oi.operands; *p; p++)
		{
			switch (*p)
			{
				case 'd': mask |= 0x0000F800u; break; // rd
				case 's': case 'b': mask |= 0x03E00000u; break; // rs
				case 't': mask |= 0x001F0000u; break; // rt
				case '<': mask |= 0x000007C0u; break; // sa
				case 'i': case 'u': case 'o': mask |= 0x0000FFFFu; break; // immediate
				case 'D': mask |= 0x000007C0u; break; // fd
				case 'S': case 'g': mask |= 0x0000F800u; break; // fs, or an FPU control register
				case 'T': mask |= 0x001F0000u; break; // ft
				case 'E': mask |= 0x0000F800u; break; // a VU0 integer or control register
				case 'X': mask |= 0x0000F800u; break; // a VU0 floating-point register, where rd is
				case 'Y': mask |= 0x001F0000u; break; // and where rt is
				case 'G': mask |= 0x0000F800u; break; // a COP0 register
				case 'r': mask |= 0x0000003Eu; break; // a performance counter
				case 'k': case 'h': mask |= 0x001F0000u; break; // the operation of CACHE, the hint of PREF
				case 'P': mask |= 0x000007C0u; break; // the kind of SYNC
				default: break; // punctuation; other families are not tested yet
			}
		}
		if (oi.flags & r5900::F_INTERLOCK)
			mask |= 1; // the interlock bit
		return mask;
	}

	// The VU0 instructions in EE code: COP2 with rs from 020 (ops_vu0.h), not
	// the transfers, branches, loads and stores.
	bool IsVu0(Op op)
	{
		return (r5900::info(op).match & 0xFE000000u) == 0x4A000000u;
	}

	// The operand bits of a VU0 instruction, whose operand letters mean other
	// fields than elsewhere: an `f` after X or Y is a single component.
	u32 Vu0OperandMask(const r5900::OpInfo& oi)
	{
		u32 mask = (oi.flags & r5900::F_VUDEST) ? 0x01E00000u : 0; // the component mask
		char previous = 0;
		for (const char* p = oi.operands; *p; previous = *p++)
		{
			switch (*p)
			{
				case 'V': case 'e': case '5': mask |= 0x000007C0u; break; // fd, id, the immediate of VIADDI
				case 'X': mask |= 0x0000F800u; break; // fs
				case 'Y': case 'n': mask |= 0x001F0000u; break; // ft, it
				case 'b':
					if (previous == 'Y')
						mask |= 0x00000003u; // the broadcast component
					break;
				case 'f':
					if (previous == 'X')
						mask |= 0x00600000u; // fsf
					else if (previous == 'Y')
						mask |= 0x01800000u; // ftf
					else
						mask |= 0x0000F800u; // is
					break;
				default: break; // A, Q, I, R, the w of VCLIPw, punctuation
			}
		}
		return mask;
	}

	bool IsFpu(Op op)
	{
		return (r5900::info(op).flags & r5900::F_FPU) != 0;
	}

	// The FPU instructions that work on the bits, where PS2Native follows the
	// console and PCSX2's interpreter rather than the recompiler (ops_fpu.h).
	bool IsFpuBitOperation(Op op)
	{
		return op == Op::ABS_S || op == Op::NEG_S || op == Op::MAX_S || op == Op::MIN_S;
	}

	bool IsUnalignedForm(Op op)
	{
		switch (op)
		{
			case Op::LWL: case Op::LWR: case Op::LDL: case Op::LDR:
			case Op::SWL: case Op::SWR: case Op::SDL: case Op::SDR:
			case Op::LQ: case Op::SQ: // these align the address themselves
				return true;
			default:
				return false;
		}
	}

	// Instructions whose PS2Native semantics deliberately differ from one
	// oracle. Each is then held to the other.
	bool DiffersFromInterpreter(Op op)
	{
		// The shift-amount register is four bits wide; PCSX2's interpreter keeps
		// all 32 (cpu/ee_simd/funnel on the console agrees with the recompiler).
		if (op == Op::MTSA)
			return true;

		// PCSX2's interpreter adds corrections to these two that the console
		// recording contradicts (cpu/ee_simd/muldiv, PMADDW of -2147483648 and
		// -1). Its recompiler computes the plain 64-bit result, as here.
		if (op == Op::PMADDW || op == Op::PMSUBW)
			return true;

		// PCSX2's interpreter works on a model of the processor's cache here,
		// and loads COP0's TagLo register from it. Its recompiler does nothing
		// (ops_cop0.h).
		if (op == Op::CACHE)
			return true;

		// VU0's instructions follow the recompiler (ops_vu0.h), whose flags,
		// clamps, special cases and integer registers PCSX2's interpreter
		// does differently.
		if (IsVu0(op))
			return true;

		// The FPU follows the recompiler, whose arithmetic, comparisons,
		// conversions and control registers PCSX2's interpreter does
		// differently. What is left to compare with the interpreter: the
		// operations on the bits, and the moves.
		if (IsFpu(op))
		{
			switch (op)
			{
				case Op::ABS_S: case Op::NEG_S: case Op::MAX_S: case Op::MIN_S:
				case Op::MOV_S: case Op::MFC1: case Op::MTC1: case Op::CTC1:
				case Op::LWC1: case Op::SWC1:
					return false;
				default:
					return true;
			}
		}
		return false;
	}

	bool DiffersFromRecompiler(Op op)
	{
		// PCSX2's recompiler clears bits 32-63 where the console and PCSX2's
		// interpreter keep them (cpu/ee/lsu).
		if (op == Op::LWR)
			return true;

		// It clamps these, and gives MAX.S and MIN.S of +0 and -0 a result that
		// depends on its register allocation (cpu/ee_fpu/arithmetic).
		return IsFpuBitOperation(op);
	}

	// Bits of FCR31 that are not compared for `op` against `oracle`.
	u32 Fcr31Ignored(Op op, bool against_interpreter)
	{
		// PCSX2's interpreter clears the overflow and underflow flags in these;
		// its recompiler, which PS2Native follows for the flags, never touches
		// those two.
		if (against_interpreter && IsFpuBitOperation(op))
			return 0x0000C000u;
		return 0;
	}

	enum class Oracle
	{
		Interpreter,
		Recompiler,
	};

	// Cases of an instruction that are not run against `oracle`: PS2Native
	// deliberately follows the other one there (ops_cop2.h says why), or the
	// case cannot be run here at all.
	bool LeftOut(Op op, Oracle oracle, u32 word)
	{
		const u32 fs = (word >> 11) & 31;

		if (op == Op::CTC2)
		{
			// Starts a VU1 micro program.
			if (fs == REG_CMSAR1)
				return true;
			// The integer registers are 16 bits wide and the status flag register
			// takes its sticky flags only, as in the recompiler and on the console.
			if (oracle == Oracle::Interpreter && fs >= 1 && fs <= REG_STATUS_FLAG)
				return true;
			// FBRST is stored before the resets it asks for, as in the interpreter
			// and on the console.
			if (oracle == Oracle::Recompiler && fs == REG_FBRST)
				return true;
		}

		// PCSX2's interpreter leaves bits 32 to 63 of the general register as
		// they were when R is read.
		if (op == Op::CFC2 && oracle == Oracle::Interpreter && fs == REG_R)
			return true;

		// COP0 (ops_cop0.h). `fs` is the number of the COP0 register here.
		// PCSX2's interpreter clears bits of the status register as it is read.
		if (op == Op::MFC0 && oracle == Oracle::Interpreter && fs == 12)
			return true;
		// Its interpreter never lets a read of the count register find it
		// unchanged, and does not carry the register's overflow into EntryHi as
		// its recompiler does.
		if (op == Op::MFC0 && oracle == Oracle::Interpreter && fs == 9)
			return true;

		return false;
	}

	// Runs `word` through PCSX2's EE recompiler: a block of this instruction
	// and a jump, entered and left once. `then_nop`: with a nop between the
	// two, for DI, which the recompiler lets take effect after the instruction
	// that follows it and not at all when that is a branch.
	void RunRecompiler(u32 word, bool then_nop)
	{
		u32 addr = kCodeAddr;
		memWrite32(addr, word);
		addr += 4;
		if (then_nop)
		{
			memWrite32(addr, 0);
			addr += 4;
		}
		memWrite32(addr, 0x08000000u | ((kIdleAddr & 0x0FFFFFFFu) >> 2)); // j kIdleAddr
		memWrite32(addr + 4, 0);
		memWrite32(kIdleAddr, 0x08000000u | ((kIdleAddr & 0x0FFFFFFFu) >> 2)); // j kIdleAddr
		memWrite32(kIdleAddr + 4, 0);
		recCpu.Clear(kCodeAddr, 0x20);

		cpuRegs.pc = kCodeAddr;
		cpuRegs.branch = 0;

		// Leave at the event test the block ends with.
		recCpu.ExitExecution();
		recCpu.Execute();
	}

	struct Tally
	{
		u32 run = 0;
		u32 failed = 0;
		u32 oracle_raised = 0; // the interpreter raised an exception PS2Native does not model
	};

	// Operands made for a case of an instruction, in place of random ones: for
	// a check that has to reach every case of something. Called with the
	// instruction's word and the random state, which it changes.
	using Shape = void (*)(u32 word, State* start, Rng& rng);

	// One instruction against one oracle. Returns the number of failures.
	Tally CheckOp(Op op, Oracle oracle, u32 trials, Rng& rng, Shape shape = nullptr)
	{
		Tally tally;
		const r5900::OpInfo& oi = r5900::info(op);
		const bool vu0_op = IsVu0(op);
		const u32 mask = vu0_op ? Vu0OperandMask(oi) : OperandMask(oi);
		const bool memory = (oi.flags & (r5900::F_LOAD | r5900::F_STORE)) != 0;
		const bool cop0_op = (oi.flags & r5900::F_COP0) != 0;

		// COP0 as it stands, to put back after every run: an instruction under
		// test may write the status or the configuration register.
		const CP0regs cp0 = cpuRegs.CP0;
		const PERFregs perf = cpuRegs.PERF;
		const auto restore_cop0 = [&cp0, &perf]() {
			cpuRegs.CP0 = cp0;
			cpuRegs.PERF = perf;
			cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
			cpuRegs.lastPERFCycle[0] = cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
		};

		// The VUs' memories as they stand, for the instructions that do not
		// touch them.
		State machine_vu;
		Capture(&machine_vu);

		for (u32 trial = 0; trial < trials; trial++)
		{
			u32 word = oi.match | (rng.Next32() & mask);
			if (memory && ((word >> 21) & 31) == 0)
				word |= (1 + rng.Below(31)) << 21; // a base register other than $zero
			if (r5900::decodeOp(word) != op)
				continue;

			// PCSX2's recompiler divides two constants at compile time, and
			// there it has a bug: the quotient is sign-extended over the
			// remainder ((u64)quot | ((u64)rem << 32), recDIVconst in
			// x86/ix86-32/iR5900MultDiv.cpp), so that a negative quotient
			// leaves -1 in HI. In a block of one instruction the only constants
			// are $zero and $zero, which gives a quotient of -1. Its
			// interpreter and PS2Native leave the dividend in HI.
			if (oracle == Oracle::Recompiler && (op == Op::DIV || op == Op::DIV1) && ((word >> 16) & 0x3FF) == 0)
				continue;

			// Another: for PMADDUW with $zero as an operand its recompiler does
			// not load the operands and then multiplies whatever the two host
			// registers hold (recPMADDUW in x86/iMMI.cpp). The product is zero
			// in its interpreter and here.
			if (oracle == Oracle::Recompiler && op == Op::PMADDUW && (((word >> 21) & 31) == 0 || ((word >> 16) & 31) == 0))
				continue;

			if (LeftOut(op, oracle, word))
				continue;

			State start;
			start.gpr[0] = {};
			for (u32 i = 1; i < 32; i++)
				start.gpr[i] = rng.Gpr();
			start.hi = rng.Gpr();
			start.lo = rng.Gpr();
			start.sa = rng.Below(16);
			for (u32& f : start.fpr)
				f = rng.Float();
			start.acc = rng.Float();
			start.fcr31 = rng.Below(4) ? rng.Next32() : 0;
			// VU0: sixteen 16-bit integer registers, the first always zero, and
			// sixteen control registers. Neither VU is running a micro program.
			start.vi[0] = 0;
			for (u32 i = 1; i < 16; i++)
				start.vi[i] = rng.Interesting32() & 0xFFFFu;
			for (u32 i = 16; i < 32; i++)
				start.vi[i] = rng.Interesting32();
			start.vi[REG_VPU_STAT] = 0;
			start.vi[REG_I] = rng.Float();
			start.vi[REG_Q] = rng.Float();
			// Its floating-point registers and accumulator: floats of every kind.
			// The first register stays what the machine has it as (0, 0, 0, 1).
			std::memcpy(start.vf[0], VU0.VF[0].UL, sizeof(start.vf[0]));
			for (u32 i = 1; i < 32; i++)
			{
				for (u32& component : start.vf[i])
					component = rng.Float();
			}
			for (u32& component : start.vu_acc)
				component = rng.Float();
			start.clip_copy = rng.Next32();
			for (u32& copy : start.micro_status)
				copy = rng.Next32();
			// VU0's data memory, and VU1's registers, which VU0's instructions
			// reach from 0x400 up: any bits, only moved. Neither VU is running.
			// For the other instructions, as the machine has them.
			if (vu0_op)
			{
				start.micro0 = rng.Next32();
				for (u32 i = 0; i < sizeof(start.vu0_mem); i += 8)
				{
					const u64 v = rng.Next();
					std::memcpy(&start.vu0_mem[i], &v, sizeof(v));
				}
				for (u32 i = 0; i < sizeof(start.vu1_regs); i += 8)
				{
					const u64 v = rng.Next();
					std::memcpy(&start.vu1_regs[i], &v, sizeof(v));
				}
			}
			else
			{
				start.micro0 = machine_vu.micro0;
				std::memcpy(start.vu0_mem, machine_vu.vu0_mem, sizeof(start.vu0_mem));
				std::memcpy(start.vu1_regs, machine_vu.vu1_regs, sizeof(start.vu1_regs));
			}
			for (u8& b : start.scratch)
				b = static_cast<u8>(rng.Next32());
			if (shape)
				shape(word, &start, rng);

			// COP0 as it stands, except for an instruction that reads or writes it.
			// Then every register holds something else, but nothing that would let
			// the recompiler's run take an interrupt at the event test its block
			// ends with: the status register keeps its low bits and gets the bits
			// the console does not implement; the configuration register stays; no
			// performance counter counts.
			std::memcpy(start.cp0, cp0.r, sizeof(start.cp0));
			start.count_age = 0;
			start.pccr = perf.n.pccr.val;
			start.pcr0 = perf.n.pcr0;
			start.pcr1 = perf.n.pcr1;
			if (cop0_op)
			{
				for (u32 i = 0; i < 32; i++)
				{
					if (i == 12)
						start.cp0[i] = ((cp0.r[i] & 0xF0C79C1Fu) | (rng.Next32() & ~0xF0C79C1Fu)) & ~0x8000u;
					else if (i != 16)
						start.cp0[i] = rng.Interesting32();
				}
				start.count_age = rng.Below(3);
				start.pccr = rng.Next32() & 0x7FFFFFFFu;
				start.pcr0 = rng.Interesting32();
				start.pcr1 = rng.Interesting32();

				const u32 rt = (word >> 16) & 31;
				const u32 rd = (word >> 11) & 31;
				// A value written to the status register leaves interrupts off.
				if (op == Op::MTC0 && rd == 12 && rt != 0)
					start.gpr[rt].UL[0] = (start.gpr[rt].UL[0] | 0x4u) & ~0x8000u;
				// One written to the performance control register leaves counting off.
				if (op == Op::MTPS && rt != 0)
					start.gpr[rt].UL[0] &= 0x7FFFFFFFu;
				// DI looks at the processor's mode and at EXL, ERL and EDI, and
				// clears EIE: any of those, which can only leave fewer interrupts
				// enabled than the status register as it stands does.
				if (op == Op::DI)
					start.cp0[12] = (start.cp0[12] & ~0x3001Eu) | (rng.Next32() & 0x3001Eu);
			}

			if (memory)
			{
				// Aim the access into the scratch memory. PCSX2's interpreter
				// raises an address error for a misaligned access, which neither
				// its recompiler nor PS2Native models, so against the interpreter
				// the address is aligned.
				u32 addr = kScratchAddr + 64 + rng.Below(96);
				if (oracle == Oracle::Interpreter && !IsUnalignedForm(op))
					addr &= ~(static_cast<u32>(oi.memSize) - 1);
				const u32 base = addr - static_cast<u32>(static_cast<s32>(static_cast<s16>(word)));
				start.gpr[(word >> 21) & 31].SD[0] = static_cast<s32>(base);
			}

			// PS2Native.
			State ours;
			Apply(start);
			if (oracle == Oracle::Recompiler && r5900::flushesCycles(r5900::Insn(word, kCodeAddr)))
			{
				// Before such an instruction PCSX2's recompiler adds the cycles of
				// the block so far to the cycle count, here those of a block of
				// this one instruction. PS2Native's function leaves that to its
				// caller (PS2N_FLUSH).
				const u32 eighths = r5900::cycles(op) * (2 - ((cpuRegs.CP0.n.Config >> 18) & 1));
				cpuRegs.cycle += ps2n::ee::blockCycles(eighths);
			}
			if (!ps2n::ee::ExecuteOwn(word, kCodeAddr))
			{
				std::fprintf(stderr, "  %s: PS2Native has no semantics for %08x\n", oi.mnemonic, word);
				tally.failed++;
				restore_cop0();
				return tally;
			}
			Capture(&ours);
			restore_cop0();

			// The oracle.
			State theirs;
			Apply(start);
			if (oracle == Oracle::Interpreter)
			{
				cpuRegs.code = word;
				cpuRegs.pc = kCodeAddr + 4;
				cpuRegs.branch = 0;
				R5900::GetInstruction(word).interpret();

				if (cpuRegs.pc != kCodeAddr + 4)
				{
					restore_cop0();
					tally.oracle_raised++;
					continue;
				}
			}
			else
			{
				RunRecompiler(word, op == Op::DI);
			}
			Capture(&theirs);
			restore_cop0();

			tally.run++;
			Tolerance tolerance;
			tolerance.fcr31_ignored = Fcr31Ignored(op, oracle == Oracle::Interpreter);
			// With nothing clamped, which of two NaN operands an operation returns
			// depends on the order the recompiler happened to give them.
			tolerance.any_nan_equal = (oracle == Oracle::Recompiler && !EmuConfig.Cpu.Recompiler.fpuOverflow);
			// Each PCSX2 implementation keeps a copy of some VU0 flags that the
			// other knows nothing of. PS2Native writes the recompiler's
			// (ops_cop2.h).
			tolerance.clip_copy_ignored = (oracle == Oracle::Interpreter);
			tolerance.micro_status_ignored = (oracle == Oracle::Interpreter);
			const std::string difference = Difference(theirs, ours, tolerance);
			if (!difference.empty())
			{
				if (tally.failed < 3)
				{
					std::fprintf(stderr, "  MISMATCH %s against PCSX2's %s: %08x  %s\n%s", oi.mnemonic,
						oracle == Oracle::Interpreter ? "interpreter" : "recompiler", word,
						r5900::disassemble(word, kCodeAddr).c_str(), difference.c_str());
					if (IsFpu(op))
					{
						std::fprintf(stderr, "      before: $f%u = %08x, $f%u = %08x, ACC = %08x, FCR31 = %08x\n", (word >> 11) & 31,
							start.fpr[(word >> 11) & 31], (word >> 16) & 31, start.fpr[(word >> 16) & 31], start.acc, start.fcr31);
					}
					if (op == Op::CFC2 || op == Op::CTC2)
					{
						std::fprintf(stderr, "      before: $vi%u = %08x, the VU interpreter's clip flag = %08x\n", (word >> 11) & 31,
							start.vi[(word >> 11) & 31], start.clip_copy);
					}
					if (vu0_op)
					{
						for (const u32 r : {(word >> 11) & 31, (word >> 16) & 31})
						{
							std::fprintf(stderr, "      before: $vf%u = %08x %08x %08x %08x, $vi%u = %08x\n", r, start.vf[r][0], start.vf[r][1],
								start.vf[r][2], start.vf[r][3], r & 15, start.vi[r & 15]);
						}
						std::fprintf(stderr,
							"      before: ACC = %08x %08x %08x %08x, I = %08x, Q = %08x, R = %08x, status = %08x, MAC = %08x, clip = %08x\n",
							start.vu_acc[0], start.vu_acc[1], start.vu_acc[2], start.vu_acc[3], start.vi[REG_I], start.vi[REG_Q], start.vi[REG_R],
							start.vi[REG_STATUS_FLAG], start.vi[REG_MAC_FLAG], start.vi[REG_CLIP_FLAG]);
					}
					for (u32 i = 0; i < 32; i++)
					{
						const bool used = i == ((word >> 21) & 31) || i == ((word >> 16) & 31);
						if (used)
							std::fprintf(stderr, "      before: $%s = %s\n", r5900::gprName(i), Hex128(start.gpr[i]).c_str());
					}
				}
				tally.failed++;
			}
		}

		return tally;
	}

	// The instructions PS2Native has semantics of its own for: the tables
	// ExecuteOwn() and ExecuteVu0() in runtime/ee_provider.cpp are built from.
	const Op kOwnOps[] = {
#define S(op, fn, args) Op::op,
#include "ps2n/r5900_sem_int.inc"
#include "ps2n/r5900_sem_cop1.inc"
#include "ps2n/r5900_sem_mmi.inc"
#include "ps2n/r5900_sem_cop2_ctl.inc"
#include "ps2n/r5900_sem_cop2.inc"
#include "ps2n/r5900_sem_cop0.inc"
#undef S
	};

	// Every instruction in `ops` that is to be compared with `oracle`, each on
	// `trials` cases. Returns the number of instructions that differ.
	int CheckOps(const std::vector<Op>& ops, Oracle oracle, u32 trials, const char* what, Rng& rng)
	{
		const char* name = (oracle == Oracle::Interpreter) ? "interpreter" : "recompiler";
		u32 count = 0, cases = 0, raised = 0, failed_ops = 0;
		std::string skipped;

		for (const Op op : ops)
		{
			if ((oracle == Oracle::Interpreter) ? DiffersFromInterpreter(op) : DiffersFromRecompiler(op))
			{
				skipped += std::string(skipped.empty() ? "" : ", ") + r5900::mnemonic(op);
				continue;
			}

			const Tally tally = CheckOp(op, oracle, trials, rng);
			count++;
			cases += tally.run;
			raised += tally.oracle_raised;
			if (tally.failed)
			{
				failed_ops++;
				std::fprintf(stderr, "  %s: %u of %u cases differ from PCSX2's %s\n", r5900::mnemonic(op), tally.failed, tally.run,
					name);
			}
			else if (tally.run == 0)
			{
				failed_ops++;
				std::fprintf(stderr, "  %s: no case could be run against PCSX2's %s\n", r5900::mnemonic(op), name);
			}
		}

		std::fprintf(stderr,
			"selftest: %s against PCSX2's %s: %u instructions, %u cases, %u instructions differ"
			" (%u cases left out: the oracle raised an exception; deliberately not compared: %s)\n",
			what, name, count, cases, failed_ops, raised, skipped.empty() ? "none" : skipped.c_str());
		return static_cast<int>(failed_ops);
	}

	// The FPU's additions keep one guard bit: of the operand with the smaller
	// exponent, they clear the bits past it, by masks chosen by the difference
	// of the two exponents, from -255 to 255 (kGuardMasks in ops_fpu.h).
	// Random operands seldom reach the far differences. Here every one of the
	// 511 is given to each instruction that adds, four times, against PCSX2's
	// recompiler: the next difference at each case, the two exponents placed
	// anywhere they fit, the mantissas and signs random. For the
	// multiply-adds fs is 1 or -1, so that the product is ft's value, give or
	// take its sign.
	int s_exponent_difference = -255;

	u32 FloatWithExponent(u32 exponent, Rng& rng)
	{
		return (rng.Below(2) << 31) | (exponent << 23) | (rng.Next32() & 0x7FFFFFu);
	}

	void ShapeExponentDifference(u32 word, State* start, Rng& rng)
	{
		const int diff = s_exponent_difference;
		s_exponent_difference = diff == 255 ? -255 : diff + 1;
		const u32 first = static_cast<u32>(diff > 0 ? diff : 0) + rng.Below(256 - static_cast<u32>(diff < 0 ? -diff : diff));
		const u32 second = static_cast<u32>(static_cast<int>(first) - diff);
		const u32 fs = (word >> 11) & 31, ft = (word >> 16) & 31;
		switch (r5900::decodeOp(word))
		{
			case Op::MADD_S: // the product, then the accumulator
				start->fpr[fs] = (rng.Below(2) << 31) | 0x3F800000u;
				start->fpr[ft] = FloatWithExponent(first, rng);
				start->acc = FloatWithExponent(second, rng);
				break;
			case Op::MSUB_S: case Op::MADDA_S: case Op::MSUBA_S: // the accumulator, then the product
				start->fpr[fs] = (rng.Below(2) << 31) | 0x3F800000u;
				start->fpr[ft] = FloatWithExponent(second, rng);
				start->acc = FloatWithExponent(first, rng);
				break;
			default: // fs, then ft
				start->fpr[fs] = FloatWithExponent(first, rng);
				start->fpr[ft] = FloatWithExponent(second, rng);
				break;
		}
	}

	int CheckExponentDifferences(Rng& rng, const char* what)
	{
		static const Op kAdders[] = {Op::ADD_S, Op::SUB_S, Op::ADDA_S, Op::SUBA_S, Op::MADD_S, Op::MSUB_S, Op::MADDA_S, Op::MSUBA_S};
		u32 cases = 0, failed_ops = 0;
		for (const Op op : kAdders)
		{
			s_exponent_difference = -255;
			const Tally tally = CheckOp(op, Oracle::Recompiler, 511 * 4, rng, ShapeExponentDifference);
			cases += tally.run;
			if (tally.failed || tally.run == 0)
			{
				failed_ops++;
				std::fprintf(stderr, "  %s: %u of %u cases differ from PCSX2's recompiler\n", r5900::mnemonic(op), tally.failed, tally.run);
			}
		}
		std::fprintf(stderr,
			"selftest: %s, every difference of exponents, against PCSX2's recompiler: %zu instructions, %u cases, %u instructions differ\n",
			what, std::size(kAdders), cases, failed_ops);
		return static_cast<int>(failed_ops);
	}

	int CheckSemantics(const ps2n::selftest::Options& options, Rng& rng)
	{
		std::vector<Op> all, fpu;
		for (const Op op : kOwnOps)
		{
			all.push_back(op);
			if (IsFpu(op))
				fpu.push_back(op);
		}

		int failures = 0;
		failures += CheckOps(all, Oracle::Interpreter, options.interp_trials, "semantics", rng);
		failures += CheckOps(all, Oracle::Recompiler, options.jit_trials, "semantics", rng);
		failures += CheckExponentDifferences(rng, "the FPU's additions");

		// The FPU again in PCSX2's other clamp modes, which change what both
		// the recompiler and PS2Native's functions do. (The fourth mode, Full,
		// is not implemented.) The recompiler compiles the mode into its code,
		// so its code is thrown away at each change.
		const u32 standing = EmuConfig.Cpu.Recompiler.GetEEClampMode();
		static const struct
		{
			u32 mode;
			const char* what;
		} kModes[] = {
			{2, "the FPU with clamp mode Extra + Preserve Sign"},
			{0, "the FPU with clamp mode None"},
		};
		for (const auto& mode : kModes)
		{
			EmuConfig.Cpu.Recompiler.SetEEClampMode(mode.mode);
			recCpu.Reset();
			failures += CheckOps(fpu, Oracle::Recompiler, options.jit_trials, mode.what, rng);
			failures += CheckExponentDifferences(rng, mode.what);
		}
		EmuConfig.Cpu.Recompiler.SetEEClampMode(standing);
		recCpu.Reset();

		// VU0's instructions in PCSX2's other VU clamp modes, and with the three
		// game fixes that change what they compute: once in the mode the
		// machine stands in, once in the mode None (Killzone's settings, the
		// clamp mode None and the I-bit fix, are covered so).
		std::vector<Op> vu0;
		for (const Op op : all)
		{
			if (IsVu0(op))
				vu0.push_back(op);
		}
		auto& rec = EmuConfig.Cpu.Recompiler;
		auto& fixes = EmuConfig.Gamefixes;
		const bool overflow = rec.vu0Overflow, extra = rec.vu0ExtraOverflow, sign = rec.vu0SignOverflow;
		const bool ibit = fixes.IbitHack, add_sub = fixes.VuAddSubHack, vu_overflow = fixes.VUOverflowHack;
		const auto set_vu = [&](bool o, bool e, bool s, bool game_fixes) {
			rec.vu0Overflow = o;
			rec.vu0ExtraOverflow = e;
			rec.vu0SignOverflow = s;
			fixes.IbitHack = game_fixes || ibit;
			fixes.VuAddSubHack = game_fixes || add_sub;
			fixes.VUOverflowHack = game_fixes || vu_overflow;
			recCpu.Reset();
		};
		static const struct
		{
			bool overflow, extra, sign, game_fixes;
			const char* what;
		} kVuModes[] = {
			{false, false, false, false, "VU0 with clamp mode None"},
			{true, false, false, false, "VU0 with clamp mode Normal"},
			{true, true, false, false, "VU0 with clamp mode Extra"},
			{true, true, true, false, "VU0 with clamp mode Extra + Preserve Sign"},
			{false, false, false, true, "VU0 with clamp mode None and the I-bit, VU add/sub and VU overflow game fixes"},
		};
		for (const auto& mode : kVuModes)
		{
			if (mode.overflow == overflow && mode.extra == extra && mode.sign == sign && !mode.game_fixes)
				continue; // the standing mode, checked above
			set_vu(mode.overflow, mode.extra, mode.sign, mode.game_fixes);
			failures += CheckOps(vu0, Oracle::Recompiler, options.jit_trials, mode.what, rng);
		}
		set_vu(overflow, extra, sign, true);
		failures += CheckOps(vu0, Oracle::Recompiler, options.jit_trials, "VU0 with the I-bit, VU add/sub and VU overflow game fixes", rng);
		set_vu(overflow, extra, sign, false);

		return failures;
	}

	// Compiled code holds the FPU's registers in the lowest lanes of host SSE
	// registers, and the other three lanes hold whatever the host left there
	// (ops_fpu.h). The functions it calls must not carry that into a result.
	// Each is called here twice on the same operands: once with the other
	// lanes clear, as from the core's registers (which the comparisons with
	// PCSX2 above go through), once with them random. The two calls must
	// leave the same result, the same FCR31 and the same counts, in each
	// clamp mode and with the FPU's game fix.
	int CheckFpuLanes(u32 trials, Rng& rng)
	{
		namespace val = ps2n::ee::val;
		namespace fpu = ps2n::ee::ops::fpu;
		using val::Fpr;

		static const struct
		{
			const char* name;
			u64 (*call)(Fpr acc, Fpr s, Fpr t);
		} kFns[] = {
			{"ADD.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::ADD_S(s, t)); }},
			{"SUB.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::SUB_S(s, t)); }},
			{"MUL.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MUL_S(s, t)); }},
			{"MADD.S", [](Fpr a, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MADD_S(a, s, t)); }},
			{"MSUB.S", [](Fpr a, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MSUB_S(a, s, t)); }},
			{"MADDA.S", [](Fpr a, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MADDA_S(a, s, t)); }},
			{"DIV.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::DIV_S(s, t)); }},
			{"SQRT.S", [](Fpr, Fpr, Fpr t) -> u64 { return fpu::toBits(val::SQRT_S(t)); }},
			{"RSQRT.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::RSQRT_S(s, t)); }},
			{"ABS.S", [](Fpr, Fpr s, Fpr) -> u64 { return fpu::toBits(val::ABS_S(s)); }},
			{"NEG.S", [](Fpr, Fpr s, Fpr) -> u64 { return fpu::toBits(val::NEG_S(s)); }},
			{"MAX.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MAX_S(s, t)); }},
			{"MIN.S", [](Fpr, Fpr s, Fpr t) -> u64 { return fpu::toBits(val::MIN_S(s, t)); }},
			{"CVT.S.W", [](Fpr, Fpr s, Fpr) -> u64 { return fpu::toBits(val::CVT_S_W(s)); }},
			{"CVT.W.S", [](Fpr, Fpr s, Fpr) -> u64 { return fpu::toBits(val::CVT_W_S(s)); }},
			{"C.EQ.S", [](Fpr, Fpr s, Fpr t) -> u64 { return val::C_EQ_S(s, t); }},
			{"C.LT.S", [](Fpr, Fpr s, Fpr t) -> u64 { return val::C_LT_S(s, t); }},
			{"C.LE.S", [](Fpr, Fpr s, Fpr t) -> u64 { return val::C_LE_S(s, t); }},
			{"MFC1", [](Fpr, Fpr s, Fpr) -> u64 { return val::MFC1(s); }},
		};

		auto& rec = EmuConfig.Cpu.Recompiler;
		auto& fixes = EmuConfig.Gamefixes;
		const u32 standing = rec.GetEEClampMode();
		const bool mul_hack = fixes.FpuMulHack;
		static const struct
		{
			u32 mode;
			bool mul_hack;
		} kModes[] = {{1, false}, {2, false}, {0, false}, {1, true}};

		const u32 saved_fcr31 = fpuRegs.fprc[31];
		const u64 saved_followed[2] = {ps2n::ee::ops::fpu::console_followed[0], ps2n::ee::ops::fpu::console_followed[1]};
		u32 cases = 0, failed = 0;
		for (const auto& mode : kModes)
		{
			rec.SetEEClampMode(mode.mode);
			fixes.FpuMulHack = mode.mul_hack;
			for (const auto& fn : kFns)
			{
				u32 differ = 0;
				for (u32 i = 0; i < trials; i++)
				{
					u32 bits[3];
					for (u32& b : bits)
						b = rng.Float();
					// The operands of PCSX2's game fix for the multiplication.
					if (mode.mul_hack && rng.Below(4) == 0)
					{
						bits[1] = 0x3E800000u;
						bits[2] = 0x40490FDBu;
					}
					const u32 fcr31 = rng.Next32();
					Fpr clean[3], dirty[3];
					for (int k = 0; k < 3; k++)
					{
						clean[k] = fpu::fromBits(bits[k]);
						dirty[k] = _mm_castsi128_ps(_mm_setr_epi32(static_cast<int>(bits[k]), static_cast<int>(rng.Float()),
							static_cast<int>(rng.Next32()), static_cast<int>(rng.Float())));
					}

					const u64 before[2] = {ps2n::ee::ops::fpu::console_followed[0], ps2n::ee::ops::fpu::console_followed[1]};
					fpuRegs.fprc[31] = fcr31;
					const u64 want = fn.call(clean[0], clean[1], clean[2]);
					const u32 want_fcr31 = fpuRegs.fprc[31];
					const u64 want_followed[2] = {ps2n::ee::ops::fpu::console_followed[0] - before[0],
						ps2n::ee::ops::fpu::console_followed[1] - before[1]};

					const u64 middle[2] = {ps2n::ee::ops::fpu::console_followed[0], ps2n::ee::ops::fpu::console_followed[1]};
					fpuRegs.fprc[31] = fcr31;
					const u64 got = fn.call(dirty[0], dirty[1], dirty[2]);
					const u32 got_fcr31 = fpuRegs.fprc[31];
					const u64 got_followed[2] = {ps2n::ee::ops::fpu::console_followed[0] - middle[0],
						ps2n::ee::ops::fpu::console_followed[1] - middle[1]};

					cases++;
					if (got != want || got_fcr31 != want_fcr31 || got_followed[0] != want_followed[0] || got_followed[1] != want_followed[1])
					{
						if (differ++ < 3)
							std::fprintf(stderr,
								"  LANES %s, clamp mode %u%s: operands %08x %08x %08x: %llx, FCR31 %08x with the other lanes clear;"
								" %llx, FCR31 %08x with them set\n",
								fn.name, mode.mode, mode.mul_hack ? " and the game fix" : "", bits[0], bits[1], bits[2],
								static_cast<unsigned long long>(want), want_fcr31, static_cast<unsigned long long>(got), got_fcr31);
					}
				}
				if (differ)
					failed++;
			}
		}
		rec.SetEEClampMode(standing);
		fixes.FpuMulHack = mul_hack;
		recCpu.Reset();
		fpuRegs.fprc[31] = saved_fcr31;
		ps2n::ee::ops::fpu::console_followed[0] = saved_followed[0];
		ps2n::ee::ops::fpu::console_followed[1] = saved_followed[1];

		std::fprintf(stderr,
			"selftest: the FPU's functions on values, with the SSE lanes above a register's clear and set: %zu functions, %zu modes,"
			" %u cases, %u differ\n",
			std::size(kFns), std::size(kModes), cases, failed);
		return static_cast<int>(failed);
	}

	// What PCSX2's opcode table calls the instruction PS2Native's decoder calls
	// `op`. PCSX2 has one entry for all of COP2 and for some groups.
	std::string ExpectedPcsx2Name(Op op, const char* identifier)
	{
		const u32 flags = r5900::info(op).flags;
		std::string name = identifier;

		if ((flags & r5900::F_VU0) && !(flags & (r5900::F_LOAD | r5900::F_STORE)))
			return "COP2";
		if (name.rfind("PMFHL_", 0) == 0)
			return "PMFHL";
		if (name.rfind("PMTHL_", 0) == 0)
			return "PMTHL";
		if (name == "MFPS" || name == "MFPC")
			return "MFC0";
		if (name == "MTPS" || name == "MTPC")
			return "MTC0";
		if (name == "CVT_W_S")
			return "CVT_W";
		if (name == "CVT_S_W")
			return "CVT_S";
		if (name.rfind("C_", 0) == 0 && name.size() > 2 && name.compare(name.size() - 2, 2, "_S") == 0)
			return name.substr(0, name.size() - 2);
		return name;
	}

	struct Row
	{
		Op op;
		const char* identifier;
	};

	const Row kRows[] = {
#define X(op, mn, match, ops, flags, mem) {Op::op, #op},
#include "ps2n/r5900_ops.inc"
#undef X
	};

	int CheckTables(const ps2n::selftest::Options& options, Rng& rng)
	{
		std::map<Op, std::string> expected_name;
		for (const Row& row : kRows)
			expected_name[row.op] = ExpectedPcsx2Name(row.op, row.identifier);

		int failures = 0;
		u32 reported = 0;
		std::map<std::string, u32> disagreements;

		auto check = [&](u32 word, bool from_table) {
			const Op op = r5900::decodeOp(word);
			const R5900::OPCODE& theirs = R5900::GetInstruction(word);

			if (op == Op::Invalid)
			{
				if (from_table)
				{
					failures++;
					std::fprintf(stderr, "  TABLE %08x: PS2Native's decoder rejects a row of its own table\n", word);
				}
				return;
			}

			const std::string& want = expected_name[op];
			if (want != theirs.Name)
			{
				const std::string key = std::string(r5900::mnemonic(op)) + " is PCSX2's " + theirs.Name;
				if (disagreements[key]++ == 0 && reported++ < 40)
					std::fprintf(stderr, "  DECODE %08x: PS2Native decodes %s, PCSX2 %s\n", word, r5900::mnemonic(op), theirs.Name);
			}
			else if (r5900::cycles(op) != theirs.cycles)
			{
				const std::string key = std::string(r5900::mnemonic(op)) + " cycles";
				if (disagreements[key]++ == 0 && reported++ < 40)
					std::fprintf(stderr, "  CYCLES %08x %s: PS2Native counts %u, PCSX2 %u\n", word, r5900::mnemonic(op),
						r5900::cycles(op), theirs.cycles);
			}
		};

		for (const Row& row : kRows)
			check(r5900::info(row.op).match, true);
		for (u32 i = 0; i < options.random_words; i++)
			check(rng.Next32(), false);

		failures += static_cast<int>(disagreements.size());
		std::fprintf(stderr,
			"selftest: decoder and cycle table against PCSX2's opcode table: %zu rows and %u random words, %zu kinds of"
			" disagreement\n",
			std::size(kRows), options.random_words, disagreements.size());
		return failures;
	}
} // namespace

int ps2n::selftest::Run(const Options& options)
{
	Rng rng{options.seed};
	std::fprintf(stderr, "selftest: seed %016llx\n", static_cast<unsigned long long>(options.seed));

	// The EE's FP control register on this thread, as when the machine runs:
	// the FPU semantics and both oracles compute under it.
	FPControlRegister::SetCurrent(EmuConfig.Cpu.FPUFPCR);
	std::fprintf(stderr, "selftest: EE clamp mode %u, VU clamp mode %u, FP control register %08x (division %08x)\n",
		EmuConfig.Cpu.Recompiler.GetEEClampMode(), EmuConfig.Cpu.Recompiler.GetVUClampMode(), EmuConfig.Cpu.FPUFPCR.bitmask,
		EmuConfig.Cpu.FPUDivFPCR.bitmask);

	int failures = 0;
	failures += CheckTables(options, rng);
	failures += CheckSemantics(options, rng);
	failures += CheckFpuLanes(options.interp_trials, rng);

	std::fprintf(stderr, "selftest: %s\n", failures ? "FAILED" : "passed");
	return failures;
}
