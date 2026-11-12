# ADR 0015: A speed step before Phase 3 ends

| | |
|---|---|
| Status | Accepted; in progress |
| Date | 2026-11-12 |
| Decided by | Maintainer, on the session's recommendation |

## Context

On 2026-11-12 every step of Phase 3 was done as far as the first level of
the first game ([game-log.md](../game-log.md)): the compiled game is the
stock run, the maintainer had played it, and step G5 had measured it. What
G5 found:

- Over the scripted run into the first level the compiled game takes 1.35
  times as long as stock PCSX2: 100 s against 74 s.
- The whole difference is in the game's own code. It takes 51 s as compiled
  code with its runtime, and 26 s as the code PCSX2's recompiler generates.
  The VUs, the IOP, the sound and the rest of the core take the same time
  in both.
- A first look at one loop says why: the generated code keeps the
  machine's registers in memory, where every instruction reads and writes
  them, and looks every address up in the core's table twice. PCSX2's
  recompiler does neither.

The brief puts performance last of its five priorities, after
compatibility, the picture, the sound and maintainability. It does not
leave it out: a game that is recompiled ahead of time and runs slower than
under the emulator it is built on has little to show for the work.

The session put the choice to the maintainer: a speed step inside Phase 3
before anything else, or to close the phase where it stood and decide on
Phase 4, leaving speed for later. It recommended the first.

## Decision

The session's recommendation, as it was put: "a speed step inside Phase 3:
change the code generator so the console's registers stay in local
variables within a block and each load or store checks its address once,
measuring every change with the profiler." The maintainer's words: "Yes
agree, proceed".

How they were read:

- **Phase 3 gets one more step, G6: speed**, before the phase ends. Its
  pass criterion is in the game log.
- **It begins with the two changes named**: one look at the address table
  for each load and store, and registers held in local variables within a
  block. What comes after them is decided by what the profile then shows,
  and reported; a change of another kind (to how time is counted, to what
  is compared with stock PCSX2) is put to the maintainer first.
- **Every change is measured**: the level run on the processor's fast
  cores, three rounds, and the profile, before and after. A change that
  gives nothing back is said to give nothing back.

## What does not change

- **The compiled game stays the stock run.** Time is counted as PCSX2's
  recompiler counts it
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)),
  and the comparisons that hold the compiled run to the stock run and to
  the interpreted run stay as they are. A faster run that is another run
  is not what was asked for.
- **Nothing is written for one game.** The changes are to how every
  function is generated.
- **No recompiler for PS2 code** comes in by this door
  ([ADR 0003](0003-no-jit-in-generated-games.md)).

## Consequences

- The contract between generated code and the runtime
  ([execution-model.md](../execution-model.md)) said that a compiled
  function's host frame holds nothing that matters. With registers in
  local variables it holds them for the length of a block, and has to put
  them back wherever the runtime, the interpreter or the core may look at
  a register: before every call out of generated code. The execution model
  is changed with the code.
- The generated code changes for every program, so every test's compiled
  runs are new runs: the whole suite, and the game's comparisons with the
  stock run and with the interpreted run, are made again after each
  change.
