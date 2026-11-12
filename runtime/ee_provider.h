// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ps2n/r5900.h"
#include "ps2n/recomp.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct R5900cpu;

// PS2Native's EE provider: the object the PCSX2 core calls to run EE code,
// in the place of its own recompiler or interpreter (pcsx2/R5900.h).
//
// It dispatches to code compiled ahead of time where there is some for the
// program in memory, and interprets everything else: the BIOS, and whatever
// the recompiler did not see.
namespace ps2n::ee
{
	/// The provider. Install it by assigning its address to PCSX2's `Cpu` after
	/// VMManager::Initialize(), then call its Reset.
	R5900cpu* Provider();

	/// The executables compiled ahead of time that this program contains. The
	/// one whose CRC is that of the executable the machine boots is used. Call
	/// before execution starts; the array must stay valid.
	void SetImages(const ImageInfo* const* images, size_t count);

	/// Whether compiled code is used at all. Off, everything is interpreted,
	/// which a run must be indistinguishable from. On by default.
	void SetStaticCodeEnabled(bool enabled);

	/// How much host stack compiled code may use below the point where the
	/// core calls Execute(). A guest call is a host call, so the thread that
	/// runs the core needs a large stack, and the provider has to know how
	/// much of it there is. Past the budget compiled frames are abandoned and
	/// execution continues from the dispatcher, which is always correct. The
	/// default suits a 1 MB stack.
	void SetHostStackBudget(size_t bytes);

	/// Writes one line per event test to `path`: its number, the PC, the EE
	/// cycle count and the fingerprint of the run so far (Stats::trace_hash).
	/// Two runs of the same program must write the same lines whether its code
	/// is compiled or interpreted. Returns false if the file cannot be made.
	bool StartTrace(const char* path);

	/// From event test number `event_test` on, each line StartTrace's file gets
	/// is followed by the registers, on lines that begin with a space.
	void SetTraceDetailFrom(uint64_t event_test);

	/// Compares this run's event tests, as they happen, with the file an
	/// earlier run wrote. Returns false if the file cannot be read.
	bool CompareTraceWith(const char* path);

	/// Where this run first differed from the one it is compared with, in
	/// words, or null if it has not. The whole comparison is only finished
	/// once StopTrace() has been called.
	const char* GetTraceMismatch();

	/// Closes the files of StartTrace and CompareTraceWith.
	void StopTrace();

	/// How the instructions executed so far were carried out. Nothing is
	/// interpreted or delegated without being counted here.
	struct Stats
	{
		/// Instructions the fallback interpreter executed.
		uint64_t interpreted = 0;
		/// Of those, inside the compiled executable's code range: what the
		/// recompiler did not cover.
		uint64_t interpreted_in_image = 0;
		/// Of the rest, by where the code is: in the kernel's segments
		/// (addresses from 80000000, the BIOS in RAM and in ROM); below
		/// 00100000, where the BIOS puts its program loader; and in the rest
		/// of user memory, which is code the executable does not hold: a module
		/// the program loaded, another executable, code it wrote.
		uint64_t interpreted_kernel = 0;
		uint64_t interpreted_loader = 0;
		uint64_t interpreted_elsewhere = 0;
		/// Of the interpreted ones, executed by calling PCSX2's interpreter
		/// function rather than PS2Native's own semantics.
		uint64_t delegated = 0;
		/// The delegated ones by instruction. Index 0 (Op::Invalid) counts the
		/// encodings PS2Native's decoder does not know.
		uint64_t delegated_by_op[static_cast<size_t>(r5900::Op::Count)] = {};

		/// EE cycles counted by the fallback interpreter; the rest of
		/// cpuRegs.cycle since the start was counted by compiled code.
		uint64_t interpreted_cycles = 0;
		/// Of those, inside the compiled executable's code range.
		uint64_t interpreted_cycles_in_image = 0;

		/// Times the dispatcher entered compiled code.
		uint64_t static_entries = 0;
		/// Event tests run (PCSX2's _cpuEventTest_Shared), from either.
		uint64_t event_tests = 0;
		/// Blocks that began at a label of compiled code and whose words were
		/// compared with what was compiled: all of them, and those that were
		/// neither a function's code nor one of its variants'.
		uint64_t blocks_checked = 0;
		uint64_t blocks_unknown = 0;
		/// Times compiled code asked about a block and was told to run it; times
		/// it asked and the interpreter carried the block out in its place (one
		/// compiled code cannot run as PCSX2's recompiler would, or code nothing
		/// was compiled from); times it asked about a timeout loop, which nothing
		/// runs: its time passes.
		uint64_t blocks_compiled = 0;
		uint64_t blocks_interpreted_at_labels = 0;
		uint64_t timeout_loop_entries = 0;
		/// Times a function was entered while one of its variants was the code
		/// in RAM (or the reverse), and that ran in its place.
		uint64_t redirected = 0;
		/// Times compiled frames were abandoned because another executable started.
		uint64_t forced_unwinds = 0;
		/// Times they were abandoned because the host stack had grown too deep.
		uint64_t stack_unwinds = 0;
		/// Times compiled code handed the rest of a block to the interpreter.
		uint64_t left = 0;
		/// EE cycles that passed in timeout loops without the loops being run
		/// (ps2n/waitloop.h), in compiled and in interpreted code.
		uint64_t timeout_loop_cycles = 0;
		/// FlushCache calls that were not made, as PCSX2's recompiler does not
		/// make them; and blocks carried out at another address than they were
		/// formed at (a mirror of the same memory), which then run as if at the
		/// first.
		uint64_t syscalls_skipped = 0;
		uint64_t other_mirror = 0;
		/// Times an FPU instruction that follows the console (ps2n/ops_fpu.h) was
		/// given operands for which stock PCSX2's recompiler may leave something
		/// else in the register: ABS.S and NEG.S of bits it clamps; MAX.S and
		/// MIN.S of such bits or of a zero of each sign. From compiled and from
		/// interpreted code.
		uint64_t console_followed_abs_neg = 0;
		uint64_t console_followed_max_min = 0;
		/// SYSCALL instructions executed, by the number in $v1 (its low byte;
		/// the kernel's interrupt-safe variants have the negated number and are
		/// counted with the plain ones), from compiled and from interpreted code.
		uint64_t syscalls[256] = {};

		/// Fingerprint of the machine's state at every event test so far. Only
		/// kept while a run is recorded or compared (StartTrace,
		/// CompareTraceWith).
		uint64_t trace_hash = 14695981039346656037ull;

		/// The compiled executable in use, or null.
		const ImageInfo* image = nullptr;
	};

	const Stats& GetStats();

	/// Addresses inside the compiled executable's code range at which the
	/// interpreter had to start a block: entry points the recompiler missed,
	/// or code it did not compile. Sorted.
	std::vector<uint32_t> GetMissingEntries();

	/// Code found in memory in the place of a compiled function, when it was
	/// neither the function nor one of its variants: the words over the
	/// function's own range, as they were when it was about to be entered.
	struct CapturedCode
	{
		uint32_t start;
		std::vector<uint32_t> words;
	};
	const std::vector<CapturedCode>& GetCapturedCode();

	/// Where compiled code handed over to the interpreter, and how often: the
	/// instruction from which the rest of a block was left to it
	/// (Stats::left), or the label at which a whole block was
	/// (Stats::blocks_interpreted_at_labels). Most frequent first.
	struct Handover
	{
		uint32_t pc;
		uint64_t count;
		bool whole_block;
	};
	std::vector<Handover> GetHandovers();

	/// Carries out one instruction with PS2Native's own semantics, on the
	/// core's registers and memory. Returns false, having done nothing, when
	/// there are none for it: control flow, and what is still delegated. A
	/// VU0 instruction is carried out as in a block of that one instruction:
	/// its flags reach both of VU0's flag registers. For tests.
	bool ExecuteOwn(uint32_t code, uint32_t pc);
} // namespace ps2n::ee
