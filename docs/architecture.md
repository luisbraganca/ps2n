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

An event runs at the first safepoint after its time, usually some cycles
late, so it acts on what happened since it was scheduled rather than on the
state it finds: a timer's compare interrupt comes because the counter passed
its compare value, not because the two are equal when the event runs.

In real time a vblank also waits until the threads have had half a frame
over the last two frames (`kernel::interruptCycles()` counts the time spent
in handlers). A handler can take longer than a frame here: a game that draws
from its vsync handler has the VIF and vector units' work done inside it,
which a console does beside the CPU. Agent Under Fire redraws each field that
way inside its first mission's building, and facing the lobby the redraw took
about 40 ms: each vblank came while the last one's handler was still drawing,
the threads never ran again, and the picture and the sound froze. The game
now slows down there instead, while in play outdoors, where it draws in the
handler every other frame, it keeps its pace. With `--virtual-clock` that work
costs no emulated time, and vblanks come on time.

Time follows the host clock by default (games run at their real speed);
`--virtual-clock` advances time by a fixed amount per safepoint and syscall
instead, making runs reproducible, which is what the tests use.

In real time the EE's work is held to the clock all the same
(`clockEeWork`): a slice of 2,048 back-edges counts 16,384 EE cycles and a
syscall 200, what `--virtual-clock` advances by, and the guest thread waits
while its work is ahead of the clock, after at most a millisecond of credit
for time it spent behind or idle. Recompiled code runs many times faster than
the EE, and games bound their polling loops by a count of tries. Agent Under
Fire waits for its vsync handler to take a finished frame, or to finish
drawing one, for 5,000,000 polls, about 90 ms on a console, and then resets
its drawing (`sub_00129fb0`). Polling at the host's speed ran that out within
a frame, nearly every frame where drawing was heavy: the game redrew stale
frames with the wrong lightmaps, and a room in its first mission flickered at
35 to 47 pictures a second; held to the clock, it shows 50. Work that keeps
the guest behind the clock is never held back, and `--no-throttle` (fast
forward) holds nothing.

### The GS thread

On the console the GS draws while the EE runs on. Drawing inside the DMA
kick instead made a frame cost the EE's work plus the drawing, and a game
that redraws a 3D scene on every vsync (Agent Under Fire's copyright screen)
never got to run its main thread. So in real-time runs the GS has a host
thread of its own, as PCSX2's MTGS does. DMA, VIF and VU1 hand their GIF data
to a 32 MB ring, and the GS thread parses it in order and queues the
primitives for the draw threads. The GIF resets (`GIF_CTRL.RST`,
`CSR.RESET`) go through the ring too, so the GS sees every write in the
order the guest made it.

The guest thread waits for the GS thread in three places only:

- each vsync queues its picture, which is memory and the display registers
  as they are at that vsync, and the guest waits while the GS thread is two
  vsyncs behind;
- reading GS memory back (`gs::currentFrame`, `gs::readPixel32`) waits until
  the ring is empty and the draw threads have drawn it;
- a full ring waits for room.

### The draw threads

A 3D scene is thousands of small triangles. Sharing only large primitives
between threads left the first mission of Agent Under Fire drawn on one
thread, at about 60% of full speed. So the draw threads (`--gs-threads`,
half the host's threads by default, at most 8) share every primitive: the
thread that decodes the GIF data (the GS thread, or the guest thread where
runs must repeat) queues each primitive with a copy of its drawing state,
and each draw thread works through the queue, drawing the rows that belong
to it, bands of four rows dealt round in turn. A pixel sees the same
operations in the same order as with one thread, so the picture is the same.

Only the order between rows changes, so the queuing thread waits for the
queue to be drawn wherever that order could show:

- before anything else touches GS memory: an image transfer, a vsync's
  picture, a read back;
- before a primitive that reads what the queue writes (a texture drawn
  earlier in it), writes what it reads (a texture its primitives still
  sample), or writes the same memory under another layout, where other
  threads own its rows. The memory is tracked per 8 KB page, so a false
  match only costs a wait.

A primitive that reads what it writes itself (a texture or CLUT in its own
frame or depth buffer, or frame and depth buffers that overlap) is drawn by
the queuing thread alone, after the queue.

### Pixels

A triangle covers the pixels whose centres (whole coordinates) are inside
it. A centre on an edge belongs to a left edge or a top one (level, the
triangle below it), not to a right or a bottom one, so an edge two
triangles share is drawn once: the top-left rule, as PCSX2's rasterizer
draws and sprites do here. Each row's span comes from the edge functions,
which doubles hold exactly (vertices are 12.4 fixed point); the float
weights only interpolate. Deciding edges with the weights went wrong under
the guest's rounding toward zero: half of a quad's bottom row came out drawn
(a row of dots under a HUD icon in Agent Under Fire), and pixels on an edge
two triangles share were drawn by both (a brighter line through a blended
HUD ring) or by neither (a dotted seam across the pause menu's figure).

Faster pixels must not change a picture: every pixel goes through the same
float operations in the same order as before. So the rasterizer works out
ahead what does not change within a primitive. With the drawing state it
picks the sampler for the texture's format (a template per format, with no
switch per texel), each level's first memory unit and its row of pages,
where each CLUT entry sits, from tables built at start-up, and the frame and
depth buffers' addressing; a texel still reads local memory as it is when
the pixel is drawn. The depth test comes before texturing, since a pixel
that fails it writes nothing whatever the alpha test says, and the frame's
pixel is read only when blending, `FBMSK` or a kept alpha needs it.
Triangles work out the weights of eight pixels of a row at a time, and
sprites their texture's u, in plain loops over eight lanes that the
compilers vectorise: each lane does one pixel's operations. Interpolating
the colours and texture coordinates in lanes as well gained nothing.

The GS thread never touches guest state. `FINISH` and `SIGNAL` set their
`CSR` bits there, and the guest thread raises the GS interrupt at its next
safepoint (`gs::pollEvents`, from `hw::intcPending`); a guest waiting idle is
woken for it (`clockWake`). The guest sees `GIF_STAT` idle as soon as its
data is queued, where a console would show the GS busy until it is done.

Because the GS thread changes when `FINISH` and `SIGNAL` arrive relative to
guest code, it is off where runs must repeat (`--virtual-clock`, the tests)
or stop where a gap is (`--strict`), and with `--gs-threads 1`. Given the
same GIF data, each vsync's picture is the same either way.

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
  the CD/DVD drive) are replaced by services that do their job directly:
  PADMAN refreshes libpad's data area from host input every frame, MCSERV
  maps memory cards to directories, and CDVDFSV and cdvdman read the disc
  image. The SPU2 is emulated at its registers (`iop/spu2.cpp`), and libsd
  is host code that drives them as Sony's library does.

A host function that has to wait (a semaphore, a disc read) blocks the
calling IOP thread and lets the scheduler run others; one that calls back into
module code (an interrupt handler, an alarm, a transfer callback) runs it as a
nested guest call. The IOP is driven from the EE's timeline: it runs when the
EE syncs with it (SIF transfers, syscalls, idle time), and host events
(alarms, timers, drive and sound completions) are scheduled in emulated time.
That includes the EE's safepoints inside an interrupt handler, so a SIF
command packet for the EE waits until the EE has re-armed its receive channel
(`sceSifSetDChain`), as on the console: the EE's handler frees the buffer
before it has finished copying the packet out.

The SPU2 runs on that timeline too: 48,000 samples per emulated second,
mixed when a driver touches its registers and by an event every 64 samples.
Its interrupts (a transfer done, an address touched) reach the IOP kernel
like any other. When it has fallen behind (the EE slept, or a frame took
long to draw), it catches up one interrupt at a time: it stops after the
sample that raised one, the IOP runs what that woke, and it mixes on. While
it is stopped, a register or memory access does not bring it up to the
clock, as one does otherwise: the IOP's answer acts at the sample that
raised the interrupt, as it would have in time. A driver that streams PCM
thus refills its buffer in order, as on the console. libsd arms each next
half of a looping transfer in its interrupt handler, as the real library
does, and the half is armed before anything more is mixed: the driver never
sees two transfers merged into one, and the input never plays its old
blocks again for want of the next half. A half the driver has not refilled
in time plays what it had, as on the console. In real time the samples keep
pace with the host's audio device, which a ring buffer feeds (`audio/`).

## The vector units

Games upload micro programs at run time, through VIF `MPG` commands or direct
writes, often from data, and start them with `MSCAL`, `VCALLMS` or `CMSAR1`.
A program runs to completion when it starts, as if the VU were infinitely
fast compared to the EE. What micro code can observe of the pipeline is kept:
results written after both halves of a 64-bit pair (an upper FMAC instruction
and a lower one) read their inputs, MAC/status/clip flags that only appear
four cycles later, Q and P latencies, stalls on register hazards (a stall
holds the whole pair, so an operation on Q paired with WAITQ reads the new
Q), branch delay slots and the E bit.

That model is written once. `core/include/ps2n/vu_micro.h` says what a
pair's words make it do to the pipeline (`decodePair`) and keeps the
pipeline's time (`stepPair`), calling hooks as results land. The interpreter
(`runtime/src/vu.cpp`) gives it hooks that compute the values, through the
operations in `runtime/include/ps2/vu_micro.h`. It decodes each pair once and
keeps the decoding while micro memory holds the same words; at most four
results are in flight, so they wait in a ring, and a table of the cycle at
which each VF component's latest write lands makes a stall check a lookup.

Micro programs are recompiled from what the runtime saw: the interpreter
records each distinct micro memory it ran programs from, with where they
started, in `ps2n_missing.vu.toml`, and `[vu] code_files` hands that to
`ps2n recompile` (`recompiler/src/vu_codegen.cpp`). The generator walks each
program through the same `stepPair`, with hooks that write C++ instead of
computing: the operations are the interpreter's own, chosen at compile time
from the instruction words (`upperFn` and `lowerFn` are constexpr), and the
flags, Q and P land where the walk found them landing. So the generated code
keeps nothing in flight at run time but the values, and adds each block's
cycles once. A block is code from one address in one state of the pipeline;
a loop settles into one or two states. Each block checks, once per change to
micro memory, that micro memory still holds the words it was compiled from.
JR goes back to the pairs after the calls whose routine reaches it (and after
the calls that routine makes, which overwrite its link register), JALR to
the addresses programs start at, and both also to the `targets` the runtime
recorded where recompiled code had to hand a program over. Anything else
hands the program to the interpreter with the pipeline written back as it
would be: a branch or E bit in a delay slot, too many states at one address,
a jump to an address nothing recorded, micro memory that changed. Tests run
random programs both ways and compare every register, data memory and the
cycle count.

On a VU1 transform loop the recompiled code runs 3.1 times as fast as the
interpreter. Agent Under Fire uploads its VU1 code once: recorded and
recompiled (24 entries, 3,371 pairs in 458 blocks), every program of a
session from the title to the lobby of the first building runs to its end
recompiled, with the same frames, sound and output as the interpreter. A
deterministic run inside the building goes from 29.6 to 33.0 vblanks per
second. In real time drawing is the limit. Facing the lobby the game shows
41 to 42 pictures a second either way. In the cutscene that opens the
mission the recompiled programs raise the pictures from 38 to 40 a second,
and the guest then waits longer for the GS thread, which may only be two
vsyncs behind: vblanks come at 42 a second instead of 46. The programs still
run inside the DMA transfer that starts them, on the guest thread; with
drawing the limit, running VU1 alongside the EE would not make Agent Under
Fire faster yet.
