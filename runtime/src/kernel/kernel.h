// High-level emulation of the EE kernel: threads, semaphores, interrupt and
// DMA handlers, alarms, and the syscall interface.
//
// Guest threads all run on one host thread. A context switch saves ps2::cpu
// into the outgoing thread's control block and loads the incoming one; the
// generated code then unwinds to the dispatcher on its own, because every
// call site checks that cpu.pc equals its return address.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2::kernel {

// Resets every kernel object and starts the program at `entry` as the main
// thread; SetupThread hands it `argv` (argv[0] is the program's path).
void init(uint32_t entry, std::vector<std::string> argv);
bool hasRunningThread();
int currentThread();
// Nothing can run: wait for (or skip to) the next event and deliver it.
void idle();
// A thread's entry function returned (its $ra pointed at kTrampThreadExit).
void onThreadReturned();
bool inInterrupt();
// Delivers pending INTC/DMAC interrupts to registered handlers, then
// switches threads if one became ready with a higher priority.
void serviceInterrupts();
void markRescheduleNeeded();
// Kernel-side vblank work (SetVSyncFlag), called by the GS vsync event.
void onVblankStart();
// Wakes interrupt delivery soon (an interrupt became pending).
void kickInterrupts();

// SIF/IOP integration: hook used by the RPC layer to release a thread
// blocked in a guest semaphore from host code.
int signalSemaFromHost(int id);

std::string describeThreads();

} // namespace ps2::kernel
