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

- **The core** is PCSX2 as it was released, but for PS2Native's changes to
  its code (`patches/pcsx2`, ADR 0050: one so far, which changes no result of
  a run): memory, the scheduler, every device, the
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
- **The vector units' micro programs** have a provider of their own, made
  the same way in small: code compiled ahead of time for the blocks of
  micro code a build has seen, an interpreter for the rest, and a record
  that follows PCSX2's VU recompiler as the record of blocks follows its EE
  recompiler ("The vector units' micro programs" at the end).
- **The I/O processor's code** has a provider of its own as well: code
  compiled ahead of time for the blocks of IOP code a build has seen, an
  interpreter for the rest, and a record that follows PCSX2's IOP
  recompiler ("The I/O processor's code" at the end).
- **Where compiled code is.** All of it, the EE code's images, the micro
  code's and the IOP code's, is built into `ps2n-host`, or, with
  `build.ps1 -Module on`, into modules of its own beside the host, one for
  each kind of code (`ps2n-game.dll`, the programs' EE code;
  `ps2n-bios.dll`, the BIOS's; `ps2n-vu.dll`, the micro code;
  `ps2n-iop.dll`, the IOP's code), which the host loads when it starts
  (`runtime/game_module.h`; [phase4-proposal.md](phase4-proposal.md), steps
  P4.1 and P4.2). A module reaches the core's state and the runtime through
  what the host exports (`runtime/include/ps2n/module.h`), and is compiled
  with PS2Native's own headers and nothing else: what it reaches of the
  core is PS2Native's description of PCSX2's structures
  (`ps2n/pcsx2_layout.h`, under PCSX2's names through `ps2n/pcsx2.h`), which
  the runtime compares with PCSX2's own when it is built and when it
  starts (`runtime/pcsx2_check.cpp`). The semantics and the generated code
  are the same text either way, but that in a module each of the core's
  variables is reached through its address in the import table. A run is
  the same run either way.

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

The rules, each transcribed from PCSX2 v2.9.170 and the same in v2.9.189, the
version PS2Native is built on since ADR 0054 (`pcsx2/x86/ix86-32/iR5900.cpp`
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
   rounded down, never less than one. PCSX2's EE cycle rate setting (its
   overclock, `EECycleRate`) changes that for a block of more than five
   cycles (40 eighths): 130% divides the cycles by 1.3 in single precision
   and rounds down, 180% and 300% divide the eighths by 16 and 32, 75%
   multiplies them by 5 or 7 32nds, 60% and 50% by 7 and 9 32nds
   (`scaleblockcycles` in `iR5900.cpp`; `ps2n::ee::blockCycles`). The
   recompiler reads the setting when it forms a block, and a change of it
   throws every block away first, so the runtime reads it whenever it throws
   its record away (step G17, ADR 0025). The recompiler works a block's cost
   out once, when it forms it; compiled code reads it at the end of every
   block from a table the runtime makes when it reads the setting
   (`ps2n::ee::cycleTable`, blocks of fewer than 4,096 eighths).
4. **Cycles added in the middle of a block.** Before some instructions the
   count so far, the instruction's own included, is added to the cycle count
   (rounded the same way, the remainder kept: the eighths below a cycle, and
   at 180% and 300%, in a block of more than five cycles so far, the eighths
   below 2 and 4 cycles; `ps2n::ee::blockRemainder`): `MFC0` from the count
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
  the answer, until the block is thrown away. That way is taken seldom and
  written at every label, so it is one call with the leader's index and
  what the table holds (`askAbout<image>()`, `blockAsk()`), whose answer
  comes back in a register (game log, "G10"). A block of a page that is not
  write-protected compares its words with memory at every execution (rule
  12). For one in a function's own code the table holds, in place of the
  answer, the number of the runtime's record of it, below `kCheckedLimit`
  (no block ends that low), and the label's call passes it on to
  `checkedAnswer()`, which does what PCSX2's code
  for the block does at every execution and nothing more: compares the
  words, from where they are in the host's memory, with what the block was
  formed from, counts the execution towards protecting the page again, and
  gives the answer the record holds. Where the words have changed, or the
  page is protected again, the block is thrown away as PCSX2's code throws
  it away (`DiscardManual()`, `ResetManualPage()`), and the label asks as
  above. A block of a function's variant has no place in the table, so its
  label asks every time; the runtime keeps its last answer about it
  (`Known`) for as long as no block has been formed or thrown away since
  (`blocks::epoch`) and the images in use are the same. Until 2026-11-16
  the runtime looked such blocks up and decided about them again at every
  execution, which, at a small wait loop, made Agent Under Fire's fourth
  mission take six to ten times stock PCSX2's time (game log, "G18", "The
  maintainer's second session").
- **Reaching a label from the instruction before it**, the function compares
  the label's address with `ps2n_end`. If they differ, the block goes on
  through the label. If they are equal the block was cut short there
  (`PS2N_SPLIT_END`), and a new one begins at the label.
- **At a branch, jump or other end** it adds the block's cycles and tests
  for events (`PS2N_BLOCK_END`; `PS2N_BLOCK_END_LOOP` where the branch may
  close an idle loop, which the flag in `ps2n_end` decides).
- **At the branch that closes a loop that may reach a fixed point**
  (`PS2N_BLOCK_END_FIXED`) it may let many times round pass at once. This is
  PS2Native's, not PCSX2's: PCSX2's recompiler runs every time round. The
  generator marks a loop of one block whose code runs straight from a leader
  to a branch back to it (neither likely nor a call, on general registers),
  with nothing in it but arithmetic on general registers and their loads and
  stores, and which writes no register it reads before writing it; not one
  that may be an idle loop (rule 5), which keeps PCSX2's treatment. Each time
  round notes whether it stored anything other than what memory held, or
  touched memory that is not plain (`ps2n_same`, `PS2N_STORE_SAME`). When
  one did neither, began at the loop's start, is still the block the image's
  table knows, and goes back, it has left the machine as it found it: the
  next time round depends on nothing else, so it and every one after it
  until the end at which an event is due do exactly the same, and nothing
  else runs on the machine before that event test (the IOP, the DMA, the
  other processors and the interrupts all wait for one). Those times round
  are not run. Their cycles pass at once (`skipFixedLoop()`), and the event
  test is made at the cycle count it would have been made at, on the same
  registers and memory, so the run is still the stock run at every event
  test and sync point. A block whose page is not write-protected is checked
  against memory each time it runs, its place in the table holding the
  number of its record and not the answer, and is never skipped. The host
  reports how many times round were not run.
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
  adds the cycles so far where the answer says, and carries itself out
  ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)). Only where the block
  has VU0 brought up to date first and VU0 is running a micro program
  (`PS2N_COP2_ASKS`), which is when there is something to do about VU0
  first, does the runtime carry it out, as `PS2N_COP2` would. The
  transfers between VU0's registers and the general registers or memory
  (`QMFC2`, `QMTC2`, `LQC2`, `SQC2`, `CFC2`, `CTC2`) read the same answer
  and, unless the runtime has something to do first, make the move
  themselves (`PS2N_COP2_MOVE`): a general register's local holds its bits
  0 to 63 and the core bits 64 to 127, and `LQC2` and `SQC2` go through the
  address table as the other loads and stores do. A `CTC2` to the status
  flag register, `FBRST` or `CMSAR1`, whose writing does more than store,
  is the runtime's (`PS2N_COP2`). `VCALLMS` and `VCALLMSR` are PCSX2's
  function, which PCSX2's recompiler calls after adding the cycles so far,
  in every block: compiled code does the same (`PS2N_VCALLMS`), with no
  register put back, since the function touches none of the EE's. The host
  reports how many COP2 instructions compiled code handed to the runtime, by
  instruction, and at how many of them VU0 was running.
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
`f31` and `facc` (its accumulator), each a register's 32 bits in the lowest
lane of a host SSE register (`val::Fpr`), as PCSX2's recompiler holds them.

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
  ([ops_fpu.h](../runtime/include/ps2n/ops_fpu.h)), taking and leaving SSE
  registers, so that a chain of them never passes through a general
  register: an addition takes its operands' exponents into general
  registers to look up the bits it keeps past its guard bit, and `MFC1` and
  `MTC1` move between a general register's local and an FPU register's.
  Nothing a function of ops_fpu.h gives depends on the other three lanes,
  which hold whatever the host left there; the self-test checks that. A
  comparison writes its condition to FCR31 in the core, where the branch
  that follows reads it, and so do the flags of a division or a square
  root: those stay in the core.
- **The loads and stores of a general register or an FPU register** take
  the address from a local and look at the core's address table once
  (`PS2N_LOAD`, `PS2N_LOAD_F`, `PS2N_STORE`, `PS2N_STORE_F`, the last two
  between memory and the SSE register directly; `PS2N_LQ`, `PS2N_SQ` and
  `PS2N_SQ_CORE` for the 128 bits of `lq` and `sq`: the local holds bits 0
  to 63 and the core bits 64 to 127, and `lq` writes all sixteen bytes to
  the core at once, its local a copy, so that an instruction reading the
  register whole from the core finds it in one store). Only when the
  address is not plain memory is the runtime called, once (`slowRead`,
  `slowWrite`, `slowRead128`, `slowWrite128`): it is told, and the core's
  own function makes the access. The locals keep the
  registers they hold on that way, changed or not, because nothing on it
  reads the EE's registers: the runtime looks at its record of blocks, the
  core's function at the device, and no exception is raised there. PCSX2's
  recompiler does the same: before such a call it writes back only the
  registers it holds in host registers the call may overwrite. Putting them
  back made that seldom-taken way most of every load and store's code (the
  game log, "G10").
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
- Where PCSX2's VUSyncHack or FullVU0SyncHack game fix is on: how far the EE
  is ahead of VU0 before a COP2 instruction, which decides whether VU0 runs on
  first.
  - microVU's macro mode subtracts 64 bits read at `VU0.nextBlockCycles` and
    compares the whole with 4 (`x86/microVU_Macro.inl`). Since v2.9.174 that
    field is 32 bits, followed by the low half of `VU0.Mem`. Unless that half
    is zero, the comparison comes out the same whatever the cycles: one way or
    the other, depending on where the system put PCSX2's memory.
  - PS2Native subtracts the field alone (`Cop2Prologue`,
    [ADR 0056](adr/0056-jak-and-daxter-at-a-130-ee-cycle-rate-its-interpreted-code-looked-for-next-nextblockcycles-kept-alone.md)).
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
  otherwise interprets until compiled code is reached again. Where it finds
  compiled code it remembers the function by the target (`callSites` in
  `recomp.h`), and the next call there is made by compiled code itself,
  without the runtime (`PS2N_CALL_DYNAMIC`). The runtime empties those
  places whenever the images in use, whether compiled code runs, or the
  core's settings may change.
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

Code where no image has a function at all:

- an overlay loaded into memory the executable does not fill;
- code a game loads from its data (Gran Turismo 3's and Jak and Daxter's are
  mostly so);
- code a program's libraries copy into kernel memory and install as system
  calls (the game and the test programs built with ps2sdk do, at `80075000`
  and `80076000`).

A run interprets it, and the capture file holds each block it ran there, as
the interpreter carried it out (`[[block]]`). Where memory held other code
at the same address at another time, it holds every version of it. Only
blocks outside the executable's code, the BIOS's files and the executables
the program loads itself are captured, so that nothing of the BIOS's ends up
with a program's.

Given that file, `ps2n-recomp compile --outside` makes images of their own,
with the executable's CRC, which the runtime uses with the executable's
([ADR 0057](adr/0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md)):

- **The first** (`<id>_outside`) holds a word at every address the blocks
  hold, put together where they adjoin, followed from where they ran as a
  BIOS image is.
  - Captures written before ADR 0057 held, where a function of it was in
    memory's place, the words over that function's whole range (`[[code]]`).
    They go into it as they always did, the first word captured at an
    address kept where two disagree.
- **Each further one** (`<id>_outside_2` and on) holds blocks that disagree
  with the first: other versions of code at the same addresses.
  - Each block goes into the first of them it agrees with. The first image
    lists them (`ImageInfo::layers`).
  - With `--outside-parts`, each is a file of its own beside the first
    (`<stem>_2.cpp`, in parts of its own where it is large). Without, all
    are in the one file.

The runtime puts all of them in use. A block in such code is identified as
any other, against the image compiled code asked from:

- Where that image's code is not what memory holds, it is identified against
  the others, and run by the one that holds it.
  - The block's record keeps which image that is.
  - The label that asked passes the block on to that image, as to a variant,
    and keeps that answer.
  - The dispatcher enters that image there from then on.
- Where none holds it, the block is interpreted and captured, as a block.

So a game that loads other code into memory it ran code from before (Jak and
Daxter, through its levels) runs compiled any version it met before, and a
session that meets nothing new captures nothing.

The build does this for a program built with a capture (the game, and a
test program with a capture file beside it; `ps2n_add_image ... CAPTURE`).
`tests/programs/overlay.s` copies two routines in turns to the same address
outside its executable (`programs/first-build/overlay`,
`programs/rebuilt/overlay`).

### Executables a program loads itself

A game can be several executables: the one the console boots, and others it
loads and starts itself, without the BIOS's loader (Agent Under Fire loads
its films, its missions and its driving missions so, through the IOP's
`SifLoadElf`, over the same addresses; ADR 0026). The core sees only the
first: its CRC stays the boot executable's, so the runtime cannot choose the
others' code by it, and captured blocks would hold only what a run met of
them.

Each is compiled as an image of its own instead (`ps2n-recomp compile
--loaded-by <boot executable>`, `ImageInfo::kImageLoaded`, tied to the boot
executable by its CRC; `build.ps1 -GameMore`), and the runtime tells from
memory which of them is loaded. When the executable reaches its entry point
it takes note of them; whenever a block is formed (`AdoptLoaded`, from
`FormRecBlock`, and from `dispatch()` before a block it has not formed yet),
if none of them in use has its code in memory there and one of the others
has (the words of the segment the address is in, by the fingerprints
compiled code is checked by), that one is put in use and any other over the
same addresses taken out, its compiled frames left. A block is still
compared with memory before compiled code runs it; choosing the image only
decides whose code is compared. What a run interprets in such an executable,
and code found in place of one of its functions, goes into the capture file
under that executable's CRC (`[[program]]`, `program =`), and the next build
compiles it into that executable's image. `tests/programs/loader.s` loads two
programs of its own over the same addresses, as the game does
(`programs/loaded/loader`).

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
implementations on random and boundary operands (and the FPU's additions
with the recompiler on every difference of their operands' exponents, which
random operands seldom reach), and lists what it deliberately does not
compare, with the reason in `host/selftest.cpp`. The
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

**The same machine.** Two runs can only be the same run if they start from
the same machine. The console's clock is read by the IOP's modules, so the
host sets it itself when the machine starts: every run that is not played
in a window at one fixed time (`ps2n-host -clock fixed`, the default:
2020-03-04, midnight, the time PCSX2 gives its input recordings), and
someone playing at this computer's time of day (`-clock host`, the default
with `-window`), which the session writes down. The memory cards and the
console's NVRAM are part of the machine too: a run without a window has no
memory card unless it is given copies (`-memcards`), and a fresh NVRAM
unless the test driver is given one to copy (`NVRAM_FROM`). Given copies,
it has a card in both slots, as a window always has: where a session began
without one, the core made a new one when the machine started, and a run of
it again gets a new one the same way.

**With stock PCSX2: sync points.** The core lets the IOP catch up with the
EE from its event test, and that moment exists whatever runs the EE.
`ps2n-host -sync <file>` writes one line for each (a *sync point*): the EE's
PC and cycle count, how far the IOP is let run, the IOP's cycle count and
its PC, and hashes of the EE's general, FPU, COP0 and VU0 registers, of
VU1's (since G11, ADR 0022; VU1 runs on the CPU thread between the EE's
blocks, so it is never in the middle of an instruction there) and of the
IOP's general registers and its COP0's (since G12, ADR 0023; the IOP is
between two of its blocks there, whatever runs it). (Registers of the machine only: not
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
by kind (general, FPU, COP0, VU0, VU1, the IOP's): at how many sync points each differed and
where first, so that one kind that differs for a known reason does not hide
the others.

**Compiled against interpreted: event tests.** Both carry out the same
blocks, so event tests happen at the same instructions and cycle counts.
The provider folds the registers at every event test into a fingerprint of
the run (the EE's, the FPU's, COP0's and VU0's, and since G12 the IOP's
PC, cycle count, general registers and COP0 registers):

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

**A session played in a window, run again.** What someone plays cannot be
compared as it is played: its input and its time are live. So a session
writes down what it takes to run it again (`ps2n-host -record`,
`host/session_record.h`): the controller in port 1 frame by frame, as
PCSX2's input recording keeps it (eighteen bytes: the buttons, both sticks,
the pressure of each button that has one), the clock it started at, the
game's patches it was played with and the EE cycle rate it was given, if
any, and every 50 frames a fingerprint of the EE's and the IOP's registers;
`game.ps1 -Play` puts the memory cards and the NVRAM it began with beside
that. (The overclock a patch's note asks for is not written down as a rate:
the note set it when the game's executable started, and the patches,
applied again, set it again then; a rate given to a run is set from the
machine's start.) The core takes input in at a frame boundary and the game reads it
during the frame that follows, so a run given the same bytes at the same
boundaries (`ps2n-host -input`, by PCSX2's own `PadData`) is the same
machine with the same input: `game.ps1 -Replay <session>` runs it in any
configuration, a comparison with the stock run included, and each run says
whether it is the session at every fingerprint. It need not be the session
as it was seen, where a renderer's picture can reach the program (a game
that reads the GS's memory back): the replay draws with the software
renderer, the session with a hardware one. Its two sides are the same run
all the same.

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

## The vector units' micro programs

The micro programs VU0 and VU1 run are run by a provider of the core's
(`CpuVU0`, `CpuVU1`): PCSX2's VU recompiler (microVU) as it ships, or
PS2Native's VU provider (`runtime/vu_interp.h`), which the host puts in its
place in PS2Native's configurations (`ps2n-host -vu-ps2n`; step G11,
ADR 0022). PS2Native's provider carries a block of micro code out by code
compiled ahead of time where the host has it, and by its interpreter
elsewhere; what it had to interpret it reports and writes down for the next
build, as the EE's provider does. The definitions are in code:
[core/include/ps2n/vu_mvu.h](../core/include/ps2n/vu_mvu.h) (microVU's
analysis of a block), [core/include/ps2n/vu_plan.h](../core/include/ps2n/vu_plan.h)
and [vu_steps.inc](../core/include/ps2n/vu_steps.inc) (what a block does, as
a list of steps), [runtime/include/ps2n/vu_ops.h](../runtime/include/ps2n/vu_ops.h)
(the steps' semantics), [runtime/include/ps2n/vu_recomp.h](../runtime/include/ps2n/vu_recomp.h)
(the contract compiled micro code is written against),
[runtime/vu_interp.cpp](../runtime/vu_interp.cpp) (the record of programs and
blocks, the planner, the interpreter) and
[recompiler/src/vu_codegen.cpp](../recompiler/src/vu_codegen.cpp) (what is
generated).

**It follows microVU, not the console** (ADR 0005), in its results and in
its time (ADR 0011): VU1's progress and its writes into its memory reach
the EE through the GIF, the VIF and the VU's registers, so a different
count of cycles would part the run from the stock run. microVU decides
everything but the values in a first pass over each block, from the state
of the pipeline the block is entered with (`vu_mvu.h`, its port, without
PCSX2); the block is carried out with that and with microVU's host
instructions in the same order on the same operands, under the VU's
floating-point control register.

**What microVU compiled before matters.** A block microVU has compiled is
used again for an entry state that matches eight bytes of the 96 of its
own, without the stalls still pending on VF and VI registers, unless the
state asks for an exact match: the stalls a block runs with are those of
the state it was first compiled for. microVU knows a program by where it
starts and by what micro memory held where it compiled from, compares
those ranges again after each write into micro memory, and compiles a new
program when none matches. The provider keeps the same record, in the
same order (both sides of a conditional branch when the branch is
compiled, an indirect jump's target when it is taken, through a cache of
jumps like microVU's), so that it runs each block with the stalls microVU
runs it with. Over the game's first 4,200 frames it records the same
programs as microVU, line for line with what microVU writes to the core's
log. The record is made as the game runs, from what micro memory holds: it
is which block comes after which, and how many cycles each takes. It holds
no code.

**A block's plan.** What microVU's second pass decides when it compiles a
block (which host instructions, on which operands, in which order, with
which registers of its allocation) the provider works out once, when the
block enters the record, and keeps as a list of *steps* (`vu_plan.h`; the
steps are listed in `vu_steps.inc`). A step is one small operation with up
to five numbers for operands: a VI register taken into a host register, a
multiply-and-add of given registers with a given clamp and flag update, an
address worked out, a store. The last step, `Leave`, says how the block
ends: the program's end, a branch, a conditional branch, an indirect jump,
and what microVU's code does there to the flags' instances and to P and Q
for the next block. A plan says nothing of where the block is: an address
in it (the link of a `BAL`, the target of a branch) is a distance
from the block's first pair, and an immediate is in the step, but for the
ones microVU reads from micro memory as its code runs (the I-bit game fix),
which a step reads there by the pair's place in the block. What is not in
the plan is the record's: the blocks after it, and its cycles. So the same
code has the same plan wherever it is written and whatever program it is
part of: over the level run, the 42,173 blocks of VU1's 6,088 programs
have 589 plans between them.

**Steps on an accessor.** Each step is a function (`vu_ops.h`), written
once, on an *accessor* that says where the VU's registers are. The
interpreter runs a plan step by step on the machine's own registers
(`step::Mem`). Compiled code calls the same functions on locals
(`step::Locals`, `vu_recomp.h`), so the two cannot differ in what a step
does, only in where it keeps the registers meanwhile.

**Compiled blocks.** `ps2n-recomp vu` writes one C++ function for each plan
of a capture (below): the plan's steps in order, each a call of the step's
function with its operands as constants. The function takes the VU's
registers into locals when it begins and puts back the ones a step wrote
when it ends; which those are is in the code, so the compiler drops the
loads of registers the block never reads and nothing is counted as it
runs. That is sound because nothing else looks at a VU's registers while
one of its blocks runs: a block runs from its first pair to its last on
the CPU thread, and what it calls out to (the GIF transfer of an XGKICK)
reads data memory, which the steps write at once. A function is named by
its plan's *key*, a 128-bit fingerprint of the VU and the steps
(`planKey`). The host's registry lists the images of compiled micro code
it was linked with (`ps2n_vu_images`), and the provider gives every block
whose plan's key one of them has that function. Compiled micro code is so
found by what a block does, not by where it is or what it was compiled
from: the plan is made from what micro memory holds when the block enters
the record, so there is no question here whether memory still holds the
code that was compiled.

**Blocks go on to each other**, as microVU's blocks jump to each other. A
block's function is given the block's entry in the record (`Link`: its
cycles, its address, the blocks after it). At its end it reorders the
flags' instances and swaps P and Q as its plan says, puts its registers
back, takes the next block (the branch's target, the block after a branch
not taken, or for an indirect jump the block `JumpTo` finds as microVU's
`mVUcompileJIT` finds it), tests that block's cycles as microVU's code
does where a block begins (`Enter`, microVU's `mVUtestCycles`), and calls
it in the place of returning (a jump: `[[clang::musttail]]`, under a
calling convention with which a block saves no registers for its caller).
It returns to the provider only when the program stops: at its end, with
its cycles used, at a T bit, or at something that is not carried out. An
interpreted block is entered and left the same way, so compiled and
interpreted blocks follow each other in any order.

**Code no build has compiled** is interpreted, counted and reported: after
a run the host says how many different plans each VU ran, how many of
them were compiled and how often those ran, how many were interpreted and
how often. `ps2n-host -vu-capture <file>` (`game.ps1 -VuCapture`) writes
the interpreted ones down, each with its steps; `ps2n-recomp vu-capture`
puts several runs' files together, and `build.ps1 -VuCapture <file>`
compiles them into the host. `ps2n-host -vu-static off` has every block
interpreted, the compiled ones too: the reference for them.

**Finding the program, at less cost.** microVU's rule is kept and part of
its cost is not. After each write into micro memory microVU forgets which
program each start address has, and at the next start or indirect jump
there compares the programs recorded for it with micro memory, the one
found last first. The provider also keeps, for each 128 bytes of micro
memory, the number of the last write into them. Where the program found
last at an address is still the first of its list and nothing was written
since into the memory it was compiled from, the comparison could only
find it equal: it is not made, and what it would have done is done. And an
indirect jump keeps the program and the block it found last: in the same
program the search for the block gives that block again, and is not made.
Over the level run 2,895,390 of 6,790,138 searches are spared so.

**What its register allocation does.** Some of microVU's results depend on
which host registers its allocator gave an operand. The planner goes
through microVU's allocation of both kinds of register, in the order
microVU's second pass makes it (one count of allocations for both), once,
when it makes the plan, and the steps say what follows from it:

- *VI registers* live in 32-bit host registers within a block (eight of
  them, the least recently used given up first, some loaded at the block's
  start, the ones a call may change emptied before an XGKICK), and only
  their low half is written back. FMEQ compares the whole register, ISW and
  ISWR store the whole of one that MFIR sign-extended in place, MTIR leaves
  a whole float in one, and the integer instructions carry the high half
  on. The steps name the host register each VI register is in, and load,
  copy and give them up where microVU's code does.
- *XMM registers*: a register microVU does not hold is loaded as an
  instruction's field asks (one component alone, VF00 without w as zeros),
  one it holds is copied whole. OPMULA and the EFU's sums (ESADD, ERSADD,
  ELENG, ERLENG, ESUM) load their operand so and read its other lanes too,
  so with one component in their field (which assemblers do not write)
  their result depends on what microVU held: the provider refuses them.
  And with AVX, PCSX2's emitter swaps the two sources of a commutative
  instruction (ADDPS, MULPS) when that makes its encoding shorter (the
  first in xmm0 to xmm7, the second in xmm8 to xmm15): where both are NaN,
  x86 gives the first source's, so which NaN a packed addition or
  multiplication gives depends on the registers. A step says whether the
  emitter swaps them, and they are swapped on a processor with AVX, as
  PCSX2 decides from the processor.

**What is not carried out.** The T and M bits on a branch, the T bit with
an XGKICK still to come at its block's end, PCSX2's XGKICK game fix, and
the forms above whose result depends on what microVU's allocator held: a
block that holds one of them has no plan, and a program that reaches it
stops the run with an error that names it. An encoding microVU knows
nothing of is carried out as microVU carries it out, which is not at all
(it generates no code for one, and ends the block, and with it the
program, after one in the upper slot that is not in a delay slot, but for
the lower NOP's word there, which it lets pass); the host says after a run
how many the blocks it analysed held.

**What is not reproduced.** microVU throws its record away when its 64 MB
of host code are full (`mVUreset`), which the provider cannot know; a
run long enough to fill it could part from the stock run there (the level
run fills 43.5 MB). The same swap of two NaNs in VU0's macro mode (COP2
instructions in EE code): there the registers are the EE recompiler's,
which compiled EE code does not have, so an addition or multiplication of
two NaNs there can give the other NaN than stock PCSX2's; no run has shown
it.

**How it is held to microVU.** The self-test (`host/selftest_vu.cpp`) runs
random micro programs on microVU and on the provider and compares
everything the VU holds afterwards, its cycle count included: on 22,000 of
them, in five settings for each VU and a sixth for VU1 (as on PCSX2's VU
thread, below), nothing differs. `selftest/vu-compiled` runs the
first 150 programs of each VU and setting with every block compiled ahead
of time, and fails if one had to be interpreted; without that capture in
the build it is skipped, not passed
(`scripts\windows\vu-selftest.ps1` captures and builds them, on Linux
`scripts/linux/vu-selftest.sh`; the capture is in the build tree). The test
programs and the game are held to the stock
run with the VUs on the provider (`same-as-stock`, VU0's and VU1's
registers at every sync point), and to the run with everything
interpreted, the micro code too (`lockstep`).

**VU1 on PCSX2's VU thread** (ADR 0051). PCSX2 can carry VU1 out on a
thread of its own (its "MTVU" setting, on by default where the computer has
three processor cores or more). The thread, the queue the main thread
fills for it (`VU_Thread`, `pcsx2/MTVU.cpp`: a program to run with the
VIF's TOP and ITOP and FBRST as they are then, what is written into VU1's
memories, an unpack) and the places where the main thread waits for VU1 are
PCSX2's core; the thread calls whichever VU1 provider is installed. So
PS2Native's provider runs there as microVU does, and what it does
differently there is what microVU's code does differently where it has
`THREAD_VU1` (`OnVuThread` in `runtime/vu_interp.cpp`):

- a run is carried out when it is asked for, whatever VU0's status
  register says of VU1;
- where a program ends, pauses or stops at a T bit, VU0's status register
  is left alone and VU1's interrupt is not raised: the main thread is told
  (`VU_Thread::mtvuInterrupts`) and does both;
- XTOP, XITOP and a pair with the T bit read the thread's own copies of
  TOP, ITOP and FBRST: compiled micro code reads all three through pointers
  that whoever runs the program sets (`vu::Ctx`), so that one compiled
  block serves both ways, and a block's plan is the same;
- the floating-point control register is set at every run's start and put
  back at its end;
- the EE's cycles are left alone at a run's end;
- VU0, where it reaches VU1's registers, waits for the thread first.

Where the thread is on is decided as in PCSX2: by the settings where a game
is played in a window (PCSX2's default for the computer, a game's own
setting over it, PCSX2's game database over both). Every other run has VU1
on the CPU thread unless `ps2n-host -pcsx2-vu-thread on` asks otherwise: a
run with the thread on need not repeat (a game can depend on when VU1's
work is done: Agent Under Fire's run does, and the same input gives it
another machine each time), VU1's registers are not the main thread's to
read while the thread works, and so nothing is compared with such a run
sync point by sync point. What the host decides so holds over a game's own
settings file, which the core reads over the host's settings, and again
when the game's executable starts: each time the core has read its
settings, the host puts its decision into the configuration and into the
settings as the core holds them in memory (`OnSettingsLoaded` in
`host/main.cpp`). The setting is decided once for a run, when the game's
disc is read: PCSX2 hears its game database only from the start of a game's
executable, and not while the next is loaded, which would switch the thread
off and on within a run, and PS2Native's change to PCSX2's code
(`patches/pcsx2`) keeps the thread's copy of the VIF's row and column up to
date only while the thread is in use; so a game the database switches the
thread off for has it off from the start, and nothing switches it on while a
game runs. A session written down says when it was played with the
thread on, since a run of it again need not be it then. The provider on the
thread is held to microVU by the self-test's sixth mode for VU1
("as on PCSX2's VU thread": both sides run as the thread would run them,
and what a program's end tells the main thread is compared with the rest),
by the console-recorded VU programs and the picture a test program's VU1
draws (`ps2n-static-vu-thread`), and by sessions run again with it.

## The I/O processor's code

The IOP's code, the BIOS's modules and a game's, is run by a provider of
the core's (`psxCpu`): PCSX2's IOP recompiler as it ships, or PS2Native's
IOP provider (`runtime/iop_provider.h`), which the host puts in its place
in PS2Native's configurations (`ps2n-host -iop-ps2n`; step G12, ADR 0023).
PS2Native's provider carries a block of IOP code out by code compiled
ahead of time where the host has it, and by its interpreter elsewhere;
what it had to interpret it reports and writes down for the next build.
The definitions are in code:
[core/include/ps2n/r3000a.h](../core/include/ps2n/r3000a.h) (the IOP's
instructions as PCSX2's IOP recompiler treats them, and what a block is by
its instructions), [runtime/include/ps2n/iop_ops.h](../runtime/include/ps2n/iop_ops.h)
(the instructions carried out, and how a block ends),
[runtime/include/ps2n/iop_recomp.h](../runtime/include/ps2n/iop_recomp.h)
(the contract compiled IOP code is written against),
[runtime/iop_provider.cpp](../runtime/iop_provider.cpp) (the record of
blocks, the interpreter, the dispatcher) and
[recompiler/src/iop_codegen.cpp](../recompiler/src/iop_codegen.cpp) (what is
generated).

**It follows PCSX2's IOP recompiler, not the console** (ADR 0005), in its
results and in its time (ADR 0011). The core gives the IOP its time in
stretches, from the EE's event test, and takes back what the IOP did not
use; the IOP's interrupts, its answers to the EE and its sound all come at
the IOP's cycle counts. That recompiler counts time by the blocks it
compiles, as PCSX2's EE recompiler does, and what a block is was fixed
when it first compiled it. So the provider keeps the record that
recompiler keeps, without the code: `Form()` is its `iopRecRecompile()`
carried out on paper, `Clear()` its `recClearIOP()`, `Reset()` its
`recResetIOP()`.

### What a block of the IOP's is

The rules, each transcribed from PCSX2 v2.9.170 and the same in v2.9.189
(`pcsx2/x86/iR3000A.cpp`, `iR3000Atables.cpp`, `BaseblockEx.h`):

1. **Where it ends.** From the block's first instruction the recompiler
   looks ahead and stops after a branch or jump and its delay slot; at an
   address where a block exists already; and, if a conditional branch goes
   back to a place inside the block, at that place. Nothing else ends one:
   not `SYSCALL`, not a page boundary, not its length.
2. **What it costs.** A cycle for each instruction, seven more for a
   multiplication and forty more for a division. The EE's clock is charged
   eight for each of the IOP's.
3. **A block that ends at a branch or jump** adds its cycles and charges
   the EE's clock. If that leaves the stretch nothing, the IOP stops there
   until it is given time again. Otherwise an event test follows if the
   cycle count has reached the next event, and an interrupt taken there
   sends the IOP elsewhere.
4. **A block that was cut short** (at another block, or at its own
   branch's target) adds its cycles, charges them and runs on into the
   next block: nothing is tested, though the stretch be used up.
5. **Wait loops.** A block whose branch goes back to its own first
   instruction and which holds nothing else but NOPs lets time pass when
   that branch is taken: to the next event, or as far as the EE's clock
   lets it; and unless that ends the stretch an event test follows whatever
   the time. The kernel's idle thread is such a loop. PCSX2's `WaitLoop`
   setting switches this off; it is read when the block is formed.
6. **`SYSCALL` and `BREAK`** raise their exception and leave the block with
   the cycles of its instructions so far, their own included; nothing is
   tested. (Over the level run the IOP carries out five million of them.)
7. **Time stands still inside a block.** The cycle count, the EE's clock
   and the PC the core has are those of the block's start until the block
   ends: a device a block reads or writes is told the time it began.
8. **A block is what memory held when it was formed.** Its instructions are
   kept, as the recompiler's host code keeps them. A store into the middle
   of a block changes memory and not the block. A write to the address a
   block begins at throws that block away, and with it every block that
   overlaps what is thrown away; a transfer into memory does the same word
   by word. Only below the highest address a block has ended at is anything
   looked at.
9. **Blocks are filed under physical addresses**, RAM and the ROMs being
   mapped at three places each. A block is entered through any of them,
   and what in it depends on its address (where a branch goes, what a call
   returns to) keeps the address it was formed at.
10. **A module's imports.** `addiu $zero, $zero, n` is the instruction by
    which an import stub names its function. When a block with one is
    formed, the table of imports the stub belongs to is looked for before
    it in memory, and if the core has a function of its own for that
    library's import (the kernel's `Kprintf`; file access on `host:`; the
    registering of a library, which the core's debugger listens to), that
    function is called when the instruction runs. Where it did the import's
    work itself the IOP goes on where it says, and nothing of the block's
    time is counted.
11. **The order of a jump and its delay slot.** The recompiler carries the
    delay slot's instruction out before the jump where it finds the two
    cannot get in each other's way (`psxTrySwapDelaySlot`), and it
    overlooks two cases, which show: after `JALR`, an instruction of the
    arithmetic kind in the delay slot that writes the link register loses
    to the return address, and a move to a coprocessor there reads the link
    register as it was before (`r3000a::swapsDelaySlot`).
12. **The rest of what it does not do as the console**: no exception on
    overflow, none on an address that is not aligned, no delay of a load's
    result; a division by zero gives the dividend as the remainder and 1 or
    -1; a load reads RAM wherever bit 28 of its address is clear; an
    encoding it has no instruction for does nothing and costs a cycle.
13. **Three addresses** at which forming a block does something more: the
    kernel's start (the core's debugger forgets its modules), the place
    where a module the user asked for is put in the kernel's list, and the
    boot code, which is told there how much RAM there is.
14. **The whole record is thrown away** when the core resets its providers
    (a game is started, a setting changes).

### Blocks compiled ahead of time

A run writes down the blocks its interpreter had to carry out, each as it
was formed: its instructions, and for the reader where it was first met
(`ps2n-host -iop-capture`, `game.ps1 -IopCapture`; `ps2n-recomp
iop-capture` puts several runs' files together). `ps2n-recomp iop` writes
one C++ function for each (`build.ps1 -IopCapture`), and the host's
registry lists the images of them it was linked with (`ps2n_iop_images`).

- **A compiled block is found by its instructions**, by a 128-bit
  fingerprint of them (`r3000a::key`), not by its address: when the
  provider forms a block and a build has a function for those
  instructions, the block runs by it. Nothing in the function says where
  the block is: a branch's target and the address a call returns to are
  reckoned from the address the record has for the block (`Block::pc`). So
  the same instructions are carried out by the same function wherever the
  IOP's kernel loaded them, in whichever of its three starts. Instructions
  that hold an address (a call's target, a pointer to a module's data) are
  other instructions at another address; a block with them that a build
  compiled for one place is interpreted at another, and captured.
- **There is no question whether memory still holds the code that was
  compiled**: the record forms a block from what memory holds, and only
  then is a function looked for.
- **The registers are locals.** A block's function takes the general
  registers, HI and LO it reads from the core the first time it reads
  each, works on locals, and puts back what it wrote before anything else
  can look: where the block ends, before `SYSCALL` or `BREAK` (the
  exception's handler reads them), before an import's function of the
  core's and before an instruction of the GTE's (the core's functions read
  and write them; the locals are taken again after). What the core does
  for a load or a store that is not RAM reads none of them, so the locals
  stay across it.
- **Loads and stores of RAM are in the block's code**: a load as the
  recompiler compiles it, a store as the core's function for it makes it,
  with the test whether a block begins at the word written (rule 8) in the
  place of the call that would announce it.
- **Blocks go on to each other**, as the recompiler's blocks jump to each
  other. Each of a block's two ways out remembers the block it led to; it
  is good for as long as no block has been thrown away since
  (`g_generation`). A block calls the next in the place of returning (a
  jump: `[[clang::musttail]]`, under a calling convention with which a
  block saves no registers for its caller), and returns to the provider
  when the stretch is over or the record has no block yet where it goes.
  An interpreted block is entered and left the same way.

**Code no build has compiled** is interpreted, counted and reported: after
a run the host says how many different blocks the IOP ran, how many of
them were compiled and how often those ran, how many were interpreted and
how often. `ps2n-host -iop-static off` has every block interpreted, the
compiled ones too: the reference for them.

**What is not carried out.** The IOP's PlayStation mode (a PlayStation
game on the console); a branch or jump, a `SYSCALL` or a `BREAK` in a delay
slot; an import the core has a function for, in a block longer than its
stub (the recompiler writes the registers back before that call, but not
one whose value the rest of the block never reads, which only its
allocation of host registers says); a block of more than 16,384 instructions;
a jump to an address that is not a multiple of four or where the IOP has
no memory, at which PCSX2 stops the machine too; code run through a mirror
of RAM above its size and then written. A run that reaches one stops with
an error that names it. The game and the test programs reach none. Until
PS2Native moved to PCSX2 v2.9.189 (ADR 0054), a jump through a register
whose delay slot is an unaligned load or store (`LWL`, `LWR`, `SWL`, `SWR`)
or an instruction of the GTE's that the recompiler does not carry out
before the jump (rule 11) was refused too: PCSX2's recompiler lost the
jump's target there until v2.9.183 (it wrote it to the EE's registers and
read it back from the IOP's), and the IOP went on at address 0. Corrected
there, the target is the one taken before the delay slot, as everywhere
else.

**What is not reproduced.** PCSX2's IOP recompiler throws its record away
when its 32 MB of host code are full, which the provider cannot know (the
level run fills 1.85 MB). Its breakpoints and its trace of the kernel's
calls, which a run here never switches on.

**How it is held to PCSX2's IOP recompiler.** The self-test
(`host/selftest_iop.cpp`) runs random IOP programs on that recompiler and
on the provider, in the same stretches of time, and compares after every
stretch where the IOP is, its cycle count, what is left of the EE's clock
and every register, and at the end the memory the program could reach,
its own code included, and the addresses at which each holds a block: on
4,000 programs nothing differs. `selftest/iop-compiled` runs the first
1,500 of them with every block compiled ahead of time,
and fails if one had to be interpreted; without that capture in the build
it is skipped, not passed (`scripts\windows\iop-selftest.ps1` captures and
builds them, on Linux `scripts/linux/iop-selftest.sh`). The test programs
and the game are held to the stock run
with the IOP on the provider (`same-as-stock`: the IOP's PC and cycle
count with the place and time, and its registers, at every sync point),
and to the run with everything interpreted, the IOP's code too
(`lockstep`).
