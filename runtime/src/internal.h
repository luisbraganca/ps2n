// Runtime-internal shared state and helpers (not part of the public API).
#pragma once

#include "ps2/runtime.h"

#include <csignal>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ps2 {

struct Options {
    std::filesystem::path elfPath;
    std::filesystem::path host0Dir = ".";      // host: / host0: device root
    std::filesystem::path cdrom;               // directory or ISO image backing cdrom0:
    std::filesystem::path memcardDir = "memcards";
    std::vector<std::string> guestArgs;
    bool interpretOnly = false;    // ignore recompiled code (differential testing)
    bool virtualClock = false;     // deterministic time: advances with execution, not wall clock
    bool noThrottle = false;       // real-time clock, but skip ahead instead of sleeping when idle
    uint64_t maxVsyncs = 0;        // stop after this many vsyncs (0 = never)
    bool fpuRoundToZero = true;    // EE FPU / VU round toward zero
    std::filesystem::path dumpFrames;   // write displayed frames here (PPM)
    uint32_t dumpEvery = 1;
    bool window = true;            // open an SDL window when built with SDL2
    bool strict = false;           // stop at the first unimplemented path (PS2_UNIMPL)
    bool traceSyscalls = false;
    bool traceFunctions = false;
    std::filesystem::path missingOut = "ps2n_missing.toml";
    std::filesystem::path padScript;   // scripted controller input (see iop/pad.h)
    std::filesystem::path settingsFile;   // --settings: the bindings file (see settings.h)
    char region = 'A';             // console region in ROMVER: A (USA), E (Europe), J (Japan), H (Asia), C (China)
};

extern Options g_opt;

// The console's ROMVER string ("0160AC20010704\n"): BIOS version, region,
// console type and date. The BIOS windows and the rom0:ROMVER file show it.
std::string_view romVersion();
extern const GameInfo* g_game;
extern volatile std::sig_atomic_t g_interruptRequested;   // set by SIGINT
extern bool g_exitRequested;
extern int g_exitCode;
// Incremented by every guest context switch (thread switch); lets long-running
// host loops (the interpreter) notice that the CPU state belongs to someone else.
extern uint64_t g_switchGeneration;

// Magic guest addresses that run host code when execution reaches them.
// They live in an unmapped part of kseg0 that no PS2 program executes.
constexpr uint32_t kTrampolineBase = 0x8FFF0000u;
constexpr uint32_t kTrampThreadExit = kTrampolineBase + 0x00;   // a thread's entry function returned
constexpr uint32_t kTrampHostReturn = kTrampolineBase + 0x10;   // nested callGuest() finished
constexpr uint32_t kTrampExec = kTrampolineBase + 0x20;         // LoadExecPS2: start the next program
constexpr uint32_t kTrampolineEnd = kTrampolineBase + 0x100;
inline bool isTrampoline(uint32_t pc) { return pc >= kTrampolineBase && pc < kTrampolineEnd; }

// cpu.pc while the program exits: never a valid return address, so every
// host frame unwinds at its next call check.
constexpr uint32_t kExitPc = 0xFFFFFFFFu;

// Kernel-area layout (the real EE kernel occupies 0x0-0x7FFFF; we put our own
// data there instead).
constexpr uint32_t kInterruptStackTop = 0x0007F000u;
constexpr uint32_t kKernelScratch = 0x00070000u;

// dispatch.cpp
void initDispatch(const GameInfo& game);
// LoadExecPS2 loaded another executable: one of the game's recompiled
// programs, or (null) one without recompiled code, known by its file name.
void programLoaded(const GameInfo* program, const std::string& name);
// The game invalidated the instruction cache, as it does after loading code
// itself: recompiled code is checked against RAM before it runs again.
void invalidateCompiledCode();
void dispatch();                       // run guest code at cpu.pc once
const FunctionInfo* findFunction(uint32_t pc);
bool hasCompiledEntry(uint32_t pc);    // function start or resume point (with --interpret: a replaced function)
void recordMissingEntry(uint32_t pc);
void writeMissingEntries();
void requestExit(int code);
// callGuest() with an explicit stack pointer (0 keeps the current $sp).
uint64_t callGuestOnStack(uint32_t address, const uint64_t* args, int argCount, uint32_t stack);

// runtime.cpp
// LoadExecPS2: loads the executable at the PS2 path argv[0] to replace the
// running program. The switch happens in the dispatcher, once every host frame
// has unwound (cpu.pc is kTrampExec). False if it cannot be loaded.
bool requestExec(std::vector<std::string> argv);
void startPendingProgram();

// interpreter.cpp
void interpret();                      // from cpu.pc until compiled code takes over
bool interpretStep();                  // one instruction (no delay-slot pairing); false on unknown op

// cop0.cpp
void initCop0();

// rounding mode
void applyGuestFloatEnvironment();

} // namespace ps2
