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
//   patches widescreen,fps        the game's patches it was played with (-patches;
//                                 none for none), with the EE overclock their note
//                                 asks for, which a replay's patches ask for again
//   ee-cycle-rate 0               PCSX2's EE cycle rate, only where the session was
//                                 given one (-ee-cycle-rate)
//   bios-roms rom1,rom2           which of the images the core loads from beside the
//                                 BIOS image it had (none for none): what the machine
//                                 is changes with them (a replay without them parts
//                                 from the session at once), so a replay must have the
//                                 same; the test driver links those (tests/guest_test.cmake)
//   packaged                      played from a game's folder the wizard made
//                                 (ps2n-host -packaged): the game's own settings file
//                                 and patch file are copied beside this file, and a
//                                 replay runs packaged with them in its data folder
//   41 pad FFFF7F7F7F7F00000000000000000000FF00   at frame 41, the 18 bytes
//   50 state 0123456789ABCDEF     at the start of frame 50, the registers' fingerprint
//   frames-a-second 50            what the console showed most of its frames at, in the
//                                 mode the game had it in: 50 (PAL) or 60. With it a
//                                 game's folder tells how long the session was in the
//                                 game's own time (ps2n-remake, docs/adr/0047)
//   end 8358                      the number of frames the session ran
//
// A session written down before the patches were (2026-11-15) has no
// patches line: it is run again with none unless it is given them. One
// written down before the BIOS's images were (2026-11-17) has no bios-roms
// line: it is run again with whatever is beside the BIOS image given. One
// written down before the frames a second were (2026-11-22) has no such
// line: a game's folder takes it for one at 60.
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

	/// Starts writing a session down into `directory`/input.txt: its clock, its
	/// patches ("widescreen,fps", "none"), PCSX2's EE cycle rate where it was
	/// given one, the images beside the BIOS image ("rom1,rom2", "none";
	/// BiosRoms()), and whether it is played from a game's folder the wizard
	/// made. False when the file cannot be made.
	bool StartRecording(const std::string& directory, const Clock& clock, const std::string& patches,
		std::optional<int> ee_cycle_rate, const std::string& bios_roms, bool packaged);

	/// Which of the images the core loads from beside a BIOS image are there, as
	/// it looks for them (LoadExtraRom in pcsx2/ps2/BiosTools.cpp: <image>.rom1,
	/// then the image's name with .rom1 for its extension; the same for rom2):
	/// "rom1,rom2", "rom1", "rom2" or "none".
	std::string BiosRoms(const std::string& bios_path);

	/// What a replay needs before the machine starts: reads `path`. False, with
	/// a message, when it cannot be read or is not such a file.
	struct ReplayInfo
	{
		std::optional<Clock> clock;
		std::optional<uint32_t> frames;
		std::optional<std::string> patches; // as -patches takes them
		std::optional<int> ee_cycle_rate;
		std::optional<std::string> bios_roms; // as BiosRoms() gives them
		bool packaged = false; // played from a game's folder the wizard made
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
