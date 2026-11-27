// SPDX-License-Identifier: GPL-3.0-or-later
//
// See rec_blocks.h. Each part names the function of PCSX2's recompiler it
// transcribes (PCSX2 v2.9.198, pcsx2/x86, Copyright 2002-2026 PCSX2 Dev Team,
// GPL-3.0+).

#include "rec_blocks.h"

#include "ps2n/rec_cop2.h"
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
	// of the ROMs (the recompiler's recLUT; blocks::detail, for Find()).
	Block**& s_ram_slots = ps2n::ee::blocks::detail::ram_slots;
	Block**& s_rom_slots = ps2n::ee::blocks::detail::rom_slots; // 1FC00000-1FFFFFFF
	Block**& s_rom12_slots = ps2n::ee::blocks::detail::rom12_slots; // 1E000000-1E7FFFFF
	u32& s_ram_size = ps2n::ee::blocks::detail::ram_size;

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

	// ---- COP2 ------------------------------------------------------------------------
	// What VU0's instructions in a block need first, and where their flags go:
	// the two passes of PCSX2's recompiler over the words of [start, nominal
	// end), shared with the recompiler, which decides the same for compiled code
	// (ps2n/rec_cop2.h).
	namespace cop2 = ps2n::r5900::cop2;
	static_assert(cop2::kFlush == kFlush && cop2::kSync == kCop2Sync && cop2::kFinish == kCop2Finish && cop2::kStatus == kCop2Status &&
				  cop2::kMac == kCop2Mac);

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
		const std::vector<u16>& passes; // the COP2 passes, by instruction of [start, nominal end)

		u16 Cop2Info(u32 addr) const
		{
			const u32 index = (addr - block.start) / 4;
			return (addr >= block.start && index < passes.size()) ? passes[index] : 0;
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
			const u32 opcode = code >> 26, rs = in.rs();

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

			// COP2 (microVU_Macro.inl): what follows from the passes for it
			// (ps2n/rec_cop2.h, which the recompiler decides the same with).
			// An interlocked transfer marks the rest of the block
			// (s_nBlockInterlocked: COP2_Interlock).
			if (cop2::decided(code))
			{
				if (opcode == 022 && rs < 020 && (code & 1))
					interlocked = true;
				info |= cop2::stepInfo(code, Cop2Info(in.addr), EmuConfig.Speedhacks.vuFlagHack);
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
				block.host = static_cast<const u8*>(PSM(inpage_ptr));
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

// The table of blocks by physical address (rec_blocks.h, blocks::detail).
Block** ps2n::ee::blocks::detail::ram_slots = nullptr;
Block** ps2n::ee::blocks::detail::rom_slots = nullptr;
Block** ps2n::ee::blocks::detail::rom12_slots = nullptr;
uint32_t ps2n::ee::blocks::detail::ram_size = 0;

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
	// The scan the recompiler shares (ps2n/rec_cop2.h): it stops never across a
	// 4 KB page boundary and never into a block that exists already. The
	// timeout loop's scan sees each word it reads.
	ps2n::r5900::TimeoutLoopScan timeout;
	const cop2::BlockEnd scanned = cop2::scanBlockEnd(
		startpc, [](u32 addr, u32* word) { return ReadCode(addr, word); }, [](u32 addr) { return Find(addr) != nullptr; },
		[&timeout](u32 addr, u32 code) { timeout.add(code, (code >> 26) == 5 ? memRead32(addr + 4) : 0); });
	const u32 end = scanned.end;
	const u32 branch_to = scanned.branch_to;

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

	std::vector<u16> passes;
	if (has_cop2)
	{
		passes.resize(nominal_size);
		cop2::microFinishPass(words.data(), nominal_size, EmuConfig.Gamefixes.FullVU0SyncHack, passes.data());
		if (EmuConfig.Speedhacks.vuFlagHack)
			cop2::flagHackPass(words.data(), nominal_size, passes.data());
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
		Former former{block, ps2n::r5900::RecConsts{}, startpc, block.factor, 0, false, passes};
		while (!former.branch && former.pc < end)
			former.Next(false);
		pc = former.pc;
		if (!former.branch)
			block.flags |= kCutShort;
	}
	block.size = (pc - startpc) >> 2;

	// The steps the interpreter only has to carry out, decided once here and
	// not at every execution (kPlain; ee_provider.cpp, RunRecSteps): a load or
	// store that may fault (kFastmem) among them, whose memory is looked at
	// there. A step's flags only lose kFastmem afterwards.
	for (Step& step : block.steps)
	{
		const u32 opcode = step.insn.raw >> 26;
		const bool is_cop2 = opcode == 022 || opcode == 066 || opcode == 076;
		constexpr u32 acted_on = kCostOnly | kDisableInterrupts | kSkippedSyscall | kFlush | kEndsBlock | kForcesEventTest | kCop2Sync |
								 kCop2Finish | kBranch;
		if (!is_cop2 && !(step.info & acted_on))
			step.info |= kPlain;
	}

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

u16* ps2n::ee::blocks::ManualCount(const Block& block)
{
	// A block that runs a lot puts its page back under write protection, in
	// the hope that whatever wrote to it will not again.
	return (block.flags & kCounted) ? &s_manual_page[block.hw_start >> 12] : nullptr;
}

void ps2n::ee::blocks::DiscardManual(Block& block)
{
	// dyna_block_discard
	s_stats.discarded++;
	ClearBlocks(block.hw_start, static_cast<u32>(block.words.size()));
}

void ps2n::ee::blocks::ResetManualPage(Block& block)
{
	// dyna_page_reset
	const u32 inpage_ptr = block.hw_start;
	s_stats.page_resets++;
	ClearBlocks(inpage_ptr & ~0xfffu, 0x400);
	s_manual_counter[inpage_ptr >> 12]++;
	mmap_MarkCountedRamPage(inpage_ptr);
}

bool ps2n::ee::blocks::EnterManual(Block& block)
{
	const u32 nominal_size = static_cast<u32>(block.words.size());

	// The words, against what the block was formed from: where they are in
	// memory was found when it was formed. A block is a few words, so they
	// are compared here rather than through a call (this runs at every
	// execution of the block, as the recompiler's own comparison does).
	const u8* memory = block.host ? block.host : static_cast<const u8*>(PSM(block.hw_start));
	u32 same = 0;
	for (; same < nominal_size; same++)
	{
		u32 word;
		std::memcpy(&word, memory + same * 4, 4);
		if (word != block.words[same])
			break;
	}
	if (same != nominal_size)
	{
		DiscardManual(block);
		return false;
	}

	if (u16* count = ManualCount(block))
	{
		const u32 sum = static_cast<u32>(*count) + (nominal_size & 0xFFFFu);
		*count = static_cast<u16>(sum);
		if (sum > 0xFFFFu)
		{
			ResetManualPage(block);
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
