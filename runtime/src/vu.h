// Vector unit micro mode (vu.cpp), and VU1 on a thread of its own
// (vu_thread.cpp). The EE-facing hooks (VCALLMS, CMSAR1, the FBRST resets)
// are declared in ps2/ops_vu0.h because recompiled code calls them.
#pragma once

#include "ps2/vu_recomp.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ps2::vu {

// Starts a VU1 micro program from byte address `startByte` (0xFFFFFFFF: from
// where the last one stopped, as MSCNT does), with VIF1's TOP and ITOP as its
// start latched them (XTOP, XITOP read them). It runs to its end now, or with
// VU1 on its own thread, is queued there.
void startVu1(uint32_t startByte, uint32_t top, uint32_t itop);
uint32_t vu1Reg(uint32_t index);   // VU1 VI / control registers, for tests and debugging

// VIF1's writes to VU1 memory, in order with the programs it starts: from
// quadword `qw` of data memory (wrapping round), `qwords` quadwords of four
// words, of which only the components `mask` names are written (bit 0: x);
// `n` bytes of micro memory from byte `at` (MPG).
void writeVu1Data(uint32_t qw, uint32_t mask, const uint32_t* words, size_t qwords);
void writeVu1Micro(uint32_t at, const uint8_t* bytes, size_t n);

// ---- VU1 on a thread of its own (vu_thread.cpp) ---------------------------------------------
// In real-time runs VU1 runs its micro programs on a host thread of its own,
// as PCSX2's MTVU does: the guest thread queues the programs and VIF1's
// writes to VU1 memory, and the VU1 thread carries them out in order. The EE
// still sees a program finished the moment it starts; whatever reads or
// writes VU1's state from the guest thread waits for the VU1 thread first
// (sync). Its XGKICK packets reach the GS in the order the programs were
// started (gs::path1Run), so the thread runs only while the GS has one of its
// own. Off by default; the runtime turns it on (--no-vu1-thread keeps VU1 on
// the guest thread). Guest thread.
void setOwnThread(bool on);
// The GS thread starts (true) or is about to stop (false); gs.cpp calls it.
void updateOwnThread(bool gsThread);
// Whether VU1 runs on its own thread now (read on the guest thread).
extern bool g_vu1Threaded;
inline bool ownThread() { return g_vu1Threaded; }
// Guest thread: waits until the VU1 thread has carried out everything queued.
void syncThread();
inline void sync() {
    if (g_vu1Threaded) syncThread();
}

// vu.cpp: runs a VU1 program to its end on the calling thread: the guest
// thread, or the VU1 thread (`threaded`: its XGKICK packets take the VU1
// thread's way to the GS).
void runVu1(uint32_t startByte, uint32_t top, uint32_t itop, bool threaded);

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
