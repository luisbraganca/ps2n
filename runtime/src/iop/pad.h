// Controllers: the PADMAN service, as a DualShock 2 per port.
//
// The game's libpad talks to PADMAN over RPC (open a port, switch to analog
// or pressure mode, set up vibration) and reads the controller state from a
// double-buffered area in EE memory, which PADMAN refreshes every frame. This
// does the same from host input: the keyboard or a game controller (SDL
// builds), or a script (--pad-script) for reproducible runs.
#pragma once

#include <cstdint>
#include <string>

namespace ps2::iop::pad {

// Buttons in libpad's bit order (PAD_SELECT = 0x0001 ... PAD_SQUARE = 0x8000), pressed = 1.
enum Button : uint16_t {
    SELECT = 0x0001, L3 = 0x0002, R3 = 0x0004, START = 0x0008, UP = 0x0010, RIGHT = 0x0020, DOWN = 0x0040, LEFT = 0x0080,
    L2 = 0x0100, R2 = 0x0200, L1 = 0x0400, R1 = 0x0800, TRIANGLE = 0x1000, CIRCLE = 0x2000, CROSS = 0x4000, SQUARE = 0x8000,
};

struct Input {
    bool connected = true;
    uint16_t buttons = 0;
    uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;   // 0x80 = centred
    uint8_t l2 = 0, r2 = 0;                                // analog triggers, when the source has them
};

void init();     // runtime start-up: reads --pad-script
void reset();    // IOP reboot: every port closes
void enable();   // the game loaded a pad module: register the RPC services
void onVblank(); // refresh every open port's data in EE memory

// Parses a script line ("120 cross+up lx=255"); for tests.
bool parseScriptLine(const std::string& line, uint64_t& frame, Input& in);

} // namespace ps2::iop::pad
