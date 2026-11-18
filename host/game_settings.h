// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a game is played with by default, over PCSX2's own settings, and its
// patch file: one definition for the runtime (ps2n-host) and for the wizard,
// whose settings pages show these defaults and write a game's own settings
// over them (docs/adr/0032), so that a default accepted in the wizard is the
// one the game is played with.

#pragma once

#include "common/Pcsx2Types.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SettingsInterface;

namespace ps2n::game
{
	// PCSX2's defaults (VMManager::SetDefaultSettings), and over them the
	// window's (ADR 0025): PCSX2's Vulkan renderer, three times the console's
	// resolution, the game's widescreen patch with its films kept at 4:3; none
	// of what the core draws over the picture (ADR 0012); none of PCSX2's
	// hotkeys, which open its own interface and save states.
	void SetDefaults(SettingsInterface& si);

	// What PS2Native carries out in PCSX2's place (the EE, its FPU, the vector
	// units, the IOP, and the time they count) and what the runtime decides
	// itself (the patches it applies, the clock, save states): fixed as PCSX2
	// has them, which the game's code was checked against (docs/adr/0032,
	// decision 2). The wizard keeps them out of a game's own settings file; the
	// runtime refuses to play with a file that sets one (step P4.8). Whole
	// sections, then single keys. The EE's cycle rate is not among them: it is
	// checked at every rate (G17).
	inline constexpr const char* kFixedSections[] = {"EmuCore/CPU", "EmuCore/CPU/Recompiler", "EmuCore/Gamefixes"};
	inline constexpr std::pair<const char*, const char*> kFixedKeys[] = {{"EmuCore/Speedhacks", "EECycleSkip"},
		{"EmuCore/Speedhacks", "IntcStat"}, {"EmuCore/Speedhacks", "WaitLoop"}, {"EmuCore/Speedhacks", "vuFlagHack"},
		{"EmuCore/Speedhacks", "vuThread"}, {"EmuCore/Speedhacks", "vu1Instant"}, {"EmuCore", "EnablePatches"},
		{"EmuCore", "EnablePINE"}, {"EmuCore", "EnableGameFixes"}, {"EmuCore", "SaveStateOnShutdown"},
		{"EmuCore", "BackupSavestate"}, {"EmuCore", "ManuallySetRealTimeClock"}, {"EmuCore", "UseSystemLocaleFormat"},
		{"EmuCore", "RtcYear"}, {"EmuCore", "RtcMonth"}, {"EmuCore", "RtcDay"}, {"EmuCore", "RtcHour"},
		{"EmuCore", "RtcMinute"}, {"EmuCore", "RtcSecond"}};

	// The fixed settings a game's own settings file sets, "section/key" each.
	std::vector<std::string> FixedSettingsIn(const SettingsInterface& si);

	// A game's patch file, as PCSX2's archive of them has it (ADR 0025).
	struct PatchFile
	{
		std::string name; // <serial>_<crc>.pnach, or <crc>.pnach
		std::string text;
		std::vector<std::string> groups; // the groups' names, in the file's order
		std::map<std::string, std::string> group_lines; // each group's lines, its note among them
	};

	// The game's patch file in PCSX2's archive (patches.zip). Without one,
	// nothing, and *error says why when it is not that the archive has none.
	std::optional<PatchFile> ReadPatchFile(const std::string& archive, std::string_view serial, u32 crc, std::string* error);

	// A patch file read from elsewhere (a game's folder), split into its groups.
	PatchFile ParsePatchFile(std::string name, std::string text);

	// PCSX2's own name for a game's widescreen group, which its setting
	// EnableWideScreenPatches applies.
	inline constexpr const char* kWidescreenGroup = "Widescreen 16:9";

	// The frame-rate group: the one group whose name says FPS. Where several
	// do, they are choices, not one patch ("60 FPS" beside "50 FPS", "Onscreen
	// FPS Display"), and none is; *fps_groups gets them all.
	std::optional<std::string> FrameRateGroup(const PatchFile& file, std::vector<std::string>* fps_groups = nullptr);

	// The EE overclock a group of a patch file asks for in its note, as
	// PCSX2's archive words it ("Might need EE Overclock (130%).", "Might need
	// EE overclock at 180%.", "set \"EE Cyclerate\" to 2"; ADR 0025): PCSX2's
	// EE cycle rate setting, 1 to 3 for 130%, 180% and 300%, or 0 where the
	// note asks for none. A note that names no figure asks for the mildest,
	// 130%; one that names two ("130-180%"), for the first.
	int OverclockAsked(std::string_view lines);
} // namespace ps2n::game
