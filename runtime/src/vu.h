// Vector unit micro mode (vu.cpp). The EE-facing hooks (VCALLMS, CMSAR1, the
// FBRST resets) are declared in ps2/ops_vu0.h because recompiled code calls them.
#pragma once

#include <cstdint>

namespace ps2::vu {

// Runs a VU1 micro program from byte address `startByte` (0xFFFFFFFF: from
// where the last one stopped, as MSCNT does) until it ends.
void runVu1(uint32_t startByte);
uint32_t vu1Reg(uint32_t index);   // VU1 VI / control registers, for tests and debugging

} // namespace ps2::vu
