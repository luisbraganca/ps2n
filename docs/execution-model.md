# Execution model

How statically compiled EE code, the fallback interpreter and the PCSX2 core
run a program together. The definitions are in code:
[runtime/include/ps2n/recomp.h](../runtime/include/ps2n/recomp.h) (the
contract generated code is written against),
[runtime/rec_blocks.h](../runtime/rec_blocks.h) (the blocks PCSX2's
recompiler would have formed),
[runtime/ee_provider.cpp](../runtime/ee_provider.cpp) (dispatcher,
interpreter, what compiled code calls) and
[recompiler/src/codegen.cpp](../recompiler/src/codegen.cpp) (what is
generated). This file is the explanation; if it and the code disagree, the
code is right and this file needs fixing.

## The parts

- **The core** is PCSX2, unmodified: memory, the scheduler, every device, the
  BIOS. It asks its EE provider to `Execute()` and expects it to keep
  `cpuRegs.cycle` moving and to call `_cpuEventTest_Shared()` when
  `cpuRegs.cycle` reaches `cpuRegs.nextEventCycle`.
- **The provider** (`ps2n::ee::Provider()`) is PS2Native's `R5900cpu`. Its
  `Execute()` is a dispatcher loop: look up the PC; run the compiled function
  that has an entry there, or interpret one block.
- **Compiled code** is one C++ function per guest function, generated ahead of
  time by `ps2n-recomp`, in *images* (`ImageInfo::kind` in `recomp.h`), whose
  code ranges do not overlap:
  - an executable's: used from its entry point on, when its CRC (PCSX2's,
    the exclusive-or of the ELF's words) is that of the executable the
    machine boots;
  - the BIOS's: four images of the EE code of the user's BIOS dump
    (`ps2n-recomp bios`, [ADR 0017](adr/0017-bios-ee-code-compiled-as-step-g8.md)),
    used from the first instruction on while the CRC of the whole ROM is
    the one they were made from;
  - the code an executable ran outside its own (code it copied into kernel
    memory, for one), as runs captured it (`ps2n-recomp compile --outside`):
    used with the executable's.
- **The interpreter** runs everything else: code that is in no image, code
  that changed in memory, the few blocks compiled code does not carry out
  (below). It is also the reference: a run with compiled code must be the
  same run as without (`ps2n-host -static off`).
- **The record of blocks** (`ps2n::ee::blocks`) is what both count time by:
  for every address at which PCSX2's EE recompiler would hold a compiled
  block, the block as it would have compiled it. Nothing in it generates
  code.

There is no guest state outside the core, but for the length of a stretch
of straight-line compiled code. Registers are `cpuRegs` and `fpuRegs`,
memory is the core's, reached through its address table (vtlb). A compiled
function keeps general registers in local variables while it computes with
them, and puts them back before anything else can look ("Registers in local
variables" below). So whenever compiled code calls out, and whenever it
returns, its host frame holds nothing that matters, which is what makes
every unwinding below safe.

## Time is counted as PCSX2's recompiler counts it

The core schedules everything (the IOP, the timers, DMA, the vertical blank)
against the EE's cycle count. Games are tested against stock PCSX2 with its
recompilers, and a few cycles decide which side of a race between the EE and
the IOP a program falls on, or how far a loop that watches the clock gets in
a frame. So PS2Native counts cycles **exactly** as PCSX2's EE recompiler
does ([ADR 0011](adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)).

That recompiler counts time by the blocks it compiles: it adds a block's
cycles, and tests for events, where the block ends. What a block is was
fixed when the recompiler first compiled it, from what the machine looked
like at that moment: which other blocks existed already, what COP0's
`Config` held, which registers the block itself had made constants. No rule
about the code alone gives the same count. PS2Native therefore keeps the
record the recompiler keeps, without the code: `blocks::Form(pc)` is its
`recRecompile()` carried out on paper, `blocks::Clear()` its `recClear()`,
`blocks::Reset()` its `recResetRaw()`.

### What a block is

The rules, each transcribed from PCSX2 v2.9.170 (`pcsx2/x86/ix86-32/iR5900.cpp`
unless another file is named):

1. **Where it ends.** From the block's first instruction the recompiler
   looks ahead and stops after a branch or jump and its delay slot; after
   `SYSCALL`, `BREAK` or `ERET`; at a 4 KB page boundary; where a block
   exists already; and, if a conditional branch goes back to a place inside
   the block, at that place (so that a loop's body becomes a block of its
   own). While compiling, three more things end a block after
   the instruction: `EI` and the trap instructions; and a load of 32 bits or
   fewer from an EE timer register (`10000000`-`10001FFF`) through an
   address the block has made a constant.
2. **What ends it decides whether events are tested for.** A block that ends
   at a branch, a jump or an instruction that ends one adds its cycles and
   tests for events. One that was cut short (a page boundary, another block,
   its own branch target, a `FlushCache` call that is not made) adds its
   cycles and tests only if it holds more than six instructions; a shorter
   one runs straight on into the next block. After `ERET`, `EI` and the
   traps an event test happens whatever the time.
3. **What it costs.** The sum of its instructions' counts (PCSX2's opcode
   table, in eighths of a cycle; a `nop` counts 9), doubled if bit 18 of
   COP0's `Config` was clear when the block was formed, divided by eight and
   rounded down, never less than one.
4. **Cycles added in the middle of a block.** Before some instructions the
   count so far, the instruction's own included, is added to the cycle count
   (rounded the same way, the remainder kept): `MFC0` from the count
   register; `MTC0` to the status or the count register; the performance
   counter instructions, except the read of their control register
   (`ps2n::r5900::flushesCycles`); and the COP2 instructions before which
   VU0 is brought up to date with the EE, which
   the recompiler decides from a pass over the whole block
   (`COP2MicroFinishPass`, `x86/iR5900Analysis.cpp`; `x86/microVU_Macro.inl`).
5. **Idle loops.** A block whose branch goes back to its own first
   instruction, and which holds nothing but `nop`s, loads, and arithmetic on
   constants and loaded values (`ps2n::r5900::WaitLoopScan`, PCSX2's own
   test), moves the cycle count on to the next event when that branch is
   taken, and an event test follows whatever the time. This is what keeps
   the BIOS's idle thread from eating host time.
6. **Timeout loops.** A block that is a loop of `nop`s counting a register
   down to zero (ps2sdk's `nopdelay`; `ps2n::r5900::TimeoutLoopScan`) is not
   run. Eight cycles pass for each count left in the register, but time goes
   no further than the next event; the count comes down by the cycles that
   passed, divided by eight; unless it has reached zero an event test
   follows, after which the same happens again. The eight is PCSX2's,
   whatever the loop's instructions cost. PCSX2's `WaitLoop` setting
   switches this and the idle loops off.
7. **`FlushCache` is not called.** A `SYSCALL` whose number (`$v1`) the
   block has made a constant, 100 or 104, is not made: 706 cycles are
   counted instead (5,650 eighths) and the block is cut short after it.
8. **`DI` takes effect one instruction late**, and not at all if that
   instruction is a branch. The instruction after it belongs to `DI`'s
   block even where the block was to end between the two. In a delay slot
   `DI` takes effect at once.
9. **A branch or jump in a delay slot is dropped**, cost and all.
10. **`SYNC`, `PREF` and `CACHE` do nothing.**
11. **Constants.** Rules 1 and 7 depend on what the recompiler knows to be
    constant in a block: `ps2n::r5900::RecConsts`
    ([rec_rules.h](../core/include/ps2n/rec_rules.h)), which the recompiler
    (the analysis) shares with the runtime.
12. **Code that is written to.** A block's page is write-protected through
    the core when the block is formed. A write there makes the core call the
    provider's `Clear`, and the blocks over the range are thrown away. A page
    that keeps being written is left unprotected by the core; a block formed
    in such a page compares its words with memory every time it runs, and is
    thrown away when they differ; after enough such runs the page is given
    another chance under protection (`memory_protect_recompiled_code`,
    `dyna_block_discard`, `dyna_page_reset`).
13. **Loads and stores that turn out not to hit plain memory.** The
    recompiler compiles a load or store whose address it does not know as a
    direct access to host memory. The first time it hits a device's
    registers the access faults, the instruction is patched and its block is
    thrown away (`vtlb_BackpatchLoadStore`, `pcsx2/vtlb.cpp`). The record
    does the same, once for each such instruction.
14. **The BIOS's program loader** is hooked at three blocks, and the
    executable's entry point is noticed when its block is formed. That is
    also where PS2Native chooses the image of the executable. The hooks run
    at the start of the block, at every execution, whether the interpreter
    or compiled code carries it out (`blockStart()` runs them, and keeps no
    answer in the image's table for such a block, so that it is asked
    every time).
15. **Blocks are filed under physical addresses**, so one formed through one
    mirror of memory is the block at the others.
16. **VU0's flags reach their registers only where something may read
    them.** With PCSX2's "mVU flag hack" setting, which is on unless
    switched off, the code for a VU0 instruction that writes the status or
    the MAC flags brings the register up to date only before a transfer of
    that register, before a micro program is called, before a store that
    may start one, and at the block's end (`COP2FlagHackPass`,
    `x86/iR5900Analysis.cpp`). Of a run of such instructions only the last
    counts, and the sticky flags collect from those alone. And the
    recompiler's VU0 code never sets the overflow and underflow flags
    (`mVUupdateFlags`, `x86/microVU_Upper.inl`). PS2Native's VU0
    instructions ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)) compute
    their flags as that code does and are told by the runtime, from the
    block, which registers they reach (`vu0::kStatus`, `vu0::kMac`). Between
    two instructions of a block microVU may leave the status register in a
    layout of its own; PS2Native keeps the console's layout throughout
    (where that can show: "What is not reproduced").

### The interpreter

`RunRecBlock()` carries out one block of the record, formed first if there
is none at the PC: its steps one by one, each as the recompiler decided
(`blocks::Step`), then the end of the block as rule 2 has it. So the
interpreter is not PCSX2's interpreter; in time it is PCSX2's recompiler.

### Compiled code

Where a block begins is decided by execution, and where it ends by history,
so generated code cannot have either built in. It can know this much: a
block only begins at a *leader* (a label of a generated function), and only
ends at a leader or after an instruction that ends one. Each generated
function keeps four locals:

- `acc`: the eighths counted since the block began or since cycles were last
  added, apart from those the recompiler has folded into the next macro's
  argument;
- `ps2n_start`: the address the current block began at;
- `ps2n_end`: the address at which the record says the block ends, with two
  flags in its low bits (a test for events if it is cut short there; an idle
  loop);
- `ps2n_cycle`: the core's cycle count. The core's own is always right:
  whenever the function adds a block's cycles to its copy it writes the sum
  to `cpuRegs.cycle`, and after every call out of the function that comes
  back into it (an event test, another function, the runtime, the core's
  function for an instruction) it takes the core's count again
  (`PS2N_TIME`). The copy only spares reading the count back from memory
  at the end of every block.

What the generated code does with them:

- **At a label where a block begins** (`PS2N_BLOCK_START(index, address)`),
  it reads the image's table `blocks[index]`. Zero means nothing is known:
  the runtime (`blockStart()`) finds or forms the block, compares its code
  with what was compiled (below), and answers. From then on the table has
  the answer, until the block is thrown away.
- **Reaching a label from the instruction before it**, the function compares
  the label's address with `ps2n_end`. If they differ, the block goes on
  through the label. If they are equal the block was cut short there
  (`PS2N_SPLIT_END`), and a new one begins at the label.
- **At a branch, jump or other end** it adds the block's cycles and tests
  for events (`PS2N_BLOCK_END`; `PS2N_BLOCK_END_LOOP` where the branch may
  close an idle loop, which the flag in `ps2n_end` decides).
- **Before an instruction of rule 4** that is in COP0, `PS2N_FLUSH` adds the
  cycles so far.
- **What depends on the block** is asked of the runtime, which looks up the
  step in the record: `PS2N_SYSCALL` (made or not), `PS2N_COP2` (what
  happens to VU0 first, and whether cycles are added), and the loads and
  stores, `PS2N_LOAD`, `PS2N_STORE` and `PS2N_ACCESS` (only when one hits
  memory that is not plain: rule 13, and for a timer read rule 1). A VU0
  instruction (`PS2N_VU0`) reads the same answer where the runtime keeps
  the last few thousand it gave (`cop2Sites`, `cop2Info()` in
  `ps2n/recomp.h`, good for as long as no block is formed or thrown away),
  and where nothing is to happen first, carries itself out
  ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)); otherwise the runtime
  carries it out as `PS2N_COP2` would.
- **`DI`** is compiled in one piece with the instruction after it, which
  runs first (rule 8), when that is a plain instruction at which no block
  can begin; in a delay slot it takes effect at once.
- **What the recompiler does not translate** is left to the interpreter,
  which finishes the same block: `leave(pc, eighths, ps2n_start)`. That is
  an unknown encoding; a branch whose delay slot holds a system instruction,
  a coprocessor branch or an unknown encoding; `DI` before a branch or
  before an instruction at which a block can begin. The host reports where
  it happened and how often ("compiled code handed over to the
  interpreter").

### Registers in local variables

The core's registers are in memory, where everything can reach them. Code
that reads and writes them there for every instruction is slow, and the C++
compiler cannot help: after a store into the console's memory it has to
assume the registers changed. So a generated function keeps the registers
it works on in local variables, which nothing else can reach and the
compiler keeps in host registers
([ADR 0015](adr/0015-a-speed-step-before-phase-3-ends.md)): the general
registers in `g1` to `g31`, bits 0 to 63 of each, and the FPU's in `f0` to
`f31` and `facc` (its accumulator), 32 bits each.

The generator knows, at every point of the code it writes, which locals
hold their register's value and which of those hold a value the core does
not have yet. From that:

- **A register is taken from the core** (`PS2N_GET`, `PS2N_GET_F`,
  `PS2N_GET_ACC`) the first time straight-line code reads it. `$zero` is a
  literal.
- **The instructions that only compute a general register from others**
  (the arithmetic, the logic, the comparisons, the shifts, the conditional
  moves) work on the locals: `g2 = val::ADDU(g4, g5)`. Their arithmetic is
  in `ps2n::ee::val` ([ops_int.h](../runtime/include/ps2n/ops_int.h)); the
  functions the interpreter uses are made of the same, so each instruction
  is still defined once.
- **The FPU's instructions** work on its registers' locals in the same way:
  `f0 = val::ADD_S(f1, f2)`, `facc = val::MADDA_S(facc, f1, f2)`
  ([ops_fpu.h](../runtime/include/ps2n/ops_fpu.h)). A comparison writes its
  condition to FCR31 in the core, where the branch that follows reads it,
  and so do the flags of a division or a square root: those stay in the
  core. `MFC1` and `MTC1` move between a general register's local and an
  FPU register's.
- **The loads and stores of a general register or an FPU register** take
  the address from a local and look at the core's address table once
  (`PS2N_LOAD`, `PS2N_LOAD_F`, `PS2N_STORE`). Only when the address is not
  plain memory are the changed registers put back, the runtime told, and
  the core's own function called.
- **Every other instruction is carried out through register numbers**, on
  the core's copy, as before. The changed registers its fields name are put
  back first (`PS2N_PUT`), and the locals no longer hold the ones it may
  write. For one of the FPU's that is every FPU register (two are left:
  the moves to and from its control registers).
- **Everything changed is put back** before the end of a block, where an
  event test may follow; before a label, which other code can reach; before
  a call; before the runtime carries out a COP2 instruction (a VU0
  instruction does not count: it touches no register of the EE's, wherever
  it is carried out), a system instruction or what the recompiler does not
  translate; and before the interpreter takes over. After a call, a system
  instruction or a delegated one the locals hold nothing; after a COP2
  instruction, nothing of what it may write.
- **After the end of a block the locals go on holding what they held.** An
  event test after which the code goes on where it was going has changed
  no register: one that takes an interrupt changes where the code goes on
  instead, and then the function returns. The registers are in the core
  for the event test all the same, which is where the comparisons with
  stock PCSX2 and with the interpreter read them (the IOP runs, and a sync
  point is recorded, only inside it).
- **A label holds what the code after it reads**, of what the ways into
  it hold. Every way in brings the locals to the label's state first: it
  puts back what it changed, and takes from the core what the label holds
  and it does not. An entry from outside the function, through the
  `switch` at its top, does the same through a way in of its own, after
  the function's code (`E_` and the label's address). The generator works
  the states out by generating each function a few times: what each
  label's code reads, and what each way in held, the time before.
- Bits 64 to 127 of a register never leave the core.

What this buys is measured in the game log, step G6.

Blocks compiled code cannot carry out are interpreted where compiled code
asks about them: one that counts double (rule 3; the BIOS's reset code
forms three such, 48 instructions, before it sets `Config`), one cut short
at an address that is not a leader, one that ends after a timer read where
no leader follows. The analysis makes leaders of page boundaries and
of the instruction after a load that may be a timer read so that the last
two are rare; the host reports how often and where it happened ("times the
interpreter carried one out at a label instead", "compiled code handed over
to the interpreter"). A timeout loop (rule 6) is not run by anything; the
runtime lets its time pass when compiled code asks about its block, and the
host counts those turns apart.

A block that is thrown away may be the one that is running (it wrote to its
own page). PCSX2's code for it would run on to the block's end, so the
record keeps such a block usable until the next event test
(`blocks::Collect()`).

### What is not reproduced

- PCSX2's EE cycle rate setting (counted as 0, with a warning).
- The recompiler throws every block away when its code cache is full.
  PS2Native has no code cache, so it cannot know when that would be. After
  such a reset the blocks are formed anew and may be cut differently.
- What the recompiler knows to be constant (rule 11) is tied to its register
  allocator in a few cases (after a call of a C function from compiled
  code). `RecConsts` follows the common cases.
- A system instruction or a coprocessor branch in a delay slot. The host
  counts them ("other things in delay slots that are not reproduced").
- `GoemonTlbHack`, a fix for one game.
- What microVU's register allocator carries from one VU0 instruction to the
  next inside a block. It can leave an operand clamped in a host register
  that the next instruction reads, in the Extra clamp modes; and what a
  `VOPMULA` with a single component computes depends on what it holds
  ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)). PS2Native's VU0
  instructions are the recompiler's for a block of that instruction alone.
- The status register in microVU's own layout between two VU0 instructions
  of a block (rule 16). Should VU0 run its micro program in between, when
  the block brings VU0 up to date, microVU writes the register in the
  console's layout where VU0 stops (`mVUendProgram`,
  `x86/microVU_Branch.inl`), and its code for the second instruction reads
  that as its own layout, where PS2Native reads it as what it is.
- Where PS2Native's instruction semantics deliberately follow the console
  and not the recompiler
  ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)), a run
  differs from stock PCSX2 in what is computed, and from there possibly in
  time.

None of the others has been seen to make a run differ: see "Comparing
two runs" for how that is tested. The last has: in a game, `MIN.S` of -0
and +0 leaves -0 in the register here and on the console, and +0 under
stock PCSX2 (the game log, step G4). The host counts how often the four
instructions of that kind are given operands for which stock PCSX2 may
leave something else, and says so after a run.

## Entering and leaving compiled code

A generated function is `uint32_t fn(uint32_t pc)`:

- `pc` is where to enter: the function's start or any other block leader. The
  function begins with a `switch (pc)` over all of them. Every block leader
  is an entry, because any block can be where an interrupt returns to. A
  call enters at the start, so that is tested first, before the switch.
- The return value is the PC at which execution continues.
- Under clang the functions have a calling convention of their own
  (`PS2N_CC`, clang's `preserve_none`): they keep no host registers for
  their caller. Nothing a compiled function holds in host registers lives
  across a call of another one (it is in the core or taken from there
  again), so the eight registers every function saved on entry and restored
  on leaving were wasted work. The runtime's functions, which compiled code
  calls, keep the platform's convention. With another compiler `PS2N_CC` is
  empty.

Calls and returns:

- `jal` to a compiled function is a C++ call: `PS2N_CALL(fn, target, ret)`.
  When the callee returns, the caller compares the returned PC with `ret`. If
  they are equal it carries on. If not, it returns that PC in turn; so does
  its caller; the dispatcher ends up with a PC and continues there.
- `jr $ra` is `return target`.
- A branch or jump into another function is a tail call (`PS2N_JUMP`,
  `[[clang::musttail]]`).
- `jalr` and `jal` to an address without compiled code go through
  `ps2n::ee::dispatch(pc)`, which runs compiled code if there is some and
  otherwise interprets until compiled code is reached again.
- `jr` through another register sets `pc` and goes back to the function's own
  entry switch (a switch table lands on one of its blocks); the default case
  is `ps2n::ee::jump(pc)`: a tail call if `pc` is the start of a compiled
  function, else `return pc`.

So every way out of the usual call/return shape (an interrupt, a system call,
a thread switch by the kernel, `longjmp`, a return to an address nobody is
waiting for) is the same mechanism: a PC travels up the host stack until a
frame recognizes it, or reaches the dispatcher. `kUnwind` (1) is returned
instead of a PC when every frame must give up; the dispatcher then continues
at `cpuRegs.pc`.

For a likely branch whose delay slot cannot be compiled (GCC's division
check, `beql divisor, $zero, 1f; break 7`) only the taken path leaves.

A function whose last instruction is in the middle of a block continues
through the instructions that follow it, compiled a second time, to the end
of that block ("tail"). At each leader on the way it tests whether the
block ends there, and if so jumps to that leader's own function. A tail
longer than 64 instructions is left to the interpreter from there.

## Events, system calls, threads

- `PS2N_BLOCK_END` calls `ps2n::ee::eventTest(next)` when an event is due. If
  the core took an interrupt, `cpuRegs.pc` is the exception vector; the value
  returned differs from `next`; every compiled frame returns; the dispatcher
  enters the BIOS's handler, compiled in the BIOS's image (or interprets it,
  without one). When the kernel returns with `ERET`, the dispatcher finds
  the entry for that PC (a block leader of the interrupted function, or of
  another thread's function) and enters it.
- `SYSCALL` and the other system instructions are carried out by PCSX2's own
  function for them (`PS2N_SYSCALL`, `PS2N_SYSTEM`). A system call usually
  leaves compiled code; the program is re-entered at the instruction after
  it. The few calls the core answers itself, and the `FlushCache` calls that
  are not made, continue in place.
- Nothing is high-level emulated. Threads, semaphores, alarms, handlers are
  the BIOS kernel's, compiled ahead of time from the user's dump, which the
  build is given (`build.ps1 -Bios`; interpreted without one).

The host stack: a guest call is a host call, so the thread that runs the core
needs a large stack (64 MB in `ps2n-host`). Guest code can make the host stack
grow without its own growing (a callee that jumps back into its caller). At
each event test reached from compiled code the provider checks the depth
against the budget the host gave it (`SetHostStackBudget`) and returns
`kUnwind` past it. `ps2n-host -stack-budget 0` does that at every event test;
the `ps2autotests/lockstep-no-stack/*` tests run that way.

## Is the code in memory the code that was compiled?

Asked block by block, when a block is formed and compiled code first asks
about it (`IdentifyBody`):

- The image holds a 64-bit hash for every *leader segment*: the words from
  one leader to the next, or to the end of the function. A generated
  program thus carries fingerprints of the game's code, not the code.
- The words in memory over the segments the block lies in are hashed and
  compared. Equal: compiled code runs the block. Different: the block is
  interpreted, and both its start and the words found in memory in the
  function's place are recorded for the recompiler (see "Variants").
- The answer is kept with the block. It stays true because of rule 12: the
  block's page is write-protected, a write there throws the block away, and
  the next time execution reaches the label a new block is formed and
  compared. A block in a page the core has given up protecting compares its
  own words every time it runs, and compiled code asks about such a block at
  every start of it.

So changed code is noticed at the next block start, as under PCSX2's
recompiler, and not before: a block that is running runs to its end.

### Variants

A program that rewrites a function, or a patch applied to memory, puts other
code where a function was compiled. The first build of a program cannot know
it. Its runtime notices, interprets, and writes what it found to the capture
file (`ps2n-host -capture`): the words over the function's own range.

Given that file (`ps2n-recomp compile --capture`), the recompiler compiles
that code too, as a *variant* of the function: same range, same block
leaders, its own hashes. At run time a block is run by whichever of them
matches memory. Other compiled code always calls the function itself; when
its label finds that a variant's code is in memory, the runtime runs the
variant in its place.

`tests/programs/selfmod.s` is such a program. `programs/first-build/selfmod`
checks that a build from the executable alone reports exactly what
`selfmod.capture.toml` holds; `programs/rebuilt/selfmod` checks that the
build with it interprets nothing of the program.

### Code an executable runs outside its own

Code where no image has a function at all: an overlay loaded into memory
the executable does not fill, or code a program's libraries copy into
kernel memory and install as system calls (the game and the test programs
built with ps2sdk do, at `80075000` and `80076000`). The first run
interprets it, and the capture file holds the words of each block it ran
there (`[[code]]`, as they were then; outside the executable's code and the
BIOS's files, so that nothing of the BIOS's ends up with a program's).
Given that file, `ps2n-recomp compile --outside` makes an image of its own
from those words, put together where blocks adjoin, followed from where
they ran as a BIOS image is (`<id>_outside`, with the executable's CRC), and
the runtime uses it with the executable's. Its blocks are checked against
memory like any other's, so other code in that place later is interpreted
and captured again. The build does this for a program built with a capture
(the game; `ps2n_add_image ... CAPTURE`); the test programs are built from
their executables alone and interpret such code.

## What is not compiled, and how it shows

Nothing is interpreted or delegated silently. `ps2n-host` prints after a run:

- how many instructions the interpreter ran, and how many of them inside the
  executable's code range (should be zero once the image is complete);
- how many were *delegated*: carried out by PCSX2's interpreter function for
  the instruction because PS2Native has no semantics of its own for it yet.
  Compiled code delegates the same instructions; the recompiler reports how
  many it emitted;
- the block starts inside the code range that had to be interpreted
  ("missing entries"), written to a file with `-capture <file>`. Giving that
  file to `ps2n-recomp compile --capture` makes them block leaders in the
  next build. The same file holds the program's code that ran outside its
  executable ("Code an executable runs outside its own");
- what of the BIOS was interpreted, by where its blocks begin (the ROM, the
  kernel's file, the rest of kernel memory, the idle thread's file, the
  program loader's file, the rest of low memory), at boot and after the
  executable's entry point; the BIOS's images in use; the instructions in
  blocks counted double; and the BIOS's files the EE runs from RAM compared
  with the ROM's, word for word, at the entry point and at the end.
  `-bios-capture <file>` writes the BIOS's block starts the interpreter
  carried out, for `ps2n-recomp bios --capture` (the test driver writes one
  for every run with compiled code);
- what became of the record of blocks: how many were formed, thrown away,
  found changed; the `FlushCache` calls not made; and the things listed
  under "What is not reproduced" that the run met.

PS2Native's own semantics so far: integer arithmetic and logic, shifts,
multiply and divide, HI/LO, the shift-amount register, loads and stores of
general registers ([ops_int.h](../runtime/include/ps2n/ops_int.h)); the FPU
([ops_fpu.h](../runtime/include/ps2n/ops_fpu.h)); the multimedia instructions
([ops_mmi.h](../runtime/include/ps2n/ops_mmi.h)); of COP0, the transfers of
its registers and of the performance counters, `DI`, and `SYNC`, `PREF` and
`CACHE` ([ops_cop0.h](../runtime/include/ps2n/ops_cop0.h)); of COP2, the
transfers between VU0's registers and the EE's registers or memory
([ops_cop2.h](../runtime/include/ps2n/ops_cop2.h)) and the VU0 instructions
([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)). Delegated: the rest of
COP0 (the TLB instructions, `ERET`, `EI`), and `VCALLMS` and `VCALLMSR`,
which start a micro program on VU0 and for which PCSX2's recompiler calls
the same function of the core.

Which behavior a function reproduces is stated at the top of its header. The
rule ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)):
PCSX2's recompiler, which games are tested against; the console where a
recording shows the recompiler wrong and PCSX2's interpreter right, or where
the recompiler has no single behavior; never a behavior that neither has.
`ps2n-host -selftest` compares every function with both PCSX2
implementations on random and boundary operands, and lists what it
deliberately does not compare, with the reason in `host/selftest.cpp`. The
`ps2autotests/never-further/*` tests hold the outputs of whole programs to
the same rule, line by line against stock PCSX2 and the console.

A corrected instruction can cost a program its match with the console, when
the old behavior was hiding a mistake elsewhere. That happened with `CTC2`
(proof-of-concept log, increment 5). Find out which before choosing: the
rule decides the instruction, not the number of programs that match.

To add a family: write `ops_<family>.h`; put its rows in a table of their
own (`core/include/ps2n/r5900_sem_*.inc`) and include that in `ExecuteOwn()`
(`runtime/ee_provider.cpp`), in `semanticCall()`
(`recompiler/src/codegen.cpp`) and in `kOwnOps` (`host/selftest.cpp`); give
the self-test the state the family touches (`State` in `host/selftest.cpp`);
run the self-test and explain every difference before excluding it; then
plant bugs to see that it reports them. A deep run:
`cmake -DSELFTEST=1 -DSELFTEST_TRIALS=20000 -DSECONDS=900 ... -P tests/guest_test.cmake`.

The FPU functions compute under the host's FP control register as PCSX2 sets
it for the EE (rounding toward zero, denormals as zero). The C++ compiler
knows nothing about that, so every operand passes through `fpu::opaque()`,
which hides the value and fixes the point of computation. Do not remove it.
PCSX2's clamp mode Full is not implemented: with it set, the provider warns,
runs no compiled code, and delegates the FPU.

## Comparing two runs

**With stock PCSX2: sync points.** The core lets the IOP catch up with the
EE from its event test, and that moment exists whatever runs the EE.
`ps2n-host -sync <file>` writes one line for each (a *sync point*): the EE's
PC and cycle count, how far the IOP is let run, and hashes of the EE's
general, FPU, COP0 and VU0 registers. (Registers of the machine only: not
the variables PCSX2's interpreter functions keep beside them, such as the
VU interpreter's pending result of a division, which a run that goes
through those functions has and a run on PCSX2's recompilers has not.) It
works under every configuration,
stock PCSX2's recompiler included (the host wraps the core's IOP provider;
PCSX2 is not touched). `-sync-against <file>` compares a run with an earlier
run's file as it goes and reports the first sync point where place or time
differ and the first where registers differ; `-sync-detail <n>` writes the
registers from sync point `n` on.

Two runs with the same record took the same path through the program at the
same cycle counts, and let every device see the same times. That is the
test of the section above: the `*/same-as-stock/*` tests run every test
program under stock PCSX2 and then compiled, and require the records to be
equal. The programs whose records differ are listed, each with its cause, in
`tests/same-as-stock-known-differences.cmake`.

A game runs long enough to meet an instruction where the console is
followed on purpose, and from there a register differs for good. So for a
game (`game.ps1 -Config same-as-stock`) place and time must be the same at
every sync point, and so must the pictures and the state of the sound
hardware at the frames looked at; the registers are reported kind
by kind (general, FPU, COP0, VU0): at how many sync points each differed and
where first, so that one kind that differs for a known reason does not hide
the others.

**Compiled against interpreted: event tests.** Both carry out the same
blocks, so event tests happen at the same instructions and cycle counts.
The provider folds the registers at every event test into a fingerprint of
the run:

- `ps2n-host -trace <file>` writes one line per event test: number, PC,
  cycle count, fingerprint so far. `-trace-detail <n>` adds the registers
  from event test `n` on.
- `ps2n-host -trace-against <file>` compares a run with an earlier run's
  file as it goes, and stops at the first event test that differs.

The `*/lockstep/*` tests run every test program interpreted and then compiled
this way. When one fails, the event test number it reports is where to look:
rerun both with `-trace-detail` a little before it and compare the registers.

**What a program prints and shows.** `ps2n-host -times <file>` (the test
driver's `TIMES=1`) writes every line the program prints with the number of
frames shown and the EE's cycle count at that moment. For a program that
draws, `-frame-every <n>` fingerprints the frame on display every `n` frames
(and with `-frame` saves each), and `-pad <file>` gives the controller in
port 1 scripted input that changes only at frame boundaries, so that two
runs get it at the same moments. Two runs that show the same fingerprints at
the same frame numbers drew the same pictures at the same times.

**What a program sounds like.** At the same frames, and always at the last
one, the host fingerprints the sound hardware: the SPU2's registers and its
memory. That memory holds the samples the program gave it, the working
memory of its reverberation, and the last 512 samples of everything it
mixed, which its mixer writes there as the console's does. The SPU2 runs on
the CPU thread in the IOP's time, whether a sound device is open or not, so
two runs that are at the same place at the same time and made the same
sound print the same fingerprints. The `*/same-as-stock/*` tests and
`game.ps1 -Config same-as-stock` hold the compiled run to the stock run in
this too.

## A program too large for one file, and more code than its executable shows

A game's executable is a million instructions. `ps2n-recomp compile --parts
<n>` spreads its functions over `n` files of about equal size, in address
order, with a header that declares them; the main file then holds only the
tables. In one file the functions are local to it; over several they are
not, and a call from one part into another is the same call.

The analysis does not find every place a block can begin: a function reached
only through a pointer it cannot see is not known to be one. The run
reports each such address the first time it has to interpret from it
(`-capture`), `ps2n-recomp capture` puts the reports of several runs into
one file, and the next build makes those addresses entries. The game log has
the numbers.

## The recompiler's analysis

`recompiler/src/analysis.cpp`. Function starts come from symbols, the entry
point, `jal` targets, code addresses found in data or built by `lui`/`addiu`
pairs, and a prologue heuristic; code no function claims becomes functions of
its own. **Function boundaries only affect speed**: a wrong one turns a jump
into a call between two C++ functions, or makes a function run on into the
next.

What has to be right is the set of block leaders: a function's start; every
branch or jump target; the instruction after a branch's or jump's delay slot;
the instruction after a system instruction; code addresses found in data
(switch tables); every 4 KB page boundary inside a function; the instruction
after a load that may read an EE timer register through a constant address;
and entries captured from an earlier run. Padding after a jump or return is
skipped. An address that is none of these and is still jumped to at run time
is interpreted and reported; a block that the record cuts short at an
address that is none of these is interpreted too.

The BIOS's files, and code captured outside an executable, have no sections
or symbols, and data lies among their code: the kernel's variables at
`800010C0` come right after one of its functions. Swept as an executable's
code is, that data
would end up inside functions, under the fingerprints the runtime compares
with memory, and every block there would be interpreted once the kernel had
written to it. Their code is followed instead (`Options::followCode`): from
where execution is known to enter it (the reset vector, the kernel's
exception vectors, the entries of the idle thread and the program loader,
where a run's block began) through every branch, jump and call, and every
jump through a register the instructions just before make a constant.
Functions are the runs of instructions reached, cut where one is entered.
Nothing is taken for code on a guess, so what is reached only through a
table (most of the kernel's handlers) is found by runs, as a pointer a game
calls through is.

`ps2n-recomp info <elf>` reports what the analysis makes of a program,
`ps2n-recomp bios-info <BIOS image>` what it makes of a BIOS, and
`ps2n-recomp disasm <file> --at <address> [--count n] [--base address]`
disassembles any part of an executable or of a raw image such as a BIOS,
with each instruction's count.
