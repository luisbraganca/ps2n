// SPDX-License-Identifier: GPL-3.0-or-later
//
// A session in the window written down, so that it can be run again without
// a window and compared, as a scripted run is (step G13, ADR 0024).
//
// What the game reads of the controller in port 1 is written down frame by
// frame, as PCSX2's own input recording takes it (pcsx2/Recording/PadData.cpp):
// eighteen bytes, the buttons, the sticks and the pressure of each button that
// has one. The core takes the controller in at a frame boundary (the host's
// queue, a controller script, then the input devices, all in
// VMManager::Internal::PollInputOnCPUThread), and the game reads it during the
// frame that follows; so the state found at the start of frame n is the one
// taken in at frame n - 1, and a replay gives it back there, exactly, through
// PadData::OverrideActualController. Every 50 frames a fingerprint of the
// EE's and the IOP's registers is written down too, so that a replay says
// whether, and from where, it is no longer the session.
//
// The file, input.txt in the session's folder (game.ps1 -Play makes one for
// every session, with the memory cards and the console's NVRAM as they were
// when it began):
//
//   # comments
//   clock 2026-11-14T23:45:07     the console's clock when the machine started
//   41 pad FFFF7F7F7F7F00000000000000000000FF00   at frame 41, the 18 bytes
//   50 state 0123456789ABCDEF     at the start of frame 50, the registers' fingerprint
//   end 8358                      the number of frames the session ran
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ps2n::host::session
{
	/// The console's clock: year, month, day, hour, minute, second.
	using Clock = std::array<int, 6>;

	/// "2026-11-14T23:45:07" (a space for the T is taken too).
	std::optional<Clock> ParseClock(const std::string& text);
	std::string FormatClock(const Clock& clock);

	/// Starts writing a session down into `directory`/input.txt. False when
	/// the file cannot be made.
	bool StartRecording(const std::string& directory, const Clock& clock);

	/// What a replay needs before the machine starts: reads `path`. False, with
	/// a message, when it cannot be read or is not such a file.
	struct ReplayInfo
	{
		std::optional<Clock> clock;
		std::optional<uint32_t> frames;
	};
	bool LoadReplay(const std::string& path, ReplayInfo* info);

	/// At the start of every frame, on the CPU thread, before the frame's input
	/// is taken in: writes down what changed (recording), or compares the
	/// fingerprint (replay).
	void FrameStart(uint32_t frame);

	/// After the frame's other input: a replay gives the controller what the
	/// session had at this frame.
	void ApplyInput(uint32_t frame);

	/// When the run stops: closes the file, and gives the lines for the host's
	/// report (none when neither was asked for).
	std::vector<std::string> Finish(uint32_t frames);
} // namespace ps2n::host::session
