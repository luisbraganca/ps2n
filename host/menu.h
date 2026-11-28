// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's menu over a game played in its window (docs/adr/0060): Esc
// opens it, and the game waits while it is open. Resume, Controls (for each
// of two players, what each of the controller's buttons and sticks is on the
// keyboard and on a controller: docs/adr/0064), Graphics card (which one
// draws the game), Reboot and Quit (each asking first: what was not saved in
// the game is lost). The window's close button opens it on Quit's question.
// The only settings a player changes once a game is made: the rest is as the
// wizard made it.
//
// It is drawn into the game's picture with the ImGui of PCSX2's renderer, by
// PS2Native's own code, never with PCSX2's own menus (docs/adr/0012). The
// runner (main.cpp) holds the machine between two frames while it is open and
// calls Tick() for each of the menu's frames; what the menu shows is drawn on
// the GS thread, and what is chosen in it is carried out on the CPU thread.

#pragma once

#include "common/Pcsx2Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SettingsInterface;

namespace ps2n::host::menu
{
	/// The devices connected, as the core lists them (InputManager::
	/// EnumerateDevices): the identifier ("SDL-0") and the name of each.
	using Devices = std::vector<std::pair<std::string, std::string>>;

	/// The players' own controls, in the game's data (data_root is the core's
	/// data folder: <game>\data\PCSX2 in a game's folder), read into `si`, the
	/// host's own settings, over the defaults: player 1's, and player 2's
	/// whether or not port 2 has its controller yet. Before the machine
	/// starts, with a window only. Returns whether there were any: until there
	/// are, the defaults are the controls, and the controllers connected are
	/// given to the players as they connect (GiveController).
	bool LoadControls(SettingsInterface& si, const std::string& data_root);

	/// Whether the controls are the player's own: read from the game's data, or
	/// set in the menu. CPU thread.
	bool ControlsAreOwn();

	/// A controller just connected (`device`, "SDL-1"), given to a player where
	/// it plays for none yet (docs/adr/0064): to player 1, beside the keys,
	/// where the controls are the defaults and player 1 has no controller
	/// connected (as before); otherwise to player 2, where player 1 plays with
	/// another controller that is connected and player 2 with none. The mapping
	/// PCSX2 would make for it automatically, its motors for the vibration,
	/// added to what the player has. `connected`: the devices connected now.
	/// With the core's settings held. The player it was given to (0 or 1) and
	/// the inputs mapped; nothing where it was not.
	struct Given
	{
		u32 port;
		u32 inputs;
	};
	std::optional<Given> GiveController(SettingsInterface& si, std::string_view device, const Devices& connected);

	/// Whether the player of `port` (0 or 1) has something to play with: an
	/// input on the keyboard, or on a controller in `connected`. With the core's
	/// settings held.
	bool PlaysWithSomething(const SettingsInterface& si, u32 port, const Devices& connected);

	/// The controllers connected, or whose they are, changed outside the menu:
	/// shown again where it is open. CPU thread.
	void Refresh();

	/// A key from the window (host/window.h's codes). CPU thread. True when the
	/// menu took it: Esc, which opens the menu and is never one of the
	/// controller's buttons, and every key while the menu is open.
	bool OnKey(u32 code, bool pressed);

	/// The pointer, its buttons (0 left, 1 right, 2 middle) and its wheel, from
	/// the window, in the picture's pixels. CPU thread. Only the menu uses them.
	void OnPointerMoved(float x, float y);
	void OnPointerButton(u32 button, bool pressed);
	void OnWheel(float dx, float dy);

	/// The window lost the keyboard: what was held for the menu will come up
	/// elsewhere. CPU thread.
	void OnFocusLost();

	/// Whether Esc has asked for the menu since it was last open. CPU thread,
	/// between two frames.
	bool Requested();

	/// The window's close button or Alt+F4: the menu asks first, on its Quit
	/// page, where Back goes back to the game. CPU thread.
	void AskQuit();

	/// The close button pressed again while Quit's question shows: Quit chosen
	/// there, refused as it is while the game saves to its memory card. CPU
	/// thread.
	void ConfirmQuit();

	/// Whether the menu is open, and whether it shows Quit's question. Any
	/// thread.
	bool IsOpen();
	bool IsAskingQuit();

	/// The menu opened over the game, called `title`, the machine held by the
	/// caller. CPU thread.
	void Open(const std::string& title);

	/// One of the menu's frames: what was chosen carried out, the menu drawn
	/// over the game's last picture. CPU thread. False once it has closed:
	/// Resume, Esc, or Quit.
	bool Tick();

	/// Whether Quit, or Reboot, was chosen (and confirmed) when the menu closed.
	bool QuitChosen();
	bool RebootChosen();

	/// The menu closed, before the machine goes on. CPU thread.
	void Close();
} // namespace ps2n::host::menu
