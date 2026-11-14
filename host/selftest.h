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
		/// Random micro programs per VU and setting, run on PCSX2's VU
		/// recompiler and on PS2Native's VU interpreter (selftest_vu.cpp).
		uint32_t vu_trials = 2000;
		/// Of those, how many of each VU and setting are run, from the first: the
		/// rest are made (so that the ones run are the same programs whatever
		/// this says) and left alone. For the programs a build has compiled
		/// (scripts/windows/vu-selftest.ps1).
		uint32_t vu_run = ~0u;
		/// Only the micro programs, none of the other checks.
		bool vu_only = false;
		uint64_t seed = 0x9E3779B97F4A7C15ull;
	};

	/// Returns the number of failed checks, having written a report to stderr.
	int Run(const Options& options);

	/// PS2Native's VU interpreter against PCSX2's VU recompiler on random
	/// micro programs, for each VU. Returns the number that differ.
	/// Of each VU and setting, the first `run` of the `trials` programs are run.
	int CheckVuPrograms(uint32_t trials, uint32_t run, uint64_t seed);
} // namespace ps2n::selftest
