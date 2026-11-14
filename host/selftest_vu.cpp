// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's VU interpreter (runtime/vu_interp.h) against PCSX2's VU
// recompiler (microVU), on random micro programs.
//
// A case is a program of a few random instruction pairs, ending with the E
// bit, and a random machine: the VF and VI registers, ACC, I, Q and P with
// their pending instances, R, the four instances of each flag, VU data
// memory. The program is run on microVU, the machine is put back as it
// was, and the program is run on the interpreter; then everything the VU
// holds is compared, its cycle count and TPC among it. The programs leave
// out what has an effect outside the VU (XGKICK), indirect jumps, what the
// interpreter does not carry out yet (the M bit, the T bit on a branch),
// and the forms whose results microVU takes from what its XMM allocator
// happens to hold, which the interpreter refuses and assemblers do not
// write (one component in the field of OPMULA and the EFU's sums, ESUM of
// VF00 without w). The T bit elsewhere is in them, with its
// interrupt enabled in half of the cases, and now and then a branch in a
// branch's delay slot. Their branches go forward, so that every program
// ends. Each mode is run three ways: clamp mode None, the same with the
// I-bit game fix (Killzone's settings), clamp mode Normal. A program that
// differs is made as small as it still differs in and printed, with the
// registers it uses as they began and as each side left them.

#include "selftest.h"

#include "runtime/vu_interp.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Config.h"
#include "pcsx2/VU.h"
#include "pcsx2/VUmicro.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

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

	// A float that exercises the arithmetic: mostly ordinary values of a few
	// magnitudes, some of every exponent, and the edges.
	// In one mode a third of the floats are NaN, of any sign and payload: where
	// both sources of an addition or multiplication are NaN, which one microVU
	// gives depends on the host registers its allocator chose.
	bool s_nanOften = false;

	u32 RandomFloat(Rng& rng)
	{
		const u32 kind = rng.Below(20);
		const u32 sign = rng.Below(2) << 31;
		if (s_nanOften && rng.Below(3) == 0)
			return sign | 0x7f800000u | (1 + rng.Below(0x7fffff));
		if (kind < 12)
			return sign | ((120u + rng.Below(16)) << 23) | (rng.Next32() & 0x7fffff);
		if (kind < 16)
			return sign | (rng.Below(256) << 23) | (rng.Next32() & 0x7fffff);
		static constexpr u32 kEdges[] = {0x00000000u, 0x3f800000u, 0x7f7fffffu, 0x7f800000u, 0x7fc00000u, 0x00000001u, 0x007fffffu,
			0x00800000u, 0x7fffffffu};
		if (kind < 19)
			return sign | kEdges[rng.Below(static_cast<u32>(std::size(kEdges)))];
		return rng.Next32();
	}

	// Registers drawn from a few, so that instructions depend on each other.
	u32 Vf(Rng& rng) { return rng.Below(8); }
	u32 Vi(Rng& rng) { return rng.Below(6); }
	u32 Dest(Rng& rng) { return 1 + rng.Below(15); }

	// Two components or more. With one, what OPMULA and the EFU's sums give
	// in microVU depends on whether its allocator holds the register already
	// (it loads the one component alone, or shuffles a whole one), which the
	// interpreter does not follow and refuses; real code gives them xyz.
	u32 MultiDest(Rng& rng)
	{
		static constexpr u32 kMasks[] = {3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
		return kMasks[rng.Below(static_cast<u32>(std::size(kMasks)))];
	}

	u32 RandomUpper(Rng& rng)
	{
		const u32 dest = Dest(rng), ft = Vf(rng), fs = Vf(rng), fd = Vf(rng);
		if (rng.Below(4) != 0)
			return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | rng.Below(0x30);
		for (;;)
		{
			const u32 col = rng.Below(4), row = rng.Below(12);
			if (col == 3 && row == 10)
				continue;
			const u32 d = (col == 2 && row == 11) ? MultiDest(rng) : dest; // OPMULA
			return (d << 21) | (ft << 16) | (fs << 11) | (row << 6) | 0x3C | col;
		}
	}

	constexpr u32 kNopUpper = 0x000002ffu;
	constexpr u32 kNopLower = 0x8000033cu;

	// In one mode a program holds encodings microVU knows nothing of
	// (mVUunknown, microVU_Tables.inl): it generates no code for one, and its
	// first pass ends the block, and with it the program, after an upper one
	// that is not in a delay slot. One upper word is exempt from that: the
	// lower NOP's, which the BIOS writes into the upper slots.
	bool s_unknownOften = false;

	u32 UnknownUpper(Rng& rng)
	{
		const u32 fields = (Dest(rng) << 21) | (Vf(rng) << 16) | (Vf(rng) << 11);
		switch (rng.Below(3))
		{
			case 0: // the first table's empty entries
				return fields | (Vf(rng) << 6) | (0x30 + rng.Below(12));
			case 1: // the four second tables' rows from 12 on
				return fields | ((12 + rng.Below(20)) << 6) | 0x3C | rng.Below(4);
			default: // and the one hole among their first twelve
				return fields | (10u << 6) | 0x3F;
		}
	}

	u32 UnknownLower(Rng& rng)
	{
		const u32 low = rng.Next32() & 0x01ffffffu;
		switch (rng.Below(4))
		{
			case 0: // the holes among the first table's instructions
			{
				static constexpr u32 kOps[] = {0x02, 0x03, 0x06, 0x07, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x19, 0x1D, 0x1E, 0x1F, 0x22, 0x23,
					0x26, 0x27, 0x2A, 0x2B};
				return (kOps[rng.Below(static_cast<u32>(std::size(kOps)))] << 25) | low;
			}
			case 1: // its entries after them, but for the one that leads to the second table
			{
				const u32 op = 0x30 + rng.Below(0x4F);
				return ((op >= 0x40 ? op + 1 : op) << 25) | low;
			}
			case 2: // the second table's empty entries
			{
				static constexpr u32 kLast[] = {0x33, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B};
				const u32 k = rng.Below(0x30 + static_cast<u32>(std::size(kLast)));
				return (0x40u << 25) | (low & ~0x3fu) | (k < 0x30 ? k : kLast[k - 0x30]);
			}
			default: // the rows of the four third tables that have no instruction
			{
				static constexpr u32 kRows[4] = {
					(1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 16) | (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29) |
						(1u << 30) | (1u << 31),
					(1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 16) | (1u << 26) | (1u << 28) | (1u << 29) | (1u << 30) | (1u << 31),
					(1u << 13) | (1u << 14) | (1u << 15) | (1u << 16) | (1u << 28) | (1u << 29) | (1u << 30) | (1u << 31),
					(1u << 13) | (1u << 14) | (1u << 15) | (1u << 16) | (1u << 28) | (1u << 30),
				};
				for (;;)
				{
					const u32 table = rng.Below(4), row = rng.Below(32);
					if (!(kRows[table] & (1u << row)))
						return (0x40u << 25) | (low & ~0x7ffu) | (row << 6) | 0x3C | table;
				}
			}
		}
	}

	// A lower instruction for pair `pair` of `count`; a branch only where its
	// delay slot is not the program's last pairs, and forward.
	u32 RandomLower(Rng& rng, bool vu1, u32 pair, u32 count, bool mayBranch, bool& branched)
	{
		branched = false;
		const u32 dest = Dest(rng);
		const u32 ft = Vf(rng), fs = Vf(rng);
		const u32 it = Vi(rng), is = Vi(rng), id = Vi(rng);
		const u32 imm11 = rng.Below(64) - 32;
		for (;;)
		{
			switch (rng.Below(10))
			{
				case 0: // loads, stores and the integer immediates
				{
					static constexpr u32 kOps[] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09};
					const u32 op = kOps[rng.Below(6)];
					if (op == 0x08 || op == 0x09)
						return (op << 25) | (((rng.Next32() >> 11) & 0xF) << 21) | (it << 16) | (is << 11) | (rng.Next32() & 0x7ff);
					const u32 a = (op == 0x01) ? ((fs << 11) | (it << 16)) : ((ft << 16) | (is << 11));
					return (op << 25) | (dest << 21) | a | (imm11 & 0x7ff);
				}
				case 1: // the flag instructions
				{
					static constexpr u32 kOps[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C};
					const u32 op = kOps[rng.Below(12)];
					if (op >= 0x10 && op <= 0x13)
						return (op << 25) | (rng.Next32() & 0xffffff);
					if (op >= 0x14 && op <= 0x17)
						return (op << 25) | (((rng.Next32() >> 11) & 1) << 21) | (it << 16) | (rng.Next32() & 0x7ff);
					return (op << 25) | (it << 16) | (is << 11);
				}
				case 2: // branches, forward, their delay slot inside the program
				{
					if (!mayBranch || pair + 3 > count)
						continue;
					const u32 target = pair + 2 + rng.Below(count - pair - 1); // a pair from after the delay slot to the end
					const u32 offset = target - pair - 1;
					static constexpr u32 kOps[] = {0x20, 0x21, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F};
					branched = true;
					return (kOps[rng.Below(8)] << 25) | (it << 16) | (is << 11) | (offset & 0x7ff);
				}
				case 3: // the integer arithmetic
				{
					static constexpr u32 kOps[] = {0x30, 0x31, 0x32, 0x34, 0x35};
					const u32 op = kOps[rng.Below(5)];
					if (op == 0x32)
						return (0x40u << 25) | (it << 16) | (is << 11) | (rng.Below(32) << 6) | op;
					return (0x40u << 25) | (it << 16) | (is << 11) | (id << 6) | op;
				}
				default: // the fourth table: moves, FDIV, EFU, R, the other loads and stores
				{
					static constexpr struct
					{
						u32 table, row;
					} kT3[] = {{0, 12}, {0, 13}, {0, 14}, {0, 15}, {0, 16}, {0, 25}, {0, 26}, {0, 28}, {0, 29}, {0, 30}, {0, 31}, {1, 12},
						{1, 13}, {1, 14}, {1, 15}, {1, 16}, {1, 26}, {1, 28}, {1, 29}, {1, 30}, {1, 31}, {2, 13}, {2, 14}, {2, 15}, {2, 16},
						{2, 28}, {2, 29}, {2, 30}, {2, 31}, {3, 13}, {3, 14}, {3, 15}, {3, 16}, {3, 28}, {3, 30}};
					const auto& t = kT3[rng.Below(static_cast<u32>(std::size(kT3)))];
					const bool reg5 = t.table == 0 ? (t.row == 14 || t.row == 15 || t.row == 30 || t.row == 31)
												   : t.table == 1 ? (t.row == 14 || t.row == 30 || t.row == 31)
																  : t.table == 2 ? (t.row == 14 || t.row == 16 || t.row == 30 || t.row == 31)
																				 : (t.row == 16);
					const bool sum = t.row == 28 || (t.table == 2 && t.row == 29); // the EFU's sums
					u32 fields = reg5 ? ((rng.Below(16)) << 21) : ((sum ? MultiDest(rng) : dest) << 21); // fsf, ftf where an instruction has them
					// ESUM of VF00 without w: what microVU gives depends on its
					// allocator again (zeros, or VF00 whole).
					if (t.table == 2 && t.row == 29 && fs == 0)
						fields |= 1u << 21;
					u32 regs = (t.row == 13 || t.row == 15) ? ((rng.Below(2) ? ft : it) << 16) | (is << 11) : (ft << 16) | (fs << 11);
					// VU1's LQD and SQD with VI00 for the address: microVU loads and
					// stores 16 bytes at a place that is not aligned to them there, and
					// the host faults. Not a case a program can use.
					if (vu1 && t.row == 13 && t.table >= 2)
						regs = t.table == 2 ? ((ft << 16) | ((1 + rng.Below(5)) << 11)) : (((1 + rng.Below(5)) << 16) | (fs << 11));
					return (0x40u << 25) | fields | regs | (t.row << 6) | 0x3C | t.table;
				}
			}
		}
	}

	struct Machine
	{
		alignas(16) u32 VF[32][4];
		u32 VI[32];
		u32 ACC[4];
		u32 pending_q, pending_p;
		u32 macflags[4], clipflags[4], statusflags[4];
		u64 cycle;
		u32 stat; // this VU's bits of VPU_STAT: busy, D, T
		std::vector<u8> mem;
		alignas(16) u8 vu1Regs[32 * 16 + 32 * 16]; // VU0 reaches VU1's registers
	};

	VURegs& Regs(int vu) { return vu ? VU1 : VU0; }
	u32 MemSize(int vu) { return vu ? 0x4000u : 0x1000u; }

	void Write(int vu, const Machine& m)
	{
		VURegs& R = Regs(vu);
		std::memcpy(R.VF, m.VF, sizeof(m.VF));
		for (int i = 0; i < 32; i++)
			R.VI[i].UL = m.VI[i];
		std::memcpy(R.ACC.UL, m.ACC, sizeof(m.ACC));
		R.pending_q = m.pending_q;
		R.pending_p = m.pending_p;
		std::memcpy(R.micro_macflags, m.macflags, sizeof(m.macflags));
		std::memcpy(R.micro_clipflags, m.clipflags, sizeof(m.clipflags));
		std::memcpy(R.micro_statusflags, m.statusflags, sizeof(m.statusflags));
		R.cycle = m.cycle;
		R.nextBlockCycles = 0;
		R.start_pc = 0;
		const u32 mask = vu ? 0x700u : 0x7u;
		VU0.VI[REG_VPU_STAT].UL = (VU0.VI[REG_VPU_STAT].UL & ~mask) | m.stat;
		std::memcpy(R.Mem, m.mem.data(), m.mem.size());
		if (vu == 0)
			std::memcpy(VU1.VF, m.vu1Regs, sizeof(m.vu1Regs));
	}

	Machine Read(int vu)
	{
		Machine m;
		const VURegs& R = Regs(vu);
		std::memcpy(m.VF, R.VF, sizeof(m.VF));
		for (int i = 0; i < 32; i++)
			m.VI[i] = R.VI[i].UL;
		std::memcpy(m.ACC, R.ACC.UL, sizeof(m.ACC));
		m.pending_q = R.pending_q;
		m.pending_p = R.pending_p;
		std::memcpy(m.macflags, R.micro_macflags, sizeof(m.macflags));
		std::memcpy(m.clipflags, R.micro_clipflags, sizeof(m.clipflags));
		std::memcpy(m.statusflags, R.micro_statusflags, sizeof(m.statusflags));
		m.cycle = R.cycle;
		m.stat = VU0.VI[REG_VPU_STAT].UL & (vu ? 0x700u : 0x7u);
		m.mem.assign(R.Mem, R.Mem + MemSize(vu));
		std::memcpy(m.vu1Regs, VU1.VF, sizeof(m.vu1Regs));
		return m;
	}

	Machine RandomMachine(int vu, Rng& rng)
	{
		Machine m = Read(vu);
		for (int r = 1; r < 32; r++)
		{
			for (int c = 0; c < 4; c++)
				m.VF[r][c] = RandomFloat(rng);
		}
		m.VF[0][0] = m.VF[0][1] = m.VF[0][2] = 0;
		m.VF[0][3] = 0x3f800000u;
		m.VI[0] = 0;
		for (int r = 1; r < 16; r++)
			m.VI[r] = rng.Next32() & 0xffff;
		for (int c = 0; c < 4; c++)
			m.ACC[c] = RandomFloat(rng);
		m.VI[REG_R] = 0x3f800000u | (rng.Next32() & 0x7fffff);
		m.VI[REG_I] = RandomFloat(rng);
		m.VI[REG_Q] = RandomFloat(rng);
		m.VI[REG_P] = RandomFloat(rng);
		m.pending_q = RandomFloat(rng);
		m.pending_p = RandomFloat(rng);
		for (int i = 0; i < 4; i++)
		{
			m.macflags[i] = rng.Next32() & 0xffff;
			m.clipflags[i] = rng.Next32() & 0xffffff;
			m.statusflags[i] = rng.Next32() & 0x3cfffffu;
		}
		m.VI[REG_TPC] = 0;
		m.cycle = 1000;
		m.stat = vu ? 0x100u : 0x1u;
		for (u8& b : m.mem)
			b = static_cast<u8>(rng.Next32());
		return m;
	}

	// The first difference between two machines, in words.
	std::string Compare(int vu, const Machine& a, const Machine& b)
	{
		char text[200];
		for (int r = 0; r < 32; r++)
		{
			for (int c = 0; c < 4; c++)
			{
				if (a.VF[r][c] != b.VF[r][c])
				{
					std::snprintf(text, sizeof(text), "vf%02d.%c: microVU %08x, PS2Native %08x", r, "xyzw"[c], a.VF[r][c], b.VF[r][c]);
					return text;
				}
			}
		}
		for (int r = 0; r < 32; r++)
		{
			if (a.VI[r] != b.VI[r])
			{
				std::snprintf(text, sizeof(text), "vi%02d: microVU %08x, PS2Native %08x", r, a.VI[r], b.VI[r]);
				return text;
			}
		}
		for (int c = 0; c < 4; c++)
		{
			if (a.ACC[c] != b.ACC[c])
			{
				std::snprintf(text, sizeof(text), "ACC.%c: microVU %08x, PS2Native %08x", "xyzw"[c], a.ACC[c], b.ACC[c]);
				return text;
			}
		}
		if (a.pending_q != b.pending_q || a.pending_p != b.pending_p)
		{
			std::snprintf(text, sizeof(text), "pending Q/P: microVU %08x/%08x, PS2Native %08x/%08x", a.pending_q, a.pending_p, b.pending_q,
				b.pending_p);
			return text;
		}
		for (int i = 0; i < 4; i++)
		{
			if (a.macflags[i] != b.macflags[i] || a.clipflags[i] != b.clipflags[i] || a.statusflags[i] != b.statusflags[i])
			{
				std::snprintf(text, sizeof(text), "flag instance %d (status, MAC, clip): microVU %08x %04x %06x, PS2Native %08x %04x %06x", i,
					a.statusflags[i], a.macflags[i], a.clipflags[i], b.statusflags[i], b.macflags[i], b.clipflags[i]);
				return text;
			}
		}
		if (a.cycle != b.cycle)
		{
			std::snprintf(text, sizeof(text), "cycle count: microVU %llu, PS2Native %llu", static_cast<unsigned long long>(a.cycle),
				static_cast<unsigned long long>(b.cycle));
			return text;
		}
		if (a.stat != b.stat)
		{
			std::snprintf(text, sizeof(text), "VPU_STAT: microVU %03x, PS2Native %03x", a.stat, b.stat);
			return text;
		}
		for (size_t i = 0; i < a.mem.size(); i += 4)
		{
			u32 x, y;
			std::memcpy(&x, &a.mem[i], 4);
			std::memcpy(&y, &b.mem[i], 4);
			if (x != y)
			{
				std::snprintf(text, sizeof(text), "data memory at %04zx: microVU %08x, PS2Native %08x", i, x, y);
				return text;
			}
		}
		if (vu == 0 && std::memcmp(a.vu1Regs, b.vu1Regs, sizeof(a.vu1Regs)) != 0)
			return "VU1's registers, through VU0's memory";
		return {};
	}

	// ---- a pair in words, for the report ---------------------------------------------------
	std::string UpperName(u32 c)
	{
		static const char* const kBc[4] = {"x", "y", "z", "w"};
		const u32 op = c & 0x3f;
		std::string name;
		if (op < 0x30)
		{
			static const char* const kRows[12][4] = {{"ADD", "ADD", "ADD", "ADD"}, {"SUB", "SUB", "SUB", "SUB"}, {"MADD", "MADD", "MADD", "MADD"},
				{"MSUB", "MSUB", "MSUB", "MSUB"}, {"MAX", "MAX", "MAX", "MAX"}, {"MINI", "MINI", "MINI", "MINI"}, {"MUL", "MUL", "MUL", "MUL"},
				{"MULq", "MAXi", "MULi", "MINIi"}, {"ADDq", "MADDq", "ADDi", "MADDi"}, {"SUBq", "MSUBq", "SUBi", "MSUBi"},
				{"ADD", "MADD", "MUL", "MAX"}, {"SUB", "MSUB", "OPMSUB", "MINI"}};
			name = kRows[op >> 2][op & 3];
			if ((op >> 2) <= 6)
				name += kBc[op & 3];
		}
		else if (op < 0x3c)
		{
			return "?";
		}
		else
		{
			const u32 row = (c >> 6) & 0x1f, col = op & 3;
			static const char* const kAcc[4] = {"ADDA", "SUBA", "MADDA", "MSUBA"};
			if (row <= 3)
				name = std::string(kAcc[row]) + kBc[col];
			else if (row == 4)
				name = std::string("ITOF") + (col == 0 ? "0" : col == 1 ? "4" : col == 2 ? "12" : "15");
			else if (row == 5)
				name = std::string("FTOI") + (col == 0 ? "0" : col == 1 ? "4" : col == 2 ? "12" : "15");
			else if (row == 6)
				name = std::string("MULA") + kBc[col];
			else if (row <= 11)
			{
				static const char* const kT[4][5] = {{"MULAq", "ADDAq", "SUBAq", "ADDA", "SUBA"}, {"ABS", "MADDAq", "MSUBAq", "MADDA", "MSUBA"},
					{"MULAi", "ADDAi", "SUBAi", "MULA", "OPMULA"}, {"CLIP", "MADDAi", "MSUBAi", "?", "NOP"}};
				name = kT[col][row - 7];
			}
			else
				return "?";
		}
		const u32 dest = (c >> 21) & 0xf;
		char text[64];
		std::snprintf(text, sizeof(text), "%s.%s%s%s%s fd%u fs%u ft%u", name.c_str(), (dest & 8) ? "x" : "", (dest & 4) ? "y" : "",
			(dest & 2) ? "z" : "", (dest & 1) ? "w" : "", (c >> 6) & 31, (c >> 11) & 31, (c >> 16) & 31);
		return text;
	}

	std::string LowerName(u32 c)
	{
		const u32 op = c >> 25;
		static const struct
		{
			u32 op;
			const char* name;
		} kOps[] = {{0x00, "LQ"}, {0x01, "SQ"}, {0x04, "ILW"}, {0x05, "ISW"}, {0x08, "IADDIU"}, {0x09, "ISUBIU"}, {0x10, "FCEQ"},
			{0x11, "FCSET"}, {0x12, "FCAND"}, {0x13, "FCOR"}, {0x14, "FSEQ"}, {0x15, "FSSET"}, {0x16, "FSAND"}, {0x17, "FSOR"}, {0x18, "FMEQ"},
			{0x1A, "FMAND"}, {0x1B, "FMOR"}, {0x1C, "FCGET"}, {0x20, "B"}, {0x21, "BAL"}, {0x24, "JR"}, {0x25, "JALR"}, {0x28, "IBEQ"},
			{0x29, "IBNE"}, {0x2C, "IBLTZ"}, {0x2D, "IBGTZ"}, {0x2E, "IBLEZ"}, {0x2F, "IBGEZ"}};
		std::string name = "?";
		if (op == 0x40)
		{
			const u32 low = c & 0x3f;
			if (low == 0x30) name = "IADD";
			else if (low == 0x31) name = "ISUB";
			else if (low == 0x32) name = "IADDI";
			else if (low == 0x34) name = "IAND";
			else if (low == 0x35) name = "IOR";
			else if (low >= 0x3c)
			{
				static const char* const kT3[4][32] = {
					{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "MOVE", "LQI", "DIV",
						"MTIR", "RNEXT", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "MFP", "XTOP", "XGKICK", "ESADD",
						"EATANxy", "ESQRT", "ESIN"},
					{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "MR32", "SQI", "SQRT",
						"MFIR", "RGET", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "XITOP", nullptr, "ERSADD",
						"EATANxz", "ERSQRT", "EATAN"},
					{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "LQD", "RSQRT",
						"ILWR", "RINIT", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "ELENG",
						"ESUM", "ERCPR", "EEXP"},
					{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "SQD", "WAITQ",
						"ISWR", "RXOR", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "ERLENG",
						nullptr, "WAITP", nullptr}};
				const char* n = kT3[low & 3][(c >> 6) & 0x1f];
				name = n ? n : "?";
			}
		}
		else
		{
			for (const auto& k : kOps)
			{
				if (k.op == op)
					name = k.name;
			}
		}
		char text[96];
		std::snprintf(text, sizeof(text), "%s dest%x t%u s%u d%u imm%03x", name.c_str(), (c >> 21) & 0xf, (c >> 16) & 31, (c >> 11) & 31,
			(c >> 6) & 31, c & 0x7ff);
		return text;
	}

	// ---- one case ---------------------------------------------------------------------------
	struct Case
	{
		std::vector<u32> words;
		u32 count; // pairs before the E bit's
		Machine start;
		u32 fbrst;
		std::vector<u32> slices; // empty: one run to the end
	};

	// Runs the case on microVU and on the interpreter; what differs, or nothing.
	std::string RunCase(int vu, const Case& c, Machine* outA = nullptr, Machine* outB = nullptr)
	{
		BaseVUmicroCPU& stock = vu ? static_cast<BaseVUmicroCPU&>(CpuMicroVU1) : static_cast<BaseVUmicroCPU&>(CpuMicroVU0);
		BaseVUmicroCPU& ours = *ps2n::vu::Interpreter(vu);
		VURegs& R = Regs(vu);
		const u32 memSize = MemSize(vu);
		auto run = [&](BaseVUmicroCPU& cpu, std::vector<u64>& trail) {
			Write(vu, c.start);
			std::memcpy(R.Micro, c.words.data(), memSize);
			R.VI[REG_TPC].UL = 0;
			VU0.VI[REG_FBRST].UL = c.fbrst;
			cpu.Clear(0, memSize);
			cpu.SetStartPC(0);
			if (c.slices.empty())
			{
				cpu.Execute(100000);
			}
			else
			{
				const u32 busy = vu ? 0x100u : 0x1u;
				for (size_t k = 0; (VU0.VI[REG_VPU_STAT].UL & busy) && k < 4096; k++)
				{
					cpu.Execute(c.slices[k % c.slices.size()]);
					u64 fold = R.cycle * 0x9E3779B97F4A7C15ull ^ R.VI[REG_TPC].UL;
					for (int r = 0; r < 16; r++)
						fold = (fold ^ R.VI[r].UL) * 0x100000001B3ull;
					trail.push_back(fold);
				}
			}
			return Read(vu);
		};
		std::vector<u64> trailA, trailB;
		const Machine a = run(stock, trailA);
		const Machine b = run(ours, trailB);
		if (outA)
			*outA = a;
		if (outB)
			*outB = b;
		if (const char* what = ps2n::vu::Unsupported(vu))
			return what;
		std::string difference = Compare(vu, a, b);
		if (difference.empty() && trailA != trailB)
		{
			size_t k = 0;
			while (k < trailA.size() && k < trailB.size() && trailA[k] == trailB[k])
				k++;
			difference = "the slices part at slice " + std::to_string(k) + " (cycle count, TPC or VI registers)";
		}
		return difference;
	}

	// A case that differs, made as small as it still differs in: pairs made NOPs,
	// one at a time, upper and lower apart.
	Case Shrink(int vu, Case c)
	{
		bool changed = true;
		for (int round = 0; changed && round < 8; round++)
		{
			changed = false;
			for (u32 i = 0; i < c.count; i++)
			{
				for (int half = 0; half < 3; half++)
				{
					Case t = c;
					u32& lower = t.words[i * 2];
					u32& upper = t.words[i * 2 + 1];
					const u32 bits = upper & 0xf8000000u;
					if (half == 0 || half == 2)
					{
						if ((upper & ~0xf8000000u) == kNopUpper && half == 0)
							continue;
						upper = (bits & ~0x80000000u) | kNopUpper; // the I bit goes with its immediate
						if (bits & 0x80000000u)
							lower = kNopLower;
					}
					if (half == 1 || half == 2)
					{
						if (lower == kNopLower || (upper & 0x80000000u))
							continue;
						lower = kNopLower;
					}
					if (t.words == c.words)
						continue;
					if (!RunCase(vu, t).empty())
					{
						c = t;
						changed = true;
						break;
					}
				}
			}
		}
		return c;
	}

	// `run`: how many of the programs are run, from the first; the rest are
	// only made, so that the random numbers go on as if they had run.
	int CheckPrograms(int vu, u32 trials, u32 run, Rng& rng, const char* what)
	{
		const u32 memSize = MemSize(vu);
		int failures = 0;
		u32 pairs = 0;
		u32 ran = 0;
		const u64 unknownBefore = ps2n::vu::Stats(vu).unknown;
		for (u32 trial = 0; trial < trials; trial++)
		{
			// The program: count pairs, the last with the E bit, then its delay slot.
			Case c;
			c.count = 2 + rng.Below(14);
			const u32 count = c.count;
			c.words.assign(memSize / 4, 0);
			bool delaySlot = false;
			bool afterEvil = false; // the pair after a branch in a delay slot: no third
			for (u32 i = 0; i < count; i++)
			{
				u32 upper = RandomUpper(rng);
				u32 lower;
				bool branched = false;
				if (!delaySlot && rng.Below(12) == 0)
				{
					upper |= 0x80000000u; // the I bit: the lower word is I
					lower = RandomFloat(rng);
				}
				else
				{
					// Sometimes a branch in a branch's delay slot.
					const bool mayBranch = !afterEvil && (!delaySlot || rng.Below(4) == 0);
					lower = RandomLower(rng, vu != 0, i, count, mayBranch, branched);
				}
				// In the mode of the encodings microVU knows nothing of: one in the
				// upper slot, one in the lower slot, or the BIOS's two NOPs in each
				// other's slots (the upper one's word is then I's value).
				bool reversed = false;
				if (s_unknownOften)
				{
					const u32 kind = rng.Below(10);
					if (kind == 0)
					{
						upper = (upper & 0x80000000u) | UnknownUpper(rng);
					}
					else if (kind == 1 && !(upper & 0x80000000u))
					{
						lower = UnknownLower(rng);
						branched = false;
					}
					else if (kind == 2 && !delaySlot)
					{
						upper = kNopLower;
						lower = kNopUpper;
						branched = false;
						reversed = true;
					}
				}
				afterEvil = delaySlot && branched;
				// The T bit, sometimes, where the interpreter carries it out (not
				// on a branch), and the D bit, which microVU leaves alone.
				if (!branched && !reversed && rng.Below(16) == 0)
					upper |= 0x08000000u;
				if (!reversed && rng.Below(16) == 0)
					upper |= 0x10000000u;
				c.words[i * 2] = lower;
				c.words[i * 2 + 1] = upper;
				delaySlot = branched;
			}
			c.words[count * 2] = kNopLower;
			c.words[count * 2 + 1] = kNopUpper | 0x40000000u; // E
			c.words[count * 2 + 2] = kNopLower;
			c.words[count * 2 + 3] = kNopUpper;
			// In that mode, sometimes on the E bit's pair too, where the block ends
			// before the E bit's delay slot, and in the delay slot.
			if (s_unknownOften && rng.Below(6) == 0)
				c.words[count * 2 + 1] = UnknownUpper(rng) | 0x40000000u;
			if (s_unknownOften && rng.Below(6) == 0)
				c.words[count * 2 + 3] = UnknownUpper(rng);
			if (trial < run)
				pairs += count + 2;

			c.start = RandomMachine(vu, rng);
			c.fbrst = rng.Below(2) ? 0x808u : 0u; // the T bit enabled for both VUs, or not
			// Half the programs run in slices of a few cycles, as the core runs VU0
			// beside the EE: each slice may stop the program at a block and the next
			// go on from there.
			if (rng.Below(2))
			{
				for (int k = 0; k < 64; k++)
					c.slices.push_back(1 + rng.Below(48));
			}

			if (trial >= run)
				continue;
			ran++;
			const std::string difference = RunCase(vu, c);
			if (difference.empty())
				continue;
			failures++;
			if (failures <= 5)
			{
				const Case small = Shrink(vu, c);
				std::fprintf(stderr, "selftest: VU%d program %u (%s%s): %s\n", vu, trial, what, c.slices.empty() ? "" : ", in slices",
					RunCase(vu, small).c_str());
				// The registers the programs use, as they began and as each left them.
				{
					Machine ma, mb;
					RunCase(vu, small, &ma, &mb);
					const Machine& m0 = small.start;
					auto row = [](const char* name, const u32* v0, const u32* va, const u32* vb) {
						std::fprintf(stderr, "selftest:     %-5s %08x %08x %08x %08x -> microVU %08x %08x %08x %08x, PS2Native %08x %08x %08x %08x\n", name,
							v0[0], v0[1], v0[2], v0[3], va[0], va[1], va[2], va[3], vb[0], vb[1], vb[2], vb[3]);
					};
					for (int r = 1; r < 8; r++)
					{
						char name[8];
						std::snprintf(name, sizeof(name), "vf%02d", r);
						row(name, m0.VF[r], ma.VF[r], mb.VF[r]);
					}
					row("ACC", m0.ACC, ma.ACC, mb.ACC);
					const u32 s0[4] = {m0.VI[REG_I], m0.VI[REG_Q], m0.pending_q, m0.VI[REG_P]};
					const u32 sa[4] = {ma.VI[REG_I], ma.VI[REG_Q], ma.pending_q, ma.VI[REG_P]};
					const u32 sb[4] = {mb.VI[REG_I], mb.VI[REG_Q], mb.pending_q, mb.VI[REG_P]};
					row("IQqP", s0, sa, sb);
					std::fprintf(stderr, "selftest:     vi01-05 %04x %04x %04x %04x %04x, FBRST %03x\n", m0.VI[1], m0.VI[2], m0.VI[3], m0.VI[4], m0.VI[5], small.fbrst);
				}
				for (u32 i = 0; i < small.count + 2; i++) // the E bit's pair and its delay slot too
				{
					const u32 upper = small.words[i * 2 + 1], lower = small.words[i * 2];
					if ((upper & ~0xf8000000u) == kNopUpper && lower == kNopLower && !(upper & 0xf8000000u))
						continue;
					std::fprintf(stderr, "selftest:   %04x %08x:%08x %s%s%s%s | %s\n", i * 8, upper, lower, (upper & 0x80000000u) ? "[I] " : "",
						(upper & 0x40000000u) ? "[E] " : "", (upper & 0x08000000u) ? "[T] " : "", UpperName(upper).c_str(),
						(upper & 0x80000000u) ? "(I)" : LowerName(lower).c_str());
				}
			}
			if (ps2n::vu::Unsupported(vu))
				ps2n::vu::ClearUnsupported(vu); // for the next case
		}
		// How many instructions of such encodings were in the blocks analysed, the
		// ones of programs made smaller for a report among them.
		char unknown[64] = "";
		if (s_unknownOften)
		{
			std::snprintf(unknown, sizeof(unknown), " (%llu instructions of such encodings in the blocks analysed)",
				static_cast<unsigned long long>(ps2n::vu::Stats(vu).unknown - unknownBefore));
		}
		std::fprintf(stderr, "selftest: VU%d micro programs (%s): %u programs, %u instruction pairs%s, %d differ from PCSX2's VU recompiler\n",
			vu, what, ran, pairs, unknown, failures);
		return failures;
	}
} // namespace

int ps2n::selftest::CheckVuPrograms(uint32_t trials, uint32_t run, uint64_t seed)
{
	Rng rng{seed ^ 0x5655u};
	// The last mode has random numbers of its own: the programs of the others
	// are the ones they were before it was added.
	Rng rngUnknown{seed ^ 0x554E4B4E4F574E55ull};
	auto& fixes = EmuConfig.Gamefixes;
	const bool ibit = fixes.IbitHack;
	const u32 fbrst = VU0.VI[REG_FBRST].UL;
	auto& rec = EmuConfig.Cpu.Recompiler;
	const bool o0 = rec.vu0Overflow, e0 = rec.vu0ExtraOverflow, s0 = rec.vu0SignOverflow;
	const bool o1 = rec.vu1Overflow, e1 = rec.vu1ExtraOverflow, s1 = rec.vu1SignOverflow;
	static const struct
	{
		bool overflow, ibit, nans, unknown;
		const char* what;
	} kModes[] = {
		{false, false, false, false, "clamp mode None"},
		{false, true, false, false, "clamp mode None and the I-bit game fix (Killzone's)"},
		{true, false, false, false, "clamp mode Normal"},
		{false, false, true, false, "clamp mode None, a third of the floats NaN"},
		{false, false, false, true, "clamp mode None, with encodings PCSX2's VU recompiler knows nothing of"},
	};
	int failures = 0;
	for (int vu = 0; vu < 2; vu++)
	{
		for (const auto& mode : kModes)
		{
			rec.vu0Overflow = rec.vu1Overflow = mode.overflow;
			rec.vu0ExtraOverflow = rec.vu1ExtraOverflow = false;
			rec.vu0SignOverflow = rec.vu1SignOverflow = false;
			fixes.IbitHack = mode.ibit;
			s_nanOften = mode.nans;
			s_unknownOften = mode.unknown;
			failures += CheckPrograms(vu, trials, run, mode.unknown ? rngUnknown : rng, mode.what);
		}
	}
	s_nanOften = false;
	s_unknownOften = false;
	rec.vu0Overflow = o0;
	rec.vu0ExtraOverflow = e0;
	rec.vu0SignOverflow = s0;
	rec.vu1Overflow = o1;
	rec.vu1ExtraOverflow = e1;
	rec.vu1SignOverflow = s1;
	fixes.IbitHack = ibit;
	VU0.VI[REG_FBRST].UL = fbrst;
	VU0.VI[REG_VPU_STAT].UL &= ~0x707u;
	return failures;
}
