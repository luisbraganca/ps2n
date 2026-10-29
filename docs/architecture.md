# Architecture

This document explains how a PS2 executable becomes a native program, and why
the pieces are shaped the way they are.

## Pipeline

```
          ┌───────────────── ps2n (recompiler/) ─────────────────┐
game.elf ─┤ ElfFile ─► analyze() ─► generate() ─► C++ + CMakeLists │
          └──────────────────────────────────────────────────────┘
                         │
config.toml ─────────────┘           ps2n_runtime (runtime/) ─┐
Ghidra map (optional)                                          ▼
                                                  native executable
```

1. **ELF reading** (`core/src/elf.cpp`): program headers for loading, section
   headers for precise code ranges, symbol tables for names and sizes.
2. **Analysis** (`recompiler/src/analysis.cpp`): finds function starts,
   decides boundaries, binds stubs/skips/overrides, then analyses each function
   for labels, resume points and jump tables.
3. **Code generation** (`recompiler/src/codegen.cpp`): one C++ function per
   guest function, a function table, `main()` and a CMake project.
4. **Runtime** (`runtime/`): loads the ELF's segments into emulated RAM at
   startup and runs the program from its entry point.

## Decoding: one table, three consumers

`core/include/ps2n/r5900_ops.inc` lists every R5900 instruction once: its
mnemonic, its encoding with operand fields zeroed, its operand syntax, flags
(branch, load, link, likely, ...) and memory access size. From it:

- `decoder.cpp` is a switch tree mirroring the hardware's opcode maps. A test
  checks that every table row decodes to itself.
- `disasm.cpp` formats operands from the operand syntax.
- `assembler.cpp` parses the same syntax back. A test pushes 10,000+ random
  encodings through disassemble → assemble and requires identical words.

`r5900_sem.inc` maps each non-control-flow instruction to the runtime
function implementing it and the decoded fields it takes. The recompiler turns
a row into text (`ADDIU(SP, SP, -32)`), and the interpreter into a call
(`ops::ADDIU(in.rt(), in.rs(), in.simm())`), so the two can never disagree
about what an instruction does.

## Function discovery

Function starts are collected from several sources, strongest first:

| Source | Notes |
|---|---|
| `[[analysis.functions]]` in the config | explicit start (and optional end) |
| function maps | Ghidra CSV exports |
| ELF symbols | `STT_FUNC` with sizes; global `STT_NOTYPE` labels (assembly) |
| entry point | |
| extra entries | `[analysis] entries` and `entries_files` (runtime output) |
| JAL targets | every `jal` in code |
| data pointers | words in data sections that point into code |
| code pointers | `lui`/`addiu` pairs that build a code address (callbacks, thread entries) |
| prologues | optional: `addiu $sp,$sp,-N` after a `jr $ra` |
| unclaimed code | non-zero code between known functions |

A pointer only becomes a function start if it *looks* like one (it follows a
`jr $ra` and its delay slot, or starts a stack frame, or starts a section);
otherwise it is treated as a label inside a function (jump tables in
`.rodata` land here). Functions with a reliable size (config, map, symbol)
absorb weaker starts inside their range, which become mid-function entry
points instead of splitting the function.

## The execution model

### Functions are C++ functions

Each guest function becomes `void name_ADDR()` with no parameters. The CPU
state is the global `ps2::cpu`; guest RAM is the global array `ps2::ram`.
Instructions are calls to inline functions in `runtime/include/ps2/ops_*.h`
taking constant register numbers, which the compiler folds into plain
arithmetic on `cpu.gpr[...]`.

Keeping the context a global (instead of a `ctx` pointer argument) matters
for speed: two distinct global objects provably do not overlap, so the
compiler knows a store to guest RAM cannot modify a guest register and keeps
register values in host registers across memory accesses. With a pointer
argument every guest store would force every register to be reloaded.

### Control flow

- Branches inside the function are `goto` to labels; the delay slot is emitted
  between evaluating the condition and jumping (or only on the taken path, for
  branch-likely).
- `jal` to a known function is a direct C++ call:

  ```cpp
  setLink(RA, 0x001001a4u);                 // $ra = return address
  ADDIU(A0, A0, -1);                        // delay slot
  PS2_CALL(0x00100178u, fib_00100178, 0x001001a4u);
  //  cpu.pc = target; fib_00100178(); if (cpu.pc != 0x001001a4u) return;
  ```

- `jr $ra` stores the return address in `cpu.pc` and returns.
- `jr $reg` with a resolved jump table becomes a `switch`; anything else
  calls the dispatcher (`ps2::call`) and returns.
- `jalr` calls through the dispatcher and checks the return address.
- `j`/branches to another function are tail calls (`cpu.pc = T; f(); return;`).
- Falling off the end of a function tail-calls the next one.

### The return-address check, and why host frames hold no guest state

After every call the caller compares `cpu.pc` with the address the callee
should have returned to. Normally they match and execution continues. When
they do not, the callee did not return normally:

- **a thread switch**: a syscall such as `WaitSema` blocked the current guest
  thread; the kernel saved `cpu` into the thread's control block and loaded
  another thread's context, whose `pc` is somewhere else;
- **longjmp**: guest code restored an older `$sp`/`$ra` and returned into a
  frame further up;
- **exit**: `cpu.pc` is set to an address that is never a return site.

In every case the caller simply returns as well. Generated code keeps no
guest state in host locals across calls, so throwing host frames away loses
nothing: the guest's own call stack is in guest memory. The chain of returns
ends at the dispatcher loop, which looks up `cpu.pc` and continues.

### Resume points

The dispatcher must be able to enter a function in the middle: at the return
address of a call (when a thread blocked inside the callee and is resumed
later), after a `syscall`, at a loop header (if a thread was preempted
there), at addresses that other functions jump or call into, and at labels
referenced from data. The analysis collects these, and each function that has
any starts with a switch:

```cpp
switch (cpu.pc) {
case 0x00100178u: break;                    // normal entry
case 0x001001a4u: goto L_001001a4;          // after the first call to fib
case 0x001001b4u: goto L_001001b4;          // after the second
default: ps2::badEntry(0x00100178u); return; // no label: interpret instead
}
```

Callers set `cpu.pc` to the function start before a normal call, so the
switch costs one indirect jump per call.

### Threads

All guest threads run on one host thread (a dedicated one with a large stack,
since guest recursion becomes host recursion). The HLE kernel keeps a thread
control block per guest thread with a saved copy of `ps2::cpu`. Switching
threads is a save and a load of that struct; the unwinding above does the rest.
This gives deterministic scheduling, no host-level races, and correct
semantics for priority preemption, `SleepThread`/`WakeupThread`, semaphores and
`RotateThreadReadyQueue`.

### Several executables

A game's executables (the boot ELF and those it starts with `LoadExecPS2`)
are recompiled into one native program. They usually occupy the same
addresses, so each has its own C++ namespace and function table. `LoadExecPS2`
loads the next file and sets `cpu.pc` to a trampoline address that is never a
return address, so every host frame unwinds as it does for an exit. Back in
the dispatcher, with no guest frames left on the host stack, the runtime
resets the kernel, the interrupt state and the IOP, loads the new segments,
selects the function table recompiled from that file (by its CRC-32; none for
a file without one, which is then interpreted) and starts the new main thread.

A loader can also copy the next program into memory and jump into it without
telling the kernel. Every function table entry therefore carries a checksum
of the function's instructions. When the dispatcher reaches code the running
program does not have, it picks another program of the game whose functions
all match RAM; after an instruction-cache invalidation it checks each
function again before running it. Calls between functions of one program
stay direct and unchecked.

### Safepoints: interrupts, timers and preemption

Time-driven work (vblank, timers, alarms, DMA completion interrupts) runs at
safepoints:

- every loop back-edge decrements a counter (`PS2_BACKEDGE`); every 2048
  back-edges `ps2::safepoint()` runs due events and delivers interrupts. A
  backward jump into another function counts too: an entry point found
  later (an interrupt that returned mid-loop) can split a loop in two, and a
  loop that polls memory for an interrupt handler's write must still take
  that interrupt;
- every syscall does the same on its way out.

Interrupt handlers (INTC and DMAC handlers registered by the game, alarm
callbacks) are guest functions called *nested*: the runtime saves `cpu`, runs
the handler to completion on the interrupt stack, and restores `cpu`, as the
real kernel does. If a handler wakes a higher-priority thread, the switch
happens right after the handler returns, at the same safepoint; the loop
header is a resume point, so the preempted thread later continues there.

Time follows the host clock by default (games run at their real speed);
`--virtual-clock` advances time by a fixed amount per safepoint and syscall
instead, making runs reproducible, which is what the tests use.

### The interpreter

`runtime/src/interpreter.cpp` executes guest code directly from memory with
the same `ops::` functions. It runs:

- code at addresses with no recompiled function (missed by the analysis, or
  loaded at run time);
- mid-function entries without a resume label (`badEntry`);
- single instructions the recompiler could not translate;
- everything, with `--interpret`, except the functions the config replaced
  (stubs, skips, overrides), which have no guest code to interpret.

It hands control back as soon as it reaches an address that has recompiled
code. Calls from interpreted code into recompiled functions are direct host
calls with the same return-address check. Every address it had to start at is
written to `ps2n_missing.toml` at exit, so the next recompile can include it.

## Memory

`ps2::read32()` and friends take an inline fast path for RDRAM
(`0x00000000-0x01FFFFFF`) and the scratchpad (`0x70000000`); everything else
(kseg0/kseg1 and uncached mirrors, hardware registers, VU memory, IOP RAM,
the BIOS window) goes through an out-of-line function that routes to the
right device. LQ/SQ ignore the low four address bits, as on hardware.

## Floating point

The EE FPU and VU have no infinities, NaNs or denormals. Operands with
exponent 255 read as ±FLT_MAX, denormals as ±0; overflowing results clamp to
±FLT_MAX and set the overflow flags; division by zero yields ±FLT_MAX. Values
are stored as raw bits so MFC1/MTC1 and loads/stores round-trip exactly. The
runtime rounds toward zero by default, like the hardware
(`--fpu-round nearest` to change).

## The IOP

On a PS2 the EE asks the IOP for files, controllers, memory cards and sound
through SIF: DMA transfers into IOP memory carry command packets (bind, call,
...), and replies come back as packets in an EE buffer plus a SIF0 DMA
interrupt that runs the EE library's command handler. The runtime implements
that wire protocol (`runtime/src/iop/iop.cpp`), so the game's own EE-side
libraries (`sceSifCallRpc`, ps2sdk's `SifCallRpc`, ...) run unmodified.

Behind it, the IOP is split in two:

- **Module code runs.** IRX modules are relocated into IOP memory and run on
  an R3000A interpreter (`iop/cpu.cpp`), so game-specific modules (sound
  drivers, streaming code, custom RPC servers) behave as written. Recompiling
  them like the EE code would also work, but IOP code is a small share of a
  game's time, and modules are loaded and replaced at run time.
- **The system is host code.** The IOP kernel (threads, semaphores, event
  flags, message boxes, memory pools, alarms, interrupts, vblank; `iop/kernel.cpp`)
  and the libraries modules import (`iop/libs.cpp`, `iop/sif.cpp`,
  `iop/modules.cpp`) are implemented on the host; an import becomes a jump to
  a trap address that runs the host function. Modules that drive hardware the
  runtime does not emulate register by register (controllers, memory cards,
  the CD/DVD drive, the SPU2) are replaced by services that do their job
  directly: PADMAN refreshes libpad's data area from host input every frame,
  MCSERV maps memory cards to directories, CDVDFSV and cdvdman read the disc
  image, and libsd keeps sound drivers running without producing sound.

A host function that has to wait (a semaphore, a disc read) blocks the
calling IOP thread and lets the scheduler run others; one that calls back into
module code (an interrupt handler, an alarm, a transfer callback) runs it as a
nested guest call. The IOP is driven from the EE's timeline: it runs when the
EE syncs with it (SIF transfers, syscalls, idle time), and host events
(alarms, timers, drive and sound completions) are scheduled in emulated time.

## The vector units

VU code is not recompiled. dvp-as places micro programs in `.vutext`
sections, which the analysis leaves out of the EE code, and games upload them
at run time through VIF `MPG` commands or direct writes, often from data. An
interpreter (`runtime/src/vu.cpp`) runs them when they are started (`MSCAL`,
`VCALLMS`, `CMSAR1`), to completion, as if the VU were infinitely fast
compared to the EE. It decodes each 64-bit pair (an upper FMAC instruction and
a lower one) and keeps what micro code can observe of the pipeline: results
written after both halves read their inputs, MAC/status/clip flags that only
appear four cycles later, Q and P latencies, stalls on register hazards,
branch delay slots and the E bit. A VU recompiler on the same model is the
next step if VU-heavy games turn out to be slow.
