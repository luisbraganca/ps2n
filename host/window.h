// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/WindowInfo.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The window a game is played in (ps2n-host -window). There is one. It is made
// on the main thread, which also serves its messages; the core's renderer
// draws into it from the GS thread.
namespace ps2n::host::window
{
	// Keys, as the core's input bindings name them ("Keyboard/Return"). A code
	// is the same on every platform: the window code of each platform turns its
	// own key codes into these. Letters and digits are their ASCII codes
	// ('A' to 'Z', '0' to '9').
	enum Key : uint32_t
	{
		KeyReturn = 0x100,
		KeyBackspace,
		KeyEscape,
		KeySpace,
		KeyTab,
		KeyUp,
		KeyDown,
		KeyLeft,
		KeyRight,
		KeyShift,
		KeyControl,
		KeyAlt,
		KeyInsert,
		KeyDelete,
		KeyHome,
		KeyEnd,
		KeyPageUp,
		KeyPageDown,
		KeyF1,
		KeyF2,
		KeyF3,
		KeyF4,
		KeyF5,
		KeyF6,
		KeyF7,
		KeyF8,
		KeyF9,
		KeyF10,
		KeyF11,
		KeyF12,
	};

	/// The code of the key a binding names, and the name of a code.
	std::optional<uint32_t> KeyFromName(std::string_view name);
	const char* NameOfKey(uint32_t code);

	// What the window tells the runner. Every one is called on the main thread.
	struct Listener
	{
		void (*key)(uint32_t code, bool pressed); // a key went down or came up; repeats are left out
		void (*resized)(uint32_t width, uint32_t height, float scale); // the area the picture goes into, in pixels
		void (*focus_lost)(); // the keys that were down will not be seen coming up
		void (*close_requested)();
		void (*shot_requested)(uint32_t number); // a check asks for the picture on display (window-check.ps1)
	};

	/// The message by which another program asks for the picture on display,
	/// numbered by wParam (Windows: PostMessage to the window of class
	/// PS2NativeGame). The runner writes it beside the session's last frame,
	/// as shot-<number>.png.
	constexpr uint32_t kShotMessage = 0x8000 + 0x50; // WM_APP + 0x50

	/// Whether this build has window code for the platform it was built for.
	bool Available();

	/// Makes the window and shows it. `width` and `height` are the size of the
	/// area the picture goes into, at a display scaling of 100%. A window that
	/// would not fit on its screen covers the screen instead; the window it
	/// goes back to is the largest that fits, at the same proportions. Main
	/// thread.
	bool Create(const std::string& title, uint32_t width, uint32_t height, const Listener& listener);

	/// Main thread, after the machine has stopped.
	void Destroy();

	/// What the core's renderers need to draw into the window; without a window,
	/// a description that says there is none (surfaceless). Any thread.
	WindowInfo GetInfo();

	/// Any thread.
	void SetTitle(const std::string& title);

	/// Whether the window covers its screen, and making it do so. Any thread.
	/// Alt+Enter and F11 switch as well.
	bool IsFullscreen();
	void SetFullscreen(bool enabled);
} // namespace ps2n::host::window
