// SPDX-License-Identifier: GPL-3.0-or-later
//
// See rec_blocks.h. Each part names the function of PCSX2's recompiler it
// transcribes (PCSX2 v2.9.170, pcsx2/x86, Copyright 2002-2026 PCSX2 Dev Team,
// GPL-3.0+).

#include "rec_blocks.h"

#include "ps2n/rec_rules.h"
#include "ps2n/waitloop.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Patch.h"
#include "pcsx2/R5900OpcodeTables.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VU.h"
#include "pcsx2/vtlb.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_set>

using namespace ps2n::ee::blocks;
using ps2n::r5900::Insn;
using ps2n::r5900::Op;

namespace
{
	Stats s_stats;

	// The blocks, by physical address. A map, because throwing blocks away
	// walks them in address order; its nodes do not move, so a Block* stays
	// good until its block is thrown away.
	std::map<u32, Block> s_blocks;

	// The same, to find one quickly: a slot for every instruction of RAM and
	// of the ROMs (the recompiler's recLUT).
	Block** s_ram_slots = nullptr;
	Block** s_rom_slots = nullptr; // 1FC00000-1FFFFFFF
	Block** s_rom12_slots = nullptr; // 1E000000-1E7FFFFF
	u32 s_ram_size = 0;

	// The highest address a block has ended at, as the recompiler keeps it
	// (maxrecmem): a request to clear from there up is ignored.
	u32 s_max_address = 0;

	// For every page of RAM: how much has been executed of the blocks that
	// compare themselves with memory, and how often the page has been put back
	// under write protection (manual_page, manual_counter).
	u16* s_manual_page = nullptr;
	u8* s_manual_counter = nullptr;

	// RAM as it was when the latest block over each part of it was formed
	// (recRAMCopy).
	u8* s_ram_copy = nullptr;

	// Loads and stores that have hit memory that is not plain, by the address
	// the recompiler knows them under (vtlb's s_fastmem_faulting_pcs).
	std::unordered_set<u32> s_faulting;

	// The block being formed (s_pCurBlock): throwing blocks away spares it.
	Block* s_forming = nullptr;

	// Blocks thrown away since the last Collect(): one of them may still be
	// being carried out.
	std::vector<std::map<u32, Block>::node_type> s_retired;

	constexpr u32 kNowhere = ~0u;

	void Allocate()
	{
		if (s_ram_slots && s_ram_size == Ps2MemSize::ExposedRam)
			return;

		std::free(s_ram_slots);
		std::free(s_rom_slots);
		std::free(s_rom12_slots);
		std::free(s_manual_page);
		std::free(s_manual_counter);
		std::free(s_ram_copy);

		// Zeroed, and only taken from the system as it is touched.
		s_ram_size = Ps2MemSize::ExposedRam;
		s_ram_slots = static_cast<Block**>(std::calloc(s_ram_size / 4, sizeof(Block*)));
		s_rom_slots = static_cast<Block**>(std::calloc(Ps2MemSize::Rom / 4, sizeof(Block*)));
		s_rom12_slots = static_cast<Block**>(std::calloc((Ps2MemSize::Rom1 + Ps2MemSize::Rom2) / 4, sizeof(Block*)));
		s_manual_page = static_cast<u16*>(std::calloc(Ps2MemSize::TotalRam >> 12, sizeof(u16)));
		s_manual_counter = static_cast<u8*>(std::calloc(Ps2MemSize::TotalRam >> 12, sizeof(u8)));
		s_ram_copy = static_cast<u8*>(std::calloc(s_ram_size, 1));
		pxAssertRel(s_ram_slots && s_rom_slots && s_rom12_slots && s_manual_page && s_manual_counter && s_ram_copy,
			"ps2n: no memory for the table of blocks");
	}

	// The slot for physical address `hw`, which Physical() gave.
	__fi Block** Slot(u32 hw)
	{
		if (hw < s_ram_size)
			return &s_ram_slots[hw >> 2];
		if (hw >= 0x1FC00000u)
			return &s_rom_slots[(hw - 0x1FC00000u) >> 2];
		return &s_rom12_slots[(hw - 0x1E000000u) >> 2];
	}

	// The word at `addr`, read as the recompiler reads code: straight from
	// physical memory.
	__fi bool ReadCode(u32 addr, u32* word)
	{
		const void* p = PSM(addr);
		if (!p)
			return false;
		std::memcpy(word, p, 4);
		return true;
	}

	// ---- COP2: when VU0 has to be brought up to date --------------------------------
	// COP2MicroFinishPass::Run (x86/iR5900Analysis.cpp), over the words of
	// [start, nominal end). One entry per instruction: kCop2Sync, kCop2Finish
	// or neither.
	void Cop2Pass(const std::vector<u32>& words, std::vector<u16>& out)
	{
		const size_t n = words.size();
		out.assign(n, 0);

		bool needs_vu0_sync = true;
		bool needs_vu0_finish = true;
		bool block_interlocked = EmuConfig.Gamefixes.FullVU0SyncHack;

		for (size_t i = 0; i < n && !block_interlocked; i++)
		{
			const u32 code = words[i];
			const u32 rs = (code >> 21) & 31;
			if ((code >> 26) == 022 && (rs == 001 || rs == 002 || rs == 005 || rs == 006) && (code & 1))
				block_interlocked = true;
		}

		for (size_t i = 0; i < n; i++)
		{
			const u32 code = words[i];
			const u32 opcode = code >> 26, rs = (code >> 21) & 31, rt = (code >> 16) & 31, funct = code & 63;

			// SB, SH, SW, SD may start VU0 through VIF0; VCALLMS and VCALLMSR do.
			if (opcode == 050 || opcode == 051 || opcode == 053 || opcode == 077 ||
				(opcode == 022 && rs >= 020 && (funct == 070 || funct == 071)))
			{
				needs_vu0_sync = true;
				needs_vu0_finish = true;
				continue;
			}

			const bool is_lqc_sqc = (opcode == 066 || opcode == 076);
			const bool is_non_interlocked_move = (opcode == 022 && rs < 020 && ((code & 1) == 0));
			const bool likely_clear = opcode == 022 && rs < 020 && rs > 004 && rt == 000;
			if ((needs_vu0_sync && (is_lqc_sqc || is_non_interlocked_move)) || likely_clear)
			{
				bool following_needs_finish = false;
				for (size_t j = i + 1; j < n; j++)
				{
					const u32 code2 = words[j];
					if ((code2 >> 26) != 022)
						continue;
					const u32 rs2 = (code2 >> 21) & 31, funct2 = code2 & 63;
					if (rs2 >= 020 && (funct2 == 070 || funct2 == 071))
						break;
					following_needs_finish = rs2 >= 020;
					if (following_needs_finish)
						break;
				}
				if (following_needs_finish && !block_interlocked)
				{
					out[i] |= kCop2Finish;
					needs_vu0_sync = false;
					needs_vu0_finish = false;
				}
				else
				{
					out[i] |= kCop2Sync;
					needs_vu0_sync = block_interlocked || (is_non_interlocked_move && likely_clear);
					needs_vu0_finish = true;
				}
				continue;
			}

			if (opcode != 022)
				continue;

			if (rs >= 020 && needs_vu0_finish)
			{
				out[i] |= kCop2Finish;
				needs_vu0_finish = false;
				needs_vu0_sync = false;
			}
			else if (needs_vu0_sync)
			{
				out[i] |= kCop2Sync;
				needs_vu0_sync = block_interlocked;
			}
		}
	}

	// ---- COP2: where VU0's flags reach their registers -------------------------------
	// COP2FlagHackPass::Run (x86/iR5900Analysis.cpp), over the words of
	// [start, nominal end): PCSX2's "mVU flag hack", which is on unless
	// switched off. The recompiler's code for a VU0 instruction that writes
	// the status or the MAC flags only brings the register up to date where
	// something may read it afterwards: before a transfer of that register,
	// before a micro program is called, before a store that may start one, and
	// at the block's end. So of a run of such instructions only the last
	// counts, and the sticky flags only ever collect from those. One entry per
	// instruction: kCop2Status, kCop2Mac or neither. (The clip flags are written
	// by every instruction that writes them; and where in the block the status
	// register is kept in microVU's own layout is not something a block lets
	// anything see.)
	void Cop2FlagPass(const std::vector<u32>& words, std::vector<u16>& out)
	{
		const size_t n = words.size();
		constexpr size_t kNone = ~size_t{0};
		size_t last_status = kNone, last_mac = kNone;
		size_t cfc2 = 0; // the CFC2 of the status register that a CTC2 to it is paired with

		const auto commit_status = [&]() {
			if (last_status != kNone)
				out[last_status] |= kCop2Status;
		};
		const auto commit_mac = [&]() {
			if (last_mac != kNone)
				out[last_mac] |= kCop2Mac;
		};

		for (size_t i = 0; i < n; i++)
		{
			const u32 code = words[i];
			const u32 opcode = code >> 26, rs = (code >> 21) & 31, rd = (code >> 11) & 31, funct = code & 63;

			// SB, SH, SW: may start VU0 through VIF0.
			if (opcode == 050 || opcode == 051 || opcode == 053)
			{
				commit_status();
				commit_mac();
				continue;
			}
			if (opcode != 022)
				continue;

			// CTC2 to the status register, then CFC2 of it: every instruction
			// in between has to have its sticky flags right.
			if (rs == 006 && rd == REG_STATUS_FLAG)
			{
				cfc2 = i;
				for (size_t j = i; j < n; j++)
				{
					const u32 other = words[j];
					if ((other >> 26) == 022 && ((other >> 21) & 31) == 002 && ((other >> 11) & 31) == REG_STATUS_FLAG)
					{
						cfc2 = j;
						break;
					}
				}
			}

			// CFC2 and CTC2 of a flag register. (For FBRST the pass tests for
			// CFC2 where its comment says CTC2; as it is written.)
			if (rs == 006 || rs == 002)
			{
				if (rd == REG_STATUS_FLAG)
					commit_status();
				else if (rd == REG_MAC_FLAG)
					commit_mac();
				else if (rd == REG_FBRST && rs == 002)
				{
					commit_status();
					commit_mac();
				}
			}

			// VCALLMS, VCALLMSR.
			if (((code >> 25) & 1) == 1 && ((code >> 2) & 15) == 14)
			{
				commit_status();
				commit_mac();
			}

			const int writes = Cop2Flags(code);
			if (writes & kWritesStatus)
			{
				// Between a CTC2 and its CFC2, and always for VDIV, VSQRT and VRSQRT.
				const u32 sub_opcode = (code & 3) | ((code >> 4) & 0x7c);
				if (i < cfc2 || (rs >= 020 && funct >= 074 && sub_opcode >= 070 && sub_opcode <= 072))
					out[i] |= kCop2Status;
				last_status = i;
			}
			if (writes & kWritesMac)
				last_mac = i;
		}

		commit_status();
		commit_mac();
	}

	// ---- forming a block --------------------------------------------------------
	// The part of recRecompile() that compiles instructions, carried out on
	// paper: what recompileNextInstruction() and the instruction recompilers
	// decide, without the code they would emit.
	struct Former
	{
		Block& block;
		ps2n::r5900::RecConsts consts; // what the recompiler knows to be constant (ps2n/rec_rules.h)
		u32 pc; // the recompiler's `pc`
		u32 factor; // 2 while the EE issues one instruction a cycle (COP0 Config, bit 18, clear), else 1
		int branch = 0; // g_branch
		bool interlocked = false; // s_nBlockInterlocked
		const std::vector<u16>& cop2; // Cop2Pass, by instruction of [start, nominal end)

		u16 Cop2Info(u32 addr) const
		{
			const u32 index = (addr - block.start) / 4;
			return (addr >= block.start && index < cop2.size()) ? cop2[index] : 0;
		}

		void Add(const Insn& in, u32 eighths, u32 info, bool delay_slot)
		{
			block.steps.push_back({in, static_cast<u16>(eighths), static_cast<u16>(info | (delay_slot ? kDelaySlot : 0))});
		}

		// recompileNextInstruction(delayslot, false).
		void Next(bool delay_slot)
		{
			if (EmuConfig.EnablePatches)
				Patch::ApplyDynamicPatches(pc);

			u32 code = 0;
			ReadCode(pc, &code);
			const u32 addr = pc;
			if (!delay_slot)
				pc += 4;

			// What vtlb's code generator knows a load or store by: the
			// recompiler's `pc` while it compiles it.
			const u32 access_pc = pc;

			const Insn in(code, addr);

			// A branch or jump in a delay slot is dropped, cost and all.
			if (delay_slot && ps2n::r5900::droppedFromDelaySlot(code))
			{
				s_stats.delay_slot_branches++;
				pc += 4;
				return;
			}

			const u32 eighths = (code == 0 ? 9u : static_cast<u32>(R5900::GetInstruction(code).cycles)) * factor;
			if (code == 0)
				Add(in, eighths, kCostOnly, delay_slot);
			else
				Compile(in, eighths, delay_slot, access_pc);

			if (delay_slot)
				pc += 4;
		}

		// The instruction's own recompiler.
		void Compile(const Insn& in, u32 eighths, bool delay_slot, u32 access_pc)
		{
			using namespace ps2n::r5900;
			const u32 code = in.raw;
			const u32 opcode = code >> 26, rs = in.rs(), rt = in.rt(), rd = in.rd();

			// Branches and jumps: the delay slot, then the end of the block.
			if (in.isControlFlow())
			{
				if (delay_slot)
				{
					// One of the coprocessor branches, which the recompiler does
					// not drop. What it makes of that is not reproduced.
					s_stats.delay_slot_oddities++;
					return;
				}
				Add(in, eighths, kBranch, false);
				Next(true);
				branch = 1;
				return;
			}

			if (in.op == Op::SYSCALL)
			{
				// recSYSCALL: FlushCache and iFlushCache are not called.
				if (consts.is(3) && isSkippedSyscall(consts.value[3]))
				{
					Add(in, eighths, kSkippedSyscall, delay_slot);
					return;
				}
				Add(in, eighths, kEndsBlock, delay_slot);
				consts.forget();
				branch = 2;
				if (delay_slot)
					s_stats.delay_slot_oddities++;
				return;
			}
			if (in.op == Op::BREAK)
			{
				Add(in, eighths, kEndsBlock, delay_slot);
				consts.forget();
				branch = 2;
				if (delay_slot)
					s_stats.delay_slot_oddities++;
				return;
			}
			if (in.op == Op::ERET || in.op == Op::EI || in.is(F_TRAP))
			{
				// recBranchCall.
				Add(in, eighths, kEndsBlock | kForcesEventTest, delay_slot);
				consts.forget();
				branch = 2;
				if (delay_slot)
					s_stats.delay_slot_oddities++;
				return;
			}

			if (in.op == Op::DI)
			{
				// recDI: it takes effect after the instruction that follows it,
				// unless it is itself in a delay slot. If what follows is a
				// branch, the code that would carry it out is never reached.
				if (delay_slot)
				{
					Add(in, eighths, kDisableInterrupts, true);
					return;
				}
				Add(in, eighths, kCostOnly, false);
				Next(false);
				if (branch != 1)
					Add(in, 0, kDisableInterrupts, false);
				return;
			}

			u32 info = 0;

			// COP0 (recMFC0, recMTC0).
			if (flushesCycles(in))
				info |= kFlush;

			// Loads and stores: whether the recompiler knew the address.
			if (in.is(F_LOAD | F_STORE))
			{
				u32 address = 0;
				if (consts.address(in, &address))
				{
					info |= kConstAddress;
					// recLoad: a read of an EE timer register ends the block.
					if (isNarrowLoad(in) && isTimerAddress(address) && !delay_slot)
					{
						info |= kEndsBlock;
						branch = 2;
					}
				}
				else if (!s_faulting.count(access_pc))
				{
					info |= kFastmem;
				}
			}

			// COP2 (microVU_Macro.inl).
			const bool cop2 = (opcode == 022);
			if (cop2 || opcode == 066 || opcode == 076)
			{
				const u16 flags = Cop2Info(in.addr);
				info |= flags & (kCop2Sync | kCop2Finish);

				if (cop2 && rs < 020)
				{
					// The four transfers.
					if (code & 1)
					{
						// COP2_Interlock, which comes before anything else and acts
						// only where the instruction is marked for a sync.
						interlocked = true;
						if (flags & kCop2Sync)
							info |= kFlush;
						else
							info &= ~kCop2Finish;
					}
					else
					{
						// mVUSyncVU0 or mVUFinishVU0, which CFC2 and QMFC2 to $zero
						// and CTC2 and QMTC2 to register 0 return before.
						const bool to_nothing = (rs == 002 || rs == 001) ? (rt == 0) : (rd == 0);
						if (to_nothing)
							info &= ~(kCop2Sync | kCop2Finish);
						else if (flags & kCop2Sync)
							info |= kFlush;
					}
				}
				else if (!cop2)
				{
					// LQC2, SQC2.
					if (flags & kCop2Sync)
						info |= kFlush;
				}
				else
				{
					// The VU0 instructions: recCOP2_SPEC1 runs VU0's micro program
					// to its end for either flag, and adds no cycles.
					if (flags & (kCop2Sync | kCop2Finish))
						info = (info & ~kCop2Sync) | kCop2Finish;

					// Whether its flags reach the registers (setupMacroOp): where
					// the pass says so, or always with the flag hack off.
					if (!EmuConfig.Speedhacks.vuFlagHack)
						info |= kCop2Status | kCop2Mac;
					else
						info |= flags & (kCop2Status | kCop2Mac);

					// VCALLMS, VCALLMSR: INTERPRETATE_COP2_FUNC.
					const u32 funct = code & 63;
					if (funct == 070 || funct == 071)
						info |= kFlush;
				}
				if (interlocked)
					info |= kCop2Interlocked;
			}

			consts.step(in);
			Add(in, eighths, info, delay_slot);
		}
	};

	// memory_protect_recompiled_code(): what the block's page needs.
	void Protect(Block& block, u32 nominal_size)
	{
		const u32 inpage_ptr = block.hw_start;

		// The kernel keeps a thread's registers in these two pages.
		const bool contains_thread_stack = ((block.start >> 12) == 0x81) || ((block.start >> 12) == 0x80001);

		const vtlb_ProtectionMode page_type = contains_thread_stack ? ProtMode_Manual : mmap_GetRamPageInfo(inpage_ptr);
		switch (page_type)
		{
			case ProtMode_NotRequired:
				break;

			case ProtMode_None:
			case ProtMode_Write:
				mmap_MarkCountedRamPage(inpage_ptr);
				s_manual_page[inpage_ptr >> 12] = 0;
				break;

			case ProtMode_Manual:
				block.flags |= kManual;
				block.words.resize(nominal_size);
				for (u32 i = 0; i < nominal_size; i++)
					ReadCode(inpage_ptr + i * 4, &block.words[i]);
				if (!contains_thread_stack && s_manual_counter[inpage_ptr >> 12] <= 3)
					block.flags |= kCounted;
				s_stats.manual++;
				break;
		}
	}

	// recClear(), which spares the block being formed.
	void ClearBlocks(u32 addr, u32 words)
	{
		if (addr >= s_max_address)
			return;
		const u32 hw = Physical(addr);
		if (hw == kNowhere)
			return;

		// From the last block that starts at or before the last word, down to
		// the first that ends at or before the first.
		auto it = s_blocks.upper_bound(hw + words * 4 - 4);
		while (it != s_blocks.begin())
		{
			--it;
			Block& block = it->second;
			if (&block == s_forming)
				continue;
			if (block.hw_start + block.size * 4 <= hw)
				break;

			*Slot(block.hw_start) = nullptr;
			if (block.cache)
				*block.cache = 0;
			const auto victim = it++;
			s_retired.push_back(s_blocks.extract(victim));
			ps2n::ee::blocks::epoch++;
			s_stats.cleared++;
		}
	}
} // namespace

uint64_t ps2n::ee::blocks::epoch = 1;

const Stats& ps2n::ee::blocks::GetStats()
{
	return s_stats;
}

int ps2n::ee::blocks::Cop2Flags(u32 code)
{
	// cop2flags(), as it is written.
	if ((code >> 26) != 022)
		return 0; // not COP2
	if (((code >> 25) & 1) == 0)
		return 0; // a branch or a transfer

	switch ((code >> 2) & 15)
	{
		case 15:
			switch ((code >> 6) & 0x1f)
			{
				case 4: // ITOF
				case 5: // FTOI
				case 12: // MOVE, MR32
				case 13: // LQI, SQI, LQD, SQD
				case 15: // MTIR, MFIR, ILWR, ISWR
				case 16: // RNEXT, RGET, RINIT, RXOR
					return 0;
				case 7: // MULAq, ABS, MULAi, CLIP
					if ((code & 3) == 1) // ABS
						return 0;
					if ((code & 3) == 3) // CLIP
						return kWritesClip;
					return kWritesStatus | kWritesMac;
				case 11: // SUBA, MSUBA, OPMULA, NOP
					if ((code & 3) == 3) // NOP
						return 0;
					return kWritesStatus | kWritesMac;
				case 14: // DIV, SQRT, RSQRT, WAITQ
					if ((code & 3) == 3) // WAITQ
						return 0;
					return kWritesStatus;
				default:
					break;
			}
			break;
		case 4: // MAXbc
		case 5: // MINIbc
		case 12: // IADD, ISUB, IADDI
		case 13: // IAND, IOR
		case 14: // VCALLMS, VCALLMSR
			return 0;
		case 7:
			if ((code & 1) == 1) // MAXi, MINIi
				return 0;
			return kWritesStatus | kWritesMac;
		case 10:
			if ((code & 3) == 3) // MAX
				return 0;
			return kWritesStatus | kWritesMac;
		case 11:
			if ((code & 3) == 3) // MINI
				return 0;
			return kWritesStatus | kWritesMac;
		default:
			break;
	}
	return kWritesStatus | kWritesMac;
}

u32 ps2n::ee::blocks::Physical(u32 pc)
{
	// The recompiler's hwLUT: RAM through its eight mirrors, the ROMs in useg,
	// kseg0 and kseg1.
	static constexpr bool kRamMirror[16] = {true, false, true, true, false, false, false, false, true, false, true, true, true, true, false, false};
	const u32 top = pc >> 28;
	const u32 rest = pc & 0x0FFFFFFFu;

	if (kRamMirror[top] && rest < Ps2MemSize::ExposedRam)
		return rest;
	if (top == 0x1 || top == 0x9 || top == 0xB)
	{
		const u32 hw = 0x10000000u | rest;
		if (hw >= 0x1FC00000u || (hw >= 0x1E000000u && hw < 0x1E800000u))
			return hw;
	}
	return kNowhere;
}

void ps2n::ee::blocks::Reset()
{
	Allocate();

	for (const auto& [hw, block] : s_blocks)
	{
		*Slot(hw) = nullptr;
		if (block.cache)
			*block.cache = 0;
	}
	s_blocks.clear();
	s_retired.clear();
	epoch++;
	s_faulting.clear();
	s_forming = nullptr;
	s_max_address = 0;
	std::memset(s_manual_page, 0, (Ps2MemSize::TotalRam >> 12) * sizeof(u16));
	std::memset(s_manual_counter, 0, (Ps2MemSize::TotalRam >> 12) * sizeof(u8));
	std::memset(s_ram_copy, 0, s_ram_size);
	s_stats.resets++;
}

Block* ps2n::ee::blocks::Find(u32 pc)
{
	const u32 hw = Physical(pc);
	if (hw == kNowhere || !s_ram_slots) [[unlikely]]
		return nullptr;
	return *Slot(hw);
}

Block* ps2n::ee::blocks::Form(u32 startpc)
{
	Allocate();
	epoch++;

	const u32 hw_start = Physical(startpc);
	if (hw_start == kNowhere)
		return nullptr;

	Block& block = s_blocks[hw_start];
	block = Block{};
	block.start = startpc;
	block.hw_start = hw_start;
	*Slot(hw_start) = &block;
	s_forming = &block;
	s_stats.formed++;

	// The BIOS's program loader, which PCSX2 hooks to learn which executable
	// starts and to start it directly.
	if (hw_start == EELOAD_START)
	{
		// The EELOAD _start function is the same across all BIOS versions.
		const u32 mainjump = memRead32(EELOAD_START + 0x9c);
		if (mainjump >> 26 == 3) // JAL
			g_eeloadMain = ((EELOAD_START + 0xa0) & 0xf0000000U) | (mainjump << 2 & 0x0fffffffU);
	}
	if (g_eeloadMain && hw_start == Physical(g_eeloadMain))
	{
		block.flags |= kHookMain;
		if (VMManager::Internal::IsFastBootInProgress())
		{
			const u32 typeAexecjump = memRead32(EELOAD_START + 0x470);
			const u32 typeBexecjump = memRead32(EELOAD_START + 0x5B0);
			const u32 typeCexecjump = memRead32(EELOAD_START + 0x618);
			const u32 typeDexecjump = memRead32(EELOAD_START + 0x600);
			if ((typeBexecjump >> 26 == 3) || (typeCexecjump >> 26 == 3) || (typeDexecjump >> 26 == 3)) // JAL to 0x822B8
				g_eeloadExec = EELOAD_START + 0x2B8;
			else if (typeAexecjump >> 26 == 3) // JAL to 0x82170
				g_eeloadExec = EELOAD_START + 0x170;
			else
				Console.WriteLn("ps2n: could not enable launch arguments for fast boot; unidentified BIOS version");
		}
	}
	if (g_eeloadExec && hw_start == Physical(g_eeloadExec))
		block.flags |= kHookExec;

	// ---- where the block ends ---------------------------------------------
	u32 i = startpc;
	u32 end = 0xffffffffu;
	u32 branch_to = 0xffffffffu;
	ps2n::r5900::TimeoutLoopScan timeout;

	for (;;)
	{
		if (i != startpc)
		{
			// Never across a 4 KB page boundary, and never into a block that
			// exists already.
			if ((i & 0xffc) == 0 || Find(i) != nullptr)
			{
				end = i;
				break;
			}
		}

		u32 code = 0;
		if (!ReadCode(i, &code))
		{
			end = i;
			break;
		}
		const u32 opcode = code >> 26, rs = (code >> 21) & 31, rt = (code >> 16) & 31, funct = code & 63;
		const s32 imm = static_cast<s16>(code);

		timeout.add(code, opcode == 5 ? memRead32(i + 4) : 0);

		bool found = false;
		const auto branch = [&]() {
			branch_to = static_cast<u32>(imm * 4) + i + 4;
			end = (branch_to > startpc && branch_to < i) ? branch_to : i + 8;
			found = true;
		};
		switch (opcode)
		{
			case 0: // special
				if (funct == 8 || funct == 9) // JR, JALR
				{
					end = i + 8;
					found = true;
				}
				else if (funct == 12 || funct == 13) // SYSCALL, BREAK
				{
					end = i + 4;
					found = true;
				}
				break;

			case 1: // regimm
				if (rt < 4 || (rt >= 16 && rt < 20))
					branch();
				break;

			case 2: // J
			case 3: // JAL
				branch_to = ((code & 0x03FFFFFFu) << 2) | ((i + 4) & 0xf0000000u);
				end = i + 8;
				found = true;
				break;

			case 4: case 5: case 6: case 7:
			case 20: case 21: case 22: case 23:
				branch();
				break;

			case 16: // cp0
				if (rs == 16 && funct == 24) // eret
				{
					end = i + 4;
					found = true;
					break;
				}
				[[fallthrough]];
			case 17: // cp1
			case 18: // cp2
				if (rs == 8)
					branch();
				break;

			default:
				break;
		}
		if (found)
			break;
		i += 4;
	}

	block.nominal_end = end;
	const u32 nominal_size = (end - startpc) >> 2;

	// ---- what kind of loop it is ---------------------------------------------
	std::vector<u32> words(nominal_size);
	bool has_cop2 = false;
	for (u32 n = 0; n < nominal_size; n++)
	{
		ReadCode(startpc + n * 4, &words[n]);
		const u32 opcode = words[n] >> 26;
		has_cop2 |= (opcode == 022 || opcode == 066 || opcode == 076);
	}

	if (branch_to == startpc)
	{
		// The idle loop test: every instruction but the branch at end - 8.
		ps2n::r5900::WaitLoopScan scan;
		for (u32 n = 0; n < nominal_size; n++)
		{
			if (startpc + n * 4 != end - 8)
				scan.add(words[n]);
		}
		if (scan.idle)
			block.flags |= kIdleLoop;
	}
	else
	{
		timeout.possible = false;
	}

	std::vector<u16> cop2;
	if (has_cop2)
	{
		Cop2Pass(words, cop2);
		if (EmuConfig.Speedhacks.vuFlagHack)
			Cop2FlagPass(words, cop2);
	}

	// ---- its page ---------------------------------------------------------------
	Protect(block, nominal_size);

	// ---- its instructions -------------------------------------------------------
	u32 pc = startpc;
	if (EmuConfig.Speedhacks.WaitLoop && timeout.possible)
	{
		// recSkipTimeoutLoop.
		block.flags |= kTimeoutLoop;
		block.timeout_reg = static_cast<u32>(timeout.reg);
		pc = end;
	}
	else
	{
		block.factor = 2u - ((cpuRegs.CP0.n.Config >> 18) & 1u);
		Former former{block, ps2n::r5900::RecConsts{}, startpc, block.factor, 0, false, cop2};
		while (!former.branch && former.pc < end)
			former.Next(false);
		pc = former.pc;
		if (!former.branch)
			block.flags |= kCutShort;
	}
	block.size = (pc - startpc) >> 2;

	// ---- older blocks over the same code ------------------------------------
	const u32 hw_end = hw_start + (pc - startpc);
	if (hw_end <= Ps2MemSize::ExposedRam)
	{
		auto it = s_blocks.upper_bound(hw_end - 4);
		while (it != s_blocks.begin())
		{
			--it;
			const Block& old = it->second;
			if (&old == &block || old.hw_start >= hw_end)
				continue;
			if (old.hw_start + old.size * 4 <= hw_start)
				break;

			if (std::memcmp(&s_ram_copy[old.hw_start], PSM(old.hw_start), old.size * 4) != 0)
			{
				// One of them was formed from code that has changed since.
				s_stats.stale++;
				ClearBlocks(startpc, (pc - startpc) / 4);
				break;
			}
		}
		std::memcpy(&s_ram_copy[hw_start], PSM(startpc), pc - startpc);
	}

	if (!(pc & 0x10000000u))
		s_max_address = std::max(pc & ~0xa0000000u, s_max_address);

	s_forming = nullptr;
	return &block;
}

Block* ps2n::ee::blocks::FindRunning(u32 start)
{
	if (Block* block = Find(start))
		return block;

	// The latest one thrown away that began there.
	const u32 hw = Physical(start);
	for (auto it = s_retired.rbegin(); it != s_retired.rend(); ++it)
	{
		if (it->key() == hw)
			return &it->mapped();
	}
	return nullptr;
}

void ps2n::ee::blocks::Collect()
{
	if (s_retired.empty()) [[likely]]
		return;
	s_retired.clear();
	epoch++;
}

void ps2n::ee::blocks::Clear(u32 addr, u32 words)
{
	s_stats.clears++;
	ClearBlocks(addr, words);
}

bool ps2n::ee::blocks::EnterManual(Block& block)
{
	const u32 inpage_ptr = block.hw_start;
	const u32 nominal_size = static_cast<u32>(block.words.size());

	// The words, against what the block was formed from.
	if (std::memcmp(block.words.data(), PSM(inpage_ptr), nominal_size * 4) != 0)
	{
		// dyna_block_discard
		s_stats.discarded++;
		ClearBlocks(inpage_ptr, nominal_size);
		return false;
	}

	if (block.flags & kCounted)
	{
		// A block that runs a lot puts its page back under write protection,
		// in the hope that whatever wrote to it will not again.
		u16& count = s_manual_page[inpage_ptr >> 12];
		const u32 sum = static_cast<u32>(count) + (nominal_size & 0xFFFFu);
		count = static_cast<u16>(sum);
		if (sum > 0xFFFFu)
		{
			// dyna_page_reset
			s_stats.page_resets++;
			ClearBlocks(inpage_ptr & ~0xfffu, 0x400);
			s_manual_counter[inpage_ptr >> 12]++;
			mmap_MarkCountedRamPage(inpage_ptr);
			return false;
		}
	}
	return true;
}

void ps2n::ee::blocks::MemoryFault(u32 pc)
{
	// vtlb_BackpatchLoadStore: the code is patched, its block queued to be
	// compiled again, and the instruction remembered.
	s_stats.fastmem_faults++;
	ClearBlocks(pc, 1);
	s_faulting.insert(pc);
}
