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
	/// is then empty): its disc's serial, and the CRC of the executable it has
	/// booted (0 before it has). CPU thread, just before the core loads the
	/// game's patches for that executable.
	void OnGameChanged(std::string_view title, std::string_view serial, uint32_t crc);

	/// The core has opened an input device ("SDL-0" is the first controller).
	/// CPU thread, possibly while the core holds its settings.
	void OnInputDeviceConnected(std::string_view identifier);

	/// The core has read its settings into its configuration, a game's own
	/// over the host's (while it holds them; again whenever they change, and
	/// before it applies its game database to them). What the host decides
	/// over a game's own settings is put into the configuration here.
	void OnSettingsLoaded();
} // namespace ps2n::host
