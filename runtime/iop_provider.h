// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's IOP provider: the I/O processor's code carried out by
// PS2Native's own code, in the place of PCSX2's IOP recompiler, giving its
// results and taking its time
// (docs/adr/0023-the-iops-code-compiled-as-step-g12.md).
//
// PCSX2's IOP recompiler counts time by the blocks it compiles, and what a
// block is was fixed when it first compiled it: where the IOP got to that
// address, and which other blocks existed then. So the provider keeps the
// record that recompiler keeps, without the code: Form() is its
// iopRecRecompile() carried out on paper, Clear() its recClearIOP(), Reset()
// its recResetIOP(). A block's instructions are kept as memory held them
// when it was formed, as the recompiler's host code keeps them.
//
// A block is carried out by the function a build compiled for it ahead of
// time (ps2n-recomp iop, ps2n/iop_recomp.h), found by a fingerprint of its
// instructions, or, where a build has none, by the interpreter here. Both
// are made of the same code for each instruction (ps2n/iop_ops.h). A run can
// write the blocks it had to interpret to a capture file, for the next build
// (WriteCapture).
//
// It is a provider of the core's, as PCSX2's recompiler is (R3000Acpu): the
// host puts it in the place of the core's (`ps2n-host -iop ps2n`). What it
// does not carry out it reports and stops on, rather than guess.
#pragma once

#include <cstddef>
#include <cstdint>

struct R3000Acpu;

namespace ps2n::iop
{
	struct ImageInfo;

	/// The provider.
	R3000Acpu* Provider();

	/// What the provider has done, for the host's report.
	struct Stats
	{
		uint64_t stretches = 0; // calls of ExecuteBlock
		uint64_t formed = 0; // blocks formed
		uint64_t thrown = 0; // blocks thrown away because their memory was written
		uint64_t resets = 0; // times the whole record was thrown away
		uint64_t live = 0; // blocks the record holds now
		uint64_t shapes = 0; // different blocks formed, by their instructions
		uint64_t shapesCompiled = 0; // of them, those a build had compiled
		uint64_t shapesInterpreted = 0; // those it had not, and that ran
		uint64_t shapeWords = 0; // the instructions of the different blocks
		uint64_t runs = 0; // blocks begun
		uint64_t runsInterpreted = 0; // of them, by the interpreter
		uint64_t instructions = 0; // the instructions of the blocks begun
		uint64_t exceptions = 0; // SYSCALL and BREAK instructions carried out
		uint64_t imports = 0; // imports of modules for which a function of the core's was called
		uint64_t importsDone = 0; // of them, those the function did itself
		uint64_t waits = 0; // branches of wait loops taken
		uint64_t unknown = 0; // instructions of encodings PCSX2's IOP recompiler has nothing for, in the blocks formed
	};
	Stats GetStats();

	/// Null while everything it met was carried out; otherwise the first thing
	/// it met and does not carry out. The IOP stops there.
	const char* Unsupported();

	/// Forgets what stopped it, for the self-test's next case.
	void ClearUnsupported();

	/// The blocks compiled ahead of time that the host was linked with
	/// (ps2n/iop_recomp.h): a block whose instructions have the key of one
	/// runs compiled.
	void SetImages(const ImageInfo* const* images, size_t count);

	/// Whether compiled blocks are used at all (they are, unless switched
	/// off): off, every block is interpreted, as the reference to compare
	/// with.
	void SetCompiledBlocksEnabled(bool enabled);

	/// How many compiled blocks the host has.
	size_t CompiledBlockCount();

	/// Writes the blocks that ran interpreted to `path`, for the recompiler
	/// (ps2n-recomp iop). With `all`, every block formed.
	bool WriteCapture(const char* path, bool all = false);

	/// The addresses from `from` up to `to` at which the record holds a block
	/// now, in order: what PCSX2's recompiler's own table shows of its blocks,
	/// to hold the one to the other (the host's self-test). Returns how many
	/// there are; `out` takes the first `capacity` of them.
	size_t BlockStarts(uint32_t from, uint32_t to, uint32_t* out, size_t capacity);
} // namespace ps2n::iop
