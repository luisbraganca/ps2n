// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

// Where a run's time goes (ps2n-host -profile): about a thousand times a
// second the host looks where the CPU thread is executing, and after the run
// it says what was there: the game's code compiled ahead of time, PS2Native's
// runtime, PCSX2's core, the host code PCSX2's recompilers generated, the
// system. The CPU thread carries the EE, the IOP and the VUs; the GS has a
// thread of its own, which is not looked at.
namespace ps2n::host::profile
{
	/// Whether this build can do it on the platform it was built for.
	bool Available();

	/// Starts the thread that takes the samples. `thread` is the CPU thread's
	/// native handle. Nothing is sampled until Enable(true).
	bool Start(void* thread);

	/// Switches sampling on and off. The CPU thread calls this once the machine
	/// exists (on) and before it is taken down (off): the areas PCSX2's
	/// recompilers generate code into are noted when it is switched on.
	void Enable(bool on);

	/// Stops the sampling thread.
	void Stop();

	/// Writes the report: how many samples fell into each kind of code, and the
	/// functions that had most of them. `frames` and `seconds` are those looked
	/// at, from frame `first_frame` on, for the report's first lines.
	bool Write(const char* path, uint32_t first_frame, uint32_t frames, double seconds);
} // namespace ps2n::host::profile
