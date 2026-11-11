// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's EE provider for the PCSX2 core: the dispatcher of code compiled
// ahead of time, and the fallback interpreter for everything else.
//
// docs/execution-model.md describes the model; runtime/include/ps2n/recomp.h
// defines the contract with compiled code.
//
// The interpreter decodes with PS2Native's decoder and carries out an
// instruction with PS2Native's own semantics where those exist
// (runtime/include/ps2n/ops_*.h). Every other instruction is carried out by
// PCSX2's interpreter function for it, reached through PCSX2's opcode table,
// and counted as delegated.
//
// The boot hooks follow pcsx2/Interpreter.cpp and pcsx2/x86/ix86-32/iR5900.cpp
// of PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+.
//
// Not implemented, and so different from PCSX2's own providers (the list of
// what differs about time is in docs/execution-model.md):
//  - the EE cycle rate setting (counted as its default, 0);
//  - GoemonTlbHack (a game fix PCSX2 applies at JAL and JR);
//  - pattern patches PCSX2 applies when its recompiler compiles a block.

#include "ee_provider.h"

#include "ps2n/codehash.h"
#include "ps2n/ops_int.h"
#include "ps2n/r5900.h"
#include "ps2n/recomp.h"
#include "ps2n/waitloop.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Dmac.h"
#include "pcsx2/R5900OpcodeTables.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VU.h"

#include "common/FastJmp.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

using ps2n::ee::FunctionInfo;
using ps2n::ee::ImageInfo;
using ps2n::ee::kInvalid;
using ps2n::ee::kTrusted;
using ps2n::ee::kUnverified;
using ps2n::ee::kUnwind;
using ps2n::ee::StaticFn;
using ps2n::r5900::Insn;
using ps2n::r5900::Op;

namespace
{
	ps2n::ee::Stats s_stats;
	u64 s_interpreted_eighths = 0;
	u64 s_interpreted_eighths_in_image = 0;

	// Where Execute() returns to when execution is to stop (value 1) or the
	// current instruction is abandoned (value 0).
	fastjmp_buf s_jmp;

	bool s_exit_requested = false;
	bool s_executing = false;
	bool s_cycle_rate_reported = false;
	bool s_full_clamp_reported = false;

	// ---- interpreter state ---------------------------------------------------
	// The cost of the instructions interpreted since the last block end, in
	// eighths of a cycle.
	u32 s_block_eighths = 0;
	// Above zero while a delay slot is being executed.
	u32 s_delay_depth = 0;

	// Where the block being interpreted began (or an impossible address when
	// it began in compiled code), whether everything in it so far is what an
	// idle loop may hold, and whether its branch went back to its start: an
	// idle loop, whose time is skipped (ps2n/waitloop.h).
	constexpr u32 kNoBlockStart = 0xFFFFFFFFu;
	u32 s_block_start = kNoBlockStart;
	ps2n::r5900::WaitLoopScan s_block_scan;
	bool s_block_is_idle_loop = false;

	// ---- compiled code -------------------------------------------------------
	const ImageInfo* const* s_images = nullptr;
	size_t s_image_count = 0;
	bool s_static_enabled = true;

	// The image of the executable in memory, when there is one, and whether
	// its code is dispatched to.
	const ImageInfo* s_image = nullptr;
	bool s_dispatching = false;

	// Set when code memory was written: every compiled frame has to be left,
	// since any of them may be executing what was overwritten.
	bool s_unwind_requested = false;

	// How often each function of the image has been compared with RAM.
	std::vector<u8> s_verify_counts;
	// A function that keeps having to be verified shares its pages with data
	// the program writes. Past this count its pages are no longer
	// write-protected, and it is verified on every entry instead.
	constexpr u8 kMaxProtections = 16;

	// For each function of the image: one past the last address any of its
	// compiled forms (the function, its variants) was compiled from.
	std::vector<u32> s_extents;

	// For each function of the image, the highest such address of it and of
	// those before it, as a physical address. A function's compiled range can
	// reach past the start of the next, so the ranges are not sorted by their
	// ends; this is what Clear() searches instead.
	std::vector<u32> s_extent_max;

	// The code found in memory in the place of a function, when nothing
	// compiled matched it.
	std::vector<ps2n::ee::CapturedCode> s_captured;

	// One bit per instruction of the image's code range: a block had to be
	// interpreted from there.
	std::vector<u64> s_missing;

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

	// PC -> function of the image with an entry there, or -1.
	struct CacheEntry
	{
		u32 pc;
		s32 index;
	};
	constexpr u32 kCacheSize = 1u << 14;
	CacheEntry s_cache[kCacheSize];

	constexpr u32 kPhysicalMask = 0x1FFFFFFFu;

	void FlushCache()
	{
		for (CacheEntry& e : s_cache)
			e = {0xFFFFFFFFu, -1};
	}

	bool InImage(u32 pc)
	{
		return s_image && (pc - s_image->text_start) < (s_image->text_end - s_image->text_start);
	}

	void FlagAllUnverified()
	{
		if (s_image)
			std::memset(s_image->state, kUnverified, s_image->function_count);
	}

	// Chooses the image of the executable that has just reached its entry
	// point.
	void SelectImage()
	{
		const u32 crc = VMManager::GetCurrentCRC();

		s_image = nullptr;
		for (size_t i = 0; i < s_image_count; i++)
		{
			if (s_images[i]->crc == crc)
				s_image = s_images[i];
		}
		s_dispatching = s_image && s_static_enabled;
		s_stats.image = s_image;
		FlushCache();

		// Frames of the image used until now must not continue.
		s_unwind_requested = true;

		if (!s_image)
		{
			Console.WriteLn(Color_StrongOrange, "ps2n: no compiled code for the executable with CRC %08X; it is interpreted", crc);
			s_verify_counts.clear();
			s_extents.clear();
			s_extent_max.clear();
			s_missing.clear();
			return;
		}

		FlagAllUnverified();
		s_verify_counts.assign(s_image->function_count, 0);
		s_extents.resize(s_image->function_count);
		s_extent_max.resize(s_image->function_count);
		u32 highest = 0;
		for (u32 i = 0; i < s_image->function_count; i++)
		{
			const FunctionInfo& f = s_image->functions[i];
			u32 extent = f.extent;
			for (u32 v = 0; v < f.variant_count; v++)
				extent = std::max(extent, s_image->variants[f.first_variant + v].extent);
			s_extents[i] = extent;

			highest = std::max(highest, extent & kPhysicalMask);
			s_extent_max[i] = highest;
		}
		s_missing.assign(((s_image->text_end - s_image->text_start) / 4 + 63) / 64, 0);
		Console.WriteLn(Color_StrongGreen, "ps2n: %s compiled code of %s (CRC %08X): %u functions at %08X-%08X, by %s",
			s_dispatching ? "using" : "NOT using (switched off)", s_image->name, crc, s_image->function_count,
			s_image->text_start, s_image->text_end, s_image->generator);
	}

	// The function of the image that can be entered at `pc`, or -1.
	s32 FindEntry(const ImageInfo& image, u32 pc)
	{
		if ((pc - image.text_start) >= (image.text_end - image.text_start))
			return -1;

		const FunctionInfo* begin = image.functions;
		const FunctionInfo* end = begin + image.function_count;
		const FunctionInfo* it = std::upper_bound(begin, end, pc, [](u32 a, const FunctionInfo& f) { return a < f.start; });
		if (it == begin)
			return -1;
		--it;
		if (pc >= it->end)
			return -1;

		if (pc != it->start)
		{
			const u32* resume = image.resume_points + it->first_resume;
			if (!std::binary_search(resume, resume + it->resume_count, pc))
				return -1;
		}
		return static_cast<s32>(it - begin);
	}

	// The index of the compiled function to run at `pc`, or -1. Compiled code
	// assumes the EE issues two instructions at a time, as the BIOS sets it up
	// (COP0 Config, bit 18); if a program switches that off, it is interpreted.
	__fi s32 LookupIndex(u32 pc)
	{
		if (!s_dispatching || !(cpuRegs.CP0.n.Config & (1u << 18)) || EmuConfig.Cpu.Recompiler.fpuFullMode)
			return -1;

		CacheEntry& e = s_cache[(pc >> 2) & (kCacheSize - 1)];
		if (e.pc != pc)
			e = {pc, FindEntry(*s_image, pc)};

		if (e.index < 0 || s_image->state[e.index] == kInvalid)
			return -1;
		return e.index;
	}

	// The compiled code to run in the place of function `index` in state
	// `state`: the function, or the variant that is in memory. (Unverified,
	// the function's own entry check finds out which.)
	__fi StaticFn CodeFor(const ImageInfo& image, u32 index, u8 state)
	{
		const FunctionInfo& f = image.functions[index];
		if (state >= ps2n::ee::kFirstVariant)
			return image.variants[f.first_variant + (state - ps2n::ee::kFirstVariant)].fn;
		return f.fn;
	}

	__fi StaticFn Lookup(u32 pc)
	{
		const s32 index = LookupIndex(pc);
		return index < 0 ? nullptr : CodeFor(*s_image, static_cast<u32>(index), s_image->state[index]);
	}

	void RecordMissing(u32 pc)
	{
		if (s_dispatching && InImage(pc))
		{
			const u32 bit = (pc - s_image->text_start) / 4;
			s_missing[bit / 64] |= 1ull << (bit % 64);
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

	// Which compiled form of function `index` the code in memory is: kTrusted
	// for the function as the executable has it, kFirstVariant + n for its
	// variant n, kInvalid for none.
	u8 IdentifyCode(const ImageInfo& image, u32 index)
	{
		const FunctionInfo& f = image.functions[index];

		u32 hashed_extent = f.extent;
		u64 hash = HashOfMemory(f.start, hashed_extent);
		if (hash == f.hash)
			return kTrusted;

		for (u32 v = 0; v < f.variant_count; v++)
		{
			const ps2n::ee::VariantInfo& variant = image.variants[f.first_variant + v];
			if (variant.extent != hashed_extent)
			{
				hashed_extent = variant.extent;
				hash = HashOfMemory(f.start, hashed_extent);
			}
			if (hash == variant.hash)
				return static_cast<u8>(ps2n::ee::kFirstVariant + v);
		}
		return kInvalid;
	}

	// Keeps the words in memory in the place of function `index`, for the
	// host to hand to the recompiler.
	void CaptureCode(const ImageInfo& image, u32 index)
	{
		const FunctionInfo& f = image.functions[index];

		ps2n::ee::CapturedCode code;
		code.start = f.start;
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
			if (have.start == code.start && have.words == code.words)
				return;
		}
		s_captured.push_back(std::move(code));
	}

	// Write-protects the pages of [start, end) through the core, which then
	// reports a write to any of them through the provider's Clear.
	void ProtectPages(u32 start, u32 end)
	{
		for (u32 page = start & ~0xFFFu; page < end; page += 0x1000)
		{
			if ((page & 0x1FFFFFFFu) < Ps2MemSize::ExposedRam)
				mmap_MarkCountedRamPage(page);
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
	// to be left. Either code memory was written, or the host stack has grown
	// too deep, which guest code can cause without growing its own stack (a
	// function that is called, and jumps back into its caller instead of
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

	// The end of an interpreted block: counts its cycles and, when an event is
	// due, lets the core handle it. cpuRegs.pc is the next instruction, so an
	// interrupt taken here returns to it.
	void EndBlock()
	{
		// A delay slot does not end a block; the branch it belongs to does, once
		// the PC is the branch's target.
		if (s_delay_depth != 0 || s_block_eighths == 0)
			return;

		cpuRegs.cycle += ps2n::ee::blockCycles(s_block_eighths);
		s_block_eighths = 0;

		if (s_block_is_idle_loop)
		{
			s_block_is_idle_loop = false;
			ps2n::ee::skipIdleTime();
		}

		if (ps2n::ee::eventDue())
			RunEventTest();
	}

	// Counts a SYSCALL about to be carried out, by its number.
	__fi void CountSyscall()
	{
		const s32 number = cpuRegs.GPR.n.v1.SL[0];
		s_stats.syscalls[static_cast<u8>(number < 0 ? -number : number)]++;
	}

	// One entry into the block of a timeout loop (ps2n/waitloop.h), as PCSX2's
	// recompiler compiles it (recSkipTimeoutLoop, x86/ix86-32/iR5900.cpp). Eight
	// cycles pass for each count left in `reg`, but time goes no further than
	// the next event; the count comes down by the cycles that passed, divided
	// by eight. Returns true when the count has reached zero: the loop is over,
	// and execution continues after it without an event test. Otherwise an event
	// test is due with cpuRegs.pc at the loop's first instruction, after which
	// the block is entered again. The interpreter and compiled code both go
	// through here, so that they do the same.
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

	// At the start of an interpreted block: when a timeout loop begins at
	// cpuRegs.pc, does what PCSX2's recompiler does in its place and returns
	// true. Compiled code makes the same test ahead of time, at every label
	// (emitTimeoutLoop in recompiler/src/codegen.cpp); the two must agree.
	bool SkipTimeoutLoop()
	{
		if (!EmuConfig.Speedhacks.WaitLoop || s_block_eighths != 0 || s_delay_depth != 0)
			return false;

		const u32 start = cpuRegs.pc;
		const ps2n::r5900::TimeoutLoop loop = ps2n::r5900::findTimeoutLoop(start, [](u32 address) { return ps2n::ee::read32(address); });
		if (!loop.found)
			return false;

		if (AdvanceTimeoutLoop(loop.reg))
			cpuRegs.pc = loop.end;
		else
			RunEventTest();
		return true;
	}

	bool StepInstruction();

	void SetLink(u32 reg)
	{
		// cpuRegs.pc is the delay slot here; the return address is the
		// instruction after it.
		cpuRegs.GPR.r[reg].UD[0] = cpuRegs.pc + 4;
	}

	// A taken branch: the delay slot runs, then control goes to `target`.
	// `fixed`: the target is in the instruction, not in a register.
	void TakeBranch(u32 target, bool fixed)
	{
		cpuRegs.branch = 1;
		s_delay_depth++;
		StepInstruction();
		s_delay_depth--;

		// cpuRegs.branch is cleared when the delay slot raised an exception, or
		// was itself a taken branch; the PC is then already where it must be.
		if (cpuRegs.branch != 0)
		{
			cpuRegs.pc = target;
			cpuRegs.branch = 0;

			// Back to where this block began, having done nothing that lasts.
			if (fixed && s_delay_depth == 0 && target == s_block_start && s_block_scan.idle)
				s_block_is_idle_loop = true;
		}

		EndBlock();
	}

	// Carries out a branch or a jump. cpuRegs.pc is the address of its delay
	// slot.
	void ExecuteBranch(const Insn& in)
	{
		const s64 s = cpuRegs.GPR.r[in.rs()].SD[0];
		const s64 t = cpuRegs.GPR.r[in.rt()].SD[0];

		u32 target = in.is(ps2n::r5900::F_BRANCH) ? in.branchTarget() : 0;
		bool taken = false;

		switch (in.op)
		{
			case Op::J:
				target = in.jumpTarget();
				taken = true;
				break;
			case Op::JAL:
				SetLink(31);
				target = in.jumpTarget();
				taken = true;
				break;
			case Op::JR:
				target = cpuRegs.GPR.r[in.rs()].UL[0];
				taken = true;
				break;
			case Op::JALR:
				target = cpuRegs.GPR.r[in.rs()].UL[0];
				if (in.rd())
					SetLink(in.rd());
				taken = true;
				break;

			case Op::BEQ: case Op::BEQL: taken = (s == t); break;
			case Op::BNE: case Op::BNEL: taken = (s != t); break;
			case Op::BLEZ: case Op::BLEZL: taken = (s <= 0); break;
			case Op::BGTZ: case Op::BGTZL: taken = (s > 0); break;
			case Op::BLTZ: case Op::BLTZL: taken = (s < 0); break;
			case Op::BGEZ: case Op::BGEZL: taken = (s >= 0); break;
			case Op::BLTZAL: case Op::BLTZALL: SetLink(31); taken = (s < 0); break;
			case Op::BGEZAL: case Op::BGEZALL: SetLink(31); taken = (s >= 0); break;

			case Op::BC0F: case Op::BC0FL: taken = !ps2n::ee::cop0Condition(); break;
			case Op::BC0T: case Op::BC0TL: taken = ps2n::ee::cop0Condition(); break;

			case Op::BC1F: case Op::BC1FL: taken = !ps2n::ee::fpuCondition(); break;
			case Op::BC1T: case Op::BC1TL: taken = ps2n::ee::fpuCondition(); break;

			case Op::BC2F: case Op::BC2FL: taken = !ps2n::ee::cop2Condition(); break;
			case Op::BC2T: case Op::BC2TL: taken = ps2n::ee::cop2Condition(); break;

			default:
				pxFailRel("ps2n: ExecuteBranch() was given an instruction that is not a branch");
				return;
		}

		if (taken)
		{
			TakeBranch(target, in.is(ps2n::r5900::F_BRANCH | ps2n::r5900::F_JUMP));
		}
		else if (in.isLikely())
		{
			// A branch-likely that is not taken skips its delay slot.
			cpuRegs.pc += 4;
			EndBlock();
		}
		else
		{
			// Not taken: the delay slot runs all the same, as a delay slot, and
			// the block ends after it, exactly as when the branch is taken.
			// (PCSX2's interpreter tests for events before the delay slot here;
			// its recompiler, like this, after it.)
			TakeBranch(cpuRegs.pc + 4, false);
		}
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
	// for this instruction.
	bool ExecuteOwn(const Insn& in)
	{
		using namespace ps2n::ee::ops;

		if (in.is(ps2n::r5900::F_FPU) && FullFpuClampMode()) [[unlikely]]
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
#define INTERLOCK in.interlock()
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
			default:
				return false;
		}

#undef S
#undef INTERLOCK
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

	// Interprets the instruction at cpuRegs.pc: a branch together with its
	// delay slot, or one other instruction. Returns true when that ended the
	// block. What ends a block is defined in ps2n/r5900.h, for the recompiler
	// and for this interpreter alike.
	bool StepInstruction()
	{
		const u32 pc = cpuRegs.pc;

		// Advanced before the fetch: an exception expects the PC past it.
		cpuRegs.pc += 4;
		cpuRegs.code = ps2n::ee::read32(pc);

		const Insn in(cpuRegs.code, pc);

		// A block begins with the first instruction counted after the end of
		// the one before. What it holds, its final branch aside, decides
		// whether it is an idle loop.
		if (s_block_eighths == 0 && s_delay_depth == 0)
		{
			s_block_start = pc;
			s_block_scan = {};
		}
		if (!in.isControlFlow())
			s_block_scan.add(cpuRegs.code);

		// An encoding PS2Native's decoder does not know is still counted, and
		// below carried out, as PCSX2 would.
		const u32 eighths = (in.valid() ? ps2n::r5900::cycles(in.op) : R5900::GetInstruction(cpuRegs.code).cycles) *
							(2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
		s_block_eighths += eighths;
		s_interpreted_eighths += eighths;
		s_stats.interpreted++;
		if (InImage(pc))
		{
			s_interpreted_eighths_in_image += eighths;
			s_stats.interpreted_in_image++;
		}
		else if (pc >= 0x80000000u)
			s_stats.interpreted_kernel++;
		else if (pc < 0x00100000u)
			s_stats.interpreted_loader++;
		else
			s_stats.interpreted_elsewhere++;

		if (in.isControlFlow())
		{
			ExecuteBranch(in);
			return true;
		}

		if (ExecuteOwn(in))
			return false;

		s_stats.delegated++;
		s_stats.delegated_by_op[static_cast<size_t>(in.op)]++;

		if (ps2n::r5900::isSystem(in))
		{
			if (ps2n::r5900::forcesEventTest(in))
				cpuRegs.nextEventCycle = cpuRegs.cycle;
			if (in.op == Op::SYSCALL)
				CountSyscall();
			R5900::GetInstruction(cpuRegs.code).interpret();
			EndBlock();
			return true;
		}

		R5900::GetInstruction(cpuRegs.code).interpret();
		return false;
	}

	// What PCSX2 does when certain addresses of the BIOS's program loader are
	// reached: it learns which executable is being started, and starts it
	// directly when fast boot is on. PCSX2's recompiler checks these at every
	// block it compiles, its interpreter after every instruction; here they
	// are checked at the start of every interpreted block.
	void BootHooks()
	{
		const u32 pc = cpuRegs.pc;

		if (pc == EELOAD_START)
		{
			// The EELOAD _start function is the same across all BIOS versions.
			const u32 mainjump = memRead32(EELOAD_START + 0x9c);
			if (mainjump >> 26 == 3) // JAL
				g_eeloadMain = ((EELOAD_START + 0xa0) & 0xf0000000U) | (mainjump << 2 & 0x0fffffffU);
		}
		else if (g_eeloadMain && pc == g_eeloadMain)
		{
			eeloadHook();
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
		else if (g_eeloadExec && pc == g_eeloadExec)
		{
			eeloadHook2();
		}
		else if (pc == VMManager::Internal::GetCurrentELFEntryPoint() && !VMManager::Internal::HasBootedELF())
		{
			VMManager::Internal::EntryPointCompilingOnCPUThread();
			SelectImage();
		}
	}

	// Interprets from cpuRegs.pc to the end of the block.
	void InterpretBlock()
	{
		BootHooks();
		if (SkipTimeoutLoop())
			return;
		while (!StepInstruction())
		{
		}
	}

	void ProviderReserve()
	{
	}

	void ProviderShutdown()
	{
	}

	// "Forget what you know about the code in memory." The core calls this
	// when an executable starts and when CPU settings change, also while
	// Execute() is running.
	void ProviderReset()
	{
		// The core has just lifted every write protection.
		FlagAllUnverified();
		s_unwind_requested = true;

		if (!s_executing)
		{
			s_block_eighths = 0;
			s_delay_depth = 0;
			s_exit_requested = false;
		}
	}

	void ProviderStep()
	{
		StepInstruction();
	}

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
		s_delay_depth = 0;

		char here;
		s_stack_base = reinterpret_cast<uptr>(&here);

		if (EmuConfig.Speedhacks.EECycleRate != 0 && !s_cycle_rate_reported)
		{
			s_cycle_rate_reported = true;
			Console.Warning("ps2n: the EE cycle rate setting (%d) is not implemented; the EE is counted at its normal rate",
				static_cast<int>(EmuConfig.Speedhacks.EECycleRate));
		}
		if (FullFpuClampMode() && !s_full_clamp_reported)
		{
			s_full_clamp_reported = true;
			Console.Warning("ps2n: the EE clamp mode Full is not implemented. FPU instructions are carried out by PCSX2's"
							" interpreter functions, and no code compiled ahead of time is used.");
		}

		for (;;)
		{
			// Before an executable has started, its entry point has to be
			// noticed here: compiled code will be what runs it.
			if (!VMManager::Internal::HasBootedELF() && cpuRegs.pc == VMManager::Internal::GetCurrentELFEntryPoint())
				BootHooks();

			s_unwind_requested = false;

			const u32 pc = cpuRegs.pc;
			if (const StaticFn fn = Lookup(pc))
			{
				s_stats.static_entries++;
				const auto cycle = cpuRegs.cycle;
				const u32 next = fn(pc);
				if (next == kUnwind)
					continue;

				cpuRegs.pc = next;
				if (next == pc && cpuRegs.cycle == cycle) [[unlikely]]
				{
					// No block ended, so nothing was executed: the function has
					// no code for an entry its table lists. Interpreting is
					// always right, and makes the address show up in the report.
					s_stats.no_progress++;
					RecordMissing(pc);
					InterpretBlock();
				}
			}
			else
			{
				RecordMissing(pc);
				InterpretBlock();
			}
		}
	}

	// Stops execution at the next event test, where the machine's state is
	// whole. Forces one at the end of the current block.
	void ProviderExitExecution()
	{
		s_exit_requested = true;
		if (!eeEventTestIsActive)
			cpuRegs.nextEventCycle = cpuRegs.cycle;
	}

	// PCSX2's interpreter functions abandon an instruction this way, for
	// example a load from a misaligned address. cpuRegs.pc is past it.
	void ProviderCancelInstruction()
	{
		fastjmp_jmp(&s_jmp, 0);
	}

	// The core reports that guest memory holding code may have changed: a
	// write hit a page that was write-protected, or the address mapping
	// changed. `size` is in words. This can be called from the handler of the
	// write's page fault, so it only marks.
	void ProviderClear(u32 addr, u32 size)
	{
		const ImageInfo* image = s_image;
		if (!image)
			return;

		const u32 lo = addr & kPhysicalMask;
		const u32 hi = lo + size * 4;

		// The first function whose compiled range, or that of one before it,
		// reaches past `lo`.
		const size_t first = std::upper_bound(s_extent_max.begin(), s_extent_max.end(), lo) - s_extent_max.begin();

		bool any = false;
		for (size_t i = first; i < image->function_count && (image->functions[i].start & kPhysicalMask) < hi; i++)
		{
			if ((s_extents[i] & kPhysicalMask) <= lo)
				continue;

			u8& state = image->state[i];
			if (state != kUnverified)
			{
				state = kUnverified;
				any = true;
			}
		}

		// A flagged function is compared with memory again the next time it is
		// entered. Frames that are already inside one are left at the next
		// event test. No event test is forced for it: the core runs the IOP
		// and its counters from event tests, so an extra one would make a
		// compiled run differ from an interpreted one. Until then such a frame
		// can still run the old code, which is what the EE's instruction cache
		// does to a program that changes code without flushing it; the flush
		// is a system call, and every system call leaves compiled code.
		if (any)
			s_unwind_requested = true;
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

const ps2n::ee::Stats& ps2n::ee::GetStats()
{
	s_stats.interpreted_cycles = s_interpreted_eighths / 8;
	s_stats.interpreted_cycles_in_image = s_interpreted_eighths_in_image / 8;
	return s_stats;
}

std::vector<uint32_t> ps2n::ee::GetMissingEntries()
{
	std::vector<uint32_t> entries;
	if (!s_image)
		return entries;

	for (size_t i = 0; i < s_missing.size(); i++)
	{
		for (u64 bits = s_missing[i]; bits; bits &= bits - 1)
		{
			u32 bit = 0;
			for (u64 b = bits; !(b & 1); b >>= 1)
				bit++;
			entries.push_back(s_image->text_start + static_cast<u32>(i * 64 + bit) * 4);
		}
	}
	return entries;
}

const std::vector<ps2n::ee::CapturedCode>& ps2n::ee::GetCapturedCode()
{
	return s_captured;
}

bool ps2n::ee::ExecuteOwn(uint32_t code, uint32_t pc)
{
	return ::ExecuteOwn(Insn(code, pc));
}

// ---- for compiled code -----------------------------------------------------------

uint32_t ps2n::ee::eventTest(uint32_t next)
{
	cpuRegs.pc = next;
	RunEventTest();
	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

uint32_t ps2n::ee::timeoutLoop(uint32_t reg, uint32_t start, uint32_t end)
{
	if (!EmuConfig.Speedhacks.WaitLoop)
		return start;

	// What the interpreter does over as many blocks (SkipTimeoutLoop), here in
	// one call: the same steps and the same event tests at the same moments.
	for (;;)
	{
		if (AdvanceTimeoutLoop(reg))
			return end;

		const uint32_t next = eventTest(start);
		if (next != start)
			return next;
	}
}

uint32_t ps2n::ee::systemInstruction(uint32_t pc, uint32_t code, uint32_t eighths)
{
	const Insn in(code, pc);

	// As StepInstruction() does it.
	cpuRegs.code = code;
	cpuRegs.pc = pc + 4;
	cpuRegs.branch = 0;
	if (ps2n::r5900::forcesEventTest(in))
		cpuRegs.nextEventCycle = cpuRegs.cycle;
	if (in.op == Op::SYSCALL)
		CountSyscall();

	R5900::GetInstruction(code).interpret();

	cpuRegs.cycle += blockCycles(eighths);
	if (eventDue())
		RunEventTest();

	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

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
		// Nothing delegated in PCSX2 v2.9.170 redirects the PC; if that ever
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
	Delegate(pc, code, 1);
}

uint32_t ps2n::ee::dispatch(uint32_t pc)
{
	if (const StaticFn fn = Lookup(pc))
		return fn(pc);

	// Interpret until compiled code is reached again. Its caller then decides,
	// by the PC, whether that is where it expected to continue.
	cpuRegs.pc = pc;
	do
	{
		RecordMissing(cpuRegs.pc);
		InterpretBlock();

		if (MustUnwind())
			return kUnwind;
	} while (!Lookup(cpuRegs.pc));

	return cpuRegs.pc;
}

uint32_t ps2n::ee::jump(uint32_t pc)
{
	// To the start of a compiled function: a call that will not come back
	// here (a tail call). The function runs on top of this frame's caller,
	// and what it returns is for that caller to judge.
	const s32 index = LookupIndex(pc);
	if (index >= 0 && s_image->functions[index].start == pc)
		return CodeFor(*s_image, static_cast<u32>(index), s_image->state[index])(pc);

	// Anything else is perhaps a return through a register other than $ra, in
	// which case a caller is waiting for exactly this PC; or it goes back to
	// the dispatcher.
	return pc;
}

uint32_t ps2n::ee::leave(uint32_t pc, uint32_t eighths)
{
	s_stats.left++;

	// The interpreter continues the block compiled code began. If compiled
	// code began it here, with nothing counted yet, the interpreter's own rule
	// makes this its start; otherwise where it began is not known, and it
	// cannot be an idle loop's block.
	s_block_eighths = eighths;
	s_block_start = kNoBlockStart;
	s_block_scan.idle = false;
	s_delay_depth = 0;
	cpuRegs.pc = pc;
	cpuRegs.branch = 0;
	InterpretBlock();

	return MustUnwind() ? kUnwind : cpuRegs.pc;
}

bool ps2n::ee::enterSlow(const ImageInfo& image, uint32_t index, uint32_t mine, uint32_t pc, uint32_t* result)
{
	// Unless something compiled runs below: leave compiled code, and continue
	// at `pc`, interpreted.
	*result = kUnwind;
	cpuRegs.pc = pc;

	// Only the image of the executable in memory is ever dispatched to.
	if (&image != s_image)
		return false;

	u8& state = image.state[index];
	if (state == kInvalid)
		return false;

	// Which of the function's compiled forms is the code in memory.
	u8 found = state;
	if (state == kUnverified)
	{
		found = IdentifyCode(image, index);

		// Protected whatever was found: the next write to this memory is then
		// reported, and the memory compared again.
		u8& count = s_verify_counts[index];
		const bool protect = (count < kMaxProtections);
		if (protect)
		{
			count++;
			ProtectPages(image.functions[index].start, s_extents[index]);
		}
		else if (count == kMaxProtections)
		{
			count++;
			s_stats.unprotected++;
		}

		if (found == kInvalid)
		{
			// Without the protection nothing will say when this memory changes
			// again, so the function then stays unused until the next reset.
			s_stats.verify_failed++;
			state = kInvalid;
			CaptureCode(image, index);
			return false;
		}

		s_stats.verified++;
		if (protect)
			state = found;
	}

	if (found == mine)
		return true;

	// Other compiled code is in this function's place: its variant when the
	// function itself was called, or the reverse. Run that.
	s_stats.redirected++;
	*result = CodeFor(image, index, found)(pc);
	return false;
}

void ps2n::ee::badEntry(const ImageInfo& image, uint32_t index, uint32_t pc)
{
	Console.Error("ps2n: compiled function %08X-%08X of %s was entered at %08X, where it has no label", image.functions[index].start,
		image.functions[index].end, image.name, pc);
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
