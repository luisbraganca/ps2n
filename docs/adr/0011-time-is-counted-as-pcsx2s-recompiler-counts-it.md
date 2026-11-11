# ADR 0011: Time is counted as PCSX2's recompiler counts it

| | |
|---|---|
| Status | Accepted; carried out on 2026-11-11, in the interpreter and in generated code |
| Date | 2026-11-11 |
| Decided by | Maintainer, on the session's recommendation |

## Context

A run of Killzone on PS2Native showed the same pictures as stock PCSX2, but
not at the same frame numbers, and nothing said why
([game-log.md](../game-log.md), "G3"). The criterion of that step asks for
every difference from stock PCSX2 to be the same or explained.

The cause, established by comparing a stock run with a PS2Native run at
every moment the core lets the IOP catch up with the EE (`ps2n-host -sync`):
**PCSX2's EE recompiler does not count time by a rule that can be stated
about the program alone.** It counts by the blocks it compiles, and what a
block is depends on what the machine looked like when the block was first
reached:

- a block ends at a branch, but also at a 4 KB page boundary, before the
  target of its own backward branch, and **wherever another block had been
  compiled before it**. Two paths that meet at an instruction make one block
  or two, depending on which ran first;
- what an instruction costs is fixed when its block is compiled, from the
  processor's configuration at that moment;
- cycles are added to the cycle count in the middle of a block at some
  instructions, with a rounding of their own;
- some things are decided from what the block itself has made constant: a
  `FlushCache` call is not made, a read of an EE timer ends the block;
- writing to memory that holds code throws blocks away, and what is compiled
  afterwards is shaped by what exists then.

Each of these moves the cycle count by a cycle or a few. A game that reads
the count register in a loop, as this one does 73,000 times a frame, sees
every one.

PS2Native counted by a simpler rule of its own, the same for compiled and
interpreted code, which no run of stock PCSX2 follows.

## Decision

The maintainer's words: "Timing against stock PCSX2: Should we fix it? If
yes, do it." The session's answer was yes, and this is what "it" was taken
to mean:

**PS2Native counts time exactly as PCSX2's EE recompiler does: by the blocks
that recompiler would have formed, with everything it does about time in
them.** A run on PS2Native is then the stock run, cycle for cycle, and any
difference between the two is a defect to be found, not a property to be
argued about.

- The runtime keeps the record the recompiler keeps: for every address where
  a block would have been compiled, the block as it would have been
  ([runtime/rec_blocks.h](../../runtime/rec_blocks.h)). Nothing in it
  generates code; ADR 0003 stands.
- The interpreter carries out those blocks. Generated code asks at every
  label where the block that begins there ends, and counts the same way
  ([execution-model.md](../execution-model.md) has the rules).
- The test is the comparison that found the cause: a run's record of sync
  points must equal the stock run's, in place, cycle count and registers.
  It is in the test suite as `<suite>/same-as-stock/<program>`.
- PS2Native's own rule is gone.

## Where a run still may differ from the stock run

- **Where PS2Native deliberately does not follow the recompiler**
  ([ADR 0005](0005-never-further-from-the-console-than-pcsx2.md): the
  console is followed where a recording shows the recompiler wrong). A
  program that depends on such an instruction prints something else, and
  from there its time differs. Three of the 63 test programs do, and a
  fourth is one stock PCSX2 crashes on; each is listed with its cause in
  `tests/same-as-stock-known-differences.cmake`.
- **When PCSX2's recompiler runs out of room for compiled code** it throws
  every block away and starts again. When that happens depends on the size
  of the host code it generated, which nothing here models. PCSX2 logs it
  when it does. In 4,200 frames of the game its log shows the two resets at
  start-up that the core asks of any provider, which are reproduced, and no
  other.
- **What the recompiler knows to be constant** is tied to its register
  allocator. The record models the instruction patterns that matter, not
  the allocator. A difference would show in the comparison.
- **VU0's own instructions** (COP2) are still carried out by PCSX2's
  interpreter functions, not as its recompiler computes them. When VU0 is
  brought up to date is reproduced, and since the first level of the game
  showed the need, so is where and how its flags reach their registers; the
  arithmetic is not yet PS2Native's.

## Consequences

- The execution model's "block rule", the contract between generated code
  and the runtime, changed: where a block ends is decided at run time.
  Generated code has to ask. Its checks of code in memory moved from whole
  functions to blocks, because PCSX2 write-protects a page when a block is
  compiled in it and never otherwise, and that has to be reproduced too.
- **A defect of the recompiler is reproduced with it.** Reading the count
  register, PCSX2's recompiler adds to it with a 64-bit addition, and the
  carry goes into the register after it, `EntryHi`, about every fourteen
  seconds. Its interpreter does not do that, and neither does a console.
  PS2Native does (`MFC0` in `ops_cop0.h`), because a run is held to the
  stock run register for register. Should PCSX2 correct it, PS2Native
  follows.
- The runtime follows one version of PCSX2's recompiler closely. Updating
  the pinned PCSX2 means running the comparison again; where the
  recompiler's rules changed, it will say so.
- The record costs memory (a table over RAM and the ROMs, a copy of RAM as
  the recompiler keeps one) and, in generated code, a load and a compare at
  the start of every block. Measured on the game: no difference in run
  time; the generated code a third larger; the host four times slower to
  rebuild after the game's code changed (119 s for 31 s). The last is the
  one to do something about.
- `DI` takes effect one instruction late, as under the recompiler, in
  compiled code too: a program that depends on the console's timing of it
  behaves here as it does under stock PCSX2.
