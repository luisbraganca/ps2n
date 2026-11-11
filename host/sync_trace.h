// SPDX-License-Identifier: GPL-3.0-or-later
//
// A record of a run that every configuration can write, stock PCSX2's
// recompiler included, so that a run on PS2Native's provider can be compared
// with a stock run and the first place they part can be found.
//
// PS2Native's own record of event tests (-trace, runtime/ee_provider.h) is
// written by its provider, which a stock run does not have. What every run
// has is the core, and the core calls the IOP's provider through a pointer
// (`psxCpu`) from inside the EE's event test, whenever the EE has got ahead
// of the IOP. That pointer is wrapped here, PCSX2 untouched: each such call
// is a *sync point*, at which the EE's registers are whole (PCSX2's
// recompiler has written them all back before an event test).
//
// Not every event test is a sync point: one that follows another within a
// few cycles finds the IOP still ahead and does not run it. A sync point
// comes every 3072 EE cycles at the latest, usually much sooner.
#pragma once

#include <cstdint>

namespace ps2n::host
{
	/// Starts recording sync points to `write_path` and comparing them with
	/// the record of an earlier run in `against_path`; either may be null.
	/// From sync point `detail_from` on, a written record is followed by the
	/// registers. False when a file cannot be opened.
	bool StartSyncTrace(const char* write_path, const char* against_path, uint64_t detail_from);

	/// Puts the wrapper in the place of the core's IOP provider. To be called
	/// on the CPU thread after the core has chosen its providers.
	void InstallSyncTrace();

	/// False when the core has put a provider of its own back.
	bool SyncTraceInstalled();

	/// Closes the files. A reference run that went on after this one stopped
	/// counts as a difference.
	void StopSyncTrace();

	/// Null while this run is the run it is compared with: the same place,
	/// the same cycle count and the same registers at every sync point so far.
	/// Otherwise what differed first. A run is not stopped by it.
	const char* GetSyncTimeMismatch(); // where the EE was, or when
	const char* GetSyncStateMismatch(); // what its registers held

	/// The registers kind by kind (0 general, 1 the FPU's, 2 COP0's, 3 VU0's):
	/// at how many sync points this run's differed from the reference run's,
	/// what the kind is called, and where the first of them was. One kind that
	/// differs for a known reason does not hide the others this way. Only
	/// counted while place and time are the same.
	uint64_t GetSyncRegisterDifferences(int kind, const char** name, const char** first);

	/// How many sync points there have been.
	uint64_t GetSyncPointCount();
} // namespace ps2n::host
