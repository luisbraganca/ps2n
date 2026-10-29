// Presents GS output: optional SDL2 window, optional PPM frame dumps.
//
// The guest runs on a worker thread. The window lives on the main thread
// (macOS requires that for windows and events), which runs hostLoop() while
// the guest runs: it shows the frames onVsync() hands over, reads the host's
// input (input/), draws the overlay (ui/) and pauses the guest at its next
// vsync while the menu is open or the Toggle Pause hotkey says so.
#pragma once

#include <atomic>
#include <cstdint>

namespace ps2::video {

void init();
void onVsync();     // guest thread
// Main thread: shows frames and handles window events until `guestDone`.
// Returns at once when there is no window.
void hostLoop(const std::atomic<bool>& guestDone);
void shutdown();    // main thread, after the guest finished

} // namespace ps2::video
