# ADR 0021: Two sessions on step G10

| | |
|---|---|
| Status | Accepted; this session's part done (2026-11-12) |
| Date | 2026-11-12 |
| Decided by | Maintainer |

## Context

The maintainer gave a second session the research that
[ADR 0019](0019-performance-as-the-research-lays-it-out.md) records, with
the words "Let's improve performance!". That session found the first one
(`ps2n-5b`) already at work on it, as step G10, in the worktree
`C:\Users\lb\Documents\ps2n-vu0` (branch `perf`), all of it uncommitted:
startup and play timed apart, profile-guided optimization of the generated
code (play at 1.13 times stock PCSX2's time, from 1.21), and loops that
reach a fixed point. The main working tree was clean, at `9be5956`.

The session put the choice to the maintainer: "Another Claude session
(ps2n-5b, idle since about 18:02) already has this same research as step
G10 ... How should this session proceed?", with three answers: take over
G10 in the worktree (the session's recommendation), work in parallel in the
main tree, or review only and change nothing.

## Decision

The maintainer's words: "Work in parallel in the main tree."

How they were read:

- **The arrangement of G8 and G9**
  ([ADR 0017](0017-bios-ee-code-compiled-as-step-g8.md),
  [ADR 0018](0018-vu0-instructions-carried-out-by-compiled-code.md)): this
  session takes changes the other has not begun, in the main working tree,
  and the other works in its worktree. Each says before any build, test or
  timing, and holds its own while the other times ("go", "done"), so that
  one's load does not distort the other's measurement. Builds may overlap.
- **The division**, agreed between the two sessions by message on
  2026-11-12: the other session keeps the COP2 transfers (`QMFC2`, `QMTC2`,
  `LQC2`, `SQC2`) and `VCALLMS`/`VCALLMSR`, which it had planned, and then
  evaluates link-time optimization. This session takes calls through a
  register made without the runtime's dispatcher; a measurement of how much
  host code each kind of generated line becomes and, from it, smaller slow
  paths in the loads and stores; and the FPU's clamp settings read once per
  function. Each leaves the other's parts of the code alone, and the two
  name the files they touch. In the logs and in `CLAUDE.md` each writes its
  own part; the few sentences both change are merged by whichever commits
  second.
- **One history**: the other session committed what it had (`2912bf4`, "G10
  so far", pushed at the maintainer's request), and this session's tree was
  fast-forwarded to it before any of its code was written.
- **Measured as G6 was**
  ([ADR 0015](0015-a-speed-step-before-phase-3-ends.md)): each change by
  itself, the level run against stock PCSX2 timed in turn, and the profile.
  What would change how time is counted, or what is compared with stock
  PCSX2, goes to the maintainer first.

## What was done

On 2026-11-12, in the main tree (the game log, "G10", "In the main tree",
has the numbers), each change measured by itself without profile-guided
optimization, stock PCSX2 timed in turn:

- **Calls through a register made by compiled code itself**
  (`callSites`, `PS2N_CALL_DYNAMIC`): play from 1.18 to 1.16 times stock
  PCSX2's time, timed in one build with the cache used and left empty
  (`ps2n-host -call-sites off`).
- **How much host code each kind of generated line becomes**
  (`ps2n-host -code-size`): the game's code was 84.2 MB, 84 bytes for each
  of its instructions, most of it the ways seldom taken of loads, stores
  and the starts and ends of blocks.
- **The way a load or store seldom takes made short**: one call of the
  runtime, and no registers put back on it, which nothing there reads and
  which PCSX2's recompiler does not put back there either. The planned form
  (one call, the registers still put back) was measured too, on three of
  the game's files: 3% less code, where leaving the put-backs out makes it
  14%. The game's code is 72.0 MB; play 1.14 times stock PCSX2's time.
- **The FPU's settings read once per function**: no change that shows;
  taken out again.

Every test passes, and the game is still the interpreted run over its first
4,200 frames after each change. With the two changes kept, the game is the
stock run over the whole level script: the same place and time at every one
of 93,221,937 sync points, the same pictures and sound state, and as the
only difference the FPU's at the 964 sync points listed since G4.

Measured together on 2026-11-13, once one history held both (the other
session's work rebased onto `33d0e77`) and with profile-guided
optimization trained for the combined code: the level run takes 1.04 times
stock PCSX2's time, play alone 1.04, still the stock run (the game log,
"G10", "Both sessions' work together").
