// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds of PS2Native's menu over a game (host/menu.h; docs/adr/0060):
// when it opens, when it closes, when an item is pointed at and when one is
// chosen. In the manner of the PlayStation 2's own menus (glassy, crystalline
// tones in a large, soft room), but PS2Native's own, made by this code when
// first needed, not files and nothing of Sony's; played through SDL's audio,
// which PCSX2's core carries, beside the game's own sound, at its volume.
// Nothing is played where the game's sound is muted (a run without anyone to
// hear it is: window-check.ps1, game.ps1 -Mute).

#pragma once

#include "common/Pcsx2Types.h"

#include <string>

namespace ps2n::host::menu::sound
{
	enum class Sound : u8
	{
		Open, // the menu opens: a low swell and a breath of air rising, glassy notes blooming over a soft fifth
		Close, // it closes: the same falling away
		Point, // an item pointed at, or reached with the keys or a controller: a soft crystalline tick
		Choose, // an item chosen: two bright glassy notes up, a sparkle over them
		Back, // back a page, or something cleared or given up: two duller notes down
		Count
	};

	/// Plays `sound`, over any of the menu's sounds still playing. Any thread.
	void Play(Sound sound);

	/// How many times each sound was asked for, played or not (the run's
	/// report: a check without sound sees that the menu would have sounded).
	std::string Counts();

	/// Writes the sounds as WAV files into `folder` (ps2n-host -menu-sounds),
	/// to be listened to. Returns whether all were written.
	bool WriteFiles(const std::string& folder);

	/// The audio device let go, before the process ends.
	void Shutdown();
} // namespace ps2n::host::menu::sound
