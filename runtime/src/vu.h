// Vector unit micro mode (vu.cpp). The EE-facing hooks (VCALLMS, CMSAR1, the
// FBRST resets) are declared in ps2/ops_vu0.h because recompiled code calls them.
#pragma once

#include "ps2/vu_recomp.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ps2::vu {

// Runs a VU1 micro program from byte address `startByte` (0xFFFFFFFF: from
// where the last one stopped, as MSCNT does) until it ends.
void runVu1(uint32_t startByte);
uint32_t vu1Reg(uint32_t index);   // VU1 VI / control registers, for tests and debugging

// Recompiled micro programs (the game's table, replacing any before), and whether they run
// (--interpret runs everything on the interpreters).
void registerPrograms(const CompiledProgram* programs, size_t count);
void useCompiledPrograms(bool on);

// A unit's micro memory changed (VIF MPG, an EE store).
inline void microChanged(int unit) { ++g_microGeneration[unit & 1]; }

// Records the micro code the interpreter runs, and writes it as a [vu]
// code file for the next recompile (see docs/config.md).
void recordPrograms(bool on);
void writeRecordedPrograms(const std::filesystem::path& file);

} // namespace ps2::vu
