# Phase 4 log: the user application

What Phase 4 does and measures, step by step. The design and the steps are
[phase4-proposal.md](phase4-proposal.md), as the maintainer decided it on
2026-11-16 ([ADR 0028](adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md)).
Every number is from a run on this computer (Windows 11, i5-14600KF), with
its date. Nothing here comes from a game or a BIOS but counts and times.

| Step | Pass criterion | State |
|---|---|---|
| P4.1 The module's cost | Every test passes; the game's time measured against today's build, and the cost put to the maintainer | **Measured** (2026-11-16): every test passes with the compiled code in a module of its own; Agent Under Fire takes the time it takes with the code built into the host (its script 1.17 times stock PCSX2's time against 1.18; the maintainer's second session replayed, 0.98 of the built-in code's time, its fourth mission 0.95); the generated code is 4 to 8% larger. Put to the maintainer, who chose a loader of PS2Native's own if it makes the game faster (below) |
| P4.2 Freestanding generated code | A game module compiled with nothing but `clang`, `lld` and PS2Native's own headers; every test passes; a game is still the stock run | |
| P4.3 The shipped toolchain | A game compiled and run on a computer with nothing else installed (a fresh Windows user, without Visual Studio) | |
| P4.4 The packaged game | Killzone and Agent Under Fire run from their folders, at the console's pace, the same runs as from the development host | |
| P4.5 The wizard | A game packaged from the disc image and the BIOS with every default accepted, without a command line | |
| P4.6 New code in play | A session that meets new code finds it compiled at its next start | |
| P4.7 Linux | On the maintainer's own Linux machine ([ADR 0004](adr/0004-proof-of-concept-environment.md)) | |

## How to run it

The compiled code in a module of its own, in a build tree of its own beside
the usual one, with the same game, BIOS and captures:

```powershell
scripts\windows\build.ps1 -BuildDir build\windows-clang-module -Module on -Game <boot executable> `
    -GameMore '<executable>|<executable>' -GameCapture <file> -Bios <BIOS image> -BiosCapture <file> `
    -VuCapture <file> -IopCapture <file> -Pgo off
scripts\windows\test.ps1 -Bios <BIOS image> -BuildDir build\windows-clang-module
scripts\windows\game.ps1 -Image <disc image> -BuildDir build\windows-clang-module ...
```

The tree remembers `-Module` as it remembers `-Game`; `build.ps1` then
builds `ps2n-game` with `ps2n-host`. Without `-Module on` nothing changes:
the compiled code is built into the host, and the generated code is the same,
byte for byte (below).

## P4.1 The module's cost

The question: what it costs in speed for a game's compiled code to be a
module of its own, loaded by a runtime built without it, as a user's
computer will build it (ADR 0028), before anything else depends on the
answer.

### What was built (2026-11-16)

- **The module.** With `-Module on` (`PS2N_GAME_MODULE`, tests/tests.cmake)
  everything the host held compiled is built into `ps2n-game.dll`: the
  game's EE code (Agent Under Fire's four executables), the BIOS's, the
  micro code, the IOP's code, and the test programs'. It is linked with the
  host's import library and with nothing of the core or the runtime, which
  it reaches through what the host exports. It exports one function, which
  hands the host the lists of compiled code the recompiler writes
  (`runtime/game_module.h`, `runtime/module_entry.cpp`); the host loads the
  module first thing (`LoadGameModule()` in `host/main.cpp`) and refuses to
  start without it.
- **The boundary.** What the generated code reaches outside itself, read
  from its objects' undefined symbols: 7 of PCSX2's variables (the EE's
  registers with the FPU's, the address table, the VUs' registers, the
  settings, the hardware registers, the IOP's registers, the IOP's memory),
  14 of the runtime's, about 35 of the runtime's functions and 19 of
  PCSX2's (the slow ways of the memory accesses, COP0's, the IOP's memory
  and events), and the C runtime's (`memcpy`, the allocator, a few of the
  standard library's, from PCSX2's precompiled header). The runtime's are
  declared `PS2N_API` (`runtime/include/ps2n/module.h`): exported by the
  host, imported by the module. PCSX2's variables are exported under names
  of PS2Native's own, which that header puts in the place of PCSX2's in a
  module, and its functions under their own, reached through the import
  library's thunks. PCSX2 is not changed.
- **The code.** The same generated C++; in a module, each of the core's
  variables is reached through its address in the import table, loaded
  first (`mov rsi, [__imp_...]`, then `[rsi+offset]`), where the host's own
  copy reaches it directly. The first link missed one symbol, the record of
  blocks' `epoch`, whose definition saw only a declaration without
  `PS2N_API`.

### Every test

533 of 533 pass with the compiled code in the module, in 3 minutes, and
533 of 533 in the usual build with the same sources (2026-11-16). In the
`bench` test from the module, the program's compiled code, the BIOS's four
images and 1,944 blocks of IOP code were in use, and no instruction of the
program's own code was interpreted.

Built into the host, the generated code is what it was before the change:
its objects' code is the same size to the byte, and so is the host
(203,372,032 bytes).

### The time

Agent Under Fire's script into its first mission (9,000 frames, nothing
drawn, `tests/games/agent-under-fire/start.pad`), three rounds, each round
stock PCSX2, the code built into the host and the code in the module in
turn, on the processor's fast cores. Times from frame 1; play is the last
2,250 frames, the first 45 seconds in the mission (from frame 6,750):

| | Stock PCSX2 | Built into the host | In a module |
|---|---:|---:|---:|
| From frame 1 | 26.59, 26.77, 26.51 s | 31.22, 31.22, 31.90 s | 31.30, 31.18, 31.20 s |
| Against stock PCSX2 (means) | 1 | 1.18 | 1.17 |
| Its play | 8.18, 8.38, 8.11 s | 9.04, 9.01, 9.52 s | 8.82, 8.95, 8.79 s |
| Against stock PCSX2 (means) | 1 | 1.12 | 1.08 |

Neither compiled run met code no build had compiled: their captures of the
game's code, the micro code and the IOP's code are empty, and those of the
BIOS's alike. Over the whole run the module takes the time the built-in
code takes, within the noise of these runs (about a second over a level
run, game log "G5"); its play came out shorter, which is not counted as a
gain without a cause.

**The maintainer's second session**, replayed (`game.ps1 -Replay`, 105,784
frames, on Vulkan at three times the console's resolution, timed every 500
frames), once in each build, one after the other. Both runs are the session
at every one of its 2,115 fingerprints; both interpreted the same 87
instructions, 24 of them in the game's executable, where compiled code
handed a block over to the interpreter at the same 12 places, and no block
of micro code or IOP code:

| | Built into the host | In a module | In a module / built in |
|---|---:|---:|---:|
| To frame 80,000 | 370.6 s | 365.3 s | 0.986 (median of the 500-frame stretches 0.981) |
| From frame 80,000, the fourth mission | 146.1 s | 139.4 s | 0.954 (0.949) |
| The whole, from frame 1 | 516.7 s | 504.7 s | 0.977 (0.972) |

The fourth mission is where compiled code calls the runtime most often: at
every time round the wait loop of "G18", whose page is not write-protected,
it calls `checkedAnswer()`, a call that from the module goes through the
import table. It took no longer. One run each: that the module came out
shorter everywhere is not counted as a gain without a cause.

**What the module costs**, then: nothing that shows in time, in the run
that calls the runtime least and in the one that calls it most; its
generated code is 4 to 8% larger (below).

### The size

The generated code's machine code (`.text` of its objects), built into the
host and in the module:

| | Built into the host | In the module | |
|---|---:|---:|---:|
| `ACTION.UK` | 46,796,002 | 50,363,765 | +7.6% |
| `DRIVING.ELF` | 37,128,688 | 39,508,077 | +6.4% |
| `MOVIE.ELF` | 4,397,842 | 4,618,697 | +5.0% |
| The boot executable | 1,243,014 | 1,312,981 | +5.6% |
| The BIOS | 1,136,395 | 1,181,701 | +4.0% |
| The IOP's code | 6,113,635 | 6,391,249 | +4.5% |
| The micro code | 1,084,505 | 1,091,957 | +0.7% |

The loads of the import table's addresses. The files: `ps2n-game.dll`
203,023,872 bytes and `ps2n-host.exe` 11,215,872, where the host with the
code built in is 203,372,032.

### What it found for the steps after

- **The module imports from the host by the host's file name**
  (`ps2n-host.exe`). A launcher named after the game (P4.4) needs the
  runtime in a library of its own that the launcher and the module both
  import from, or the module linked against an import library that names
  the launcher, which the wizard can make with `lld` from a list of the
  runtime's exports. For P4.4 to decide.
- **The C runtime and pieces of Microsoft's standard library** are in the
  module only because the generated code includes PCSX2's precompiled
  header; P4.2's freestanding code needs neither.
- **Not tried with the module**: profile-guided optimization's training
  build (`-Pgo generate`) and link-time optimization (the build refuses
  both with `-Module on` for now), and the profiler and `-code-size`,
  which read the host's own debugging information.

### The maintainer's answer

Put to the maintainer on 2026-11-16, with the recommendation to go on to
P4.2 on this design; the other way, a loader of PS2Native's own that places
the module beside the runtime, so that compiled code reaches the core's
variables directly, without the import table. The maintainer: "If the
custom loader makes the game faster, I'd go with the custom loader. If not,
your recommendation." Whether it would is measured next.
