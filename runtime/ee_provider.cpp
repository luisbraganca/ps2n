// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's EE provider for the PCSX2 core: the dispatcher of code compiled
// ahead of time, and the fallback interpreter for everything else.
//
// docs/execution-model.md describes the model; runtime/include/ps2n/recomp.h
// defines the contract with compiled code.
//
// Both count time, and test for events, exactly as PCSX2's EE recompiler
// does: by the blocks it would have formed, which rec_blocks.h keeps. The
// interpreter carries those blocks out instruction by instruction; compiled
// code asks at every label where the block it is in ends. Each function here
// that stands for a part of the recompiler's generated code names it (PCSX2
// v2.9.189, pcsx2/x86, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+).
//
// An instruction is carried out with PS2Native's own semantics where those
// exist (runtime/include/ps2n/ops_*.h). Every other instruction is carried
// out by PCSX2's interpreter function for it, reached through PCSX2's opcode
// table, and counted as delegated.
//
// Not implemented, and so different from PCSX2's recompiler (the list is in
// docs/execution-model.md):
//  - GoemonTlbHack (a game fix PCSX2 applies at JAL and JR);
//  - its throwing away of every block when it runs out of room for code.

#include "ee_provider.h"
#include "rec_blocks.h"

#include "ps2n/codehash.h"
#include "ps2n/ops_int.h"
#include "ps2n/ops_vu0.h"
#include "ps2n/r5900.h"
#include "ps2n/rec_cop2.h"
#include "ps2n/rec_rules.h"
#include "ps2n/recomp.h"
#include "ps2n/romdir.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Dmac.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900OpcodeTables.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VU.h"
#include "pcsx2/VUmicro.h"

#include "common/FastJmp.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Defined in pcsx2/VU0.cpp; PCSX2 declares it in no header.
void _vu0WaitMicro(); // runs VU0's micro program until it ends or stops at an M bit

using ps2n::ee::FunctionInfo;
using ps2n::ee::ImageInfo;
using ps2n::ee::kUnwind;
using ps2n::ee::StaticFn;
using ps2n::ee::blocks::Block;
using ps2n::ee::blocks::Step;
using ps2n::r5900::Insn;
using ps2n::r5900::Op;
namespace blocks = ps2n::ee::blocks;

namespace
{
	ps2n::ee::Stats s_stats;

	// What the interpreter has counted, in eighths of a cycle: everything, and
	// inside the compiled executable's code. Where the block being carried out
	// belongs.
	u64 s_interpreted_eighths = 0;
	u64 s_interpreted_eighths_in_image = 0;
	bool s_interpreting_image = false;

	// Where Execute() returns to when execution is to stop (value 1) or the
	// current instruction is abandoned (value 0).
	fastjmp_buf s_jmp;

	bool s_exit_requested = false;
	bool s_executing = false;
	bool s_full_clamp_reported = false;

	// Every block is to be thrown away before the next one is formed (the
	// recompiler's eeRecNeedsReset).
	bool s_blocks_need_reset = false;

	// The cost of a block of each number of eighths below kCycleTable, and
	// what a flush keeps of them, at the rate ps2n::ee::cycleRate says.
	void MakeCycleTable()
	{
		for (uint32_t eighths = 0; eighths < ps2n::ee::kCycleTable; eighths++)
			ps2n::ee::cycleTable[eighths] = ps2n::ee::scaledBlockCycles(eighths);
		const int32_t rate = ps2n::ee::cycleRate;
		ps2n::ee::remainderMask = rate > 1 ? (1u << (rate + 2)) - 1 : 7u;
	}

	// recResetRaw(): every block thrown away. Those formed from here on count
	// by the EE cycle rate setting as it is now (ps2n/recomp.h): the
	// recompiler reads it whenever it forms one, and the core throws every
	// block away whenever it changes.
	void ResetBlocks()
	{
		const int32_t rate = EmuConfig.Speedhacks.EECycleRate;
		if (rate != ps2n::ee::cycleRate)
		{
			ps2n::ee::cycleRate = rate;
			MakeCycleTable();
		}
		blocks::Reset();
	}

	// ---- compiled code -------------------------------------------------------
	const ImageInfo* const* s_images = nullptr;
	size_t s_image_count = 0;
	bool s_static_enabled = true;

	// The images in use: the BIOS's (ImageInfo::kind), from the first
	// instruction on, while the ROM is the one they were made from; the
	// executable's, from its entry point on; and of the executables it loads
	// itself, the one whose code memory holds, at most one over any address.
	// Their code ranges do not overlap.
	struct InUse
	{
		const ImageInfo* image;
	};
	std::vector<InUse> s_in_use;
	bool s_bios_looked_for = false;

	// For each image that has been in use, one bit per instruction of its
	// code range: a block had to be interpreted from there. Kept when the
	// image goes out of use, for the capture.
	std::map<const ImageInfo*, std::vector<u64>> s_missing;

	// The executables the running one loads itself (ps2n::ee::kImageLoaded,
	// tied to it by its CRC), any of which is put in use when memory holds its
	// code (AdoptLoaded); and how many times each was.
	std::vector<const ImageInfo*> s_loaded;
	std::map<const ImageInfo*, u64> s_loaded_uses;

	// The executable's image among them, when there is one; and whether
	// compiled code is dispatched to at all.
	const ImageInfo* s_image = nullptr;
	bool s_dispatching = false;

	// Set when another executable has started: every compiled frame has to be
	// left, since it belongs to the one before.
	bool s_unwind_requested = false;

	// The code found in memory in the place of a function, when nothing
	// compiled matched it.
	std::vector<ps2n::ee::CapturedCode> s_captured;

	// Where compiled code handed over to the interpreter, and how often: the
	// rest of a block (by the instruction it left at), a whole block (by its
	// label).
	std::unordered_map<u32, u64> s_left_at;
	std::unordered_map<u32, u64> s_interpreted_at;

	// The compiled functions whose code ran, a bit for each of an image's
	// functions, and in the order they first did (GetFunctionsRan).
	std::map<const ImageInfo*, std::vector<u64>> s_ran_bits;
	std::vector<ps2n::ee::FunctionRan> s_ran_order;

	// ---- the BIOS ----------------------------------------------------------------
	// Whether the executable has reached its entry point: what ran before it
	// is the boot.
	bool s_after_entry = false;

	// The addresses in the BIOS's places at which the interpreter formed a
	// block; and the BIOS's files against memory when the executable reached
	// its entry point.
	std::unordered_set<u32> s_bios_starts;
	std::vector<ps2n::ee::BiosFileCheck> s_bios_at_entry;

	// The words of the blocks the interpreter ran outside the executable, the
	// BIOS's files and the executables the program loaded itself, where no
	// compiled code ran them, as they were then, by the block's start: every
	// version of each, where memory held other code there at other times
	// (docs/adr/0057).
	std::map<u32, std::vector<std::vector<u32>>> s_unheld;

	// The ROM's directory (ps2n/romdir.h) places the files the EE runs from RAM
	// where PCSX2 has them.
	static_assert(ps2n::bios::kRamPieces[0].address == (0x80000000u | EEKERNEL_START) &&
				  ps2n::bios::kRamPieces[1].address == EENULL_START && ps2n::bios::kRamPieces[2].address == EELOAD_START);

	// The files of the ROM's directory that the EE runs, read from the ROM in
	// memory the first time they are needed.
	struct BiosLayout
	{
		bool read = false;
		std::vector<ps2n::bios::RomFile> files;
		u32 kernel_size = 0, idle_size = 0, loader_size = 0; // in bytes
	};
	BiosLayout s_bios;

	const BiosLayout& Bios()
	{
		if (!s_bios.read && eeMem)
		{
			s_bios.read = true;
			s_bios.files = ps2n::bios::readRomDirectory(eeMem->ROM, Ps2MemSize::Rom);
			const auto size = [](const char* name) {
				const std::optional<ps2n::bios::RomFile> f = ps2n::bios::findRomFile(s_bios.files, name);
				return f ? f->size : 0u;
			};
			s_bios.kernel_size = size("KERNEL");
			s_bios.idle_size = size("EENULL");
			s_bios.loader_size = size("EELOAD");
		}
		return s_bios;
	}

	// Which of the BIOS's places (ps2n::ee::BiosPlace) a block that begins at
	// `pc` lies in, or kBiosPlaces when it is none of them: user memory from
	// 00100000, where the executable is.
	u8 BiosPlaceOf(u32 pc)
	{
		const u32 hw = blocks::Physical(pc);
		if (hw == ~0u)
			return ps2n::ee::kBiosPlaces;
		if (hw >= 0x1E000000u) // the ROMs, as blocks::Physical() files them
			return ps2n::ee::kBiosRom;
		const BiosLayout& bios = Bios();
		if (pc >= 0xA0000000u)
			return ps2n::ee::kBiosUncached;
		if (pc >= 0x80000000u)
			return (hw - EEKERNEL_START) < bios.kernel_size ? ps2n::ee::kBiosKernel : ps2n::ee::kBiosKernelOther;
		if (pc >= 0x00100000u)
			return ps2n::ee::kBiosPlaces;
		if (pc - EENULL_START < bios.idle_size)
			return ps2n::ee::kBiosIdle;
		if (pc - EELOAD_START < bios.loader_size)
			return ps2n::ee::kBiosLoader;
		return ps2n::ee::kBiosLowOther;
	}

	// The BIOS's files that the EE runs from RAM, each against what memory
	// holds where it runs.
	std::vector<ps2n::ee::BiosFileCheck> CompareBiosFilesNow()
	{
		std::vector<ps2n::ee::BiosFileCheck> checks;
		if (!eeMem)
			return checks;
		const BiosLayout& bios = Bios();
		for (const ps2n::bios::RamPiece& piece : ps2n::bios::kRamPieces)
		{
			const u32 hw = blocks::Physical(piece.address);
			ps2n::ee::BiosFileCheck check;
			check.file = piece.file;
			check.address = piece.address;
			if (const std::optional<ps2n::bios::RomFile> f = ps2n::bios::findRomFile(bios.files, piece.file))
			{
				check.words = std::min<u32>(f->size, Ps2MemSize::MainRam - hw) / 4;
				for (u32 i = 0; i < check.words; i++)
				{
					u32 rom, ram;
					std::memcpy(&rom, eeMem->ROM + f->offset + i * 4, 4);
					std::memcpy(&ram, eeMem->Main + hw + i * 4, 4);
					if (rom != ram)
					{
						if (!check.differing++)
							check.first = piece.address + i * 4;
						check.last = piece.address + i * 4;
						if (check.sample.size() < 256)
							check.sample.push_back({piece.address + i * 4, rom, ram});
					}
				}
			}
			checks.push_back(check);
		}
		return checks;
	}

	// The host stack: where Execute() was entered, and how far below that
	// compiled code may go before its frames are abandoned.
	uptr s_stack_base = 0;
	size_t s_stack_budget = 256 * 1024;

	// The record of event tests, for comparing one run with another: the file
	// being written, the file of an earlier run being compared with, where the
	// two first differed, and from which event test on the written lines are
	// followed by the registers.
	std::FILE* s_trace_file = nullptr;
	std::FILE* s_trace_reference = nullptr;
	std::string s_trace_mismatch;
	u64 s_trace_detail_from = ~0ull;

	// PC -> the image in use with an entry there and the function's index in
	// it, or -1.
	struct CacheEntry
	{
		u32 pc;
		s32 index;
		const ImageInfo* image;
	};
	constexpr u32 kCacheSize = 1u << 14;
	CacheEntry s_cache[kCacheSize];

	// Which of a function's compiled forms a block's code is (Block::body).
	constexpr u8 kBodyFunction = 0; // the function as the executable has it; 1 + n: its variant n
	constexpr u8 kBodyNone = blocks::kBodyNone;
	constexpr u8 kBodyUnknown = blocks::kBodyUnknown;

	// What blockStart() last told compiled code it could run, about a block it
	// cannot keep in its image's table and so asks about at every execution:
	// one of a page that is not write-protected, whose words are compared at
	// every execution, and one of a variant of a function. The answer holds for
	// as long as no block has been formed or thrown away (blocks::epoch, which
	// starts at one, so that an empty place never matches) and the images in use
	// and whether compiled code runs are what they were (FlushCache() empties
	// these). What it saves is looking the block up and deciding about it again,
	// which, at every execution of a small block, took several times the
	// block's own time.
	struct Known
	{
		const ImageInfo* image;
		u64 epoch;
		Block* block;
		u32 index;
		u32 body;
		u32 answer;
		// Where another image holds the version of code run outside the
		// executable that memory holds (docs/adr/0057): its function, which
		// runs in this one's place.
		StaticFn other = nullptr;
	};
	constexpr u32 kKnownSize = 1u << 10;
	Known s_known[kKnownSize];

	__fi Known& KnownPlace(u32 index, u32 body)
	{
		return s_known[(index + body * 0x9E37u) & (kKnownSize - 1)];
	}

	// A block of a page that is not write-protected, in a function's own code:
	// the image's table names it by the number of its record
	// (ps2n::ee::checkedRecords, ps2n::ee::kCheckedLimit), and compiled code
	// compares the block's words with memory at every execution itself
	// (ps2n::ee::askAbout), as PCSX2's code for the block does, without the
	// runtime looking the block up or deciding about it again. A record is in
	// use for as long as the place in the table it was given to still holds its
	// number: the place is zeroed when the block is thrown away (Block::cache),
	// and when its image is put in use again (Use()).
	using Checked = ps2n::ee::Checked;
	Checked (&s_checked)[ps2n::ee::kCheckedLimit] = ps2n::ee::checkedRecords;
	u32 s_checked_next = 1;

	// The number of a record that is not in use, or zero when all are.
	u32 NewChecked()
	{
		for (int round = 0; round < 2; round++)
		{
			while (s_checked_next < ps2n::ee::kCheckedLimit)
			{
				const u32 id = s_checked_next++;
				const Checked& c = s_checked[id];
				if (!c.place || *c.place != id)
					return id;
			}
			s_checked_next = 1;
		}
		return 0;
	}

	// Whether dispatch() fills ps2n::ee::callSites (SetCallSitesEnabled).
	bool s_call_sites_enabled = true;

	// What an empty place of ps2n::ee::callSites holds: the call goes to
	// dispatch(), as it would without them.
	uint32_t PS2N_CC CallThroughDispatch(uint32_t pc)
	{
		return ps2n::ee::dispatch(pc);
	}

	// Empties the places where dispatch() remembers the compiled function it
	// found for a call (ps2n/recomp.h). None may outlive a change of what
	// Lookup() would answer: of the images in use, of whether compiled code
	// runs at all, of the core's settings. Every compiled function is entered
	// through Lookup() first, and every way that puts an image in use comes
	// here, so the places are emptied before compiled code first reads them.
	void FlushCallSites()
	{
		for (ps2n::ee::CallSite& site : ps2n::ee::callSites)
			site = {0xFFFFFFFFu, &CallThroughDispatch};
	}

	// Counts up whenever what Lookup() answers may change, which is also when
	// the images in use may: what the interpreter noted of a block for its
	// report (Block::noted, Block::missing_noted) holds while it stays the same.
	u64 s_images_epoch = 1;

	void FlushCache()
	{
		s_images_epoch++;
		for (CacheEntry& e : s_cache)
			e = {0xFFFFFFFFu, -1, nullptr};
		for (Known& k : s_known)
			k = {};
		FlushCallSites();
	}

	// Inside the executable's code.
	bool InImage(u32 pc)
	{
		return s_image && (pc - s_image->text_start) < (s_image->text_end - s_image->text_start);
	}

	bool InUseImage(const ImageInfo* image)
	{
		for (const InUse& u : s_in_use)
		{
			if (u.image == image)
				return true;
		}
		return false;
	}

	// Puts `image` in use. Nothing is known of any of its blocks yet.
	void Use(const ImageInfo& image)
	{
		std::memset(image.blocks, 0, image.leader_count * sizeof(u32));
		s_in_use.push_back({&image});
		s_missing[&image].resize(((image.text_end - image.text_start) / 4 + 63) / 64, 0);
		s_dispatching = s_static_enabled;
		FlushCache();
	}

	// Chooses the image of the executable that has just reached its entry
	// point. The BIOS's stay in use.
	void SelectImage()
	{
		const u32 crc = VMManager::GetCurrentCRC();

		std::erase_if(s_in_use, [](const InUse& u) { return u.image->kind != ps2n::ee::kImageBios; });
		s_image = nullptr;
		s_loaded.clear();
		const ImageInfo* outside = nullptr; // the code it ran outside its own, as runs captured it
		for (size_t i = 0; i < s_image_count; i++)
		{
			if (s_images[i]->crc != crc)
				continue;
			if (s_images[i]->kind == ps2n::ee::kImageExecutable)
				s_image = s_images[i];
			else if (s_images[i]->kind == ps2n::ee::kImageOutside)
				outside = s_images[i];
			else if (s_images[i]->kind == ps2n::ee::kImageLoaded)
				s_loaded.push_back(s_images[i]);
		}
		s_dispatching = s_static_enabled && !s_in_use.empty();
		s_stats.image = s_image;
		FlushCache();

		// Frames of the image used until now must not continue.
		s_unwind_requested = true;

		if (!s_image)
		{
			Console.WriteLn(Color_StrongOrange, "ps2n: no compiled code for the executable with CRC %08X; it is interpreted", crc);
			return;
		}

		Use(*s_image);
		Console.WriteLn(Color_StrongGreen, "ps2n: %s compiled code of %s (CRC %08X): %u functions at %08X-%08X, by %s",
			s_dispatching ? "using" : "NOT using (switched off)", s_image->name, crc, s_image->function_count, s_image->text_start,
			s_image->text_end, s_image->generator);
		if (outside && (outside->function_count || outside->layer_count))
		{
			Use(*outside);
			// And the images of other versions of that code at the same
			// addresses, after it (docs/adr/0057).
			u32 other_functions = 0;
			for (u32 i = 0; i < outside->layer_count; i++)
			{
				Use(*outside->layers[i]);
				other_functions += outside->layers[i]->function_count;
			}
			Console.WriteLn(Color_StrongGreen, "ps2n: and of the code it ran outside its executable: %u functions at %08X-%08X%s",
				outside->function_count, outside->text_start, outside->text_end,
				outside->layer_count ?
					fmt::format("; other versions of it at the same addresses: {} images, {} functions", outside->layer_count, other_functions).c_str() :
					"");
		}
		for (const ImageInfo* loaded : s_loaded)
		{
			Console.WriteLn(Color_StrongGreen, "ps2n: and of %s, which it may load itself (CRC %08X): %u functions at %08X-%08X, used while in memory",
				loaded->name, loaded->program_crc, loaded->function_count, loaded->text_start, loaded->text_end);
		}
	}

	// Puts the BIOS's images in use, from the first instruction on: those
	// made from the ROM the machine has, which the CRC of the whole ROM tells.
	void UseBios()
	{
		s_bios_looked_for = true;
		if (!eeMem)
			return;

		u32 crc = 0;
		for (u32 i = 0; i + 4 <= Ps2MemSize::Rom; i += 4)
		{
			u32 word;
			std::memcpy(&word, eeMem->ROM + i, 4);
			crc ^= word;
		}

		bool any = false;
		for (size_t i = 0; i < s_image_count; i++)
		{
			const ImageInfo& image = *s_images[i];
			if (image.kind != ps2n::ee::kImageBios)
				continue;
			any = true;
			if (image.crc != crc)
				continue;
			Use(image);
			s_stats.bios_images++;
			s_stats.bios_functions += image.function_count;
		}
		s_dispatching = s_static_enabled && !s_in_use.empty();
		if (s_stats.bios_images)
		{
			Console.WriteLn(Color_StrongGreen, "ps2n: %s compiled code of the BIOS (CRC %08X): %u images, %u functions",
				s_dispatching ? "using" : "NOT using (switched off)", crc, static_cast<unsigned>(s_stats.bios_images),
				static_cast<unsigned>(s_stats.bios_functions));
		}
		else if (any)
		{
			Console.WriteLn(Color_StrongOrange, "ps2n: no compiled code for the BIOS with CRC %08X; its code is interpreted", crc);
		}
	}

	// The function of `image` that holds `pc`, or null.
	const FunctionInfo* FunctionOf(const ImageInfo& image, u32 pc)
	{
		if ((pc - image.text_start) >= (image.text_end - image.text_start))
			return nullptr;

		const FunctionInfo* begin = image.functions;
		const FunctionInfo* end = begin + image.function_count;
		const FunctionInfo* it = std::upper_bound(begin, end, pc, [](u32 a, const FunctionInfo& f) { return a < f.start; });
		if (it == begin)
			return nullptr;
		--it;
		return pc < it->end ? it : nullptr;
	}

	// The index of `pc` among the leaders of `image`, or -1 when it is not one.
	s32 LeaderIndex(const ImageInfo& image, u32 pc)
	{
		const u32* begin = image.leaders;
		const u32* end = begin + image.leader_count;
		const u32* it = std::lower_bound(begin, end, pc);
		return (it != end && *it == pc) ? static_cast<s32>(it - begin) : -1;
	}

	// The function of the image that can be entered at `pc`, or -1.
	s32 FindEntry(const ImageInfo& image, u32 pc)
	{
		const FunctionInfo* f = FunctionOf(image, pc);
		if (!f)
			return -1;

		const u32* leaders = image.leaders + f->first_leader;
		if (!std::binary_search(leaders, leaders + f->leader_count, pc))
			return -1;
		return static_cast<s32>(f - image.functions);
	}

	// The image in use and the function of it that can be entered at `pc`.
	__fi const CacheEntry& LookupEntry(u32 pc)
	{
		CacheEntry& e = s_cache[(pc >> 2) & (kCacheSize - 1)];
		if (e.pc != pc)
		{
			e = {pc, -1, nullptr};
			for (const InUse& u : s_in_use)
			{
				const s32 index = FindEntry(*u.image, pc);
				if (index >= 0)
				{
					e.index = index;
					e.image = u.image;
					break;
				}
			}
		}
		return e;
	}

	// The compiled function to enter at `pc`, or null. PCSX2's fourth clamp
	// mode for the EE's FPU is not implemented, and compiled code has the FPU
	// semantics built in: with it set, nothing compiled runs.
	__fi StaticFn Lookup(u32 pc)
	{
		if (!s_dispatching || EmuConfig.Cpu.Recompiler.fpuFullMode)
			return nullptr;

		const CacheEntry& e = LookupEntry(pc);
		return e.index < 0 ? nullptr : e.image->functions[e.index].fn;
	}

	// Whether `pc` is in one of the BIOS's files, where the BIOS's images have
	// their code (ps2n/romdir.h): the ROM, the kernel's, the idle thread's, the
	// program loader's. RAM through the uncached mirror is taken where the
	// kernel's segment has it.
	bool InBiosFile(u32 pc)
	{
		u32 at = pc;
		if (pc >= 0xA0000000u && pc < 0xC0000000u && blocks::Physical(pc) < Ps2MemSize::MainRam)
			at = pc - 0x20000000u;
		const u8 place = BiosPlaceOf(at);
		return place == ps2n::ee::kBiosRom || place == ps2n::ee::kBiosKernel || place == ps2n::ee::kBiosIdle ||
			   place == ps2n::ee::kBiosLoader;
	}

	// Notes that compiled code ran a block of the function of `image` that
	// holds `pc` (asked once for each block it runs, by blockStart()).
	void MarkRan(const ImageInfo& image, u32 pc)
	{
		const FunctionInfo* f = FunctionOf(image, pc);
		if (!f)
			return;
		const size_t i = static_cast<size_t>(f - image.functions);
		std::vector<u64>& bits = s_ran_bits[&image];
		if (bits.empty())
			bits.resize((image.function_count + 63) / 64, 0);
		if (bits[i / 64] & (1ull << (i % 64)))
			return;
		bits[i / 64] |= 1ull << (i % 64);
		s_ran_order.push_back({image.id, f->start});
	}

	// Whether an image in use has a function that holds `pc`.
	bool HeldByImage(u32 pc)
	{
		for (const InUse& u : s_in_use)
		{
			if (FunctionOf(*u.image, pc))
				return true;
		}
		return false;
	}

	// Whether an executable the program loaded itself, in use, has a function
	// that holds `pc`: what runs there is that executable's, which its own
	// capture names (CaptureCode, RecordMissing).
	bool HeldByLoaded(u32 pc)
	{
		for (const InUse& u : s_in_use)
		{
			if (u.image->kind == ps2n::ee::kImageLoaded && FunctionOf(*u.image, pc))
				return true;
		}
		return false;
	}

	void RecordMissing(u32 pc);

	// RecordMissing() for the block about to be interpreted at `pc`, `block`
	// when it has been formed already: once for as long as the images in use
	// stay what they are, since marking it again changes nothing.
	__fi void NoteMissing(Block* block, u32 pc)
	{
		if (block && block->missing_noted == s_images_epoch && block->start == pc) [[likely]]
			return;
		RecordMissing(pc);
		if (block && s_dispatching && block->start == pc)
			block->missing_noted = s_images_epoch;
	}

	void RecordMissing(u32 pc)
	{
		if (!s_dispatching)
			return;
		for (const InUse& u : s_in_use)
		{
			const u32 offset = pc - u.image->text_start;
			if (offset < u.image->text_end - u.image->text_start)
			{
				const u32 bit = offset / 4;
				u64& bits = s_missing[u.image][bit / 64];
				if (!(bits & (1ull << (bit % 64))) && u.image->kind != ps2n::ee::kImageBios)
					s_stats.new_code++;
				bits |= 1ull << (bit % 64);
				return;
			}
		}
	}

	// The fingerprint of the code in guest memory at [start, end), or 0 when
	// it is not all plain memory.
	u64 HashOfMemory(u32 start, u32 end)
	{
		ps2n::CodeHash hash;
		for (u32 addr = start; addr < end;)
		{
			const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
			if (entry.isHandler(addr))
				return 0;

			const u32 page_end = std::min(end, (addr | vtlb_private::VTLB_PAGE_MASK) + 1);
			const u8* p = reinterpret_cast<const u8*>(entry.assumePtr(addr));
			for (; addr < page_end; addr += 4, p += 4)
			{
				u32 word;
				std::memcpy(&word, p, 4);
				hash.add(word);
			}
		}
		return hash.value;
	}

	// Keeps the words in memory in the place of function `f` of `image`, for
	// the host to hand to the recompiler: for an executable the program loads
	// itself, named by that executable's CRC.
	void CaptureCode(const ImageInfo& image, const FunctionInfo& f)
	{
		ps2n::ee::CapturedCode code;
		code.start = f.start;
		code.program = image.kind == ps2n::ee::kImageLoaded ? image.program_crc : 0;
		for (u32 addr = f.start; addr < f.end; addr += 4)
		{
			const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
			if (entry.isHandler(addr))
				return;

			u32 word;
			std::memcpy(&word, reinterpret_cast<const void*>(entry.assumePtr(addr)), 4);
			code.words.push_back(word);
		}

		for (const ps2n::ee::CapturedCode& have : s_captured)
		{
			if (have.program == code.program && have.start == code.start && have.words == code.words)
				return;
		}
		s_captured.push_back(std::move(code));
		s_stats.new_code++;
	}

	// Whether memory holds `image`'s code at `pc`: the words of the segment
	// of its function that `pc` is in, from the leader at or before it to the
	// next, are those the function or one of its variants was compiled from.
	bool CodeInMemory(const ImageInfo& image, u32 pc)
	{
		const FunctionInfo* f = FunctionOf(image, pc);
		if (!f)
			return false;
		const u32* first = image.leaders + f->first_leader;
		const u32* last = first + f->leader_count;
		const u32* next = std::upper_bound(first, last, pc);
		if (next == first)
			return false;
		const u32 index = static_cast<u32>(next - 1 - image.leaders);
		const u64 hash = HashOfMemory(image.leaders[index], next != last ? *next : f->end);
		if (hash == 0)
			return false;
		if (hash == image.segment_hashes[index])
			return true;
		for (u32 v = 0; v < f->variant_count; v++)
		{
			if (hash == image.variants[f->first_variant + v].segment_hashes[index - f->first_leader])
				return true;
		}
		return false;
	}

	// A block was just formed at `pc`. Where the executable loads others
	// itself (ps2n::ee::kImageLoaded) and the one of them whose code memory
	// holds there is not in use, it is put in use, and any other of them over
	// the same addresses taken out of use; compiled code of one taken out
	// that is running is left. Nothing else tells which of them is in memory:
	// the core does not see the executable change. Each block is still
	// compared with memory before compiled code runs it (IdentifyBody).
	void AdoptLoaded(u32 pc)
	{
		if (s_loaded.empty() || !s_static_enabled)
			return;
		for (const InUse& u : s_in_use)
		{
			if (u.image->kind == ps2n::ee::kImageLoaded && CodeInMemory(*u.image, pc))
				return;
		}
		for (const ImageInfo* image : s_loaded)
		{
			if (InUseImage(image) || !CodeInMemory(*image, pc))
				continue;
			const size_t replaced = std::erase_if(s_in_use, [image](const InUse& u) {
				return u.image->kind == ps2n::ee::kImageLoaded && u.image->text_start < image->text_end && image->text_start < u.image->text_end;
			});
			Use(*image);
			s_loaded_uses[image]++;
			if (replaced)
				s_unwind_requested = true;
			Console.WriteLn(Color_StrongGreen, "ps2n: using compiled code of %s, which the executable loaded itself (CRC %08X), from %08X",
				image->name, image->program_crc, pc);
			return;
		}
	}

	// ---- time and events -----------------------------------------------------
	// Folds the machine's state at an event test into the run's fingerprint.
	// Event tests happen at the same instructions and cycle counts whether a
	// block was compiled or interpreted, so two runs of one program must
	// produce the same sequence, and the first difference says where they
	// part.
	void RecordEventTest()
	{
		// Only when a run is being recorded or compared: this reads a kilobyte
		// and a half of registers, at every event test.
		if (!s_trace_file && !s_trace_reference)
			return;

		u64 h = s_stats.trace_hash;
		const auto fold = [&h](const void* data, size_t size) {
			const u8* p = static_cast<const u8*>(data);
			for (size_t i = 0; i < size; i++)
				h = (h ^ p[i]) * 1099511628211ull;
		};
		fold(&cpuRegs.pc, sizeof(cpuRegs.pc));
		fold(&cpuRegs.cycle, sizeof(cpuRegs.cycle));
		fold(&cpuRegs.GPR, sizeof(cpuRegs.GPR));
		fold(&cpuRegs.HI, sizeof(cpuRegs.HI));
		fold(&cpuRegs.LO, sizeof(cpuRegs.LO));
		fold(&cpuRegs.sa, sizeof(cpuRegs.sa));
		fold(&cpuRegs.CP0, sizeof(cpuRegs.CP0));
		fold(&fpuRegs, sizeof(fpuRegs));
		fold(&VU0.VF, sizeof(VU0.VF));
		fold(&VU0.VI, sizeof(VU0.VI));
		// The IOP's place, its time and its registers (step G12): it is between
		// two of its blocks here, whatever carries them out.
		fold(&psxRegs.pc, sizeof(psxRegs.pc));
		fold(&psxRegs.cycle, sizeof(psxRegs.cycle));
		fold(&psxRegs.GPR, sizeof(psxRegs.GPR));
		fold(&psxRegs.CP0, sizeof(psxRegs.CP0));
		s_stats.trace_hash = h;

		using ull = unsigned long long;
		char line[96];
		std::snprintf(line, sizeof(line), "%llu %08X %llu %016llX", static_cast<ull>(s_stats.event_tests), cpuRegs.pc,
			static_cast<ull>(cpuRegs.cycle), static_cast<ull>(h));

		if (s_trace_file)
		{
			std::fprintf(s_trace_file, "%s\n", line);
			if (s_stats.event_tests >= s_trace_detail_from)
			{
				// Lines that begin with a space are for reading, not comparing.
				for (u32 r = 0; r < 32; r++)
				{
					std::fprintf(s_trace_file, "%s%s=%016llX%s", (r % 8) ? " " : "  ", ps2n::r5900::gprName(r),
						static_cast<ull>(cpuRegs.GPR.r[r].UD[0]), (r % 8 == 7) ? "\n" : "");
				}
				std::fprintf(s_trace_file, "  hi=%016llX lo=%016llX hi1=%016llX lo1=%016llX sa=%X fcr31=%08X status=%08X epc=%08X\n",
					static_cast<ull>(cpuRegs.HI.UD[0]), static_cast<ull>(cpuRegs.LO.UD[0]), static_cast<ull>(cpuRegs.HI.UD[1]),
					static_cast<ull>(cpuRegs.LO.UD[1]), cpuRegs.sa, fpuRegs.fprc[31], cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.EPC);
			}
		}

		if (s_trace_reference && s_trace_mismatch.empty())
		{
			char reference[1024];
			bool have = false;
			while (std::fgets(reference, sizeof(reference), s_trace_reference))
			{
				if (reference[0] != ' ')
				{
					have = true;
					break;
				}
			}
			if (have)
				reference[std::strcspn(reference, "\r\n")] = 0;

			if (!have)
				s_trace_mismatch = fmt::format("the reference run had no event test {}; this run: {}", s_stats.event_tests, line);
			else if (std::strcmp(reference, line) != 0)
				s_trace_mismatch = fmt::format("at event test {}: reference \"{}\", this run \"{}\"", s_stats.event_tests, reference, line);

			if (!s_trace_mismatch.empty())
				Console.Error("ps2n: this run parts from the reference run %s", s_trace_mismatch.c_str());
		}
	}

	// Runs the core's event test. cpuRegs.pc is the next instruction.
	void RunEventTest()
	{
		// No block is being carried out here: those thrown away since the last
		// time can go for good.
		blocks::Collect();

		s_stats.event_tests++;
		RecordEventTest();
		_cpuEventTest_Shared();

		if (s_exit_requested)
		{
			s_exit_requested = false;
			fastjmp_jmp(&s_jmp, 1);
		}
	}

	// After an event test reached from compiled code: whether its frames have
	// to be left. Either another executable has started, or the host stack has
	// grown too deep, which guest code can cause without growing its own stack
	// (a function that is called, and jumps back into its caller instead of
	// returning). No frame holds guest state, so leaving them is always safe.
	bool MustUnwind()
	{
		if (s_unwind_requested)
		{
			s_unwind_requested = false;
			s_stats.forced_unwinds++;
			return true;
		}

		char here;
		if (s_stack_base - reinterpret_cast<uptr>(&here) > s_stack_budget) [[unlikely]]
		{
			s_stats.stack_unwinds++;
			return true;
		}
		return false;
	}

	// Counts a SYSCALL about to be carried out, by its number.
	__fi void CountSyscall()
	{
		const s32 number = cpuRegs.GPR.n.v1.SL[0];
		s_stats.syscalls[static_cast<u8>(number < 0 ? -number : number)]++;
	}

	// PCSX2's fourth clamp mode for the EE's FPU, "Full", is a recompiler of
	// its own (x86/iFPUd.cpp) that PS2Native's FPU semantics do not reproduce.
	// With it set, the FPU instructions go to PCSX2's interpreter functions and
	// no compiled code runs, since compiled code has the semantics built in.
	__fi bool FullFpuClampMode()
	{
		return EmuConfig.Cpu.Recompiler.fpuFullMode;
	}

	// Carries out `in` with PS2Native's own semantics. False when it has none
	// for this instruction. Inlined where the interpreter carries a block's
	// plain steps out (RunRecSteps), a call less for each instruction.
	__fi bool ExecuteOwn(const Insn& in)
	{
		using namespace ps2n::ee::ops;

		// The setting first: what the instruction is takes a call (r5900::info()).
		if (FullFpuClampMode() && in.is(ps2n::r5900::F_FPU)) [[unlikely]]
			return false;

#define RD in.rd()
#define RS in.rs()
#define RT in.rt()
#define SA in.sa()
#define SIMM in.simm()
#define UIMM in.uimm()
#define FD in.fd()
#define FS in.fs()
#define FT in.ft()
#define PERF in.perfReg()
#define CREG in.rd()
#define S(op, fn, args) \
	case Op::op: \
		fn args; \
		return true;

		switch (in.op)
		{
#include "ps2n/r5900_sem_int.inc"
#include "ps2n/r5900_sem_cop1.inc"
#include "ps2n/r5900_sem_mmi.inc"
#include "ps2n/r5900_sem_cop2_ctl.inc"
#include "ps2n/r5900_sem_cop0.inc"
			default:
				return false;
		}

#undef S
#undef CREG
#undef PERF
#undef FT
#undef FS
#undef FD
#undef UIMM
#undef SIMM
#undef SA
#undef RT
#undef RS
#undef RD
	}

	// A VU0 instruction (COP2 with rs from 020) by PS2Native's semantics, with
	// `flags` saying where its flags reach VU0's flag registers (vu0::kStatus,
	// vu0::kMac). Returns false for VCALLMS, VCALLMSR and what PS2Native's
	// decoder does not know, which are PCSX2's functions.
	bool ExecuteVu0(const Insn& in, u32 flags)
	{
		// Named in full: PCSX2's VU interpreter has functions of the same names
		// in the global namespace.
#define VUFLAGS flags
#define DEST in.dest()
#define FD in.fd()
#define FS in.fs()
#define FT in.ft()
#define BC in.bc()
#define FSF in.fsf()
#define FTF in.ftf()
#define IMM5 in.imm5()
#define S(op, fn, args) \
	case Op::op: \
		ps2n::ee::ops::fn args; \
		return true;

		switch (in.op)
		{
#include "ps2n/r5900_sem_cop2.inc"
			default:
				return false;
		}

#undef S
#undef IMM5
#undef FTF
#undef FSF
#undef BC
#undef FT
#undef FS
#undef FD
#undef DEST
#undef VUFLAGS
	}

	// ---- carrying out the recompiler's blocks ------------------------------------
	// recSafeExitExecution(): execution is to stop at an event test, and one is
	// to come at the end of the block being carried out. Inside an event test,
	// the IOP is stopped short instead.
	void SafeExitExecution()
	{
		s_exit_requested = true;
		if (!eeEventTestIsActive)
		{
			cpuRegs.nextEventCycle = 0;
		}
		else if (psxRegs.iopCycleEE > 0)
		{
			psxRegs.iopBreak += psxRegs.iopCycleEE;
			psxRegs.iopCycleEE = 0;
		}
	}

	// recRecompile(), the part before the block itself: the executable's
	// entry point is noticed when its block is formed, and a reset asked for
	// while executing is carried out here.
	Block* FormRecBlock(u32 pc)
	{
		if (blocks::Physical(pc) == VMManager::Internal::GetCurrentELFEntryPoint())
		{
			const bool booted = VMManager::Internal::HasBootedELF();
			VMManager::Internal::EntryPointCompilingOnCPUThread();
			if (!booted)
			{
				SelectImage();
				s_after_entry = true;
				s_bios_at_entry = CompareBiosFilesNow();
			}
		}

		if (s_blocks_need_reset)
		{
			s_blocks_need_reset = false;
			ResetBlocks();
		}

		Block* block = blocks::Form(pc);
		if (!block) [[unlikely]]
		{
			Console.Error("ps2n: execution reached %08X, which is neither RAM nor ROM", pc);
			pxFailRel("ps2n: execution left RAM and ROM");
		}

		// Perhaps an executable the program loaded itself.
		AdoptLoaded(pc);
		return block;
	}

	// recSkipTimeoutLoop(): one entry into the block of a timeout loop
	// (ps2n/waitloop.h). Eight cycles pass for each count left in `reg`, but
	// time goes no further than the next event; the count comes down by the
	// cycles that passed, divided by eight. Returns true when the count has
	// reached zero: the loop is over, and execution continues after it without
	// an event test. Otherwise an event test is due with cpuRegs.pc at the
	// loop's first instruction, after which the block is entered again.
	bool AdvanceTimeoutLoop(u32 reg)
	{
		// The recompiler tests this first, before it lets any time pass.
		if (ps2n::ee::eventDue())
			return false;

		const u64 before = cpuRegs.cycle;
		const u64 count = cpuRegs.GPR.r[reg].UL[0];
		u64 after = before + count * 8;
		if (cpuRegs.nextEventCycle < after)
			after = cpuRegs.nextEventCycle;
		cpuRegs.cycle = after;
		s_stats.timeout_loop_cycles += after - before;

		// The recompiler writes bits 0 to 31 of the register and leaves the rest.
		const u32 left = static_cast<u32>(count - ((after - before) >> 3));
		cpuRegs.GPR.r[reg].UL[0] = left;
		return left == 0;
	}

	// What the recompiler's code does about VU0 before a COP2 instruction it
	// has marked (COP2_Interlock, mVUSyncVU0, mVUFinishVU0 in
	// x86/microVU_Macro.inl). The cycles of the block so far have been added
	// already where it adds them.
	void Cop2Prologue(const Step& step)
	{
		if (!(VU0.VI[REG_VPU_STAT].UL & 1))
			return; // VU0 is not running a micro program

		const u32 code = step.insn.raw;
		const u32 rs = (code >> 21) & 31;
		const bool transfer = (code >> 26) == 022 && rs < 020;

		// How far the EE is ahead of VU0.
		const auto ahead = []() {
			s64 cycles = static_cast<s64>(cpuRegs.cycle - VU0.cycle);
			// The recompiler reads 64 bits there (xSUB(rax, ptr64[&VU0.nextBlockCycles])),
			// and compares all 64 with 4. The field is 32 bits since PCSX2 v2.9.174,
			// and the four bytes after it are the low half of VU0.Mem, the address of
			// VU0's memory. PS2Native subtracts the field alone, zero-extended, which
			// is what the recompiler did while the field was 64 bits (v2.9.170).
			// Where one of these two game fixes is on, the two comparisons differ unless
			// that half is zero (docs/app-log.md, "The 64-bit read of nextBlockCycles").
			if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
				cycles -= static_cast<u32>(VU0.nextBlockCycles);
			return cycles;
		};

		if (transfer && (code & 1))
		{
			// An interlocked transfer, which only waits where it is marked for a
			// sync. CTC2 and QMTC2 let the micro program run as far as an M bit;
			// CFC2 and QMFC2 to its end.
			if (!(step.info & blocks::kCop2Sync))
				return;
			if (rs == 006 || rs == 005)
			{
				if (ahead() >= 4)
					BaseVUmicroCPU::ExecuteBlockJIT(CpuVU0, true);
				_vu0WaitMicro();
			}
			else
			{
				_vu0FinishMicro();
			}
		}
		else if (step.info & blocks::kCop2Sync)
		{
			if (ahead() >= 4)
				BaseVUmicroCPU::ExecuteBlockJIT(CpuVU0, (step.info & blocks::kCop2Interlocked) != 0);
		}
		else
		{
			_vu0FinishMicro();
		}
	}

	// A COP2 instruction, LQC2 or SQC2 itself, with nothing more done about
	// VU0 (Cop2Prologue has done that). A VU0 instruction's flags reach VU0's
	// flag registers where the block says (blocks::kCop2Status, kCop2Mac).
	void Cop2Operation(const Step& step)
	{
		using namespace ps2n::ee::ops;
		const Insn& in = step.insn;
		const u32 code = in.raw;
		const u32 opcode = code >> 26, rs = in.rs();

		if (opcode == 066)
		{
			LQC2(in.rt(), rs, in.simm());
		}
		else if (opcode == 076)
		{
			SQC2(in.rt(), rs, in.simm());
		}
		else if (rs == 001)
		{
			QMFC2(in.rt(), in.rd());
		}
		else if (rs == 002)
		{
			CFC2(in.rt(), in.rd());
		}
		else if (rs == 005)
		{
			QMTC2(in.rt(), in.rd());
		}
		else if (rs == 006)
		{
			CTC2(in.rt(), in.rd());
		}
		else
		{
			// A VU0 instruction: PS2Native's semantics (ps2n/ops_vu0.h).
			if (rs >= 020)
			{
				const u32 flags = ((step.info & blocks::kCop2Status) ? ps2n::ee::vu0::kStatus : 0u) |
								  ((step.info & blocks::kCop2Mac) ? ps2n::ee::vu0::kMac : 0u);
				if (ExecuteVu0(in, flags)) [[likely]]
					return;
			}

			// VCALLMS and VCALLMSR: PCSX2's function, which the recompiler
			// calls too (the one past the function that would run VU0's micro
			// program to its end first).
			cpuRegs.code = code;
			cpuRegs.pc = in.addr + 4;
			s_stats.delegated++;
			s_stats.delegated_by_op[static_cast<size_t>(in.op)]++;
			if (rs < 020)
			{
				R5900::GetInstruction(code).interpret();
				return;
			}
			Int_COP2SPECIAL1PrintTable[code & 63]();
		}
	}

	// A load or store the recompiler compiled as a direct access to host
	// memory has hit memory that is not plain: its code would fault, be
	// patched, and its block be thrown away (vtlb_BackpatchLoadStore in
	// pcsx2/vtlb.cpp). Once for each such piece of code.
	void FastmemFault(Step& step)
	{
		// The address vtlb's code generator knows the instruction by: the one
		// after it, or its own in a delay slot.
		step.info &= ~blocks::kFastmem;
		blocks::MemoryFault((step.info & blocks::kDelaySlot) ? step.insn.addr : step.insn.addr + 4);
	}

	// A plain step that is a load or store the recompiler compiled as a direct
	// access to host memory (kFastmem): the first time it hits memory that is
	// not plain, its block is thrown away as the recompiler's code is patched
	// (RunRecStep does the same for the others).
	__fi void PlainFastmem(Step& step)
	{
		if ((step.info & blocks::kFastmem) && EmuConfig.Cpu.Recompiler.EnableFastmem)
		{
			const Insn& in = step.insn;
			if (ps2n::ee::notPlainMemory(cpuRegs.GPR.r[in.rs()].UL[0] + static_cast<u32>(in.simm())))
				FastmemFault(step);
		}
	}

	// An instruction PS2Native has no semantics of its own for, carried out by
	// PCSX2's interpreter function for it.
	[[gnu::noinline]] void Delegate(const Insn& in)
	{
		s_stats.delegated++;
		s_stats.delegated_by_op[static_cast<size_t>(in.op)]++;
		if (in.op == Op::SYSCALL)
			CountSyscall();
		cpuRegs.code = in.raw;
		cpuRegs.pc = in.addr + 4;
		cpuRegs.branch = 0;
		R5900::GetInstruction(in.raw).interpret();
	}

	// Carries out a step that is not a branch. `eighths`: the cost of the
	// block so far.
	void RunRecStep(Step& step, u32& eighths)
	{
		const Insn& in = step.insn;
		const u32 info = step.info;

		eighths += step.eighths;
		s_interpreted_eighths += step.eighths;
		if (s_interpreting_image)
			s_interpreted_eighths_in_image += step.eighths;

		// Most instructions: nothing but the instruction itself (blocks::kPlain).
		if (info & blocks::kPlain) [[likely]]
		{
			PlainFastmem(step);
			if (ExecuteOwn(in)) [[likely]]
				return;
			Delegate(in);
			return;
		}

		if (info & blocks::kCostOnly)
			return;

		if (info & blocks::kDisableInterrupts)
		{
			// recDI: where the recompiler put the instruction's effect.
			ps2n::ee::ops::DI();
			return;
		}

		if (info & blocks::kSkippedSyscall)
		{
			// recSYSCALL: what the BIOS's routine is taken to cost.
			eighths += ps2n::r5900::kSkippedSyscallEighths;
			s_stats.syscalls_skipped++;
			return;
		}

		// scaleblockcycles_clear()
		if (info & blocks::kFlush)
		{
			cpuRegs.cycle += ps2n::ee::blockCycles(eighths);
			eighths = ps2n::ee::blockRemainder(eighths);
		}

		if (info & (blocks::kCop2Sync | blocks::kCop2Finish | blocks::kFastmem | blocks::kForcesEventTest | blocks::kEndsBlock)) [[unlikely]]
		{
			if (info & (blocks::kCop2Sync | blocks::kCop2Finish))
				Cop2Prologue(step);
			if ((info & blocks::kFastmem) && EmuConfig.Cpu.Recompiler.EnableFastmem &&
				ps2n::ee::notPlainMemory(cpuRegs.GPR.r[in.rs()].UL[0] + static_cast<u32>(in.simm())))
			{
				FastmemFault(step);
			}
			// recBranchCall
			if (info & blocks::kForcesEventTest)
				cpuRegs.nextEventCycle = cpuRegs.cycle;
			// Where execution continues, unless the instruction says otherwise.
			if (info & blocks::kEndsBlock)
				cpuRegs.pc = in.addr + 4;
		}

		const u32 opcode = in.raw >> 26;
		if (opcode == 022 || opcode == 066 || opcode == 076)
		{
			Cop2Operation(step);
			return;
		}

		if (ExecuteOwn(in))
			return;
		Delegate(in);
	}

	// iBranchTest(): the end of a block. The idle loop's way of ending is in
	// RunRecBranch().
	__fi void EndRecBlock(u32 eighths, bool test)
	{
		cpuRegs.cycle += ps2n::ee::blockCycles(eighths);
		if (test && ps2n::ee::eventDue())
			RunEventTest();
	}

	// Carries out the branch or jump that ends a block, with its delay slot
	// (`delay`, or null when the recompiler dropped it), and ends the block.
	__fi void RunRecBranch(const Block& block, Step& step, Step* delay, u32 eighths)
	{
		const Insn& in = step.insn;
		const u32 next = in.addr + 8;
		eighths += step.eighths;
		s_interpreted_eighths += step.eighths;
		if (s_interpreting_image)
			s_interpreted_eighths_in_image += step.eighths;

		// The link is written first, and for the conditional ones before the
		// register is compared.
		switch (in.op)
		{
			case Op::JAL:
			case Op::BLTZAL: case Op::BLTZALL: case Op::BGEZAL: case Op::BGEZALL:
				cpuRegs.GPR.r[31].UD[0] = next;
				break;
			default:
				break;
		}

		const s64 s = cpuRegs.GPR.r[in.rs()].SD[0];
		const s64 t = cpuRegs.GPR.r[in.rt()].SD[0];

		u32 target = in.is(ps2n::r5900::F_BRANCH) ? in.branchTarget() : 0;
		bool taken = false;
		bool fixed = true; // the target is in the instruction

		switch (in.op)
		{
			case Op::J:
			case Op::JAL:
				target = in.jumpTarget();
				taken = true;
				break;
			case Op::JR:
				target = cpuRegs.GPR.r[in.rs()].UL[0];
				taken = true;
				fixed = false;
				break;
			case Op::JALR:
				target = cpuRegs.GPR.r[in.rs()].UL[0];
				if (in.rd())
					cpuRegs.GPR.r[in.rd()].UD[0] = next;
				taken = true;
				fixed = false;
				break;

			case Op::BEQ: case Op::BEQL: taken = (s == t); break;
			case Op::BNE: case Op::BNEL: taken = (s != t); break;
			case Op::BLEZ: case Op::BLEZL: taken = (s <= 0); break;
			case Op::BGTZ: case Op::BGTZL: taken = (s > 0); break;
			case Op::BLTZ: case Op::BLTZL: case Op::BLTZAL: case Op::BLTZALL: taken = (s < 0); break;
			case Op::BGEZ: case Op::BGEZL: case Op::BGEZAL: case Op::BGEZALL: taken = (s >= 0); break;

			case Op::BC0F: case Op::BC0FL: taken = !ps2n::ee::cop0Condition(); break;
			case Op::BC0T: case Op::BC0TL: taken = ps2n::ee::cop0Condition(); break;
			case Op::BC1F: case Op::BC1FL: taken = !ps2n::ee::fpuCondition(); break;
			case Op::BC1T: case Op::BC1TL: taken = ps2n::ee::fpuCondition(); break;
			case Op::BC2F: case Op::BC2FL: taken = !ps2n::ee::cop2Condition(); break;
			case Op::BC2T: case Op::BC2TL: taken = ps2n::ee::cop2Condition(); break;

			default:
				pxFailRel("ps2n: RunRecBranch() was given an instruction that is not a branch");
				return;
		}

		// A likely branch that is not taken has no delay slot.
		if (delay && (taken || !in.isLikely()))
			RunRecStep(*delay, eighths);

		// Whatever the delay slot did to the PC, the branch decides it.
		cpuRegs.pc = taken ? target : next;

		if (taken && fixed && (block.flags & blocks::kIdleLoop) && target == block.start && EmuConfig.Speedhacks.WaitLoop)
		{
			// An idle loop: time moves on to the next event, which is then
			// handled whatever the time.
			cpuRegs.cycle += ps2n::ee::blockCycles(eighths);
			if (!ps2n::ee::eventDue())
				cpuRegs.cycle = cpuRegs.nextEventCycle;
			RunEventTest();
		}
		else
		{
			EndRecBlock(eighths, true);
		}
	}

	// Where a block lies, for the report (Block::where).
	enum Where : u8
	{
		kWhereImage, // inside the executable's code
		kWhereKernel, // from 80000000
		kWhereLoader, // below 00100000
		kWhereElsewhere,
	};

	// Notes where `block` lies, for as long as the images in use stay what they
	// are: what CountRecSteps() would otherwise work out at every execution.
	[[gnu::noinline]] void NoteBlock(Block& block)
	{
		const u32 pc = block.start;
		block.where = InImage(pc) ? kWhereImage : pc >= 0x80000000u ? kWhereKernel : pc < 0x00100000u ? kWhereLoader : kWhereElsewhere;
		block.bios_place = block.where == kWhereImage ? ps2n::ee::kBiosPlaces : BiosPlaceOf(pc);
		block.noted = s_images_epoch;
	}

	// Counts `steps` interpreted instructions of `block`.
	__fi void CountRecSteps(Block& block, u64 steps)
	{
		if (block.noted != s_images_epoch) [[unlikely]]
			NoteBlock(block);
		s_stats.interpreted += steps;
		switch (block.where)
		{
			case kWhereImage:
				s_stats.interpreted_in_image += steps;
				return;
			case kWhereKernel:
				s_stats.interpreted_kernel += steps;
				break;
			case kWhereLoader:
				s_stats.interpreted_loader += steps;
				break;
			default:
				s_stats.interpreted_elsewhere += steps;
				break;
		}
		if (block.bios_place < ps2n::ee::kBiosPlaces)
			s_stats.bios_interpreted[s_after_entry][block.bios_place] += steps;
	}

	// Carries out the steps of `block` from step `first` on, with `eighths`
	// counted so far, and ends the block.
	void RunRecSteps(Block& block, size_t first, u32 eighths)
	{
		bool ended = false; // by an instruction that ends a block without being a branch
		const size_t count = block.steps.size();
		CountRecSteps(block, count - first);
		if (block.factor != 1) [[unlikely]]
			s_stats.interpreted_doubled += count - first;
		s_interpreting_image = block.where == kWhereImage;

		for (size_t i = first; i < count; i++)
		{
			Step& step = block.steps[i];

			// Most instructions: nothing but the instruction itself
			// (blocks::kPlain), carried out here rather than through
			// RunRecStep(), a call less for each. A plain step does not end the
			// block.
			if (step.info & blocks::kPlain) [[likely]]
			{
				eighths += step.eighths;
				s_interpreted_eighths += step.eighths;
				if (s_interpreting_image)
					s_interpreted_eighths_in_image += step.eighths;
				PlainFastmem(step);
				if (!ExecuteOwn(step.insn)) [[unlikely]]
					Delegate(step.insn);
				continue;
			}

			if (step.info & blocks::kBranch)
			{
				Step* delay = (i + 1 < count && (block.steps[i + 1].info & blocks::kDelaySlot)) ? &block.steps[i + 1] : nullptr;
				RunRecBranch(block, step, delay, eighths);
				return;
			}

			RunRecStep(step, eighths);
			ended |= (step.info & blocks::kEndsBlock) != 0;
		}

		if (ended)
		{
			// Where the instruction left the PC: after itself, or at an
			// exception's handler.
			EndRecBlock(eighths, true);
		}
		else
		{
			// The block was cut short (a page boundary, another block, the
			// target of its own branch, a FlushCache call that is not made). A
			// short one links to the next without an event test.
			cpuRegs.pc = block.start + block.size * 4;
			EndRecBlock(eighths, block.size > 6);
		}
	}

	// Carries out one block, as the code the recompiler would have compiled
	// for it does. A block that is thrown away while it runs stays good until
	// the next event test (blocks::Collect).
	void RunRecBlock(Block& block)
	{
		if (cpuRegs.pc != block.start) [[unlikely]]
			s_stats.other_mirror++;

		// The BIOS's block starts the interpreter carries out, for the
		// recompiler; and the words of every block it carries out outside the
		// executable, the BIOS's files and the executables the program loaded
		// itself that no compiled code ran (none held it, or none held what
		// memory holds), every version of it (docs/adr/0057).
		if (!(block.flags & blocks::kInterpreted)) [[unlikely]]
		{
			block.flags |= blocks::kInterpreted;
			if (BiosPlaceOf(block.start) < ps2n::ee::kBiosPlaces)
				s_bios_starts.insert(block.start);
			if ((block.body == kBodyNone || block.body == kBodyUnknown) && !InImage(block.start) && !InBiosFile(block.start) &&
				!HeldByLoaded(block.start))
			{
				// The words the runtime compares with memory before compiled code
				// runs the block (IdentifyBody): from its start to where the scan
				// for its end stopped, past a block cut short there, or to its last
				// instruction where that is further.
				const u32 end = std::max(block.nominal_end, block.start + block.size * 4);
				std::vector<u32> words;
				words.reserve((end - block.start) / 4);
				for (u32 at = block.start; at < end; at += 4)
					words.push_back(memRead32(at));
				std::vector<std::vector<u32>>& versions = s_unheld[block.start];
				if (std::find(versions.begin(), versions.end(), words) == versions.end())
				{
					versions.push_back(std::move(words));
					s_stats.new_code++;
				}
			}
		}

		// What the BIOS's program loader is hooked for.
		if (block.flags & (blocks::kHookMain | blocks::kHookExec)) [[unlikely]]
		{
			s_stats.hooked_block_runs++;
			if (block.flags & blocks::kHookMain)
				eeloadHook();
			if (block.flags & blocks::kHookExec)
				eeloadHook2();
		}

		// A block in a page that is not write-protected checks itself first.
		if ((block.flags & blocks::kManual) && !blocks::EnterManual(block))
			return;

		if (block.flags & blocks::kTimeoutLoop)
		{
			if (AdvanceTimeoutLoop(block.timeout_reg))
				cpuRegs.pc = block.nominal_end;
			else
				RunEventTest();
			return;
		}

		RunRecSteps(block, 0, 0);
	}

	// ---- compiled code and the recompiler's blocks ---------------------------------
	// Which compiled code the words of `block`, which begins at leader `index`
	// of `image`, are: the function's own, one of its variants', or none.
	// Compared by the leader segments the block lies in; a block that reaches
	// outside the image's code, or begins where no leader is, is nobody's.
	u8 IdentifyBody(const ImageInfo& image, const Block& block, u32 index)
	{
		const FunctionInfo* first = FunctionOf(image, block.start);
		if (!first)
			return kBodyNone;

		const u32 stop = std::max(block.nominal_end, block.start + block.size * 4);

		struct Segment
		{
			u32 index;
			u64 hash;
			bool in_first;
		};
		std::vector<Segment> segments;

		const FunctionInfo* f = first;
		u32 i = index;
		for (u32 a = block.start; a < stop;)
		{
			if (a >= f->end)
			{
				// On into the function that follows, if one does.
				f++;
				if (f == image.functions + image.function_count || f->start != a)
					return kBodyNone;
			}
			if (i >= image.leader_count || image.leaders[i] != a)
				return kBodyNone;

			const u32 end = (i + 1 < f->first_leader + f->leader_count) ? image.leaders[i + 1] : f->end;
			segments.push_back({i, HashOfMemory(a, end), f == first});
			a = end;
			i++;
		}

		const auto matches = [&](const u64* variant_hashes) {
			for (const Segment& s : segments)
			{
				const u64 expected =
					(variant_hashes && s.in_first) ? variant_hashes[s.index - first->first_leader] : image.segment_hashes[s.index];
				if (s.hash == 0 || s.hash != expected)
					return false;
			}
			return true;
		};

		if (matches(nullptr))
			return kBodyFunction;
		for (u32 v = 0; v < first->variant_count; v++)
		{
			if (matches(image.variants[first->first_variant + v].segment_hashes))
				return static_cast<u8>(1 + v);
		}
		return kBodyNone;
	}

	// Whether compiled code can carry out `block` as the recompiler's code
	// would. Not: a block whose instructions count double (formed while the EE
	// issued one instruction a cycle; compiled code counts single); one that
	// is not run at all; one cut short where compiled code has no label to
	// notice it; one that ends after a load that compiled code does not expect
	// to end a block. (One that runs a hook can: blockStart() runs the hook.)
	bool CompiledCodeCanRun(const ImageInfo& image, const Block& block)
	{
		if (block.factor != 1 || (block.flags & blocks::kTimeoutLoop))
			return false;
		if ((block.flags & blocks::kCutShort) && LeaderIndex(image, block.nominal_end) < 0)
			return false;
		for (const Step& step : block.steps)
		{
			if ((step.info & blocks::kEndsBlock) && ps2n::r5900::isNarrowLoad(step.insn) && LeaderIndex(image, step.insn.addr + 4) < 0)
				return false;
		}
		// What the recompiler decided of the block's COP2 instructions, where it
		// wrote that into the function's code (ImageInfo::cop2; ADR 0046), has
		// to be what the record says of them here.
		if (image.cop2_count && block.body == kBodyFunction)
		{
			const ps2n::ee::Cop2Decided* const first = image.cop2;
			const ps2n::ee::Cop2Decided* const last = image.cop2 + image.cop2_count;
			for (const Step& step : block.steps)
			{
				if (!ps2n::r5900::cop2::decided(step.insn.raw))
					continue;
				const ps2n::ee::Cop2Decided* const it = std::lower_bound(first, last, step.insn.addr,
					[](const ps2n::ee::Cop2Decided& d, u32 pc) { return d.pc < pc; });
				if (it != last && it->pc == step.insn.addr && (step.info & ps2n::r5900::cop2::kDecided) != it->info)
				{
					s_stats.cop2_differs++;
					return false;
				}
			}
		}
		return true;
	}

	// What compiled code is told of `block` (recomp.h).
	u32 PackBlock(const Block& block)
	{
		u32 word = block.nominal_end & ps2n::ee::kBlockEndMask;
		if (block.size > 6)
			word |= ps2n::ee::kBlockTestsAtSplit;
		if ((block.flags & blocks::kIdleLoop) && EmuConfig.Speedhacks.WaitLoop)
			word |= ps2n::ee::kBlockIdleLoop;
		return word;
	}

	// The step of the block that began at `start` which is the instruction at
	// `pc`: its position among the block's steps, or -1. Compiled code only
	// knows where its block began; the block may have been thrown away since.
	struct StepRef
	{
		Block* block;
		s64 index;
	};
	StepRef FindStep(u32 start, u32 pc)
	{
		Block* block = blocks::FindRunning(start);
		if (!block) [[unlikely]]
			return {nullptr, -1};

		// Usually there is a step an instruction; then it is at this one.
		const size_t count = block->steps.size();
		size_t guess = (pc - block->start) / 4;
		if (guess < count && block->steps[guess].insn.addr == pc && !(block->steps[guess].info & blocks::kDisableInterrupts))
			return {block, static_cast<s64>(guess)};
		for (size_t i = 0; i < count; i++)
		{
			// DI has two steps; the first is the one that stands for the instruction.
			if (block->steps[i].insn.addr == pc)
				return {block, static_cast<s64>(i)};
		}
		return {block, -1};
	}

	[[noreturn]] void NoSuchStep(u32 start, u32 pc)
	{
		Console.Error("ps2n: compiled code is at %08X in a block that began at %08X, and the runtime has no such instruction in it",
			pc, start);
		pxFailRel("ps2n: compiled code and the runtime disagree about a block");
		std::abort();
	}

	void ProviderReserve()
	{
	}

	void ProviderShutdown()
	{
		// A machine started after this one may have another ROM.
		s_in_use.clear();
		s_image = nullptr;
		s_loaded.clear();
		s_dispatching = false;
		s_bios_looked_for = false;
		FlushCache();
	}

	// "Forget what you know about the code in memory." The core calls this
	// when an executable starts and when CPU settings change, also while
	// Execute() is running (recResetEE): then every block is thrown away
	// before the next one is formed, and execution is left as soon as it can
	// be.
	void ProviderReset()
	{
		// The settings may be what changed: whether compiled code runs at all
		// depends on one of them (Lookup()).
		FlushCallSites();

		if (s_executing)
		{
			s_blocks_need_reset = true;
			SafeExitExecution();
		}
		else
		{
			s_blocks_need_reset = false;
			ResetBlocks();
			s_exit_requested = false;
		}
	}

	void ProviderStep()
	{
	}

	// recExecute(), and the dispatcher the recompiler's blocks return to.
	void ProviderExecute()
	{
		// Zero the first time and when an instruction is cancelled; non-zero
		// when execution is to stop. Either way no compiled frame is left.
		if (fastjmp_set(&s_jmp) != 0)
		{
			s_executing = false;
			return;
		}

		s_executing = true;

		char here;
		s_stack_base = reinterpret_cast<uptr>(&here);

		if (FullFpuClampMode() && !s_full_clamp_reported)
		{
			s_full_clamp_reported = true;
			Console.Warning("ps2n: the EE clamp mode Full is not implemented. FPU instructions are carried out by PCSX2's"
							" interpreter functions, and no code compiled ahead of time is used.");
		}
		if (s_blocks_need_reset)
		{
			s_blocks_need_reset = false;
			ResetBlocks();
		}
		if (!s_bios_looked_for)
			UseBios();

		for (;;)
		{
			s_unwind_requested = false;

			const u32 pc = cpuRegs.pc;
			if (const StaticFn fn = Lookup(pc))
			{
				s_stats.static_entries++;
				const u32 next = fn(pc);
				if (next != kUnwind)
					cpuRegs.pc = next;
				continue;
			}

			Block* block = blocks::Find(pc);
			if (!block)
			{
				block = FormRecBlock(pc);
				// Forming the block at an executable's entry point is what
				// chooses its image: its compiled code runs from this very
				// block on.
				if (Lookup(pc))
					continue;
			}
			NoteMissing(block, pc);
			RunRecBlock(*block);
		}
	}

	// Stops execution at the next event test, where the machine's state is
	// whole.
	void ProviderExitExecution()
	{
		SafeExitExecution();
	}

	// PCSX2's interpreter functions abandon an instruction this way, for
	// example a load from a misaligned address. cpuRegs.pc is past it.
	void ProviderCancelInstruction()
	{
		fastjmp_jmp(&s_jmp, 0);
	}

	// The core reports that guest memory holding code may have changed: a
	// write hit a page that was write-protected, or the address mapping
	// changed. `size` is in words. The blocks over it are thrown away
	// (recClear); compiled code finds out at its next label.
	void ProviderClear(u32 addr, u32 size)
	{
		blocks::Clear(addr, size);
	}

	R5900cpu s_provider = {
		ProviderReserve,
		ProviderShutdown,
		ProviderReset,
		ProviderStep,
		ProviderExecute,
		ProviderExitExecution,
		ProviderCancelInstruction,
		ProviderClear,
	};
} // namespace

// ---- for the host ----------------------------------------------------------------

R5900cpu* ps2n::ee::Provider()
{
	return &s_provider;
}

void ps2n::ee::SetImages(const ImageInfo* const* images, size_t count)
{
	s_images = images;
	s_image_count = count;
	FlushCache();
}

void ps2n::ee::SetStaticCodeEnabled(bool enabled)
{
	s_static_enabled = enabled;
	FlushCache();
}

void ps2n::ee::SetCallSitesEnabled(bool enabled)
{
	s_call_sites_enabled = enabled;
	FlushCallSites();
}

void ps2n::ee::SetHostStackBudget(size_t bytes)
{
	s_stack_budget = bytes;
}

bool ps2n::ee::StartTrace(const char* path)
{
	StopTrace();
	s_trace_file = std::fopen(path, "wb");
	if (!s_trace_file)
		return false;
	std::setvbuf(s_trace_file, nullptr, _IOFBF, 1 << 20);
	return true;
}

bool ps2n::ee::CompareTraceWith(const char* path)
{
	if (s_trace_reference)
		std::fclose(s_trace_reference);
	s_trace_mismatch.clear();
	s_trace_reference = std::fopen(path, "rb");
	return s_trace_reference != nullptr;
}

void ps2n::ee::SetTraceDetailFrom(uint64_t event_test)
{
	s_trace_detail_from = event_test;
}

const char* ps2n::ee::GetTraceMismatch()
{
	return s_trace_mismatch.empty() ? nullptr : s_trace_mismatch.c_str();
}

void ps2n::ee::StopTrace()
{
	if (s_trace_file)
	{
		std::fclose(s_trace_file);
		s_trace_file = nullptr;
	}
	if (s_trace_reference)
	{
		// A reference run that went on after this one stopped is a difference too.
		char reference[1024];
		while (s_trace_mismatch.empty() && std::fgets(reference, sizeof(reference), s_trace_reference))
		{
			if (reference[0] != ' ')
			{
				reference[std::strcspn(reference, "\r\n")] = 0;
				s_trace_mismatch = fmt::format("this run stopped after event test {}; the reference run went on: \"{}\"",
					s_stats.event_tests, reference);
			}
		}
		std::fclose(s_trace_reference);
		s_trace_reference = nullptr;
	}
}

// ps2n/ops_fpu.h
uint64_t ps2n::ee::ops::fpu::console_followed[2] = {0, 0};

const ps2n::ee::Stats& ps2n::ee::GetStats()
{
	// What compiled code counted itself (askAbout()) since the last time.
	static u64 s_checked_runs_counted = 0;
	const u64 checked = checkedRuns - s_checked_runs_counted;
	s_checked_runs_counted = checkedRuns;
	s_stats.blocks_compiled += checked;
	s_stats.blocks_compiled_known += checked;
	s_stats.checked_runs += checked;

	s_stats.interpreted_cycles = s_interpreted_eighths / 8;
	s_stats.interpreted_cycles_in_image = s_interpreted_eighths_in_image / 8;
	s_stats.console_followed_abs_neg = ops::fpu::console_followed[0];
	s_stats.console_followed_max_min = ops::fpu::console_followed[1];
	std::fill(std::begin(s_stats.bios_block_starts), std::end(s_stats.bios_block_starts), 0);
	for (const u32 pc : s_bios_starts)
	{
		const u8 place = BiosPlaceOf(pc);
		if (place < kBiosPlaces)
			s_stats.bios_block_starts[place]++;
	}
	return s_stats;
}

std::vector<uint32_t> ps2n::ee::GetBiosBlockStarts()
{
	std::vector<uint32_t> starts(s_bios_starts.begin(), s_bios_starts.end());
	std::sort(starts.begin(), starts.end());
	return starts;
}

const std::vector<ps2n::ee::BiosFileCheck>& ps2n::ee::GetBiosFilesAtEntry()
{
	return s_bios_at_entry;
}

std::vector<ps2n::ee::CapturedCode> ps2n::ee::GetCodeOutside()
{
	std::vector<CapturedCode> code;
	for (const auto& [start, versions] : s_unheld)
	{
		for (const std::vector<u32>& words : versions)
			code.push_back({start, words});
	}
	return code;
}

std::vector<ps2n::ee::BiosFileCheck> ps2n::ee::CompareBiosFiles()
{
	return CompareBiosFilesNow();
}

namespace
{
	// The addresses in `image`'s code at which a block had to be interpreted,
	// sorted.
	std::vector<uint32_t> MissingIn(const ImageInfo* image)
	{
		std::vector<uint32_t> entries;
		const auto it = image ? s_missing.find(image) : s_missing.end();
		if (it == s_missing.end())
			return entries;
		for (size_t i = 0; i < it->second.size(); i++)
		{
			for (u64 bits = it->second[i]; bits; bits &= bits - 1)
			{
				u32 bit = 0;
				for (u64 b = bits; !(b & 1); b >>= 1)
					bit++;
				entries.push_back(image->text_start + static_cast<u32>(i * 64 + bit) * 4);
			}
		}
		return entries;
	}
} // namespace

std::vector<uint32_t> ps2n::ee::GetMissingEntries()
{
	return MissingIn(s_image);
}

std::vector<ps2n::ee::LoadedProgram> ps2n::ee::GetLoadedPrograms()
{
	std::vector<LoadedProgram> programs;
	for (const ImageInfo* image : s_loaded)
	{
		const auto uses = s_loaded_uses.find(image);
		programs.push_back({image, uses == s_loaded_uses.end() ? 0 : uses->second, MissingIn(image)});
	}
	return programs;
}

const std::vector<ps2n::ee::CapturedCode>& ps2n::ee::GetCapturedCode()
{
	return s_captured;
}

std::vector<ps2n::ee::FunctionRan> ps2n::ee::GetFunctionsRan()
{
	return s_ran_order;
}

std::vector<ps2n::ee::Handover> ps2n::ee::GetHandovers()
{
	std::vector<Handover> handovers;
	for (const auto& [pc, count] : s_left_at)
		handovers.push_back({pc, count, false});
	for (const auto& [pc, count] : s_interpreted_at)
		handovers.push_back({pc, count, true});
	std::sort(handovers.begin(), handovers.end(), [](const Handover& a, const Handover& b) {
		return a.count != b.count ? a.count > b.count : a.pc < b.pc;
	});
	return handovers;
}

bool ps2n::ee::ExecuteOwn(uint32_t code, uint32_t pc)
{
	const Insn in(code, pc);
	// A VU0 instruction as in a block of that one instruction, at whose end
	// its flags reach both flag registers.
	if (in.opcode() == 022 && in.rs() >= 020)
		return ExecuteVu0(in, ps2n::ee::vu0::kStatus | ps2n::ee::vu0::kMac);
	return ::ExecuteOwn(in);
}

// ---- for compiled code -----------------------------------------------------------

uint32_t ps2n::ee::blockExit = 0;

uint32_t ps2n::ee::blockAsk(const ImageInfo& image, uint32_t index)
{
	uint32_t block = 0;
	const uint32_t pc = blockStart(image, index, 0, &block);
	if (block == 0)
		blockExit = pc;
	return block;
}

ps2n::ee::Checked ps2n::ee::checkedRecords[ps2n::ee::kCheckedLimit];
uint64_t ps2n::ee::checkedRuns = 0;

namespace
{
	// What follows when a record no longer holds, out of checkedAnswer()'s
	// way so that it needs no frame of its own: the block thrown away, as
	// PCSX2's code for it throws it away, and what is formed there now asked
	// about.
	[[gnu::noinline]] u32 CheckedChanged(Checked& c)
	{
		blocks::DiscardManual(*static_cast<Block*>(c.block));
		return ps2n::ee::blockAsk(*c.image, c.index);
	}
	[[gnu::noinline]] u32 CheckedPageProtected(Checked& c)
	{
		blocks::ResetManualPage(*static_cast<Block*>(c.block));
		return ps2n::ee::blockAsk(*c.image, c.index);
	}
} // namespace

uint32_t ps2n::ee::checkedChanged(uint32_t id)
{
	return CheckedChanged(s_checked[id]);
}

uint32_t ps2n::ee::checkedPageProtected(uint32_t id)
{
	return CheckedPageProtected(s_checked[id]);
}

// What a module compiled before askAbout() compared the words itself calls
// (2026-11-20).
uint32_t ps2n::ee::checkedAnswer(uint32_t id)
{
	Checked& c = s_checked[id];

	// As blocks::EnterManual() does: the words, against what the block was
	// formed from; then the execution counted.
	for (u32 i = 0; i < c.count; i++)
	{
		u32 word;
		std::memcpy(&word, c.memory + i * 4, 4);
		if (word != c.words[i]) [[unlikely]]
			return CheckedChanged(c);
	}
	if (c.page)
	{
		const u32 sum = static_cast<u32>(*c.page) + c.size;
		*c.page = static_cast<u16>(sum);
		if (sum > 0xFFFFu) [[unlikely]]
			return CheckedPageProtected(c);
	}
	s_stats.blocks_compiled++;
	s_stats.blocks_compiled_known++;
	s_stats.checked_runs++;
	return c.answer;
}

uint32_t ps2n::ee::blockStart(const ImageInfo& image, uint32_t index, uint32_t body, uint32_t* out)
{
	*out = 0;
	const u32 addr = image.leaders[index];
	if (body != kBodyFunction)
		s_stats.variant_asks++;

	// Asked about before, with nothing changed since: the same block, the same
	// body, compiled code still able to run it. What is left to do is what the
	// recompiler's code for it does at every execution, for a block of a page
	// that is not write-protected (Known).
	Known& known = KnownPlace(index, body);
	if (known.epoch == blocks::epoch && known.image == &image && known.index == index && known.body == body && s_dispatching) [[likely]]
	{
		cpuRegs.pc = addr;
		if (known.other)
		{
			// Another image's version of the code runs here, as below.
			s_stats.redirected_versions++;
			return known.other(addr);
		}
		if (!(known.block->flags & blocks::kManual) || blocks::EnterManual(*known.block))
		{
			*out = known.answer;
			s_stats.blocks_compiled++;
			s_stats.blocks_compiled_known++;
			return addr;
		}
		// Thrown away: its words changed, or its page is protected again. As
		// below, what is formed there now is what runs.
	}

	for (;;)
	{
		cpuRegs.pc = addr;

		// Only the images in use are ever dispatched to: the BIOS's, and the
		// executable's in memory.
		if (!InUseImage(&image) || !s_dispatching) [[unlikely]]
			return kUnwind;

		Block* block = blocks::Find(addr);
		if (!block)
		{
			block = FormRecBlock(addr);
			// Forming it may have been what started another executable.
			if (!InUseImage(&image) || !s_dispatching)
				return kUnwind;
		}

		// What the block's words are was decided against an image no longer in
		// use: decided again.
		if (block->body != kBodyUnknown && block->owner && block->owner != &image &&
			!InUseImage(static_cast<const ImageInfo*>(block->owner))) [[unlikely]]
			block->body = kBodyUnknown;

		if (block->body == kBodyUnknown)
		{
			const ImageInfo* owner = &image;
			u8 found = (block->start == addr) ? IdentifyBody(image, *block, index) : kBodyNone;
			// Code run outside the executable: where this image's is not what
			// memory holds, another version of it at the same addresses may be
			// (ImageInfo::layers; docs/adr/0057). So may the program's own
			// version of code in a place of the BIOS's, which it patched there
			// (the program loader's, in Gran Turismo 3).
			if (found == kBodyNone && block->start == addr &&
				(image.kind == ps2n::ee::kImageOutside || image.kind == ps2n::ee::kImageBios))
			{
				for (const InUse& u : s_in_use)
				{
					if (u.image == &image || u.image->kind != ps2n::ee::kImageOutside)
						continue;
					const s32 i = LeaderIndex(*u.image, addr);
					if (i < 0)
						continue;
					const u8 other_body = IdentifyBody(*u.image, *block, static_cast<u32>(i));
					if (other_body != kBodyNone)
					{
						found = other_body;
						owner = u.image;
						s_stats.blocks_other_versions++;
						break;
					}
				}
			}
			block->body = found;
			block->owner = owner;
			s_stats.blocks_checked++;
			if (block->body == kBodyNone && block->start == addr)
			{
				// Memory holds code nothing was compiled from: kept for the
				// recompiler. In an executable's function's place, the
				// function's words; outside the executable, the blocks that
				// run there, as the interpreter carries them out (RunRecBlock;
				// docs/adr/0057).
				s_stats.blocks_unknown++;
				if (image.kind != ps2n::ee::kImageOutside)
				{
					if (const FunctionInfo* f = FunctionOf(image, addr))
						CaptureCode(image, *f);
				}
				RecordMissing(addr);
			}
		}

		const ImageInfo& holder = block->owner ? *static_cast<const ImageInfo*>(block->owner) : image;
		if (block->body != kBodyNone && CompiledCodeCanRun(holder, *block))
		{
			if (&holder != &image || block->body != body)
			{
				// Other compiled code is in this function's place: its variant
				// when the function itself was entered, or the reverse; or
				// another image's version of code run outside the executable.
				// That runs; its own label will ask here again.
				const FunctionInfo& f = *FunctionOf(holder, addr);
				const StaticFn other = (block->body == kBodyFunction) ? f.fn : holder.variants[f.first_variant + block->body - 1].fn;
				if (&holder != &image)
				{
					// Kept, so that this label does not look the block up again
					// while nothing changes; and the dispatcher enters the other
					// image here from now on.
					s_stats.redirected_versions++;
					known = {&image, blocks::epoch, block, index, body, 0, other};
					const s32 entry = FindEntry(holder, addr);
					CacheEntry& e = s_cache[(addr >> 2) & (kCacheSize - 1)];
					if (entry >= 0 && e.pc == addr)
						e = {addr, entry, &holder};
				}
				else
				{
					s_stats.redirected++;
				}
				return other(addr);
			}

			// The program loader's blocks PCSX2 hooks for fast boot: its
			// recompiler calls the hook at the block's start, at every
			// execution, and then runs the block's code.
			if (block->flags & (blocks::kHookMain | blocks::kHookExec)) [[unlikely]]
			{
				s_stats.hooked_block_runs++;
				if (block->flags & blocks::kHookMain)
					eeloadHook();
				if (block->flags & blocks::kHookExec)
					eeloadHook2();
			}

			// A block in a page that is not write-protected checks itself at
			// every execution, and is formed anew when it has changed.
			if ((block->flags & blocks::kManual) && !blocks::EnterManual(*block))
				continue;

			*out = PackBlock(*block);
			const bool hooked = (block->flags & (blocks::kHookMain | blocks::kHookExec)) != 0;
			const bool manual = (block->flags & blocks::kManual) != 0;
			const bool in_table = body == kBodyFunction && !hooked && *out >= ps2n::ee::kCheckedLimit;
			u32 checked = 0;
			u32* const place = &image.blocks[index];
			// One place at a time knows a block: the one zeroed when it is
			// thrown away. Another that knew it (of an image over the same
			// addresses, used before) asks again.
			if (in_table && block->cache && block->cache != place)
				*block->cache = 0;
			if (in_table && !manual)
			{
				// From now on the function's label need not ask, until the
				// block is thrown away.
				*place = *out;
				block->cache = place;
			}
			else if (in_table && block->host && (checked = NewChecked()) != 0)
			{
				// From now on the function's label compares the block's words
				// itself, until the block is thrown away.
				s_checked[checked] = {block->host, block->words.data(), static_cast<u32>(block->words.size()),
					static_cast<u32>(block->words.size()) & 0xFFFFu, blocks::ManualCount(*block), *out, index, &image, place, block};
				*place = checked;
				block->cache = place;
			}
			else if (!hooked)
			{
				// Its label asks every time: the answer is kept, for as long as
				// it holds.
				known = {&image, blocks::epoch, block, index, body, *out};
			}
			MarkRan(image, addr);
			s_stats.blocks_compiled++;
			return addr;
		}

		// Not one compiled code can carry out: the interpreter does. (A
		// timeout loop is not run by anything.)
		if (block->flags & blocks::kTimeoutLoop)
		{
			s_stats.timeout_loop_entries++;
		}
		else
		{
			s_stats.blocks_interpreted_at_labels++;
			s_interpreted_at[addr]++;
		}
		RunRecBlock(*block);
		return MustUnwind() ? kUnwind : cpuRegs.pc;
	}
}

uint32_t ps2n::ee::eventTest(uint32_t next)
{
	cpuRegs.pc = next;
	RunEventTest();
	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

uint32_t ps2n::ee::systemInstruction(uint32_t pc, uint32_t code, uint32_t eighths)
{
	const Insn in(code, pc);

	// recBranchCall: ERET, EI and the traps are followed by an event test
	// whatever the time. BREAK (recBREAK) is not.
	if (ps2n::r5900::forcesEventTest(in))
		cpuRegs.nextEventCycle = cpuRegs.cycle;

	// The core's function reads the instruction word and the PC.
	cpuRegs.code = code;
	cpuRegs.pc = pc + 4;
	cpuRegs.branch = 0;
	R5900::GetInstruction(code).interpret();

	EndRecBlock(eighths, true);
	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

uint32_t ps2n::ee::syscallInstruction(uint32_t pc, uint32_t code, uint32_t start, uint32_t eighths)
{
	const StepRef ref = FindStep(start, pc);
	if (ref.index < 0) [[unlikely]]
		NoSuchStep(start, pc);

	if (ref.block->steps[ref.index].info & blocks::kSkippedSyscall)
	{
		// recSYSCALL: a FlushCache call is not made. Its block is cut short
		// after it, as RunRecSteps() ends one.
		s_stats.syscalls_skipped++;
		cpuRegs.pc = pc + 4;
		EndRecBlock(eighths + ps2n::r5900::kSkippedSyscallEighths, ref.block->size > 6);
		return MustUnwind() ? kUnwind : cpuRegs.pc;
	}

	CountSyscall();
	cpuRegs.code = code;
	cpuRegs.pc = pc + 4;
	cpuRegs.branch = 0;
	R5900::GetInstruction(code).interpret();

	EndRecBlock(eighths, true);
	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

// The step compiled code asked for the last time at each of so many places,
// so that a COP2 instruction that comes round again in the same block finds
// its step without looking the block up. An answer holds for as long as no
// block has been formed or thrown away since. Compiled code reads the copy of
// the step's info (PS2N_VU0); a step's info changes afterwards only in a
// load or store whose fault throws its block away (FastmemFault), which
// counts the epoch up. The epoch starts at zero, which is never the blocks'
// own: they begin at one.
ps2n::ee::Cop2Site ps2n::ee::cop2Sites[ps2n::ee::kCop2Sites];

static_assert(ps2n::ee::kStepFlush == blocks::kFlush && ps2n::ee::kStepCop2Sync == blocks::kCop2Sync &&
			  ps2n::ee::kStepCop2Finish == blocks::kCop2Finish && ps2n::ee::kStepCop2Status == blocks::kCop2Status &&
			  ps2n::ee::kStepCop2Mac == blocks::kCop2Mac);

namespace
{
	__fi Step& StepAt(u32 start, u32 pc)
	{
		ps2n::ee::Cop2Site& site = ps2n::ee::cop2Sites[(pc >> 2) & (ps2n::ee::kCop2Sites - 1)];
		if (site.pc == pc && site.start == start && site.epoch == blocks::epoch) [[likely]]
			return *static_cast<Step*>(site.step);

		const StepRef ref = FindStep(start, pc);
		if (ref.index < 0) [[unlikely]]
			NoSuchStep(start, pc);
		Step& step = ref.block->steps[ref.index];
		site = {pc, start, blocks::epoch, &step, step.info};
		return step;
	}
} // namespace

uint32_t ps2n::ee::cop2InfoSlow(uint32_t pc, uint32_t start)
{
	return StepAt(start, pc).info;
}

namespace
{
	ps2n::ee::Cop2Kind Cop2KindOf(const Insn& in)
	{
		using namespace ps2n::ee;
		const u32 opcode = in.raw >> 26, rs = in.rs();
		if (opcode == 066)
			return kCop2Lqc2;
		if (opcode == 076)
			return kCop2Sqc2;
		switch (rs)
		{
			case 001: return kCop2Qmfc2;
			case 002: return kCop2Cfc2;
			case 005: return kCop2Qmtc2;
			case 006: return kCop2Ctc2;
			default: break;
		}
		if (rs >= 020 && in.op != ps2n::r5900::Op::VCALLMS && in.op != ps2n::r5900::Op::VCALLMSR)
			return kCop2Vu0;
		return kCop2Other;
	}
} // namespace

int32_t ps2n::ee::cycleRate = 0;
uint32_t ps2n::ee::cycleTable[kCycleTable];
uint32_t ps2n::ee::remainderMask = 7;

namespace
{
	// The default rate's, before anything runs (the self-test counts by it
	// too, without a reset).
	struct CycleTableAtStart
	{
		CycleTableAtStart() { MakeCycleTable(); }
	} s_cycle_table_at_start;
} // namespace

uint32_t ps2n::ee::scaledBlockCycles(uint32_t eighths)
{
	// scaleblockcycles_calculation(), pcsx2/x86/ix86-32/iR5900.cpp, as it is
	// written there: a block of five cycles or fewer (40 eighths) counts as at
	// the default rate; 1 divides the cycles by 1.3 in single precision, 2 and
	// 3 shift the eighths by 4 and 5, -1 to -3 multiply them by 5 to 9 32nds.
	const int32_t rate = cycleRate;
	uint32_t cycles;
	if (rate == 0 || eighths <= 40 || rate < -99 || rate > 3)
		cycles = eighths >> 3;
	else if (rate > 1)
		cycles = eighths >> (2 + rate);
	else if (rate == 1)
		cycles = static_cast<uint32_t>(static_cast<float>(eighths >> 3) / 1.3f);
	else if (rate == -1)
		cycles = (eighths <= 80 || eighths > 168 ? 5u : 7u) * eighths / 32;
	else
		cycles = (static_cast<uint32_t>(5 + (-2 * (rate + 1))) * eighths) >> 5;
	return cycles < 1 ? 1 : cycles;
}

uint32_t ps2n::ee::cop2Instruction(uint32_t pc, uint32_t start, uint32_t eighths)
{
	Step& step = StepAt(start, pc);

	const Cop2Kind kind = Cop2KindOf(step.insn);
	s_stats.cop2_runtime[kind]++;
	if (VU0.VI[REG_VPU_STAT].UL & 1)
		s_stats.cop2_runtime_vu0_running[kind]++;

	// As RunRecStep() does it; the instruction's own count is in `eighths`.
	if (step.info & blocks::kFlush)
	{
		cpuRegs.cycle += blockCycles(eighths);
		eighths = blockRemainder(eighths);
	}
	if (step.info & (blocks::kCop2Sync | blocks::kCop2Finish))
		Cop2Prologue(step);
	if ((step.info & blocks::kFastmem) && EmuConfig.Cpu.Recompiler.EnableFastmem &&
		notPlainMemory(cpuRegs.GPR.r[step.insn.rs()].UL[0] + static_cast<u32>(step.insn.simm())))
	{
		FastmemFault(step);
	}
	Cop2Operation(step);
	return eighths;
}

void ps2n::ee::vcallms(uint32_t pc, uint32_t code)
{
	// What cop2Instruction() comes to for these two: their record says only
	// that the cycles are added first, which compiled code has done (the
	// record of a block, rec_blocks.cpp, gives VCALLMS and VCALLMSR kFlush and
	// nothing about VU0), and Cop2Operation() hands them to PCSX2's function.
	s_stats.cop2_runtime[kCop2Other]++;
	if (VU0.VI[REG_VPU_STAT].UL & 1)
		s_stats.cop2_runtime_vu0_running[kCop2Other]++;
	s_stats.delegated++;
	s_stats.delegated_by_op[static_cast<size_t>((code & 63) == 071 ? ps2n::r5900::Op::VCALLMSR : ps2n::r5900::Op::VCALLMS)]++;

	cpuRegs.code = code;
	cpuRegs.pc = pc + 4;
	Int_COP2SPECIAL1PrintTable[code & 63]();
}

uint64_t ps2n::ee::skipFixedLoop(uint64_t cycle, uint32_t cycles)
{
	// No event is due at `cycle`; after n more times round the count is
	// cycle + n * cycles, and the first n at which one is due is the time
	// round whose end tests for it.
	const uint64_t left = cpuRegs.nextEventCycle - cycle;
	const uint64_t rounds = (left + cycles - 1) / cycles;
	cycle += rounds * cycles;
	cpuRegs.cycle = cycle;
	s_stats.fixed_loop_rounds += rounds;
	s_stats.fixed_loop_cycles += rounds * cycles;
	s_stats.fixed_loop_skips++;
	return cycle;
}

namespace
{
	// The steps of the loads and stores compiled code last reached memory that
	// is not plain with, as cop2Sites keeps the COP2 instructions' (apart from
	// them, which a game of many VU0 instructions keeps busy): good for as long
	// as no block has been formed or thrown away (blocks::epoch).
	struct FaultSite
	{
		u32 pc;
		u32 start;
		u64 epoch;
		Step* step;
	};
	constexpr u32 kFaultSites = 4096;
	FaultSite s_fault_sites[kFaultSites];

	__fi Step& FaultStepAt(u32 start, u32 pc)
	{
		FaultSite& site = s_fault_sites[(pc >> 2) & (kFaultSites - 1)];
		if (site.pc == pc && site.start == start && site.epoch == blocks::epoch) [[likely]]
			return *site.step;
		const StepRef ref = FindStep(start, pc);
		if (ref.index < 0) [[unlikely]]
			NoSuchStep(start, pc);
		Step& step = ref.block->steps[ref.index];
		site = {pc, start, blocks::epoch, &step};
		return step;
	}
} // namespace

bool ps2n::ee::memoryFault(uint32_t pc, uint32_t start)
{
	// Every access of compiled code to memory that is not plain comes here, and
	// only the first of each instruction's changes anything: the step is found
	// where the last few thousand are kept, not looked up in its block every
	// time (in a replay of Jak and Daxter, 2026-11-20, the lookups took 1.7% of
	// the run, 8.0% of Agent Under Fire's).
	Step& step = FaultStepAt(start, pc);

	if ((step.info & blocks::kFastmem) && EmuConfig.Cpu.Recompiler.EnableFastmem)
		FastmemFault(step);
	return (step.info & blocks::kEndsBlock) != 0;
}

template <typename T>
T ps2n::ee::slowRead(uint32_t address, uint32_t pc, uint32_t start)
{
	memoryFault(pc, start);
	return vtlb_memRead<T>(address);
}

template <typename T>
T ps2n::ee::slowReadEnds(uint32_t address, uint32_t pc, uint32_t start, bool* ends)
{
	*ends = memoryFault(pc, start);
	return vtlb_memRead<T>(address);
}

template <typename T>
void ps2n::ee::slowWrite(uint32_t address, T value, uint32_t pc, uint32_t start)
{
	memoryFault(pc, start);
	vtlb_memWrite<T>(address, value);
}

RETURNS_R128 ps2n::ee::slowRead128(uint32_t address, uint32_t pc, uint32_t start)
{
	memoryFault(pc, start);
	return vtlb_memRead128(address);
}

void TAKES_R128 ps2n::ee::slowWrite128(uint32_t address, r128 value, uint32_t pc, uint32_t start)
{
	memoryFault(pc, start);
	vtlb_memWrite128(address, value);
}

// The sizes in which compiled code loads and stores general and FPU registers
// (PS2N_LOAD, PS2N_STORE).
template mem8_t ps2n::ee::slowRead<mem8_t>(uint32_t, uint32_t, uint32_t);
template mem16_t ps2n::ee::slowRead<mem16_t>(uint32_t, uint32_t, uint32_t);
template mem32_t ps2n::ee::slowRead<mem32_t>(uint32_t, uint32_t, uint32_t);
template mem64_t ps2n::ee::slowRead<mem64_t>(uint32_t, uint32_t, uint32_t);
template mem8_t ps2n::ee::slowReadEnds<mem8_t>(uint32_t, uint32_t, uint32_t, bool*);
template mem16_t ps2n::ee::slowReadEnds<mem16_t>(uint32_t, uint32_t, uint32_t, bool*);
template mem32_t ps2n::ee::slowReadEnds<mem32_t>(uint32_t, uint32_t, uint32_t, bool*);
template mem64_t ps2n::ee::slowReadEnds<mem64_t>(uint32_t, uint32_t, uint32_t, bool*);
template void ps2n::ee::slowWrite<mem8_t>(uint32_t, mem8_t, uint32_t, uint32_t);
template void ps2n::ee::slowWrite<mem16_t>(uint32_t, mem16_t, uint32_t, uint32_t);
template void ps2n::ee::slowWrite<mem32_t>(uint32_t, mem32_t, uint32_t, uint32_t);
template void ps2n::ee::slowWrite<mem64_t>(uint32_t, mem64_t, uint32_t, uint32_t);

namespace
{
	__fi void Delegate(u32 pc, u32 code, u32 branch)
	{
		// The core's function reads the instruction word, and may read the PC
		// and whether it is in a delay slot.
		cpuRegs.code = code;
		cpuRegs.pc = pc + 4;
		cpuRegs.branch = branch;
		R5900::GetInstruction(code).interpret();
		cpuRegs.branch = 0;

		// Compiled code continues with the next instruction unconditionally.
		// Nothing delegated in PCSX2 v2.9.189 redirects the PC; if that ever
		// changes, it must not go unnoticed.
		if (cpuRegs.pc != pc + 4) [[unlikely]]
		{
			Console.Error("ps2n: the delegated instruction %08X at %08X changed the PC to %08X", code, pc, cpuRegs.pc);
			pxFailRel("ps2n: a delegated instruction changed the PC");
		}
	}
} // namespace

void ps2n::ee::delegate(uint32_t pc, uint32_t code)
{
	Delegate(pc, code, 0);
}

void ps2n::ee::delegateInDelaySlot(uint32_t pc, uint32_t code)
{
	// PCSX2's recompiler does not tell its interpreter functions that an
	// instruction is in a delay slot.
	Delegate(pc, code, 0);
}

// Where dispatch() remembers the compiled function it found for a call whose
// target is only known at run time (PS2N_CALL_DYNAMIC). Emptied by
// FlushCallSites(), which every way that puts an image in use goes through,
// before compiled code can first read it.
ps2n::ee::CallSite ps2n::ee::callSites[ps2n::ee::kCallSites];

uint32_t ps2n::ee::dispatch(uint32_t pc)
{
	// Where the program loads executables itself, forming the block here may
	// put the one in memory in use (AdoptLoaded) before its first block runs,
	// and take another out, whose compiled frames are then left.
	if (!s_loaded.empty() && !blocks::Find(pc))
	{
		FormRecBlock(pc);
		if (MustUnwind())
		{
			cpuRegs.pc = pc;
			return kUnwind;
		}
	}

	if (const StaticFn fn = Lookup(pc))
	{
		// The next call there goes to the function from compiled code itself
		// (PS2N_CALL_DYNAMIC).
		if (s_call_sites_enabled)
			callSites[(pc >> 2) & (kCallSites - 1)] = {pc, fn};
		return fn(pc);
	}

	// Interpret until compiled code is reached again. Its caller then decides,
	// by the PC, whether that is where it expected to continue.
	cpuRegs.pc = pc;
	do
	{
		// Noted before the block is formed, as before: forming it may change
		// the images in use.
		const u32 at = cpuRegs.pc;
		Block* block = blocks::Find(at);
		NoteMissing(block, at);
		if (!block)
			block = FormRecBlock(at);
		RunRecBlock(*block);

		if (MustUnwind())
			return kUnwind;
	} while (!Lookup(cpuRegs.pc));

	return cpuRegs.pc;
}

uint32_t PS2N_CC ps2n::ee::jump(uint32_t pc)
{
	// To the start of a compiled function: a call that will not come back
	// here (a tail call). The function runs on top of this frame's caller,
	// and what it returns is for that caller to judge.
	if (const StaticFn fn = Lookup(pc))
	{
		const CacheEntry& e = LookupEntry(pc);
		if (e.image->functions[e.index].start == pc)
			return fn(pc);
	}

	// Anything else is perhaps a return through a register other than $ra, in
	// which case a caller is waiting for exactly this PC; or it goes back to
	// the dispatcher.
	return pc;
}

uint32_t ps2n::ee::leave(uint32_t pc, uint32_t eighths, uint32_t start)
{
	s_stats.left++;
	s_left_at[pc]++;

	// The interpreter carries out the rest of the block compiled code began.
	const StepRef ref = FindStep(start, pc);
	if (ref.index < 0) [[unlikely]]
		NoSuchStep(start, pc);
	RunRecSteps(*ref.block, static_cast<size_t>(ref.index), eighths);

	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

void ps2n::ee::badEntry(const ImageInfo& image, uint32_t function, uint32_t pc)
{
	Console.Error("ps2n: compiled function %08X-%08X of %s was entered at %08X, where it has no label", image.functions[function].start,
		image.functions[function].end, image.name, pc);
	pxFailRel("ps2n: a compiled function was entered at an address it has no label for");
	std::abort();
}

bool ps2n::ee::cop0Condition()
{
	// CPCOND0, pcsx2/COP0.cpp
	return ((dmacRegs.stat.CIS | ~dmacRegs.pcr.CPC) & 0x3FF) == 0x3FF;
}

bool ps2n::ee::cop2Condition()
{
	// CP2COND, pcsx2/COP2.cpp
	return ((VU0.VI[REG_VPU_STAT].US[0] >> 8) & 1) != 0;
}
