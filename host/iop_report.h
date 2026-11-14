// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the I/O processor runs: a measurement for step G12 (docs/game-log.md).
//
// The core reaches the IOP's provider through one pointer (`psxCpu`): every
// stretch of IOP time goes through its `ExecuteBlock`, and every write into
// the IOP's memory that is not an IOP store into a device is announced to it
// (`Clear`: a store of the IOP's own, a transfer from the EE, the disc or the
// sound hardware). That pointer is wrapped here, PCSX2 untouched, as
// sync_trace.h wraps it: the provider does what it did, and is only watched.
//
// PCSX2's IOP recompiler keeps, for every address a block of its can begin
// at, where that block's host code is (`psxRecLUT`), and its core keeps the
// address of the list of modules the IOP's kernel has loaded. From the two
// the report says, frame by frame and over the run: how much IOP time passed,
// in how many stretches, and how long the host took for it; how often the
// IOP's memory was written, and how often that threw blocks away; which
// modules were loaded where, and when; how many blocks the recompiler formed
// in each, with how many different contents at the same place; and how much
// of its room for host code the recompiler used. It holds addresses, counts
// and the names of modules, nothing of their code.
#pragma once

#include <cstdint>

namespace ps2n::host::iopreport
{
	/// Opens the report's files: `path`, and beside it the same name with
	/// `-frames.csv` in place of its extension. False when one of them cannot
	/// be opened.
	bool Start(const char* path);

	/// Puts the watch in front of the core's IOP provider. On the CPU thread,
	/// after the core has chosen its providers and before the record of sync
	/// points is put in front of them (sync_trace.h), which then stands in
	/// front of the watch.
	void Install();

	/// False when the core has put a provider of its own back; true when
	/// nothing is watched.
	bool Installed();

	/// What the watch stands in front of, when `provider` is the watch;
	/// otherwise `provider` itself.
	const void* Under(const void* provider);

	/// How many blocks PCSX2's IOP recompiler holds now, by its own table:
	/// none, in a run in which it carried out nothing. Whether or not anything
	/// is watched.
	uint32_t RecompilerBlocks();

	/// The end of frame `frame`: what was counted during it goes to the file of
	/// frames, and the blocks and the modules are looked over.
	void Frame(uint32_t frame);

	/// Takes the watch away and writes the report, `frames` frames having been
	/// shown. False when it cannot be written.
	bool Stop(uint32_t frames);
} // namespace ps2n::host::iopreport
