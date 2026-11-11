// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

// Checks of PS2Native's instruction tables and semantics against the PCSX2
// core they have to agree with. Run by `ps2n-host -selftest`, on the CPU
// thread, in an initialized machine that has not started executing.
namespace ps2n::selftest
{
	struct Options
	{
		/// Random cases per instruction against PCSX2's interpreter functions.
		uint32_t interp_trials = 4000;
		/// Random cases per instruction against PCSX2's EE recompiler. Each one
		/// compiles a block, so these are slower.
		uint32_t jit_trials = 400;
		/// Random instruction words for the decoder and cycle-table checks.
		uint32_t random_words = 2000000;
		uint64_t seed = 0x9E3779B97F4A7C15ull;
	};

	/// Returns the number of failed checks, having written a report to stderr.
	int Run(const Options& options);
} // namespace ps2n::selftest
