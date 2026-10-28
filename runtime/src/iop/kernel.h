// The IOP kernel, high-level emulated: threads and their scheduler,
// semaphores, event flags, message boxes, memory pools, time, interrupts,
// vblank, and the system memory allocator (sysmem). Module code runs on the
// interpreter (cpu.h); these services run on the host.
#pragma once

#include <cstdint>
#include <string>

namespace ps2::iop::kernel {

// IOP memory layout chosen by the HLE kernel. The real kernel and BIOS
// modules live in the low memory; here it holds the kernel's own data.
constexpr uint32_t kThunkArea = 0x00000200u;      // generated guest routines
constexpr uint32_t kThunkAreaEnd = 0x00001000u;
constexpr uint32_t kSifCommandBuffer = 0x00001000u;   // where the EE sends SIF command packets
constexpr uint32_t kKernelData = 0x00002000u;     // tables guest code may read (ctype, ...)
constexpr uint32_t kInterruptStackTop = 0x00008000u;
constexpr uint32_t kUserMemory = 0x00010000u;     // sysmem hands out [kUserMemory, 2 MB)

// Wait types as ReferThreadStatus reports them.
enum WaitType : int {
    TSW_NONE = 0, TSW_SLEEP = 1, TSW_DELAY = 2, TSW_SEMA = 3, TSW_EVENTFLAG = 4, TSW_MBX = 5, TSW_VPL = 6, TSW_FPL = 7,
    TSW_SERVICE = 0x100,   // waiting on a host service (module load, CD read, RPC); not a real IOP wait type
};

void init();

// ---- scheduling (driven by iop::sync) ------------------------------------------------------
// Runs IOP threads for up to `budget` instructions. Returns instructions run;
// less than the budget means every thread is waiting.
int64_t runSlice(int64_t budget);
bool running();          // the current thread may keep running (checked per instruction)
bool hasReadyThread();
uint64_t systemTime();   // IOP clock cycles since start-up

// ---- services for other HLE code ---------------------------------------------------------
int currentThread();     // -1 when idle
bool inInterrupt();
// Blocks the calling thread from inside an HLE function; it resumes at the
// HLE call's return with $v0 = the value passed to wakeThread.
void blockCurrent(int waitType, int waitId);
void wakeThread(int thid, int32_t result);
// WakeupThread semantics: ends a SleepThread, or counts a pending wakeup.
void wakeupThread(int thid);
// Creates and starts a thread; its entry returns to `returnTo` (0: ExitThread).
int spawnThread(uint32_t entry, int priority, uint32_t stackSize, uint32_t gp, uint32_t returnTo, uint32_t a0 = 0,
                uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);
void exitCurrent(bool deleteThread);   // from an HLE function running on the thread
// Calls guest code in interrupt context (nested): handlers, alarms, callbacks.
uint32_t callHandler(uint32_t fn, uint32_t arg, uint32_t gp, uint32_t a1 = 0, uint32_t a2 = 0);

// System memory (sysmem). mode: 0 lowest address first, 1 highest, 2 at `addr`.
uint32_t allocMemory(uint32_t size, int mode = 0, uint32_t addr = 0);
int freeMemory(uint32_t addr);
uint32_t freeMemorySize();

// Event flags for HLE modules that wait like module code does.
int createEventFlag(uint32_t attr, uint32_t initBits);
void setEventFlag(int id, uint32_t bits);
void clearEventFlag(int id, uint32_t mask);   // flags &= mask

// ---- hardware glue ----------------------------------------------------------------------------
void raiseInterrupt(int line);   // INTC line (0-31) or DMA interrupt (32 + channel)
void onVblank(bool start);
uint32_t intcRead(uint32_t phys);
void intcWrite(uint32_t phys, uint32_t value);
void setStatusRegister(uint32_t sr);
void syscallInstruction(uint32_t pc, uint32_t insn);
void faultCurrent();             // the current thread cannot continue; stop it

std::string describeThreads();

} // namespace ps2::iop::kernel
