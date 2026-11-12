// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string_view>

// What host_interface.cpp (the callbacks the PCSX2 core requires of a
// frontend) needs from main.cpp (the runner).
namespace ps2n::host
{
	/// Called by the core once per guest frame, on the CPU thread.
	void OnGuestFrame();

	/// Runs `function` on the CPU thread: at once when called from it, otherwise
	/// at the next guest frame. With `block`, returns after it has run.
	void RunOnCPUThread(std::function<void()> function, bool block);

	/// The core has found out which game is running, or that none is (`title`
	/// is then empty). CPU thread.
	void OnGameChanged(std::string_view title);

	/// The core has opened an input device ("SDL-0" is the first controller).
	/// CPU thread, possibly while the core holds its settings.
	void OnInputDeviceConnected(std::string_view identifier);
} // namespace ps2n::host
