// Presents GS output: optional SDL2 window, optional PPM frame dumps.
//
// The guest runs on a worker thread. The window lives on the main thread
// (macOS requires that for windows and events), which runs hostLoop() while
// the guest runs; onVsync() only hands frames over.
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
// Host input for controller port `port` (0 or 1): the keyboard drives port
// 0, game controllers ports 0 and 1 in the order they were connected. Without
// SDL, port 0 has a controller with nothing pressed and port 1 is empty.
struct HostPad {
    bool connected = false;
    uint16_t buttons = 0;   // libpad bit order, pressed = 1
    uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    uint8_t l2 = 0, r2 = 0;
};
HostPad hostPad(int port);

} // namespace ps2::video
