# ADR 0046: Compiled games at least as fast as stock PCSX2, as users get them

- Date: 2026-11-21
- Status: accepted (the maintainer's choice, below).

## Context

The speed step of ADR 0043 left compiled games, in the configuration users
get (a game's folder, its modules), at 1.06 to 1.19 times stock PCSX2's time
without a profile ([docs/app-log.md](../app-log.md), "Where the step
stands"). The profile a folder counts after a session of its user's (ADR
0045) took 8.0% off Jak and Daxter's time and 3.6% off Gran Turismo 3's. With
it no game had been timed against stock PCSX2, and Agent Under Fire and
Killzone had not been counted at all. The maintainer's words of 2026-11-20,
"Compiled should always be faster. We need to check why and fix it.", were
not met yet.

Asked "What do you suggest working on next?", the session put three things to
the maintainer, recommending the first:

1. 0.2, with a way to bring the game folders of an older version up to date;
2. speed, until compiled games match PCSX2;
3. Phase 4's report, then Phase 5.

The maintainer chose "Speed: match PCSX2".

The profiles of 2026-11-20 (each game's compiled run against stock PCSX2 on
the same session), with what the code around them does, put the extra time:

- **In the game's EE code:**
  - its blocks' starts and ends;
  - what its VU0 instructions and COP2 moves ask the runtime at every
    execution, which PCSX2's recompiler decides when it compiles;
  - its 128-bit instructions, which wait on stores of their registers' low
    halves;
  - its accesses to devices' registers.
- **In the IOP's compiled code:** what each block does on entry and on exit,
  which PCSX2's linked blocks do not.
- **In the VU provider:** what it does to enter and leave each micro program.
  Its search for the program and its jumps cost what microVU's do.

## Decision

1. **The goal.** Every game's sessions at or below stock PCSX2's time in the
   configuration users get: a game's folder with the profile it counts itself
   (ADR 0045).
2. **Measured first, as users get it.** Each game's folder is made from the
   maintainer's captures and counted as a folder counts, on one session of
   the maintainer's. It is then timed against stock PCSX2 on another session,
   in turns, and profiled.
3. **Then the costs the profiles show,** one change at a time, each measured
   and kept only if it gains, in the order the profiles give. The session
   expects this order:
   1. the IOP's block entry and exit;
   2. the COP2 information decided when the code is generated;
   3. the VU provider's entry and exit of a micro program;
   4. accesses to devices;
   5. the 128-bit instructions;
   6. block starts and ends;
   7. the IOP's jump to the next block.
4. **Time is counted as before** (ADR 0011). Every change keeps the run the
   stock run at every sync point, and every recorded session the session at
   every fingerprint. What compiled code is told when it is generated (the
   COP2 information) is compared with the runtime's own record of the block
   before compiled code runs it; a block where the two differ is interpreted
   and reported.
5. **Put to the maintainer first, each with its measured cost, should a gap
   remain:**
   - VU0's registers kept in the host's registers across instructions;
   - a comparison against constants at every block start, which costs code
     size;
   - a profile-guided build of the runtime itself.
6. **Put to the maintainer with the results:** what becomes of a folder's
   counts once the code it compiles has changed (its round run again after an
   update, or not).
7. **Only at the maintainer's word:** the new programs go into their game
   folders, and anything is committed.

The measurements and the changes are in [docs/app-log.md](../app-log.md),
"Compiled games against PCSX2, as users get them".

## Where it stood the same day, and the maintainer's answers

Measured as users get them, compiled games took 1.06 to 1.11 times stock
PCSX2's time on the maintainer's sessions (Silent Hill 3's start-up, which
interprets what none of its sessions of a few seconds has met, 1.25). Of the
changes of point 3:

- **Kept:**
  - the COP2 information decided when the code is generated: Jak and Daxter
    8.2% less time without counts, Agent Under Fire 1.8%, Killzone 1.6%;
  - the VU provider's entry of a micro program: Jak and Daxter 1.5 to 2%.
- **Tried and taken out,** having gained nothing that shows:
  - accesses to devices asking only where they may fault;
  - the IOP's block exit, and with it the jump to the next block, which a
    folder's counts already make a direct one.
- **Not made:** the 128-bit instructions and the blocks' starts and ends. By
  the profiles each is a fraction of a second on one or two games.

With the two kept: Jak and Daxter 1.04, Gran Turismo 3 1.06, Agent Under Fire
1.08, Killzone 1.05 (not counted: it has no session of 3 minutes). The goal of
point 1 is not reached. What is left is spread over many small costs, and
PS2Native's runtime with PCSX2's core takes 34% to 50% of the processor
thread's time in these runs.

The session put three decisions to the maintainer with these numbers,
recommending yes to the first two and, for the third, the profile-guided
build of the runtime (point 5's third item) before the smaller ones. The
maintainer's answers: "1. Yes", "2. Yes", "3. Ok".

1. **What was done is committed and pushed.**
2. **The new programs go into the maintainer's game folders on this
   computer.** None had counted its profile yet. Each compiles its game whole
   before its next start (what a module and the host share changed, and a
   folder's compile now counts its toolchain in what it compiled from), and
   counts after its next session of 3 minutes, as before.
3. **Next for speed: the runtime itself built with a profile of play,**
   measured first, with its cost (the build in two passes, and a profile
   trained on the maintainer's sessions). It is compared against stock PCSX2
   built without a profile: with a profile-built runtime, stock PCSX2 timed on
   that same runtime would be given the profile too.

Point 6 is still open. No folder has counts that an update could make stale
yet, and users have no way to bring a folder up to date. It is put to the
maintainer with a measurement of what counts taken before an update are still
worth after it.
