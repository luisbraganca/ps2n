// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's VU interpreter: VU0's and VU1's micro programs carried out by
// PS2Native's own code, in the place of PCSX2's VU recompiler (microVU),
// giving its results and taking its time
// (docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md).
//
// It follows microVU as far as microVU decides anything: the first pass over
// a block is ps2n/vu_mvu.h; the record of programs and blocks microVU keeps
// is kept the same way, filled in the same order, because what microVU
// compiled first decides the stalls a block is run with; the second pass,
// here, carries the instructions out with the same host instructions on the
// same operands, under the VU's floating-point control register, and does at
// each block's end what microVU's code does there.
//
// It is a provider of the core's, as microVU is (BaseVUmicroCPU): the host
// puts it in the place of the core's (`ps2n-host -vu-ps2n`). What it does not
// carry out yet it reports and stops on, rather than guess.
#pragma once

#include <cstdint>

class BaseVUmicroCPU;

namespace ps2n::vu {

/// The provider for VU `vu` (0 or 1).
BaseVUmicroCPU* Interpreter(int vu);

/// What the interpreter has done, for the host's report.
struct InterpreterStats
{
	uint64_t runs = 0; // calls of Execute that ran
	uint64_t blocks = 0; // blocks carried out
	uint64_t pairs = 0; // instruction pairs carried out
	uint64_t compiles = 0; // blocks analysed (microVU's compiles)
	uint64_t programs = 0; // programs created (microVU's "Cached Prog")
	uint64_t jumps = 0; // indirect jumps looked up
	uint64_t pauses = 0; // runs that stopped because their cycles were used
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

} // namespace ps2n::vu
