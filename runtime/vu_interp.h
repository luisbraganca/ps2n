// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's VU provider: VU0's and VU1's micro programs carried out by
// PS2Native's own code, in the place of PCSX2's VU recompiler (microVU),
// giving its results and taking its time
// (docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md).
//
// It follows microVU as far as microVU decides anything: the first pass over
// a block is ps2n/vu_mvu.h; the record of programs and blocks microVU keeps
// is kept the same way, filled in the same order, because what microVU
// compiled first decides the stalls a block is run with; what its second
// pass decides (the host registers its allocator gives each operand, where
// that shows in results) is decided the same way, once for each block, and
// written down as the block's plan (ps2n/vu_plan.h).
//
// A block is carried out by its plan: by the function a build has compiled
// from that plan ahead of time (ps2n-recomp vu, ps2n/vu_recomp.h), found by
// the plan's key, or, where a build has none, step by step by the
// interpreter here. Both are the same code for each step (ps2n/vu_ops.h),
// with the host instructions microVU generates, on the same operands, under
// the VU's floating-point control register. A run can write the plans it
// had to interpret to a capture file, for the next build (WriteCapture).
//
// It is a provider of the core's, as microVU is (BaseVUmicroCPU): the host
// puts it in the place of the core's (`ps2n-host -vu-ps2n`). What it does not
// carry out yet it reports and stops on, rather than guess.
#pragma once

#include <cstddef>
#include <cstdint>

class BaseVUmicroCPU;

namespace ps2n::vu {

struct ImageInfo;

/// The provider for VU `vu` (0 or 1).
BaseVUmicroCPU* Interpreter(int vu);

/// What the provider has done, for the host's report.
struct InterpreterStats
{
	uint64_t runs = 0; // calls of Execute that ran
	uint64_t blocks = 0; // blocks carried out
	uint64_t interpreted = 0; // of them, those carried out step by step, no build having compiled their plan
	uint64_t pairs = 0; // instruction pairs carried out
	uint64_t compiles = 0; // blocks analysed (microVU's compiles)
	uint64_t programs = 0; // programs created (microVU's "Cached Prog")
	uint64_t jumps = 0; // indirect jumps looked up
	uint64_t searches = 0; // starts and jumps whose program had to be looked for among the recorded ones
	uint64_t compares = 0; // recorded programs compared with micro memory for that
	uint64_t spared = 0; // searches not made: nothing had been written where the program found last was compiled from
	uint64_t pauses = 0; // runs that stopped because their cycles were used
	// Instructions of encodings PCSX2's VU recompiler knows nothing of, in the
	// blocks analysed: carried out as it carries them out, which is not at all.
	uint64_t unknown = 0;
};
const InterpreterStats& Stats(int vu);

/// Null while everything it met was carried out; otherwise the first thing
/// it met and does not carry out yet. The VU stops there.
const char* Unsupported(int vu);

/// Forgets what stopped VU `vu`, for the self-test's next case.
void ClearUnsupported(int vu);

/// Writes a line for every program created, as microVU writes them to the
/// core's log ("Cached Prog"), to `path`; null stops.
bool LogPrograms(int vu, const char* path);

/// The blocks compiled ahead of time that the host was linked with
/// (ps2n/vu_recomp.h): a block whose plan has the key of one runs compiled.
void SetImages(const ImageInfo* const* images, size_t count);

/// Whether compiled blocks are used at all (they are, unless switched off):
/// off, every block is interpreted, as the reference to compare with.
void SetCompiledBlocksEnabled(bool enabled);

/// How many compiled blocks the host has for VU `vu`.
size_t CompiledBlockCount(int vu);

/// The plans of the blocks VU `vu` has met, each counted once however many
/// blocks of however many programs had it.
struct PlanStats
{
	uint64_t plans = 0; // plans made
	uint64_t steps = 0; // their steps
	uint64_t compiled = 0; // of the plans, those a build had compiled
	uint64_t interpreted = 0; // those it had not, and that ran
	uint64_t compiledRuns = 0; // how often a block ran compiled
	uint64_t interpretedRuns = 0; // and interpreted
};
PlanStats Plans(int vu);

/// Writes the plans of both VUs that ran interpreted to `path`, for the
/// recompiler (ps2n-recomp vu).
bool WriteCapture(const char* path);

} // namespace ps2n::vu
