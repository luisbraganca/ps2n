// SPDX-License-Identifier: GPL-3.0-or-later
//
// The I/O processor's instructions (a MIPS R3000A), as PCSX2's IOP
// recompiler treats them: which instruction an encoding is to it, which
// encodings it has none for, what ends a block, and what a block costs.
// Transcribed from PCSX2 v2.9.170 (pcsx2/x86/iR3000A.cpp, iR3000Atables.cpp,
// iR3000A.h; Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+).
//
// Shared by PS2Native's IOP provider (runtime/iop_provider.h), which forms
// blocks as that recompiler forms them and interprets them, and by the
// recompiler (ps2n-recomp iop), which writes a C++ function for each block a
// run captured. Nothing here touches memory or the machine.
#pragma once

#include <cstdint>

namespace ps2n::r3000a
{
	// What the recompiler makes of an encoding. Where it has two names for
	// one thing (ADD and ADDU: it raises no overflow exception), there is one
	// here.
	enum class Op : uint8_t
	{
		Unknown, // no instruction: the recompiler generates nothing for it (rpsxNULL)
		ADDIU, // and ADDI
		SLTI,
		SLTIU,
		ANDI,
		ORI,
		XORI,
		LUI,
		ADDU, // and ADD
		SUBU, // and SUB
		AND,
		OR,
		XOR,
		NOR,
		SLT,
		SLTU,
		SLL,
		SRL,
		SRA,
		SLLV,
		SRLV,
		SRAV,
		MULT,
		MULTU,
		DIV,
		DIVU,
		MFHI,
		MTHI,
		MFLO,
		MTLO,
		J,
		JAL,
		JR,
		JALR,
		BEQ,
		BNE,
		BLEZ,
		BGTZ,
		BLTZ,
		BGEZ,
		BLTZAL,
		BGEZAL,
		LB,
		LBU,
		LH,
		LHU,
		LW,
		LWL,
		LWR,
		SB,
		SH,
		SW,
		SWL,
		SWR,
		MFC0, // and CFC0
		MTC0, // and CTC0
		RFE,
		SYSCALL,
		BREAK,
		GTE, // COP2, LWC2 and SWC2: the core's function for the instruction (pcsx2/IopGte.cpp)
		IMPORT, // addiu $zero, $zero, n: the instruction by which an import stub of a module names its function
	};

	constexpr uint32_t rs(uint32_t code) { return (code >> 21) & 0x1F; }
	constexpr uint32_t rt(uint32_t code) { return (code >> 16) & 0x1F; }
	constexpr uint32_t rd(uint32_t code) { return (code >> 11) & 0x1F; }
	constexpr uint32_t sa(uint32_t code) { return (code >> 6) & 0x1F; }
	constexpr uint32_t funct(uint32_t code) { return code & 0x3F; }
	constexpr int32_t imm(uint32_t code) { return static_cast<int16_t>(code & 0xFFFF); }
	constexpr uint32_t immu(uint32_t code) { return code & 0xFFFF; }

	// The tables rpsxBSC, rpsxSPC, rpsxREG, rpsxCP0, rpsxCP2 and rpsxCP2BSC.
	constexpr Op decode(uint32_t code)
	{
		switch (code >> 26)
		{
			case 0:
				switch (funct(code))
				{
					case 0: return Op::SLL;
					case 2: return Op::SRL;
					case 3: return Op::SRA;
					case 4: return Op::SLLV;
					case 6: return Op::SRLV;
					case 7: return Op::SRAV;
					case 8: return Op::JR;
					case 9: return Op::JALR;
					case 12: return Op::SYSCALL;
					case 13: return Op::BREAK;
					case 16: return Op::MFHI;
					case 17: return Op::MTHI;
					case 18: return Op::MFLO;
					case 19: return Op::MTLO;
					case 24: return Op::MULT;
					case 25: return Op::MULTU;
					case 26: return Op::DIV;
					case 27: return Op::DIVU;
					case 32:
					case 33: return Op::ADDU;
					case 34:
					case 35: return Op::SUBU;
					case 36: return Op::AND;
					case 37: return Op::OR;
					case 38: return Op::XOR;
					case 39: return Op::NOR;
					case 42: return Op::SLT;
					case 43: return Op::SLTU;
					default: return Op::Unknown;
				}
			case 1:
				switch (rt(code))
				{
					case 0: return Op::BLTZ;
					case 1: return Op::BGEZ;
					case 16: return Op::BLTZAL;
					case 17: return Op::BGEZAL;
					default: return Op::Unknown;
				}
			case 2: return Op::J;
			case 3: return Op::JAL;
			case 4: return Op::BEQ;
			case 5: return Op::BNE;
			case 6: return Op::BLEZ;
			case 7: return Op::BGTZ;
			case 8: return Op::ADDIU;
			case 9: return (code >> 16) == 0x2400 ? Op::IMPORT : Op::ADDIU;
			case 10: return Op::SLTI;
			case 11: return Op::SLTIU;
			case 12: return Op::ANDI;
			case 13: return Op::ORI;
			case 14: return Op::XORI;
			case 15: return Op::LUI;
			case 16:
				switch (rs(code))
				{
					case 0:
					case 2: return Op::MFC0;
					case 4:
					case 6: return Op::MTC0;
					case 16: return Op::RFE;
					default: return Op::Unknown;
				}
			case 18:
				if (funct(code) == 0)
				{
					switch (rs(code))
					{
						case 0:
						case 2:
						case 4:
						case 6: return Op::GTE;
						default: return Op::Unknown;
					}
				}
				switch (funct(code))
				{
					case 0x01: case 0x06: case 0x0C: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x16:
					case 0x1B: case 0x1C: case 0x1E: case 0x20: case 0x28: case 0x29: case 0x2A: case 0x2D: case 0x2E:
					case 0x30: case 0x3D: case 0x3E: case 0x3F:
						return Op::GTE;
					default: return Op::Unknown;
				}
			case 32: return Op::LB;
			case 33: return Op::LH;
			case 34: return Op::LWL;
			case 35: return Op::LW;
			case 36: return Op::LBU;
			case 37: return Op::LHU;
			case 38: return Op::LWR;
			case 40: return Op::SB;
			case 41: return Op::SH;
			case 42: return Op::SWL;
			case 43: return Op::SW;
			case 46: return Op::SWR;
			case 50:
			case 58: return Op::GTE;
			default: return Op::Unknown;
		}
	}

	// The instructions at which the recompiler's look-ahead stops: the block
	// ends after their delay slot (iopRecRecompile).
	constexpr bool isBranch(Op op)
	{
		switch (op)
		{
			case Op::J:
			case Op::JAL:
			case Op::JR:
			case Op::JALR:
			case Op::BEQ:
			case Op::BNE:
			case Op::BLEZ:
			case Op::BGTZ:
			case Op::BLTZ:
			case Op::BGEZ:
			case Op::BLTZAL:
			case Op::BGEZAL:
				return true;
			default:
				return false;
		}
	}

	// Of those, the ones whose target is in the instruction.
	constexpr bool isConditional(Op op)
	{
		switch (op)
		{
			case Op::BEQ:
			case Op::BNE:
			case Op::BLEZ:
			case Op::BGTZ:
			case Op::BLTZ:
			case Op::BGEZ:
			case Op::BLTZAL:
			case Op::BGEZAL:
				return true;
			default:
				return false;
		}
	}

	// Where a branch at `pc` goes when it is taken.
	constexpr uint32_t branchTarget(uint32_t pc, uint32_t code)
	{
		return static_cast<uint32_t>(imm(code) * 4) + pc + 4;
	}
	constexpr uint32_t jumpTarget(uint32_t pc, uint32_t code)
	{
		return ((code & 0x03FFFFFF) << 2) | ((pc + 4) & 0xF0000000);
	}

	// Whether the recompiler carries a delay slot's instruction out before
	// the branch it belongs to (psxTrySwapDelaySlot): `brs`, `brt` and `brd`
	// are the registers the branch reads and the one it writes, `slot` the
	// delay slot's word. It does where it finds the two cannot get in each
	// other's way; what it overlooks shows in results, so PS2Native's
	// provider keeps its order: with JALR, an instruction of the arithmetic
	// kind in the delay slot that writes the link register is carried out
	// first, and the link register then gets the return address after it; and
	// a move to a coprocessor that reads the link register reads what it held
	// before.
	constexpr bool swapsDelaySlot(uint32_t brs, uint32_t brt, uint32_t brd, uint32_t slot)
	{
		if (slot == 0)
			return true;
		const uint32_t srs = rs(slot);
		const uint32_t srt = rt(slot);
		const uint32_t srd = rd(slot);
		switch (slot >> 26)
		{
			case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
			case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
			case 40: case 41: case 42: case 43: case 46:
				return !((brs != 0 && brs == srt) || (brt != 0 && brt == srt) || (brd != 0 && (brd == srs || brd == srt)));
			case 50:
			case 58:
				return true;
			case 0:
				switch (funct(slot))
				{
					case 0: case 2: case 3: case 4: case 6: case 7:
					case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
					case 42: case 43:
						return !((brs != 0 && brs == srd) || (brt != 0 && brt == srd) || (brd != 0 && (brd == srs || brd == srt)));
					case 15: case 24: case 25: case 26: case 27:
						return true;
					default:
						return false;
				}
			case 16: case 17: case 18: case 19:
				switch (srs)
				{
					case 0:
					case 2:
						return !((brs != 0 && brs == srt) || (brt != 0 && brt == srt) || (brd != 0 && brd == srt));
					case 4:
					case 6:
						return true;
					default:
						return (slot >> 26) == 18;
				}
			default:
				return false;
		}
	}

	// What an instruction costs: one cycle, and for a multiplication or a
	// division a penalty on top (psxInstCycles_Mult, psxInstCycles_Div).
	constexpr uint32_t cycles(Op op)
	{
		switch (op)
		{
			case Op::MULT:
			case Op::MULTU:
				return 1 + 7;
			case Op::DIV:
			case Op::DIVU:
				return 1 + 40;
			default:
				return 1;
		}
	}

	// A block the recompiler formed, as its words say it: `count` words from
	// its first instruction on.
	struct Shape
	{
		bool valid = false; // false: not a block the recompiler forms (no words, or more after its branch)
		bool branch = false; // it ends at a branch or jump and its delay slot; otherwise it was cut short, and runs on into the next
		uint32_t cycles = 0; // what it costs, whole
		const char* refused = nullptr; // what in it PS2Native's provider does not carry out
	};

	// The longest a block may be here. PCSX2's recompiler has no limit but
	// its 65,535 (it asserts); code that runs that far without a branch is no
	// code.
	inline constexpr uint32_t kMaxBlockWords = 16384;

	constexpr Shape shape(const uint32_t* words, uint32_t count)
	{
		Shape s;
		if (count == 0 || count > kMaxBlockWords)
			return s;
		for (uint32_t i = 0; i < count; i++)
		{
			const Op op = decode(words[i]);
			s.cycles += cycles(op);
			if (!isBranch(op))
				continue;
			// The branch and its delay slot are the block's last two words.
			if (i + 2 != count)
				return s;
			const Op slot = decode(words[i + 1]);
			s.cycles += cycles(slot);
			s.branch = true;
			if (isBranch(slot))
				s.refused = "a branch or jump in a delay slot";
			else if (slot == Op::SYSCALL || slot == Op::BREAK)
				s.refused = "SYSCALL or BREAK in a delay slot";
			else if (op == Op::JR || op == Op::JALR)
			{
				// Where the recompiler keeps the jump's target in a host register
				// across the delay slot, and the delay slot's instruction is one
				// for which it gives up every host register (it calls the core for
				// it), it writes the target to the EE's registers and reads it
				// back from the IOP's, where nothing wrote it: the IOP goes on at
				// address 0 (rpsxJR, rpsxJALR: X86TYPE_PCWRITEBACK for
				// X86TYPE_PSX_PCWRITEBACK). Neither that nor a guess is carried
				// out.
				const uint32_t jrs = rs(words[i]);
				const uint32_t jrd = op == Op::JALR ? rd(words[i]) : 0;
				const bool swap = op == Op::JR ? swapsDelaySlot(jrs, 0, 0, words[i + 1]) : (jrd != jrs && swapsDelaySlot(jrs, 0, jrd, words[i + 1]));
				if (!swap && (slot == Op::LWL || slot == Op::LWR || slot == Op::SWL || slot == Op::SWR || slot == Op::GTE))
					s.refused = "a jump through a register whose delay slot, an unaligned load or store or an instruction of the GTE's, comes "
								"after the jump's target is taken: PCSX2's IOP recompiler loses the target there";
			}
			s.valid = true;
			return s;
		}
		s.valid = true;
		return s;
	}

	// A block that is a wait loop to the recompiler (s_nBlockFF): its branch
	// goes back to its first instruction, and everything in it but the branch
	// is a NOP. With PCSX2's WaitLoop setting such a block lets time pass to
	// the next event when the branch is taken (iPsxBranchTest).
	constexpr bool waitLoop(uint32_t pc, const uint32_t* words, uint32_t count)
	{
		if (count < 2)
			return false;
		const uint32_t at = count - 2;
		const uint32_t code = words[at];
		const Op op = decode(code);
		uint32_t target = 0xFFFFFFFF;
		if (op == Op::J || op == Op::JAL)
			target = jumpTarget(pc + at * 4, code);
		else if (isConditional(op))
			target = branchTarget(pc + at * 4, code);
		if (target != pc)
			return false;
		for (uint32_t i = 0; i < count; i++)
		{
			if (i != at && words[i] != 0)
				return false;
		}
		return true;
	}

	// A fingerprint of a block: its words, and how many. Not its address: the
	// same code does the same wherever it is, given where it is.
	struct Key
	{
		uint64_t a = 0;
		uint64_t b = 0;
		constexpr bool operator==(const Key& o) const { return a == o.a && b == o.b; }
	};

	constexpr Key key(const uint32_t* words, uint32_t count)
	{
		Key k{0x9E3779B97F4A7C15ull ^ count, 0xC2B2AE3D27D4EB4Full + count};
		for (uint32_t i = 0; i < count; i++)
		{
			k.a = (k.a ^ words[i]) * 0x100000001B3ull;
			k.a ^= k.a >> 29;
			k.b = (k.b + words[i] + (k.b << 6) + (k.b >> 2)) * 0xFF51AFD7ED558CCDull;
			k.b ^= k.b >> 31;
		}
		return k;
	}
} // namespace ps2n::r3000a
