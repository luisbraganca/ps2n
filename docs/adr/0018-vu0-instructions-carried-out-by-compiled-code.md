# ADR 0018: VU0's instructions carried out by compiled code

| | |
|---|---|
| Status | Accepted; done (2026-11-12) |
| Date | 2026-11-12 |
| Decided by | Maintainer, choosing among the session's suggestions |

## Context

Step G7 ([ADR 0016](0016-vu0-instructions-with-semantics-of-ps2natives-own.md))
gave the game's VU0 instructions semantics of PS2Native's own, and left the
level run's time where it was: 1.21 times stock PCSX2's. The profile showed
why. The arithmetic is cheap; the way to it is not. Compiled code hands every
VU0 instruction to the runtime (158 million in the level run), which finds
the instruction in its record of the block PCSX2's recompiler would have
formed, does what that block does about VU0 first, and dispatches on the
instruction: about 3 s of the 90 s run. And before each such call compiled
code puts the registers it holds in locals back into the core.

Asked what to work on next, the session recommended taking the game further
by script, after step G8 (the BIOS's EE code compiled, ADR 0017), which
another session had begun in the same working tree. As the second item:
"Make the VU0 instructions cheaper to reach. The compiled code would call
the new semantics directly instead of going through the runtime, which
costs about 3 s of the 90-second level run. That is worth doing only if
speed matters before going further into the game." And: "If you want both
sessions working at once, this one would need its own copy of the
repository and its own build tree."

## Decision

The maintainer's words: "Make the VU0 instructions cheaper to reach. if you
need add a worktree"

How they were read:

- **Step G9, inside Phase 3**: compiled code carries out a VU0 instruction
  itself where the runtime's record of its block says that nothing is to be
  done about VU0 first and no cycles are to be added first; elsewhere it
  hands the instruction to the runtime as before. The semantics are G7's,
  inline, with the instruction's register numbers as constants. Measured on
  the level run and with the profile, as in G6 and G7.
- **In a worktree of its own** (`C:\Users\lb\Documents\ps2n-vu0`, branch
  `vu0-direct`), with a build tree of its own, because the session working
  on G8 edits the same files in the main working tree. The worktree reads
  the PCSX2 and ps2autotests submodules and the dependency package from the
  main tree, through directory junctions, and writes nothing there. The two
  sessions tell each other before timing anything, so that one's load does
  not distort the other's measurement; whichever step is committed second
  is rebased onto the first.

## What does not change

- **How time is counted** ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)):
  the decision is the same record of the same blocks, read through a
  shortcut. The compiled game stays the stock run at every sync point.
- **What the instructions compute** (ADR 0016): the same functions, which
  the self-test holds to PCSX2's recompiler.
- **The rest of COP2**: the transfers (`QMFC2`, `QMTC2`, `CFC2`, `CTC2`,
  `LQC2`, `SQC2`), which read or write the EE's registers or memory, and
  `VCALLMS` and `VCALLMSR` still go through the runtime whole.

## What was done

On 2026-11-12 (the game log, "G9"): compiled code reads the runtime's
answer for a VU0 instruction's block from the runtime's cache
(`PS2N_VU0`, `cop2Info()` in `ps2n/recomp.h`) and, where nothing is to be
done about VU0 first, carries the instruction out itself with the
semantics of `ps2n/ops_vu0.h` inline, keeping the registers it holds in
locals past it: 913 of the executable's 4,690 COP2 instructions. The level
run takes 1.19 times stock PCSX2's time (89.5 s against 74.9 s, timed in
turn), from 1.21 at G7: a gain at the edge of what the measurement shows,
which the profile confirms (the runtime's share 7.5 s from 8.8). The game
is still the stock run and the interpreted run, and every test passes.
