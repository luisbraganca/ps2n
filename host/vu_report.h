// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the vector units run: a measurement for step G11 (docs/game-log.md).
//
// The core reaches its two VU providers through two pointers (`CpuVU0`,
// `CpuVU1`). Every write into a VU's micro memory is announced to the
// provider before it is made (`Clear`: a VIF `MPG` transfer, a store of the
// EE into that memory, a scratchpad transfer), every micro program is started
// through it (`SetStartPC`), and all of its running goes through `Execute`.
// Those pointers are wrapped here, PCSX2 untouched, as sync_trace.h wraps the
// IOP's: the providers do what they did, and are only watched.
//
// The report says, for each VU: how often its micro memory was written, at
// which places (address and length), and with how many different contents at
// each place; at which addresses programs were started, with how many
// different contents of the whole micro memory; and how long the host spent
// running each (the processor's time-stamp counter around `Execute`; one VU's
// provider running inside the other's is counted for the inner one only).
// Beside it go the same counts frame by frame, and each distinct content
// written, for later study: they are the game's code, and stay in the build
// tree with the run's other files. Every hundredth frame a line in the core's
// log says which frame it is, so that the lines microVU writes when it caches
// a program can be placed in time.
#pragma once

#include <cstdint>

namespace ps2n::host::vureport
{
	/// Opens the report's files: `path`, and beside it the same name with
	/// `-frames.csv` and `-uploads.bin` in place of its extension. False when
	/// one of them cannot be opened.
	bool Start(const char* path);

	/// Puts the watch in front of the core's two VU providers. On the CPU
	/// thread, after the core has chosen its providers.
	void Install();

	/// False when the core has put a provider of its own back; true when
	/// nothing is watched.
	bool Installed();

	/// The end of frame `frame`: what was counted during it goes to the file of
	/// frames.
	void Frame(uint32_t frame);

	/// Takes the watch away and writes the report, `frames` frames having been
	/// shown. False when it cannot be written.
	bool Stop(uint32_t frames);
} // namespace ps2n::host::vureport
