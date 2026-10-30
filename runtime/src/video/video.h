// Presents GS output: optional SDL2 window, optional PPM frame dumps.
//
// The guest runs on a worker thread. The window lives on the main thread
// (macOS requires that for windows and events), which runs hostLoop() while
// the guest runs: it shows the frames present() hands over, reads the host's
// input (input/), draws the overlay (ui/) and pauses the guest at its next
// vsync while the menu is open or the Toggle Pause hotkey says so.
#pragma once

#include <atomic>
#include <cstdint>

namespace ps2::gs {
struct Frame;
}

namespace ps2::video {

// What becomes of one vsync's picture.
struct Present {
    uint64_t index = 0;   // the vsync's number, which names its dump
    bool dump = false;
    bool show = false;
};

void init();
Present onVsync();   // guest thread, every vsync: pauses there while asked to
// Any thread (the GS's, when it has one): dumps and shows a vsync's picture.
void present(gs::Frame&& frame, const Present& how);
// Main thread: shows frames and handles window events until `guestDone`.
// Returns at once when there is no window.
void hostLoop(const std::atomic<bool>& guestDone);
void shutdown();    // main thread, after the guest finished

} // namespace ps2::video
