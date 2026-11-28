// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/WindowInfo.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The window a game is played in (ps2n-host -window). There is one. It is made
// on the main thread, which also serves its messages; the core's renderer
// draws into it from the GS thread. A Win32 window on Windows, an X11 window
// on Linux (under Wayland, through XWayland), as PCSX2's own Linux build
// draws into.
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
		// The window is to close: `ask`, by its close button or Alt+F4 (the game
		// then asks first, docs/adr/0060); not, from RequestClose or a check.
		void (*close_requested)(bool ask);
		void (*shot_requested)(uint32_t number); // a check asks for the picture on display (window-check.ps1)
		// The pointer over the picture, in its pixels; its buttons (0 left, 1
		// right, 2 middle); its wheel, in notches. For PS2Native's menu over the
		// game (host/menu.h), which alone uses them.
		void (*pointer_moved)(float x, float y);
		void (*pointer_button)(uint32_t button, bool pressed);
		void (*wheel)(float dx, float dy);
		// Whether something over the picture is used with the pointer now (the
		// menu over the game): the pointer is not hidden while it is
		// (host/pointer_hiding.h). Asked on the main thread too.
		bool (*pointer_used)();
		// The pointer has hidden over the picture (host/pointer_hiding.h): until
		// it moves, it points at nothing.
		void (*pointer_hidden)();
	};

	/// The message by which another program asks for the picture on display,
	/// numbered by wParam (Windows: PostMessage to the window of class
	/// PS2NativeGame). The runner writes it beside the session's last frame,
	/// as shot-<number>.png. On Linux the same request is a client message
	/// of type kShotAtom sent to the window (of class PS2Native), its number
	/// in the first long of its data.
	constexpr uint32_t kShotMessage = 0x8000 + 0x50; // WM_APP + 0x50
	constexpr const char* kShotAtom = "PS2NATIVE_SHOT";

	/// The message by which another program asks whether the pointer is hidden
	/// over the picture now (Windows; window-check.ps1's `pointer` step): the
	/// window answers 1 when it is, 0 when it is not.
	constexpr uint32_t kPointerMessage = 0x8000 + 0x51; // WM_APP + 0x51

	/// WM_CLOSE's wParam that closes the window without asking (Windows):
	/// RequestClose's, and a check's (window-check.ps1). The close button's is
	/// 0, and the game asks first.
	constexpr uintptr_t kCloseWithoutAsking = 0x50534E;

	/// Whether this build has window code for the platform it was built for.
	bool Available();

	/// Makes the window and shows it. `width` and `height` are the size of the
	/// area the picture goes into, at a display scaling of 100%. A window that
	/// would not fit on its screen covers the screen instead; the window it
	/// goes back to is the largest that fits, at the same proportions. Main
	/// thread.
	bool Create(const std::string& title, uint32_t width, uint32_t height, const Listener& listener);

	/// A window that is never shown, for a run without one whose renderer can
	/// draw only into a window: PCSX2's OpenGL on Linux makes no context
	/// without one. False where there is no display to make it on. Main
	/// thread.
	bool CreateHidden();

	/// Serves the window's events on the main thread until `done` is set
	/// (Linux; on Windows the runner serves the thread's messages itself).
	void ServeUntil(const std::atomic<bool>& done);

	/// Main thread, after the machine has stopped.
	void Destroy();

	/// What the core's renderers need to draw into the window; without a window,
	/// a description that says there is none (surfaceless). Any thread.
	WindowInfo GetInfo();

	/// Any thread.
	void SetTitle(const std::string& title);

	/// Whether the window covers its screen, and making it do so. Any thread.
	/// Alt+Enter and F11 switch as well. Covering it, the pointer hides over
	/// the picture while it is still (host/pointer_hiding.h, docs/adr/0068).
	bool IsFullscreen();
	void SetFullscreen(bool enabled);

	/// Closes the window as its close button does, without asking: the runner
	/// is told (Listener::close_requested), and the window goes when the machine
	/// has stopped. Any thread. The menu's Quit.
	void RequestClose();

	/// A notice over the picture, PS2Native's own (docs/adr/0035), never the
	/// core's on-screen display: `title` on a line above `text`, in the
	/// window's top left corner, for `seconds`, letting clicks through. One
	/// shown while another is replaces it. Any thread.
	void ShowNotice(const std::string& title, const std::string& text, uint32_t seconds);
} // namespace ps2n::host::window
