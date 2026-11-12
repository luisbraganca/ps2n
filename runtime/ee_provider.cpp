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
// v2.9.170, pcsx2/x86, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+).
//
// An instruction is carried out with PS2Native's own semantics where those
// exist (runtime/include/ps2n/ops_*.h). Every other instruction is carried
// out by PCSX2's interpreter function for it, reached through PCSX2's opcode
// table, and counted as delegated.
//
// Not implemented, and so different from PCSX2's recompiler (the list is in
// docs/execution-model.md):
//  - the EE cycle rate setting (counted as its default, 0);
//  - GoemonTlbHack (a game fix PCSX2 applies at JAL and JR);
//  - its throwing away of every block when it runs out of room for code.

#include "ee_provider.h"
#include "rec_blocks.h"

#include "ps2n/codehash.h"
#include "ps2n/ops_int.h"
#include "ps2n/ops_vu0.h"
#include "ps2n/r5900.h"
#include "ps2n/rec_rules.h"
#include "ps2n/recomp.h"

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
#include <string>
#include <unordered_map>

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
	bool s_cycle_rate_reported = false;
	bool s_full_clamp_reported = false;

	// Every block is to be thrown away before the next one is formed (the
	// recompiler's eeRecNeedsReset).
	bool s_blocks_need_reset = false;

	// ---- compiled code -------------------------------------------------------
	const ImageInfo* const* s_images = nullptr;
	size_t s_image_count = 0;
	bool s_static_enabled = true;

	// The image of the executable in memory, when there is one, and whether
	// its code is dispatched to.
	const ImageInfo* s_image = nullptr;
	bool s_dispatching = false;

	// Set when another executable has started: every compiled frame has to be
	// left, since it belongs to the one before.
	bool s_unwind_requested = false;

	// The code found in memory in the place of a function, when nothing
	// compiled matched it.
	std::vector<ps2n::ee::CapturedCode> s_captured;

	// One bit per instruction of the image's code range: a block had to be
	// interpreted from there.
	std::vector<u64> s_missing;

	// Where compiled code handed over to the interpreter, and how often: the
	// rest of a block (by the instruction it left at), a whole block (by its
	// label).
	std::unordered_map<u32, u64> s_left_at;
	std::unordered_map<u32, u64> s_interpreted_at;

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

	// Which of a function's compiled forms a block's code is (Block::body).
	constexpr u8 kBodyFunction = 0; // the function as the executable has it; 1 + n: its variant n
	constexpr u8 kBodyNone = blocks::kBodyNone;
	constexpr u8 kBodyUnknown = blocks::kBodyUnknown;

	void FlushCache()
	{
		for (CacheEntry& e : s_cache)
			e = {0xFFFFFFFFu, -1};
	}

	bool InImage(u32 pc)
	{
		return s_image && (pc - s_image->text_start) < (s_image->text_end - s_image->text_start);
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
			s_missing.clear();
			return;
		}

		// Nothing is known of any of its blocks yet.
		std::memset(s_image->blocks, 0, s_image->leader_count * sizeof(u32));
		s_missing.assign(((s_image->text_end - s_image->text_start) / 4 + 63) / 64, 0);
		Console.WriteLn(Color_StrongGreen, "ps2n: %s compiled code of %s (CRC %08X): %u functions at %08X-%08X, by %s",
			s_dispatching ? "using" : "NOT using (switched off)", s_image->name, crc, s_image->function_count, s_image->text_start,
			s_image->text_end, s_image->generator);
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

	// The compiled function to enter at `pc`, or null. PCSX2's fourth clamp
	// mode for the EE's FPU is not implemented, and compiled code has the FPU
	// semantics built in: with it set, nothing compiled runs.
	__fi StaticFn Lookup(u32 pc)
	{
		if (!s_dispatching || EmuConfig.Cpu.Recompiler.fpuFullMode)
			return nullptr;

		CacheEntry& e = s_cache[(pc >> 2) & (kCacheSize - 1)];
		if (e.pc != pc)
			e = {pc, FindEntry(*s_image, pc)};
		return e.index < 0 ? nullptr : s_image->functions[e.index].fn;
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

	// Keeps the words in memory in the place of function `f`, for the host to
	// hand to the recompiler.
	void CaptureCode(const FunctionInfo& f)
	{
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
				SelectImage();
		}

		if (s_blocks_need_reset)
		{
			s_blocks_need_reset = false;
			blocks::Reset();
		}

		Block* block = blocks::Form(pc);
		if (!block) [[unlikely]]
		{
			Console.Error("ps2n: execution reached %08X, which is neither RAM nor ROM", pc);
			pxFailRel("ps2n: execution left RAM and ROM");
		}
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
			if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
				cycles -= VU0.nextBlockCycles;
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
			eighths &= 7;
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

		s_stats.delegated++;
		s_stats.delegated_by_op[static_cast<size_t>(in.op)]++;
		if (in.op == Op::SYSCALL)
			CountSyscall();
		cpuRegs.code = in.raw;
		cpuRegs.pc = in.addr + 4;
		cpuRegs.branch = 0;
		R5900::GetInstruction(in.raw).interpret();
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
	void RunRecBranch(const Block& block, Step& step, Step* delay, u32 eighths)
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

	// Counts `steps` interpreted instructions of a block that starts at `pc`.
	__fi void CountRecSteps(u32 pc, u64 steps)
	{
		s_stats.interpreted += steps;
		if (InImage(pc))
			s_stats.interpreted_in_image += steps;
		else if (pc >= 0x80000000u)
			s_stats.interpreted_kernel += steps;
		else if (pc < 0x00100000u)
			s_stats.interpreted_loader += steps;
		else
			s_stats.interpreted_elsewhere += steps;
	}

	// Carries out the steps of `block` from step `first` on, with `eighths`
	// counted so far, and ends the block.
	void RunRecSteps(Block& block, size_t first, u32 eighths)
	{
		bool ended = false; // by an instruction that ends a block without being a branch
		const size_t count = block.steps.size();
		CountRecSteps(block.start, count - first);
		s_interpreting_image = InImage(block.start);

		for (size_t i = first; i < count; i++)
		{
			Step& step = block.steps[i];
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

		// What the BIOS's program loader is hooked for.
		if (block.flags & (blocks::kHookMain | blocks::kHookExec)) [[unlikely]]
		{
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

	// The block at cpuRegs.pc, formed if there is none yet, carried out.
	void InterpretRecBlock()
	{
		Block* block = blocks::Find(cpuRegs.pc);
		if (!block)
			block = FormRecBlock(cpuRegs.pc);
		RunRecBlock(*block);
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
	// is not run at all or runs a hook; one cut short where compiled code has
	// no label to notice it; one that ends after a load that compiled code
	// does not expect to end a block.
	bool CompiledCodeCanRun(const ImageInfo& image, const Block& block)
	{
		if (block.factor != 1 || (block.flags & (blocks::kTimeoutLoop | blocks::kHookMain | blocks::kHookExec)))
			return false;
		if ((block.flags & blocks::kCutShort) && LeaderIndex(image, block.nominal_end) < 0)
			return false;
		for (const Step& step : block.steps)
		{
			if ((step.info & blocks::kEndsBlock) && ps2n::r5900::isNarrowLoad(step.insn) && LeaderIndex(image, step.insn.addr + 4) < 0)
				return false;
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
	}

	// "Forget what you know about the code in memory." The core calls this
	// when an executable starts and when CPU settings change, also while
	// Execute() is running (recResetEE): then every block is thrown away
	// before the next one is formed, and execution is left as soon as it can
	// be.
	void ProviderReset()
	{
		if (s_executing)
		{
			s_blocks_need_reset = true;
			SafeExitExecution();
		}
		else
		{
			s_blocks_need_reset = false;
			blocks::Reset();
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
		if (s_blocks_need_reset)
		{
			s_blocks_need_reset = false;
			blocks::Reset();
		}

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
			RecordMissing(pc);
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
	s_stats.interpreted_cycles = s_interpreted_eighths / 8;
	s_stats.interpreted_cycles_in_image = s_interpreted_eighths_in_image / 8;
	s_stats.console_followed_abs_neg = ops::fpu::console_followed[0];
	s_stats.console_followed_max_min = ops::fpu::console_followed[1];
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

uint32_t ps2n::ee::blockStart(const ImageInfo& image, uint32_t index, uint32_t body, uint32_t* out)
{
	*out = 0;
	const u32 addr = image.leaders[index];

	for (;;)
	{
		cpuRegs.pc = addr;

		// Only the image of the executable in memory is ever dispatched to.
		if (&image != s_image || !s_dispatching) [[unlikely]]
			return kUnwind;

		Block* block = blocks::Find(addr);
		if (!block)
		{
			block = FormRecBlock(addr);
			// Forming it may have been what started another executable.
			if (&image != s_image || !s_dispatching)
				return kUnwind;
		}

		if (block->body == kBodyUnknown)
		{
			block->body = (block->start == addr) ? IdentifyBody(image, *block, index) : kBodyNone;
			s_stats.blocks_checked++;
			if (block->body == kBodyNone && block->start == addr)
			{
				// Memory holds code nothing was compiled from: kept for the recompiler.
				s_stats.blocks_unknown++;
				if (const FunctionInfo* f = FunctionOf(image, addr))
					CaptureCode(*f);
				RecordMissing(addr);
			}
		}

		if (block->body != kBodyNone && CompiledCodeCanRun(image, *block))
		{
			if (block->body != body)
			{
				// Other compiled code is in this function's place: its variant
				// when the function itself was entered, or the reverse. That
				// runs; its own label will ask here again.
				s_stats.redirected++;
				const FunctionInfo& f = *FunctionOf(image, addr);
				const StaticFn other = (block->body == kBodyFunction) ? f.fn : image.variants[f.first_variant + block->body - 1].fn;
				return other(addr);
			}

			// A block in a page that is not write-protected checks itself at
			// every execution, and is formed anew when it has changed.
			if ((block->flags & blocks::kManual) && !blocks::EnterManual(*block))
				continue;

			*out = PackBlock(*block);
			if (body == kBodyFunction && !(block->flags & blocks::kManual))
			{
				// From now on the function's label need not ask, until the
				// block is thrown away.
				image.blocks[index] = *out;
				block->cache = &image.blocks[index];
			}
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

namespace
{
	// The step compiled code asked for the last time at each of so many
	// places, so that a COP2 instruction that comes round again in the same
	// block finds its step without looking the block up. An answer holds for
	// as long as no block has been formed or thrown away since.
	struct StepSite
	{
		u32 pc = 0;
		u32 start = 0;
		u64 epoch = 0; // never the blocks' own, which begins at one
		Step* step = nullptr;
	};
	constexpr size_t kStepSites = 4096;
	StepSite s_step_sites[kStepSites];

	__fi Step& StepAt(u32 start, u32 pc)
	{
		StepSite& site = s_step_sites[(pc >> 2) & (kStepSites - 1)];
		if (site.pc == pc && site.start == start && site.epoch == blocks::epoch) [[likely]]
			return *site.step;

		const StepRef ref = FindStep(start, pc);
		if (ref.index < 0) [[unlikely]]
			NoSuchStep(start, pc);
		site = {pc, start, blocks::epoch, &ref.block->steps[ref.index]};
		return *site.step;
	}
} // namespace

uint32_t ps2n::ee::cop2Instruction(uint32_t pc, uint32_t start, uint32_t eighths)
{
	Step& step = StepAt(start, pc);

	// As RunRecStep() does it; the instruction's own count is in `eighths`.
	if (step.info & blocks::kFlush)
	{
		cpuRegs.cycle += blockCycles(eighths);
		eighths &= 7;
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

bool ps2n::ee::memoryFault(uint32_t pc, uint32_t start)
{
	const StepRef ref = FindStep(start, pc);
	if (ref.index < 0) [[unlikely]]
		NoSuchStep(start, pc);
	Step& step = ref.block->steps[ref.index];

	if ((step.info & blocks::kFastmem) && EmuConfig.Cpu.Recompiler.EnableFastmem)
		FastmemFault(step);
	return (step.info & blocks::kEndsBlock) != 0;
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
	// PCSX2's recompiler does not tell its interpreter functions that an
	// instruction is in a delay slot.
	Delegate(pc, code, 0);
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
		InterpretRecBlock();

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
		const FunctionInfo* f = FunctionOf(*s_image, pc);
		if (f && f->start == pc)
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
