// Everything generated code needs: CPU state, instruction semantics and the
// runtime entry points behind control transfers the recompiler could not
// resolve statically.
//
// Execution model (see docs/architecture.md):
//   * Each guest function is a C++ function `void f()`; the caller sets
//     cpu.pc to the target and calls it directly.
//   * After every call the caller checks `cpu.pc == return address`. A
//     mismatch means the callee did not return normally (thread switch,
//     longjmp, exit), so the caller returns too; the dispatcher resumes the
//     right code later through the callee's resume switch.
//   * Host frames hold no guest state, so unwinding them at any call boundary
//     is safe. That is what lets one host thread run every guest thread.
#pragma once

#include "ps2/compiler.h"
#include "ps2/cpu.h"
#include "ps2/memory.h"
#include "ps2/ops_cop0.h"
#include "ps2/ops_fpu.h"
#include "ps2/ops_int.h"
#include "ps2/ops_mmi.h"
#include "ps2/ops_vu0.h"

#include <cstddef>
#include <cstdint>

namespace ps2 {

using RecompFn = void (*)();

// Safepoint countdown, decremented on every loop back-edge; when it drops
// below zero the code calls safepoint().
extern int32_t g_slice;

// Runs the guest code at `target` (recompiled, or interpreted when no
// recompiled code covers it) until it returns. Used for JALR, for J/JAL to
// addresses without a known function, and for unresolved JR.
void call(uint32_t target);

// Services timers, vsync and interrupts at a loop back-edge. Returns false if
// the current guest thread was switched out or the program is exiting; the
// generated code then returns (cpu.pc already holds `resumePc`).
bool safepoint(uint32_t resumePc);

// SYSCALL at `pc`. cpu.pc is pc + 4 on entry; a blocking syscall switches
// threads, which changes cpu.pc.
void syscall(uint32_t pc);

// A function was entered at a pc it has no resume label for (the recompiler
// missed an entry point). Falls back to the interpreter from cpu.pc.
void badEntry(uint32_t functionStart);

// Interprets the single instruction at pc (the recompiler had no translation).
void interpretOne(uint32_t pc);

void trap(uint32_t pc, uint32_t code);        // TEQ/TNE/... condition met
void breakpoint(uint32_t pc, uint32_t code);  // BREAK
void eret(uint32_t pc);                       // ERET (kernel-mode code)
void skippedFunction(uint32_t addr, const char* name);

// Host implementations of guest functions (stub_list.inc). The C++ names get a
// trailing underscore so libc macros such as putchar() cannot interfere.
namespace stub {
#define PS2_STUB(name, summary) void name##_();
#include "ps2/stub_list.inc"
#undef PS2_STUB
} // namespace stub

} // namespace ps2

// Helpers the generated code uses to stay compact. They are macros only so
// the `return` inside them returns from the generated function.
#define PS2_CALL(target_, fn_, ret_)      \
    do {                                   \
        ::ps2::cpu.pc = (target_);         \
        fn_();                             \
        if (::ps2::cpu.pc != (ret_))       \
            return;                        \
    } while (0)

#define PS2_CALL_DYNAMIC(target_, ret_)    \
    do {                                   \
        ::ps2::cpu.pc = (target_);         \
        ::ps2::call(target_);              \
        if (::ps2::cpu.pc != (ret_))       \
            return;                        \
    } while (0)

#define PS2_SYSCALL(addr_)                 \
    do {                                   \
        ::ps2::cpu.pc = (addr_) + 4;       \
        ::ps2::syscall(addr_);             \
        if (::ps2::cpu.pc != (addr_) + 4)  \
            return;                        \
    } while (0)

#define PS2_BACKEDGE(target_)                                                  \
    do {                                                                       \
        if (--::ps2::g_slice < 0 && !::ps2::safepoint(target_))                \
            return;                                                            \
    } while (0)
