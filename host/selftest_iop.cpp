// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's IOP provider against PCSX2's IOP recompiler, on random programs
// (step G12, docs/adr/0023-the-iops-code-compiled-as-step-g12.md).
//
// A program is written into the IOP's RAM and run twice from the same
// machine, in the same stretches of time: on PCSX2's IOP recompiler and on
// PS2Native's provider. After every stretch the two must agree on where the
// IOP is, on its cycle count, on what is left of the EE's clock, and on every
// register; at the end also on the memory the program could reach, its own
// code included, and on the addresses at which each holds a block, which is
// what keeps the time the same from there on.
//
// The programs are made to reach what the provider has to follow: every
// instruction PCSX2's recompiler has, and encodings it has none for; branches
// forward and back, so that blocks are cut short at other blocks and loops
// become blocks of their own; jumps through registers into the three places
// RAM is mapped at; JALR with a delay slot that writes its link register;
// stores into the program's own code; SYSCALL and BREAK, with a handler that
// returns; loads and stores at every alignment; multiplications and
// divisions of the awkward numbers; a wait loop at the end; an import stub of
// a module whose function the core carries out itself; event tests and an
// interrupt in the middle; stretches of no time at all.

#include "selftest.h"

#include "runtime/iop_provider.h"

#include "ps2n/r3000a.h"
#include "ps2n/r5900.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/IopHw.h"
#include "pcsx2/IopMem.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/x86/BaseblockEx.h"

#include "fmt/format.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Where the host code of the block that begins at each address is: PCSX2's
// IOP recompiler's own table (pcsx2/x86/iR3000A.cpp).
extern uptr psxRecLUT[];

namespace
{
	struct Rng
	{
		u64 state;
		u64 Next()
		{
			state ^= state >> 12;
			state ^= state << 25;
			state ^= state >> 27;
			return state * 0x2545F4914F6CDD1Dull;
		}
		u32 Next32() { return static_cast<u32>(Next() >> 32); }
		u32 Below(u32 n) { return Next32() % n; }
	};

	constexpr u32 kVector = 0x00000080; // the exception handler
	constexpr u32 kVectorWords = 32;
	constexpr u32 kCode = 0x00010000; // the program
	constexpr u32 kCodeWords = 160; // the most it may be, with what follows it
	constexpr u32 kData = 0x00020000; // what it loads and stores
	constexpr u32 kDataBytes = 0x1000;
	constexpr u32 kSlices = 12;

	// The registers a program uses, by what they hold. A register that holds
	// an address the program jumps through is only ever given another such
	// address: PCSX2's recompiler stops the whole machine at a jump to where
	// the IOP has no memory. $4 is left alone too: it is where the import the
	// core carries out itself looks for its text.
	constexpr u32 kValues[] = {1, 2, 3, 5, 6, 7};
	constexpr u32 kDataPointers[] = {8, 9, 10, 11};
	constexpr u32 kCodePointers[] = {12, 13, 14, 15};
	constexpr u32 kLinks[] = {16, 17, 18, 19, 31};
	constexpr u32 kWords[] = {20, 21, 22, 23}; // instructions that are safe to write over the program's own
	constexpr u32 kStatus = 24; // a value the status register may be given
	constexpr u32 kScratch = 25;

	struct Case
	{
		u32 count = 0; // the program's instructions, before the wait loop that ends it
		u32 words[kCodeWords] = {};
		u32 gpr[34] = {};
		u32 status = 0;
		u8 data[kDataBytes] = {};
		s32 slices[kSlices] = {};
		u32 next_event = 0; // cycles from the start to the first event test; 0: none in the run
		bool interrupt = false; // an interrupt is pending when the first event test comes
	};

	struct Snapshot
	{
		u32 gpr[34];
		u32 cp0[32];
		u32 cp2d[32];
		u32 cp2c[32];
		u32 pc;
		u64 cycle;
		s32 left;
	};

	struct Outcome
	{
		Snapshot after[kSlices];
		u8 data[kDataBytes];
		u32 code[kCodeWords];
		u32 vector[kVectorWords];
		std::vector<u32> starts;
		std::string refused;
	};

	u32 Pick(Rng& rng, const u32* list, u32 n) { return list[rng.Below(n)]; }
#define PICK(rng, list) Pick(rng, list, static_cast<u32>(std::size(list)))

	u32 RandomValue(Rng& rng)
	{
		static const u32 kEdges[] = {0, 1, 2, 3, 0xFFFFFFFF, 0xFFFFFFFE, 0x80000000, 0x80000001, 0x7FFFFFFF, 0x0000FFFF, 0x00008000, 0xFFFF8000, 0x00010000};
		switch (rng.Below(4))
		{
			case 0: return PICK(rng, kEdges);
			case 1: return rng.Below(64);
			case 2: return 0u - rng.Below(64);
			default: return rng.Next32();
		}
	}

	// Encodings.
	u32 I(u32 op, u32 rs, u32 rt, u32 imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
	u32 R(u32 rs, u32 rt, u32 rd, u32 sa, u32 funct) { return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct; }
	u32 Offset(u32 from, u32 to) { return (to - (from + 1)) & 0xFFFF; } // a branch at instruction `from` to instruction `to`

	u32 ValueOrZero(Rng& rng) { return rng.Below(8) ? PICK(rng, kValues) : 0; }

	// An encoding PCSX2's IOP recompiler has no instruction for.
	u32 Unknown(Rng& rng)
	{
		for (;;)
		{
			const u32 word = rng.Next32();
			if (ps2n::r3000a::decode(word) == ps2n::r3000a::Op::Unknown)
				return word;
		}
	}

	// One instruction that neither branches nor raises an exception.
	u32 Plain(Rng& rng)
	{
		const u32 d = ValueOrZero(rng), s = ValueOrZero(rng), t = ValueOrZero(rng);
		switch (rng.Below(30))
		{
			case 0: case 1: case 2: case 3:
				return I(8 + rng.Below(7), rng.Below(6) ? s : PICK(rng, kLinks), d, rng.Next32());
			case 4:
				return I(15, 0, d, rng.Next32()); // LUI
			case 5: case 6: case 7:
			{
				static const u32 kFuncts[] = {32, 33, 34, 35, 36, 37, 38, 39, 42, 43};
				return R(s, t, d, 0, PICK(rng, kFuncts));
			}
			case 8: case 9:
			{
				static const u32 kShifts[] = {0, 2, 3};
				return R(0, t, d, rng.Below(32), PICK(rng, kShifts));
			}
			case 10:
			{
				static const u32 kShifts[] = {4, 6, 7};
				return R(s, t, d, 0, PICK(rng, kShifts));
			}
			case 11: case 12:
				return R(s, t, 0, 0, 24 + rng.Below(4)); // MULT, MULTU, DIV, DIVU
			case 13:
				return rng.Below(2) ? R(0, 0, d, 0, rng.Below(2) ? 16 : 18) : R(s, 0, 0, 0, rng.Below(2) ? 17 : 19); // MFHI, MFLO, MTHI, MTLO
			case 14: case 15: case 16: case 17:
			{
				static const u32 kLoads[] = {32, 33, 34, 35, 36, 37, 38};
				return I(PICK(rng, kLoads), PICK(rng, kDataPointers), d, rng.Below(64) - 32);
			}
			case 18: case 19: case 20: case 21:
			{
				static const u32 kStores[] = {40, 41, 42, 43, 46};
				return I(PICK(rng, kStores), PICK(rng, kDataPointers), t, rng.Below(64) - 32);
			}
			case 22:
				// A store into the program's own code: one of the instructions that
				// are safe there, over one of the places a jump may go to, or a
				// little after it.
				return I(43, PICK(rng, kCodePointers), PICK(rng, kWords), rng.Below(3) * 4);
			case 23:
				return (16u << 26) | ((rng.Below(2) ? 0u : 2u) << 21) | ((rng.Below(4) ? d : kScratch) << 16) | (rng.Below(32) << 11); // MFC0, CFC0
			case 24:
			{
				// MTC0, CTC0: registers nothing depends on, or the status register
				// from the register that holds a value it may have.
				static const u32 kFree[] = {3, 5, 6, 7, 9, 11};
				if (rng.Below(4) == 0)
					return (16u << 26) | (4u << 21) | (kStatus << 16) | (12u << 11);
				return (16u << 26) | ((rng.Below(2) ? 4u : 6u) << 21) | (t << 16) | (PICK(rng, kFree) << 11);
			}
			case 25:
			{
				// The GTE's transfers: the core's functions.
				static const u32 kMoves[] = {0, 2, 4, 6};
				if (rng.Below(3) == 0)
					return I(rng.Below(2) ? 50 : 58, PICK(rng, kDataPointers), rng.Below(32), (rng.Below(16) - 8) * 4); // LWC2, SWC2
				return (18u << 26) | (PICK(rng, kMoves) << 21) | (d << 16) | (rng.Below(32) << 11);
			}
			case 26:
				return Unknown(rng);
			case 27:
				return 0x24000000u | rng.Below(64); // addiu $zero, $zero, n: an import's number, with no table before it
			case 28:
				return 0; // NOP
			default:
				return I(9, s, d, rng.Next32());
		}
	}

	// The program, the machine it starts in and the stretches it is run in.
	Case Make(Rng& rng)
	{
		Case c;
		c.count = 4 + rng.Below(60);
		const u32 end = c.count; // the wait loop
		const u32 stub = end + 8; // an import stub, after its table

		u32 i = 0;
		while (i < c.count)
		{
			// Where a branch or jump could still be followed by its delay slot.
			const bool room = i + 1 < c.count;
			const u32 kind = rng.Below(20);
			const u32 label = rng.Below(c.count + 1); // the wait loop's place among them
			if (kind < 3 && room)
			{
				// A conditional branch.
				const u32 s = ValueOrZero(rng), t = ValueOrZero(rng);
				switch (rng.Below(8))
				{
					case 0: c.words[i] = I(4, s, t, Offset(i, label)); break;
					case 1: c.words[i] = I(5, s, t, Offset(i, label)); break;
					case 2: c.words[i] = I(6, s, 0, Offset(i, label)); break;
					case 3: c.words[i] = I(7, s, 0, Offset(i, label)); break;
					case 4: c.words[i] = I(1, s, 0, Offset(i, label)); break;
					case 5: c.words[i] = I(1, s, 1, Offset(i, label)); break;
					case 6: c.words[i] = I(1, rng.Below(4) ? s : 31, 16, Offset(i, label)); break;
					default: c.words[i] = I(1, rng.Below(4) ? s : 31, 17, Offset(i, label)); break;
				}
				c.words[i + 1] = Plain(rng);
				i += 2;
			}
			else if (kind == 3 && room)
			{
				// J or JAL, sometimes to the import stub.
				const u32 to = (rng.Below(6) == 0) ? stub : label;
				c.words[i] = (((rng.Below(2) || to == stub) ? 3u : 2u) << 26) | (((kCode + to * 4) >> 2) & 0x03FFFFFF);
				c.words[i + 1] = Plain(rng);
				i += 2;
			}
			else if (kind == 4 && room)
			{
				// JR or JALR through a register that holds a place in the
				// program. The delay slot is sometimes one of those whose order
				// with the jump shows: it gives the jump's register another place,
				// it writes the link register, or it reads it.
				const u32 s = PICK(rng, kCodePointers);
				const u32 other = PICK(rng, kCodePointers);
				u32 d = 0;
				if (rng.Below(2))
				{
					c.words[i] = R(s, 0, 0, 0, 8);
				}
				else
				{
					// (Its own register for the link only where the address after
					// it is still one a store through that register may write
					// near: never the wait loop.)
					d = rng.Below(8) ? PICK(rng, kLinks) : ((rng.Below(2) && i + 4 < c.count) ? s : 0);
					c.words[i] = R(s, 0, d, 0, 9);
				}
				const bool links = d != 0 && d != s;
				switch (rng.Below(8))
				{
					case 0: c.words[i + 1] = rng.Below(2) ? I(13, other, s, 0) : R(other, 0, s, 0, 33); break; // ori s, other, 0; addu s, other, $zero
					case 1: c.words[i + 1] = links ? R(ValueOrZero(rng), ValueOrZero(rng), d, 0, 33) : Plain(rng); break; // addu d, x, y
					case 2: c.words[i + 1] = (16u << 26) | (4u << 21) | (d << 16) | (3u << 11); break; // mtc0 d, $3
					case 3: c.words[i + 1] = I(9, d, PICK(rng, kValues), 4); break; // addiu x, d, 4
					case 4: c.words[i + 1] = R(d, ValueOrZero(rng), PICK(rng, kValues), 0, 33); break; // addu x, d, y
					default: c.words[i + 1] = Plain(rng); break;
				}
				// Not a pair at which PCSX2's recompiler loses the jump's target,
				// which the provider refuses (r3000a::shape).
				while (ps2n::r3000a::shape(&c.words[i], 2).refused)
					c.words[i + 1] = Plain(rng);
				i += 2;
			}
			else if (kind == 5)
			{
				c.words[i++] = rng.Below(2) ? 0x0000000Cu : 0x0000000Du; // SYSCALL, BREAK
			}
			else
			{
				c.words[i++] = Plain(rng);
			}
		}

		// The wait loop that ends it; then an import table of the library whose
		// function 14 the core carries out itself, and the stub for it.
		c.words[end] = (2u << 26) | (((kCode + end * 4) >> 2) & 0x03FFFFFF);
		c.words[end + 1] = 0;
		c.words[end + 2] = 0x41E00000;
		c.words[end + 3] = 0;
		c.words[end + 4] = 0x00000101;
		c.words[end + 5] = 0x6D737973; // "sysm"
		c.words[end + 6] = 0x00006D65; // "em"
		c.words[end + 7] = 0;
		c.words[stub] = 0x03E00008; // jr $ra
		c.words[stub + 1] = 0x2400000E; // addiu $zero, $zero, 14

		// The machine.
		static const u32 kPlaces[] = {0x00000000, 0x80000000, 0xA0000000};
		for (u32 r : kValues)
			c.gpr[r] = RandomValue(rng);
		for (u32 r : kDataPointers)
			c.gpr[r] = (kData + 0x800 + rng.Below(16) * 4 + (rng.Below(4) ? 0 : rng.Below(4))) | (rng.Below(4) ? PICK(rng, kPlaces) : 0x00200000u);
		for (u32 r : kCodePointers)
			c.gpr[r] = (kCode + rng.Below(c.count > 3 ? c.count - 3 : 1) * 4) | PICK(rng, kPlaces);
		for (u32 r : kLinks)
			c.gpr[r] = RandomValue(rng);
		c.gpr[20] = 0;
		c.gpr[21] = I(9, 1, 1, 1); // addiu $1, $1, 1
		c.gpr[22] = R(3, 4, 2, 0, 38); // xor $2, $3, $4
		c.gpr[23] = R(0, 6, 5, 3, 0); // sll $5, $6, 3
		static const u32 kStatuses[] = {0x00000000, 0x00000401, 0x00010000, 0x00010401, 0x00000404};
		c.gpr[kStatus] = PICK(rng, kStatuses);
		c.gpr[kScratch] = RandomValue(rng);
		c.gpr[29] = kData + 0x400; // the stack an import's function writes to
		c.gpr[4] = kData + 0xC00; // and the empty text it is given
		c.gpr[32] = RandomValue(rng);
		c.gpr[33] = RandomValue(rng);
		c.status = rng.Below(6) ? 0 : PICK(rng, kStatuses);

		for (u8& byte : c.data)
			byte = static_cast<u8>(rng.Next32());
		std::memset(c.data + 0xC00, 0, 64);

		for (s32& slice : c.slices)
		{
			switch (rng.Below(6))
			{
				case 0: slice = static_cast<s32>(rng.Below(3)) - 1; break; // no time at all, or less
				case 1: slice = 1 + static_cast<s32>(rng.Below(16)); break;
				default: slice = 8 + static_cast<s32>(rng.Below(400)); break;
			}
		}
		if (rng.Below(3) == 0)
		{
			c.next_event = 2 + rng.Below(60);
			c.interrupt = rng.Below(2) != 0;
		}
		return c;
	}

	// The handler at the exception vector: an interrupt is acknowledged and
	// the program goes on where it was; after SYSCALL or BREAK it goes on at
	// the next instruction.
	const u32 kHandler[] = {
		0x401A6800, // mfc0  $k0, Cause
		0x335A007C, // andi  $k0, $k0, 0x7C
		0x17400007, // bne   $k0, $zero, +7
		0x00000000, // nop
		0x3C1B1F80, // lui   $k1, 0x1F80
		0xAF601070, // sw    $zero, 0x1070($k1)   (the interrupt status: all of it acknowledged)
		0x401A7000, // mfc0  $k0, EPC
		0x00000000, // nop
		0x03400008, // jr    $k0
		0x42000010, // rfe
		0x401A7000, // mfc0  $k0, EPC
		0x00000000, // nop
		0x275A0004, // addiu $k0, $k0, 4
		0x03400008, // jr    $k0
		0x42000010, // rfe
	};

	u32* Ram32(u32 address)
	{
		return reinterpret_cast<u32*>(iopMem->Main + address);
	}

	// The addresses PCSX2's recompiler holds a block at, in the memory a
	// program can run.
	void RecompilerStarts(std::vector<u32>& out)
	{
		// What the table holds where no block begins: the last word of RAM is
		// never run.
		const uptr none = PC_GETBLOCK_(0x001FFFFCu, psxRecLUT)->GetFnptr();
		const auto scan = [&](u32 from, u32 words) {
			for (u32 i = 0; i < words; i++)
			{
				if (PC_GETBLOCK_(from + i * 4, psxRecLUT)->GetFnptr() != none)
					out.push_back(from + i * 4);
			}
		};
		scan(kVector, kVectorWords);
		scan(kCode, kCodeWords);
	}

	void ProviderStarts(std::vector<u32>& out)
	{
		u32 buffer[kVectorWords + kCodeWords];
		size_t n = ps2n::iop::BlockStarts(kVector, kVector + kVectorWords * 4, buffer, std::size(buffer));
		out.assign(buffer, buffer + n);
		n = ps2n::iop::BlockStarts(kCode, kCode + kCodeWords * 4, buffer, std::size(buffer));
		out.insert(out.end(), buffer, buffer + n);
	}

	// Runs the case on one provider, from the same machine each time.
	void RunOn(R3000Acpu* provider, bool ours, const Case& c, Outcome& out)
	{
		std::memset(&psxRegs, 0, sizeof(psxRegs));
		std::memcpy(psxRegs.GPR.r, c.gpr, sizeof(c.gpr));
		psxRegs.GPR.r[0] = 0;
		psxRegs.CP0.n.Status = c.status;
		psxRegs.CP0.n.PRid = 0x1F;
		psxRegs.pc = kCode;
		psxRegs.cycle = 1000;
		psxRegs.iopNextEventCycle = psxRegs.cycle + (c.next_event ? c.next_event : 0x40000000u);

		std::memset(Ram32(kVector), 0, kVectorWords * 4);
		std::memcpy(Ram32(kVector), kHandler, sizeof(kHandler));
		std::memcpy(Ram32(kCode), c.words, sizeof(c.words));
		std::memcpy(iopMem->Main + kData, c.data, sizeof(c.data));

		// No counter is due, so that an event test does nothing but look for an
		// interrupt; and one is waiting, or none.
		psxNextStartCounter = psxRegs.cycle;
		psxNextDeltaCounter = 0x7FFFFFFF;
		psxHu32(HW_ICTRL) = 1;
		psxHu32(HW_IMASK) = c.interrupt ? 1 : 0;
		psxHu32(HW_ISTAT) = c.interrupt ? 1 : 0;

		psxCpu = provider;
		provider->Reset();
		for (u32 k = 0; k < kSlices; k++)
		{
			Snapshot& s = out.after[k];
			s.left = provider->ExecuteBlock(c.slices[k]);
			std::memcpy(s.gpr, psxRegs.GPR.r, sizeof(s.gpr));
			std::memcpy(s.cp0, psxRegs.CP0.r, sizeof(s.cp0));
			std::memcpy(s.cp2d, psxRegs.CP2D.r, sizeof(s.cp2d));
			std::memcpy(s.cp2c, psxRegs.CP2C.r, sizeof(s.cp2c));
			s.pc = psxRegs.pc;
			s.cycle = psxRegs.cycle;
		}
		std::memcpy(out.data, iopMem->Main + kData, sizeof(out.data));
		std::memcpy(out.code, Ram32(kCode), sizeof(out.code));
		std::memcpy(out.vector, Ram32(kVector), sizeof(out.vector));
		out.starts.clear();
		out.refused.clear();
		if (ours)
		{
			ProviderStarts(out.starts);
			if (const char* what = ps2n::iop::Unsupported())
			{
				out.refused = what;
				ps2n::iop::ClearUnsupported();
			}
		}
		else
		{
			RecompilerStarts(out.starts);
		}
	}

	// Empty when the two runs are the same; otherwise what differs first.
	std::string Compare(const Outcome& a, const Outcome& b)
	{
		if (!b.refused.empty())
			return "PS2Native's provider does not carry out: " + b.refused;
		for (u32 k = 0; k < kSlices; k++)
		{
			const Snapshot& x = a.after[k];
			const Snapshot& y = b.after[k];
			char text[200];
			if (x.pc != y.pc || x.cycle != y.cycle || x.left != y.left)
			{
				std::snprintf(text, sizeof(text), "after stretch %u: PC %08X, cycle %llu, %d of the EE's cycles left; PS2Native %08X, %llu, %d", k, x.pc,
					static_cast<unsigned long long>(x.cycle), x.left, y.pc, static_cast<unsigned long long>(y.cycle), y.left);
				return text;
			}
			for (u32 r = 0; r < 34; r++)
			{
				if (x.gpr[r] != y.gpr[r])
				{
					std::snprintf(text, sizeof(text), "after stretch %u: register %u (32 HI, 33 LO) %08X, PS2Native %08X", k, r, x.gpr[r], y.gpr[r]);
					return text;
				}
			}
			for (u32 r = 0; r < 32; r++)
			{
				if (x.cp0[r] != y.cp0[r])
				{
					std::snprintf(text, sizeof(text), "after stretch %u: COP0 register %u %08X, PS2Native %08X", k, r, x.cp0[r], y.cp0[r]);
					return text;
				}
				if (x.cp2d[r] != y.cp2d[r] || x.cp2c[r] != y.cp2c[r])
				{
					std::snprintf(text, sizeof(text), "after stretch %u: the GTE's register %u: data %08X control %08X, PS2Native %08X %08X", k, r,
						x.cp2d[r], x.cp2c[r], y.cp2d[r], y.cp2c[r]);
					return text;
				}
			}
		}
		if (std::memcmp(a.data, b.data, sizeof(a.data)))
			return "the memory the program stores to";
		if (std::memcmp(a.code, b.code, sizeof(a.code)) || std::memcmp(a.vector, b.vector, sizeof(a.vector)))
			return "the program's own code in memory";
		if (a.starts != b.starts)
		{
			std::string text = "the addresses at which a block is held: PCSX2's recompiler";
			for (u32 at : a.starts)
				text += fmt::format(" {:X}", at);
			text += "; PS2Native";
			for (u32 at : b.starts)
				text += fmt::format(" {:X}", at);
			return text;
		}
		return {};
	}

	std::string Check(const Case& c)
	{
		static Outcome a, b;
		RunOn(&psxRec, false, c, a);
		RunOn(ps2n::iop::Provider(), true, c, b);
		return Compare(a, b);
	}

	// The same case with as many of its instructions made NOPs as still leave
	// it differing.
	Case Shrink(const Case& c)
	{
		Case small = c;
		for (u32 i = 0; i < c.count; i++)
		{
			if (small.words[i] == 0)
				continue;
			Case trial = small;
			trial.words[i] = 0;
			if (!Check(trial).empty())
				small = trial;
		}
		return small;
	}
} // namespace

int ps2n::selftest::CheckIopPrograms(uint32_t trials, uint64_t seed)
{
	Rng rng{seed ^ 0x494F50u};

	// What the machine holds that the runs change.
	const psxRegisters regs = psxRegs;
	R3000Acpu* const provider = psxCpu;
	const u64 next_start = psxNextStartCounter;
	const s32 next_delta = psxNextDeltaCounter;
	const u32 ictrl = psxHu32(HW_ICTRL), imask = psxHu32(HW_IMASK), istat = psxHu32(HW_ISTAT);
	const u64 ee_next = cpuRegs.nextEventCycle;
	const bool action = iopEventAction;
	std::vector<u8> vector(kVectorWords * 4), code(kCodeWords * 4), data(kDataBytes);
	std::memcpy(vector.data(), Ram32(kVector), vector.size());
	std::memcpy(code.data(), Ram32(kCode), code.size());
	std::memcpy(data.data(), iopMem->Main + kData, data.size());

	int failures = 0;
	u64 instructions = 0;
	const ps2n::iop::Stats before = ps2n::iop::GetStats();
	for (u32 trial = 0; trial < trials; trial++)
	{
		const Case c = Make(rng);
		instructions += c.count;
		const std::string difference = Check(c);
		if (difference.empty())
			continue;
		failures++;
		if (failures > 5)
			continue;
		const Case small = Shrink(c);
		std::fprintf(stderr, "selftest: IOP program %u: %s\n", trial, Check(small).c_str());
		std::fprintf(stderr, "selftest:   status %08X, an event test after %u cycles%s; the EE's cycles given:", small.status, small.next_event,
			small.interrupt ? " with an interrupt waiting" : "");
		for (s32 slice : small.slices)
			std::fprintf(stderr, " %d", slice);
		std::fprintf(stderr, "\nselftest:   registers:");
		for (u32 r = 1; r < 34; r++)
			std::fprintf(stderr, " %u=%08X", r, small.gpr[r]);
		std::fprintf(stderr, "\n");
		for (u32 i = 0; i < small.count + 10; i++)
		{
			if (small.words[i] == 0 && i < small.count)
				continue;
			const ps2n::r5900::Insn in(small.words[i], kCode + i * 4);
			std::fprintf(stderr, "selftest:   %08X %08X  %s (as the EE's disassembler reads it)\n", kCode + i * 4, small.words[i],
				ps2n::r5900::disassemble(in).c_str());
		}
	}

	psxRec.Reset();
	ps2n::iop::Provider()->Reset();
	psxRegs = regs;
	psxCpu = provider;
	psxNextStartCounter = next_start;
	psxNextDeltaCounter = next_delta;
	psxHu32(HW_ICTRL) = ictrl;
	psxHu32(HW_IMASK) = imask;
	psxHu32(HW_ISTAT) = istat;
	cpuRegs.nextEventCycle = ee_next;
	iopEventAction = action;
	std::memcpy(Ram32(kVector), vector.data(), vector.size());
	std::memcpy(Ram32(kCode), code.data(), code.size());
	std::memcpy(iopMem->Main + kData, data.data(), data.size());

	const ps2n::iop::Stats after = ps2n::iop::GetStats();
	std::fprintf(stderr,
		"selftest: IOP programs: %u programs, %llu instructions; PS2Native's provider formed %llu blocks for them, threw %llu away for "
		"stores into them, carried out %llu SYSCALL and BREAK instructions and %llu imports the core has a function for, and let a wait "
		"loop pass time %llu times; %d differ from PCSX2's IOP recompiler\n",
		trials, static_cast<unsigned long long>(instructions), static_cast<unsigned long long>(after.formed - before.formed),
		static_cast<unsigned long long>(after.thrown - before.thrown), static_cast<unsigned long long>(after.exceptions - before.exceptions),
		static_cast<unsigned long long>(after.imports - before.imports), static_cast<unsigned long long>(after.waits - before.waits), failures);
	return failures;
}
