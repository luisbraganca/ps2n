# Phase 4 log: the user application

What Phase 4 does and measures, step by step. The design and the steps are
[phase4-proposal.md](phase4-proposal.md), as the maintainer decided it on
2026-11-16 ([ADR 0028](adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md)).
Every number is from a run on this computer (Windows 11, i5-14600KF), with
its date. Nothing here comes from a game or a BIOS but counts and times.

| Step | Pass criterion | State |
|---|---|---|
| P4.1 The module's cost | Every test passes; the game's time measured against today's build, and the cost put to the maintainer | **Measured** (2026-11-16): every test passes with the compiled code in a module of its own; Agent Under Fire takes the time it takes with the code built into the host (its script 1.17 times stock PCSX2's time against 1.18; the maintainer's second session replayed, 0.98 of the built-in code's time, its fourth mission 0.95); the generated code is 4 to 8% larger. **Done** (2026-11-17): put to the maintainer, who chose a loader of PS2Native's own if it made the game faster; the code it would give, which reaches the core directly, measured slower, so a module reaches the runtime through its import table ([ADR 0029](adr/0029-a-games-module-reaches-the-runtime-through-its-import-table.md)) |
| P4.2 Freestanding generated code | A game module compiled with nothing but `clang`, `lld` and PS2Native's own headers; every test passes; a game is still the stock run | **Done** (2026-11-17): the generated code compiled against PS2Native's description of PCSX2's structures, which the runtime checks against PCSX2's own (636 assertions when it is built, the settings' bits when it starts), with no system header and no standard library, into four modules (the programs' EE code, the BIOS's, the micro code, the IOP's code) linked with the host's import library and nothing else; every test passes (533) in the three build trees; Agent Under Fire's script is the stock run at every sync point from the modules, and the maintainer's sessions replayed are the sessions at every fingerprint; the modules take what P4.1's module took (the script 1.17 times stock PCSX2's time, built in 1.19) |
| P4.3 The shipped toolchain | A game compiled and run on a computer with nothing else installed (a fresh Windows user, without Visual Studio) | **Done** (2026-11-17): the toolchain is LLVM's own 22.1.3 ([ADR 0030](adr/0030-the-wizards-compiler-is-llvms-own-release.md)), whose code is the code the development builds make, byte for byte; a folder of 254 MB (`scripts\windows\kit.ps1`) and one command in it (`make-game.ps1`) compile Agent Under Fire from its disc image and the BIOS (101 seconds here); on the maintainer's Windows 10 laptop, without Visual Studio, in about 8 minutes into the same code, byte for byte, and the game played there for 15 minutes within 3% of the console's pace, a run this computer replays at every fingerprint. The run found the session record missing two things (the patches as applied, the BIOS's ROM images), put right |
| P4.4 The packaged game | Killzone and Agent Under Fire run from their folders, at the console's pace, the same runs as from the development host | **Done** (2026-11-17): a game is a folder that plays it (`make-game.ps1 -Title`), started from a launcher named after the game, which starts the runtime beside it with the folder's settings, without a console, and says why when it cannot play; the sessions written down in the folder. Killzone and Agent Under Fire, started from their launchers, played at the console's pace (50.00 frames a second in play), nothing of their code interpreted; the maintainer's four sessions of Agent Under Fire and three of Killzone's from the development host, run again from the folders, and the launchers' sessions run again by the development host, are the sessions at every fingerprint. Killzone's code needed a module to have its own stack probe (`__chkstk`), which a test of the module tree holds |
| P4.5 The wizard | A game packaged from the disc image and the BIOS with every default accepted, without a command line | **Under way** (2026-11-17): the maintainer's word that it goes through all of PCSX2's settings, and how ([ADR 0032](adr/0032-the-wizard-goes-through-pcsx2s-settings.md)); PCSX2's own per-game settings pages work in `ps2n-wizard`, compiled unchanged, what PS2Native carries out in PCSX2's place fixed; a game's settings are a file in its folder that the runtime reads (`-packaged`), and a session played so is run again at every fingerprint; the five steps, from the disc image to a folder that plays the game, in `PS2Native.exe` at the root of the folder `kit.ps1` makes: Agent Under Fire made with every default accepted in 336 seconds, and played from its folder at the console's pace. The maintainer's own run is next |
| P4.6 New code in play | A session that meets new code finds it compiled at its next start | |
| P4.7 Linux | On the maintainer's own Linux machine ([ADR 0004](adr/0004-proof-of-concept-environment.md)) | |

## How to run it

The compiled code in modules of its own, in a build tree of its own beside
the usual one, with the same game, BIOS and captures:

```powershell
scripts\windows\build.ps1 -BuildDir build\windows-clang-module -Module on -Game <boot executable> `
    -GameMore '<executable>|<executable>' -GameCapture <file> -Bios <BIOS image> -BiosCapture <file> `
    -VuCapture <file> -IopCapture <file> -Pgo off
scripts\windows\test.ps1 -Bios <BIOS image> -BuildDir build\windows-clang-module
scripts\windows\game.ps1 -Image <disc image> -BuildDir build\windows-clang-module ...
```

The tree remembers `-Module` as it remembers `-Game`; `build.ps1` then
builds the four modules (`ps2n-modules`: `ps2n-game`, `ps2n-bios`,
`ps2n-vu`, `ps2n-iop`) with `ps2n-host`. Without `-Module on` the compiled
code is built into the host as before (P4.2, "Every test"). `-Module
imports` compiles the generated code as for a module and links it into the
host all the same, whose linker fills the import table's entries itself:
what reaching the core through the import table costs, alone, the modules'
files apart (below).

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
your recommendation." And: "If it doesn't affect game speed, only compile
speed, I'd also go with your recommendation."

### Would a loader of PS2Native's own make the game faster?

Such a loader would change one thing: compiled code would reach the core's
variables directly, as code built into the host does. Measured alone
(2026-11-17): the same generated code compiled as for a module and linked
into the host all the same (`build.ps1 -BuildDir build\windows-clang-imports
-Module imports`), timed beside the code built into the host as usual. Its
generated code is the module's, to the byte (the sizes above).

The first build of it stopped at the BIOS's first compiled function, at a
call to the start of the host's image: the linker kept a function that only
the import table's entries named, here filled by the linker itself, only
when something else named it too, and dropped the rest. The host now exports
them, as for a module (tests/tests.cmake); then every test passed (533 of
533, as in the usual build and with the module, all three built from the
same sources on 2026-11-17).

| Agent Under Fire | Stock PCSX2 | Built into the host (direct) | The same, through the import table |
|---|---:|---:|---:|
| Its script, from frame 1 | 26.69, 26.33, 26.43 s | 31.21, 31.08, 31.26 s | 30.75, 30.67, 30.77 s |
| Against stock PCSX2 (means) | 1 | 1.18 | 1.16 |
| Its play | 8.20, 8.13, 8.18 s | 9.01, 8.98, 8.99 s | 8.70, 8.72, 8.75 s |
| Against stock PCSX2 (means) | 1 | 1.10 | 1.07 |

| The second session replayed | Built into the host (direct) | Through the import table | |
|---|---:|---:|---:|
| To frame 80,000 | 366.2 s | 361.2 s | 0.987 |
| From frame 80,000, the fourth mission | 146.1 s | 139.4 s | 0.954 |
| The whole, from frame 1 | 512.3 s | 500.6 s | 0.977 |

Both replays are the session at every fingerprint and interpreted the same
87 instructions. The code that reaches the core directly is the slower one,
in every part. The module, timed the same night against the built-in code
once more, came out as before: the session 0.979 of its time, the fourth
mission 0.959, the part before it 0.987; its script runs 31.37, 31.22, 31.07
s against 31.45, 31.44, 30.98, its play 8.85, 8.79, 8.75 s against 9.07,
9.11, 8.92. So the module is as fast as the code that goes through the
import table inside the host: what it gains is not its being a file of its
own.

Why the import table's code is faster is not known. In the fourth mission's
wait loop the two differ only in how the core's register block is addressed
(`[rip+offset]` against `[rbp+offset]`, the address loaded once), the same
instructions otherwise; it may come from where the code falls in memory
rather than from the import table itself. That is for the speed step left
for later.

**The outcome**, by the maintainer's rule: a loader of PS2Native's own
would not make the game faster, so a module reaches the runtime through its
import table, as an ordinary DLL does
([ADR 0029](adr/0029-a-games-module-reaches-the-runtime-through-its-import-table.md)).
P4.1 is done; P4.2 goes on on this design.

## P4.2 Freestanding generated code

The question: can a game's compiled code be built with what the wizard is
to bring, `clang` and `lld`, and PS2Native's own headers, and nothing else
(ADR 0028, decision 1); with the host a runtime built once that loads the
compiled code as modules: the game's, the BIOS's, the micro code's, the
IOP code's.

### What was built (2026-11-17)

- **What compiled code reaches of the core, described by PS2Native**
  (`runtime/include/ps2n/pcsx2_layout.h`): PCSX2's structures that compiled
  code reads or writes, transcribed field for field into a namespace of
  their own: the EE's registers with the FPU's, the vector units' (the
  whole of `VURegs`, since there are two side by side), the IOP's, the
  VIFs' (compiled micro code reads `itop` and `top`), the IOP's memory, the
  address table (of which compiled code reads only where the entries are),
  and of PCSX2's settings the words compiled code reads (the FPU's and
  VU0's clamping, three game fixes, the FPU's two rounding settings), with
  where they are among the settings; the numbers it uses (the VI registers'
  numbers, a bit of a VU's flags, the address table's page size); and the
  types of the 19 functions of the core it calls.
- **PCSX2's names, either way** (`ps2n/pcsx2.h`). In the runtime, built with
  PCSX2's headers, they are PCSX2's own. In a module (`PS2N_MODULE`) they
  are PS2Native's description under PCSX2's names, and the core's
  variables and functions are imported under the symbols PCSX2's compiler
  gives them, which the host exports. The semantics and the generated code
  are the same text either way; the generator is unchanged but for one
  line of the lists it writes (`<stddef.h>` where it was `<cstddef>`).
- **The description checked against PCSX2's** (`runtime/pcsx2_check.cpp`).
  When the runtime is built, 636 assertions compare it with PCSX2's
  headers: the size and alignment of 29 structures, the place and type of
  269 fields, every number, the type of every function and variable
  compiled code reaches. A change to any of them upstream stops the
  runtime's build. The settings are words of bit-fields, whose bits a
  compiler cannot place; the host compares the eight that compiled code
  reads when it starts, with where the VIFs' registers are, and refuses to
  go on if any differs (`ps2n::pcsx2::CheckLayout()`). The check caught its
  own first draft: where the CPU options and the game fixes are among
  PCSX2's settings had been left as guesses, and the runtime's build
  stopped with the true places (4 and 552 bytes in).
- **No standard library in the semantics.** `std::memcpy` and `std::memset`
  became the compiler's own (`__builtin_memcpy`, `__builtin_memset`), which
  is what they were compiled as; the FPU's guard-bit masks a structure of
  their own instead of `std::array`; the MMI's saturation limits a small
  table instead of `std::numeric_limits`; PLZCW's count of leading zeros
  the compiler's builtin instead of `std::countl_zero`, whose version in
  Microsoft's library looks at a variable of the C runtime's
  (`__isa_available`) each time to choose its instruction; two loops over
  braced lists loops over arrays; `<cstdint>` and `<cstddef>`, `<stdint.h>`
  and `<stddef.h>`. What compiled micro code needs of a plan, its steps and
  its key, moved into a header without `std::string`
  (`core/include/ps2n/vu_step.h`).
- **Four modules** (tests/tests.cmake): `ps2n-game` (the programs' EE code:
  the game's four executables, and in this build the test programs'),
  `ps2n-bios`, `ps2n-vu` (the micro code) and `ps2n-iop` (the IOP's code),
  each with a list of its own (`ps2n-recomp registry`). Each is compiled
  with none of the system's include directories (`/X`), freestanding, with
  no library named in its objects (`/Zl`), no security cookies (`/GS-`),
  and nothing of what PCSX2's build gives its own files; code that would
  have to run when a module is loaded is an error
  (`-Werror=global-constructors`), since nothing would run it. Each is
  linked with the host's import library and nothing else (`/NODEFAULTLIB`),
  with no entry point (`/NOENTRY`). The one name the compiler asks of a C
  runtime, `_fltused` (named by any file that does floating-point
  arithmetic), the module defines itself (`runtime/module_entry.cpp`). The
  host loads the four in that order and puts their lists together
  (`LoadGameModules()` in `host/main.cpp`).

### What a module needs

- **To compile:** generated files of each kind compiled outside the
  developer shell, with `-I` to PS2Native's directories, `/X` and
  `-ffreestanding`, include 19 of PS2Native's headers (`runtime/include`,
  `core/include`) and 18 of the compiler's own, 569 KB: `<stdint.h>`,
  `<stddef.h>` and its pieces, and the SSE intrinsics as far as SSE4.1
  (the compiler's whole include folder is 15.6 MB here).
- **To link:** the four linked again by `lld-link` alone, outside the
  developer shell (no `LIB`, no `INCLUDE`), from the objects the build
  links, with the host's import library and nothing else: all four link,
  to within a kilobyte of the build's own files (which also name their
  debugging information).
- **What each imports**, from its import table: from `ps2n-host.exe` and
  nothing else; `ps2n-game` 55 symbols, `ps2n-bios` 52, `ps2n-vu` 4,
  `ps2n-iop` 20.

### Every test

533 of 533 pass in each of the three trees, built from the same sources on
2026-11-17: the compiled code built into the host as usual, in the four
modules, and compiled as for a module and linked into the host
(`-Module imports`).

Built into the host, the EE code's objects (the game's four executables,
the BIOS's, the test programs') came out the same size to the byte. The
IOP code's are 700 bytes larger each, which is not their code: the whole
list of PCSX2's headers that the semantics now include brings two
functions of Microsoft's standard library into each
(`time_get::_Getvals`), which the linker keeps once. The host is 1,536
bytes larger (the check, the loader of four modules, and the session's new
micro code and IOP code, below).

### The game

The maintainer played Agent Under Fire on 2026-11-17, on the build of
P4.1 with the code in the host: 27.6 minutes (82,844 frames, at 49.99
frames a second, where the console shows 50.00). The session met no new EE
code of the game's, and one block each of VU1's micro code and of the
IOP's code that no build had compiled, each run once; both are compiled in
since.

From the modules:

- **Agent Under Fire's script into its first mission** (9,000 frames,
  `tests/games/agent-under-fire/start.pad`), held to stock PCSX2
  (`same-as-stock`): the stock run at every one of its 49,981,658 sync
  points, with the same picture at the frame compared and the sound
  hardware in the same state; nothing of the game's EE code, of its micro
  code or of its IOP code interpreted.
- **The session of 2026-11-17**, replayed: the session at every one of its
  1,656 fingerprints, the sound at its last frame as it was in play;
  nothing of its micro code or IOP code interpreted.
- **The maintainer's second session**, replayed (below): the session at
  every one of its 2,115 fingerprints, as from the host.

### The time

As P4.1 timed it (2026-11-17, every run fully compiled). Agent Under Fire's
script, three rounds, stock PCSX2, the code built into the host and the
code in the modules in turn, on the fast cores; times from frame 1, and
its play, the last 2,250 frames:

| | Stock PCSX2 | Built into the host | In the modules |
|---|---:|---:|---:|
| From frame 1 | 26.27, 26.43, 26.30 s | 31.37, 31.24, 31.08 s | 30.82, 30.74, 30.54 s |
| Against stock PCSX2 (means) | 1 | 1.19 | 1.17 |
| Its play | 8.16, 8.14, 8.13 s | 8.99, 8.99, 8.94 s | 8.82, 8.75, 8.67 s |
| Against stock PCSX2 (means) | 1 | 1.10 | 1.07 |

The maintainer's second session replayed (105,784 frames, on Vulkan at
three times the console's resolution, timed every 500 frames):

| | Built into the host | In the modules | In the modules / built in |
|---|---:|---:|---:|
| To frame 80,000 | 364.2 s | 359.7 s | 0.988 (median of the 500-frame stretches 0.986) |
| From frame 80,000, the fourth mission | 143.4 s | 139.1 s | 0.970 (0.967) |
| The whole, from frame 1 | 507.6 s | 498.9 s | 0.983 (0.978) |

What P4.1 measured, then: the modules take no longer than the code built
into the host, as P4.1's one module did (0.977 and 0.979 of its time over
the session), and as the code reached through the import table did inside
the host (ADR 0029).

### The size

Machine code of the generated objects (`.text`), built into the host and
in the modules, from the same sources:

| | Built into the host | In the modules | |
|---|---:|---:|---:|
| `ACTION.UK` | 46,796,702 | 50,360,618 | +7.6% |
| `DRIVING.ELF` | 37,129,388 | 39,524,227 | +6.4% |
| `MOVIE.ELF` | 4,398,542 | 4,620,251 | +5.0% |
| The boot executable | 1,244,414 | 1,272,679 | +2.3% |
| The test programs | 71,831,164 | 75,978,540 | +5.8% |
| The BIOS | 1,136,395 | 1,179,403 | +3.8% |
| The IOP's code, the game's and the self-test's | 6,125,739 and 2,854,835 | 6,431,675 and 2,957,567 | +5.0% and +3.6% |
| The micro code, the game's and the self-test's | 1,086,408 and 902,332 | 1,081,967 and 897,226 | -0.4% and -0.6% |

The loads of the import table's addresses, as in P4.1; the game's code in
the modules is what it was in P4.1's module (`ACTION.UK` 50,363,765 bytes
then): PS2Native's description of the core gives the code PCSX2's own
headers gave. The files: `ps2n-game.dll` 189,061,632 bytes,
`ps2n-bios.dll` 1,328,128, `ps2n-vu.dll` 2,119,168, `ps2n-iop.dll`
10,037,760, together 202,546,688 where P4.1's one module was 203,023,872;
`ps2n-host.exe` 11,218,432.

### What it found for the steps after

- **P4.3 has its list**: what a module needs of the compiler is `clang`
  (as `clang-cl`), `lld-link` and those 18 headers; of PS2Native, its 19
  headers and the runtime's import library.
- **The symbols a module imports PCSX2's variables and functions under are
  Windows's** (the names Microsoft's ABI gives them, in `ps2n/pcsx2.h`):
  P4.7 needs Linux's beside them.
- **The test programs' EE code is in `ps2n-game`** in this build; a
  packaged game's holds only the game's (P4.4), as the modules still import
  from `ps2n-host.exe` by name (P4.1).
- **Not tried with the modules**, as with P4.1's: profile-guided
  optimization's training build and link-time optimization.

## P4.3 The shipped toolchain

The question: can a game be compiled and played on a computer with nothing
else installed, from its disc image and a BIOS image, with what PS2Native
ships. The maintainer chose the compiler, LLVM's own release, and where the
step's criterion is checked, another computer of theirs without Visual
Studio ([ADR 0030](adr/0030-the-wizards-compiler-is-llvms-own-release.md)).

### What was built (2026-11-17)

- **The compiler**: LLVM's own 22.1.3 for Windows on x64, its archive
  (861,872,156 bytes) fetched from LLVM's release into `deps` and checked
  against the SHA-256 the release publishes, and of it `clang-cl` (108 MB),
  `lld-link` (72 MB) and the compiler's headers. Both programs need nothing
  but Windows's own libraries. Their version is the one Visual Studio
  brings, from the same commit of LLVM's (`e9846648fd61`): the same
  generated files compiled by both, with the same flags, gave the same
  objects, every section byte for byte. LLVM's licence, which the archive
  does not carry, is taken from its source at the same tag.
- **A game's executables from its disc image**: `ps2n-recomp extract`
  (`core/include/ps2n/iso9660.h`, with a unit test on a small image built
  in memory) reads the disc's ISO 9660 file system a sector at a time and
  writes out the files named, or the executable `SYSTEM.CNF` boots. From
  Agent Under Fire's image, its four executables came out the same, byte
  for byte, as the copies extracted before, in a tenth of a second.
- **The folder** (`scripts\windows\kit.ps1`, into `build\kit`): the runtime
  (`ps2n-host.exe` from a tree built with `-Module on`, the libraries of
  PCSX2's it imports, and theirs, the Vulkan renderer's shader compiler,
  which PCSX2 loads while it runs, Microsoft's C++ runtime, and PCSX2's
  resources: 55.6 MB); the toolchain (`clang-cl`, `lld-link`, the
  compiler's headers, `ps2n-recomp`, PS2Native's headers the generated code
  includes, the runtime's import library: 198.5 MB); the licences; and the
  one command, `make-game.ps1`. 254 MB, nothing in it from a game or a
  BIOS. `kit.ps1` fetches LLVM's archive when `deps` has not got it.
- **The command** (`make-game.ps1 -Image <disc image> -Bios <BIOS image>`;
  for Agent Under Fire also `-More MOVIE.ELF,ACTION.UK,DRIVING.ELF`; with
  `-Captures <folder>` what earlier runs had to interpret): it takes the
  game's executables from the disc image, translates them, the BIOS's code
  and the captured micro code and IOP code to C++ as the development build
  does, compiles the files with `clang-cl`, as many at once as the
  computer has processors, links the four modules with `lld-link` into
  `games\<name>` beside a copy of the runtime, and with `-Play` opens the
  game in a window, as `game.ps1 -Play` does: the session written down to
  be run again, and what it had to interpret added to the game's own
  captures, which the next compile takes.

The Microsoft C++ runtime's libraries come from Visual Studio's files
meant for redistribution; PCSX2's own libraries need them, so linking the
host with the C runtime built in would not do without them. They are in
the folder for the maintainer's own computers; whether a packaged game
given to others may carry them is put to the maintainer at P4.4.

### On this computer

`make-game.ps1` run from `build\kit` in a process whose `PATH` holds only
Windows's own folders, with Visual Studio's `LIB`, `INCLUDE` and `LIBPATH`
unset, from Agent Under Fire's disc image, the BIOS and the captures of the
maintainer's sessions: 101 seconds, of which 12 to translate, 86 to compile
205 files 20 at a time, 3 to link. The modules: `ps2n-game.dll` 104,018,432
bytes (the game's code alone), `ps2n-bios.dll` 1,327,104, `ps2n-vu.dll`
1,139,200, `ps2n-iop.dll` 6,871,552.

- **The same code**: the 197 objects of generated code it compiled are,
  in every code section, the bytes the development build's module tree
  compiled from the same files.
- **It runs**: the host started from the game's folder with nothing but
  Windows on the path finds every library it imports, loads the four
  modules and passes the check of PS2Native's description of PCSX2's
  structures.
- **It plays the game**: the maintainer's session of 2026-11-17 replayed
  from the game's folder (its host, its modules, its libraries) is the
  session at every one of its 1,656 fingerprints, the sound at its last
  frame as it was in play; nothing of its micro code or IOP code was
  interpreted.
- **It plays in a window**: `make-game.ps1 -PlayOnly` from the folder,
  again with nothing but Windows on the path, muted and stopped after 900
  frames for the check: the game on Vulkan in its window, with its
  widescreen patch from the installed PCSX2's archive, 49.07 frames a
  second over those 18 seconds of its start (the console shows 50.00); the
  session written down, and what it had to interpret added to the game's
  captures.

### On another computer of the maintainer's

The folder `build\p43\for-another-computer` (the kit, made again without a
game in it, and beside it the captures of the maintainer's sessions) was
run by the maintainer on 2026-11-17 on the only other Windows computer they
have ("It's the only windows machine I have besides this one"): a laptop
with Windows 10, an Intel Core i7-7700HQ (4 cores, 8 threads), 16 GB of
memory and an NVIDIA GeForce GTX 1050 (Vulkan 1.3), without Visual Studio,
the folder, the disc image and the BIOS image on a USB stick.

- **It compiled the game**, in about 8 minutes (the times of the files it
  wrote): the 205 objects it compiled are, in every code section, the bytes
  this computer's compile of the same files gave, and the four modules are
  the same size to the byte.
- **It played the game**: 44,249 frames (14 minutes 45 seconds of the
  game's) in at most 15 minutes 14 seconds (from the session's folder being
  made to the console's settings being saved as the window closed): within
  3% of the console's pace over the whole session, its start included.
- **It was the same run**: the session, replayed here from the same
  modules, is the session at every one of its 884 fingerprints, once the
  BIOS image here had nothing beside it, as on the laptop.

P4.3's criterion is met. The run found two things wrong, both put right
since (below):

- **The patches.** The laptop has no PCSX2, and so no archive of its
  patches: the game played as its disc has it, in 4:3, without the
  widescreen patch the window asks for (the maintainer: "btw it wasn't
  1920x1080"), and the session was written down as played with the patches
  asked for, which a replay elsewhere would have applied. Now, without an
  archive, the host plays without them, says so, and writes the session
  down as played with none; a run told to apply them, or a session played
  with them run again, stops instead of being another run.
  `make-game.ps1 -PatchArchive <file>` gives an archive where PCSX2 is not
  installed (the one in the resources folder of PCSX2's release, ADR 0028;
  PS2Native ships none).
- **What the machine starts from.** The images the core loads from beside
  the BIOS image, ROM1 and ROM2, make it another machine: with them beside
  the BIOS image here, the replay parted from the session at its first
  fingerprint (frame 50, a second in). A session now writes down which it
  had (`bios-roms`), a replay is given those (`tests/guest_test.cmake`),
  and the host refuses to run a session again with a BIOS that has others.
  `make-game.ps1` also keeps the console's NVRAM beside a session, as
  `game.ps1 -Play` does, which the laptop's session, its first, did not
  need.

## P4.4 The packaged game

The question: can a game be played from a folder of its own, started as any
program is started, and be there the game it is from the development host:
Killzone and Agent Under Fire at the console's pace, the same runs. Microsoft's
C++ runtime travels in the folder, the maintainer's choice at the step's start
([ADR 0031](adr/0031-microsofts-c-runtime-travels-in-the-folder.md)).

### What was built (2026-11-17)

- **The game's folder** (`make-game.ps1 -Title <name>`, into
  `games\<name>`): the four modules, the runtime as the kit has it, the
  game's launcher, `<name>.exe`, and `settings.txt`, what the launcher
  starts the runtime with, one option to a line: the disc image and the
  BIOS image, read where they are (the BIOS image linked into the folder
  with the ROM1, ROM2 and EROM images beside it, where it can be, so that
  the console's settings are kept there), the folder's own places for the
  rest, the archive of PCSX2's patches an installed PCSX2 has, the
  window's title; `%GAME%` stands for the folder. In the folder too: the
  game's memory cards and the console's settings (`data`), every session
  played (`sessions`), and `last-run.log`, what the runtime said the last
  time. `-PlayOnly` writes the settings again, with another `-Renderer` or
  `-PatchArchive`.
- **The launcher** (`host/launcher.cpp`; `ps2n-launcher.exe` in the kit's
  runtime, copied under the game's name): a Windows program without a
  console, with Microsoft's C runtime built in, so that it imports from
  `KERNEL32`, `USER32` and `SHELL32` alone and can say what is missing when
  the rest of the folder is not whole. It starts the runtime beside it,
  `ps2n-host.exe -settings settings.txt` and whatever it was given itself,
  without a console window, keeps what the runtime wrote in
  `last-run.log`, and, when the runtime could not play the game (its exit
  codes 1 and 4), says so in a message box with the runtime's own last
  lines.
- **What the runtime does for it** (`host/main.cpp`): `-settings <file>`
  reads options from a file in its place, one to a line, `#` lines left
  out, `%GAME%` the file's folder; started with no options at all, the
  runtime reads `settings.txt` beside it. `-sessions <folder>` writes each
  session down in a folder of its own there, named after the time it
  began, with the memory cards and the console's NVRAM as they were and
  what the session had to interpret: what `game.ps1 -Play` and
  `make-game.ps1 -Play` did around the host, done by the host, which in a
  game's folder nothing else starts. `-title` names the window. An archive
  of patches named that is not there counts as none: the game then plays
  as its disc has it, as on a computer without PCSX2 (P4.3).
- **A packaged game run as it is**: `game.ps1 -Runtime
  <folder>\ps2n-host.exe` puts a game folder's runtime, with its modules,
  in the place of the build tree's, for any run, a session run again among
  them.
- **The stack probe.** Killzone's folder did not link at first:
  `__chkstk` was missing, which one of its functions calls
  (`fn_003E73F0`, whose locals take 4,256 bytes, more than a page). A
  function whose locals take more than a page of the stack calls it before
  it moves the stack pointer down, so that each page is touched in order:
  Windows commits a thread's stack a page at a time, as the page below the
  last is touched. Microsoft's C runtime has it, and a module has no C
  runtime (P4.2, which found `_fltused` the one name a module's code asks
  of one: Agent Under Fire's code has no function that large). A module
  now defines its own beside `_fltused` (`runtime/module_entry.cpp`): it
  touches a byte in each page from the caller's stack pointer down to the
  size asked, and changes no register but R10, R11 and the flags. No test
  program's code calls it, so the module tree has a test of its own,
  `selftest/module-stack-probe` (`tests/module/stack_probe.cpp`): built as
  a module is, it runs functions whose locals take one to fifty pages and a
  bit on a thread whose stack Windows commits a page at a time. With the
  module's `__chkstk` the thread's stack grows from 2 pages to 51 and every
  sum is right; with a `__chkstk` that does nothing, the program stops at
  an access violation.

### The two games from their folders

- **Made** from the kit with the captures the development tree is built
  from (Agent Under Fire's in `build\game\aut`, Killzone's in
  `build\game\killzone`, the BIOS's in `build\bios`): Agent Under Fire in
  99 seconds (205 files compiled, 20 at a time), Killzone in 108 (112
  files). A folder takes 172.4 MB (Agent Under Fire) and 147.3 MB
  (Killzone) without its sessions, its data and the BIOS image's link: the
  runtime, 55.7 MB with the launcher, and the four modules, 113.4 and
  90.0 MB. The generated code and objects (`work`, which can be deleted)
  take 382 and 325 MB more.

| | Agent Under Fire | Killzone |
|---|---:|---:|
| `ps2n-game.dll`, the game's EE code | 104,018,432 bytes | 83,660,800 bytes |
| `ps2n-bios.dll`, the BIOS's | 1,327,104 | 1,327,104 |
| `ps2n-vu.dll`, the micro code | 1,139,200 | 394,240 |
| `ps2n-iop.dll`, the IOP's code | 6,871,552 | 4,603,392 |

- **Started from their launchers** as Explorer starts a program, in the
  folder, given only what the check needed: no sound, a window of 1280 by
  720, the controller script the window's pace was measured with in Phase
  3, and a frame to stop at. Each time the launcher started the runtime
  with the folder's settings; one window opened, the game's, titled with
  the game's name, and no console window; the runtime played the game with
  its patches from the installed PCSX2's archive (Agent Under Fire's
  widescreen patch; Killzone's widescreen and frame-rate patches, with the
  130% EE overclock the latter asks for), interpreted nothing of the game's
  EE code, micro code or IOP code (of the EE's instructions, as on the
  development host, the BIOS's 51 at boot, and in Agent Under Fire 12
  where compiled code hands the rest of a block over on purpose, step
  G18), and wrote the session down in the folder; the launcher gave back
  the runtime's exit code.

| | Agent Under Fire | Killzone |
|---|---|---|
| Controller script | into its first mission (`start.pad`, step G18) | into its first level (`level1.pad`, step G16) |
| The whole session | 8,999 frames in 180.0 s: 49.96 a second | 9,299 frames in 186.2 s: 49.92 a second |
| Its play | frames 6,750 to 9,000 in 45.000 s: 50.00 a second | frames 6,600 to 9,300 in 54.000 s: 50.00 a second |

The console shows 50.00 frames a second; the whole session has its start
in it, the runtime loading and the console booting.

- **The same runs.** The sessions the development host wrote down, run
  again from the folders (`game.ps1 -Replay -Runtime`, on Vulkan at three
  times the console's resolution, as they were played), and the sessions
  the launchers wrote down, run again by the development host:

| Session | Written down by | Run again by | Frames | The session at every fingerprint |
|---|---|---|---:|---:|
| The maintainer's first (2026-11-16) | the development host | Agent Under Fire's folder | 42,322 | 846 of 846 |
| The maintainer's second (2026-11-16) | the development host | Agent Under Fire's folder | 105,784 | 2,115 of 2,115 |
| The maintainer's third (2026-11-16) | the development host | Agent Under Fire's folder | 68,497 | 1,369 of 1,369 |
| The maintainer's of 2026-11-17 | the development host | Agent Under Fire's folder | 82,844 | 1,656 of 1,656 |
| Killzone's start, with both patches, with the EE overclock and without (G17, two sessions) | the development host | Killzone's folder | 2,000 each | 40 of 40 each |
| Killzone's level script in the window (G17; given the patches it was played with, which its record is older than) | the development host | Killzone's folder | 9,300 | 186 of 186 |
| Agent Under Fire's, above | its launcher | the development host | 9,000 | 180 of 180 |
| Killzone's, above | its launcher | the development host | 9,300 | 186 of 186 |

The development host ran Killzone's with the game's EE code interpreted,
its build holding Agent Under Fire's; the run ends on the same picture and
the same sound as the development host's session of the same script two
days before (`g17-win`), which Killzone's folder ran again too.
- **One block of micro code** was interpreted in the folder's runs of the
  maintainer's first two sessions, and in no run of the development
  host's: VU1's block at `0x0D98`, two instruction pairs that end a
  program, run 3,199 and 5,507 times, the run still the session at every
  fingerprint. The development host holds it from the VU self-test's
  capture, which has a random program with the same plan, so no run of the
  game there ever had to interpret it, and none of the game's captures had
  it; the folder is made from the game's captures alone. It is in Agent
  Under Fire's capture now (`build\game\aut\vu-capture.toml`, 1,625 of
  VU1's blocks), which the development trees compile too, and the first
  session, run again from the folder made with it, interpreted none of its
  micro code and is the session at every one of its 846 fingerprints. A
  game's captures made on the development host can miss what the host
  holds from elsewhere; a packaged game's own sessions write it down, for
  the next compile (P4.6 does that compile by itself).
- **Moved**: Killzone's folder copied elsewhere
  (`build\p44\moved\Killzone`, without its generated code and its
  sessions) and started from its launcher there: the runtime ran from the
  copy, with the copy's settings, BIOS, data and sessions, 600 frames at
  49.66 a second with its start, and wrote the session down in the copy's
  `sessions`, with the memory cards and the console's NVRAM as they were.
  The settings name the disc image and the archive of patches where they
  are; the rest is the folder's.
- **When it cannot play**, the launcher says so with what the runtime
  said: given an option the runtime does not take, its message box read
  "ps2n-host: unknown option '-no-such-option', or its value is missing"
  (the runtime's exit code 4; the first version showed the end of the help
  that follows that line, and now shows the runtime's own last lines);
  given a BIOS image that is not there, "ps2n-host: BIOS image not found:
  C:\no such folder\SCPH-70004.BIN" (exit code 1). That start left a
  session's folder behind, with copies of the memory cards and no record:
  the runtime made it while it read its options. It now makes it once the
  machine can be set up (`MakeSessionFolder()`): the same start leaves
  none, and a session played still gets its folder, with the memory cards,
  the console's NVRAM and the record.
- **Every test passes**: 533 in the usual tree and in the imports tree,
  534 in the module tree (`selftest/module-stack-probe` the one more).

P4.4's criterion is met: Killzone and Agent Under Fire run from their
folders, at the console's pace, the same runs as from the development host.

### What it leaves for the steps after

- A folder names the disc image and PCSX2's archive of patches where they
  were when it was made; taken to another computer, its `settings.txt`
  needs them where they are there, or `make-game.ps1` run there. The
  wizard (P4.5) writes the same folder and settings.
- What a session in the folder had to interpret is written down in it,
  and the next `make-game.ps1` compiles it; compiling it by itself in the
  background, for the game's next start, is P4.6.

## P4.5 The wizard

The question: can a game be packaged from its disc image and the BIOS by an
application with every default accepted, without a command line. At the
step's start the maintainer said what its settings step is: "the wizard
should go through all the PCSX2 settings", PCSX2's defaults but for the
window's, and chose, on the session's recommendations, how
([ADR 0032](adr/0032-the-wizard-goes-through-pcsx2s-settings.md)): every
setting of PCSX2's that applies to a game; those that change how the
processors are emulated shown but fixed at what PS2Native follows; the
core's on-screen display not offered (ADR 0012); PCSX2's own per-game
settings pages inside the wizard if they build into it without changing
PCSX2; PCSX2's program settings, hotkeys and RetroAchievements left out.

### What has been built so far (2026-11-17)

- **PCSX2's own settings pages in a wizard of PS2Native's**
  (`ps2n-wizard`, `wizard/`, `build.ps1 -Wizard on`, Qt 6.12 from PCSX2's
  dependency package): the pages a game has (Summary, Emulation, Patches,
  Cheats, Game Fixes, Graphics, Audio, Memory Cards, Network & HDD,
  Advanced) and the dialogs they open, compiled from the submodule
  unchanged. Compiling PCSX2's own settings window would have brought in
  every page, and with the pages left out (Interface, Debug, Hotkeys) its
  updater, its debugger and its controller windows; so the window that
  holds the pages is PS2Native's (`wizard/settings_window.cpp`: PCSX2's
  `SettingsWindow` class, from its header, implemented from PCSX2's own
  file with only the pages a game has). What the pages call of the rest of
  PCSX2's frontend, where no machine runs, is PS2Native's too: the
  machine's thread told that a setting changed, a game list rescanned,
  three of `QtHost`'s helpers (`wizard/stand_ins.cpp`), and a
  `MainWindow.h` of the wizard's own, found before PCSX2's
  (`wizard/pcsx2-qt`), which declares the two things the pages use of it;
  and the callbacks PCSX2's core asks of a frontend
  (`wizard/host_interface.cpp`, the runtime's list with nothing to show a
  game in). The wizard recognizes a game from its disc image with PCSX2's
  own code and databases (`GameList::PopulateEntryFromPath`): Killzone,
  `SCES-52004`, CRC `6624A78C`, PAL-M, "Playable" with four stars. The
  pages look and work as in PCSX2; their category icons are not there yet
  (PCSX2's icon theme is not loaded).
- **A game's settings are a file in its folder.** The pages write what
  is changed into the game's own settings file, where PCSX2's core loads
  it as the game's layer over the defaults when it reads the disc
  (`data\PCSX2\gamesettings\<serial>_<crc>.ini`). So that a default
  accepted in the wizard is the one the game is played with, the defaults
  the pages show and the runtime's are one definition
  (`host/game_settings.h`, `ps2n::game::SetDefaults`: PCSX2's, with
  Vulkan, three times the console's resolution, the widescreen patch with
  the films at 4:3, none of the on-screen display, none of PCSX2's
  hotkeys). The game's patch file is taken from PCSX2's archive into the
  folder (as ADR 0028 has it: the game keeps the one patch file it uses),
  and the settings file is made the first time with the frame-rate group
  enabled and the EE overclock the groups applied ask for, by the
  runtime's own rules, now shared (`ps2n::game::FrameRateGroup`,
  `OverclockAsked`): for Killzone, "50/60 FPS" and the EE cycle rate at
  130%.
- **The runtime plays such a folder** (`ps2n-host -packaged`): the
  defaults under the game's settings are the wizard's, the patch file is
  read from the folder, and no archive of PCSX2's is needed any more. A
  session played so writes `packaged` in its record and has the game's
  settings file and patch file beside it; a replay puts them in its data
  folder (`tests/guest_test.cmake`) and runs packaged.

Measured, with Killzone's folder given the wizard's settings and played
through its launcher with the level script (9,300 frames): 49.95 frames a
second in the window, the patches and the overclock as the game's
settings have them ("widescreen patches on; groups enabled: 50/60 FPS; EE
cycle rate 1"), and the session, run again by the development host, is
the session at every one of its 186 fingerprints, ending on the same
picture and the same sound. It is not the session of the same script
played with `-patches` (P4.4): the runtime applied the overclock a patch
asks for when the game's executable started, and PCSX2 applies a game's
own settings, the overclock among them, when it reads the disc, during
the BIOS's boot; from the same start (no memory card, no NVRAM), the two
part at the first fingerprint (frame 50), and the packaged run ends on the
same picture and sound whichever start it has. The new timing met 3
blocks of IOP code and 22 of VU1's micro code no compile had seen, which
the session wrote down for the next. Every test passes: 533 in the usual
tree, 534 in the module tree.

### The wizard's five steps (2026-11-17)

- **Its window** (`wizard/wizard_window.cpp`), one page for each of the
  brief's steps: the game, the BIOS, the settings, the recompile, the
  game's folder. Every default can be accepted as it is.
  1. **The game**: its disc image, recognized by PCSX2's own code and
     database (title, serial, CRC, region, PCSX2's compatibility rating),
     and its executables, found by their ELF headers
     (`ps2n-recomp executables`, `iso::executables` with a unit test): the
     one the disc boots, and every other program of the EE's on it, which a
     game made of several executables loads itself and the wizard compiles
     all of. For Agent Under Fire, the three `make-game.ps1 -More` named by
     hand, and `ACTION.UK`'s five other languages; for Killzone, a
     `LOADER.ELF` nothing compiled before. The game's name gives its folder,
     under the folder's `games`.
  2. **The BIOS**: recognized by PCSX2's own code, and remembered.
  3. **The settings**: PCSX2's pages for the game, in its own folder (above),
     with a button that goes back to the defaults; PCSX2's own buttons, which
     close its window or open another, are not there.
  4. **The recompile** (`wizard/pipeline.cpp`): what `make-game.ps1` does,
     each program a process of its own, the compiles as many at once as the
     computer has processors, with what it is doing, a bar of the files
     compiled, and what a program said when it failed.
  5. **The game's folder** (`wizard/package.cpp`): the runtime, the launcher
     under the game's name, the BIOS image linked in where it can be, and
     `settings.txt` with `-packaged`; and what may be done with it: "This
     folder is yours alone. It holds code translated from your game and from
     your console's BIOS: do not give it to anyone, nor put it where others
     can take it." With "Play it now", it starts the game.
- **What is fixed** (ADR 0032, decision 2): what PS2Native carries out in
  PCSX2's place, the EE, its FPU, the VUs, the IOP and the time they count,
  and what the runtime decides itself, is shown disabled at PCSX2's value,
  each saying why in its help and its tooltip: the rounding and clamping
  modes, the recompilers, wait loop and INTC spin detection, fast memory,
  the EE cache, the mVU flag hack, instant VU1, extended RAM, the game
  fixes (all of them, at what PCSX2's database gives the game) and the
  compatibility patches, save states, PINE, EE cycle skipping, MTVU, the
  console's clock. The EE's cycle rate, checked at every rate (G17), is
  not among them. They are taken out of a game's settings file too, so
  that a value written there before does not stand.
- **In the folder** `kit.ps1` makes: the wizard in `runtime`, beside the
  runtime whose libraries and resources it shares, with Qt's libraries and
  the five plugins it loads (`qt\plugins`, `qt.conf`); and `PS2Native.exe`
  at the root, the launcher, which there starts the wizard. What only the
  wizard needs is listed in `runtime\wizard-files.txt` (the wizard, Qt,
  and what they import: `harfbuzz.dll` beside Qt's own), which a game's
  folder leaves out, whether the wizard or `make-game.ps1` made it. The
  folder is 292 MB. Started from it with nothing but Windows on the path,
  `PS2Native.exe` opens the wizard's window in half a second. Its first
  version did not open: Qt's `Qt6Gui.dll` imports `harfbuzz.dll`, which
  the folder did not have, and Windows' box saying so belongs to a process
  of the system's, so the wizard sat waiting; the folder takes what the
  wizard and its plugins import as it takes the runtime's now.
- **The icons** of PCSX2's pages: its two icon themes, compiled into the
  wizard from where they are in the submodule (none of PCSX2's logo), the
  white one for a dark window, as PCSX2 chooses.

Measured. **Agent Under Fire made by the wizard**, every default accepted
(the wizard driven by its own check, `--auto`, which presses Next whenever
a page is complete): 336 seconds from the disc image to the folder, 333 of
them the recompile (419 files, 20 at a time); its EE code 379 MB, against
104 MB with the four executables `make-game.ps1` was given, for the five
other languages' `ACTION` files. Its folder, started from its launcher
with the controller script into the first mission: the console's pace,
50.00 frames a second in play, 49.94 with the start, with all of its IOP
code and micro code interpreted (no session had met any of it yet), which
the session wrote down for the next compile.

### The maintainer's run (2026-11-18)

The maintainer made a game the project had never run, *Gran Turismo 3:
A-Spec* (`SCES-50294`, PAL), with `PS2Native.exe` from the folder
`kit.ps1` makes, without a command line: its disc image and the BIOS
chosen, three settings changed (the aspect ratio to 16:9, and the patch
groups "No-Interlacing" and "Adjustable units" beside the "60 FPS" group
the wizard enables), the rest accepted. The recompile took 7 seconds, and
"Play it now" played the game from its folder: 8,678 frames in 182.2 s,
47.59 a second where the console shows 60 (the "60 FPS" patch). In the
maintainer's words, "The game is all slowed down".

Why. The disc's one executable, the program it boots (`SCES_502.94`,
39,647 instructions at `01000000`), is a loader: the game's code is loaded
by it from the game's data while it runs, and is on the disc as no
executable, so nothing had compiled it. The session interpreted 8.36
billion instructions of it (25,727 blocks), and, as a game's first session
does, all of the IOP's code it met (8,485 blocks) and VU1's micro code
(283 blocks); and 1.27 billion instructions of the BIOS's kernel (510
block starts), which a folder's BIOS module, compiled with no capture, has
not got. The session wrote down what it interpreted, the game's code with
its words (`capture.toml`, as G8's code in kernel memory is written down),
but not what it interpreted of the BIOS: a session in a game's folder
wrote no BIOS capture, although the compile of a folder takes one
(`bios-capture.toml`). It does now (`host/main.cpp`, `-sessions`).

The game made again from what the session wrote down (`make-game.ps1
-Captures`, into a folder of its own: 111 s, the game's module 15.2 MB
from 3.1 MB), then with the BIOS's capture as well, which the session run
again wrote (151 s), and the session run again from each folder without a
window, on Vulkan at three times the console's resolution:

| The folder | The session's 8,678 frames | Interpreted: the game's code | The IOP's blocks | VU1's blocks | The BIOS's kernel |
|---|---:|---:|---:|---:|---:|
| As the wizard made it | 113.6 s | 8.36 billion instructions | 8,485 | 283 | 1.27 billion instructions |
| Made again with the session's captures | 36.2 s | none | none | none | 1.27 billion instructions |
| And the BIOS's capture | 28.5 s | none | none | none | 51 instructions, at boot |

Each run is the session at every one of its 173 fingerprints, and ends on
the same picture and the same sound. At the console's pace those frames
take 144.6 s; the folder made again runs them in a fifth of that. Of the
second make's 151 s, 146 are one file: the game's code found outside its
executable, 28.7 MB of C++ (`game_outside.cpp`), which the recompiler
writes whole where it writes an executable's code in parts; every other
file was compiled within 10 s.

After this run, at the maintainer's word, the aspect ratio's default is
16:9 for every game (ADR 0032).

The maintainer then made the game again in the wizard, from the same disc
image (its settings and its session kept, and the BIOS's capture that the
session run again wrote put among the folder's captures), and played it
again: 13,581 frames in 227.5 s, 59.69 a second where the console shows
60, its start and loading included ("Looks good"). It interpreted 546
blocks of the game's code the first session had not met (3.3 million
instructions), one block of IOP code and one of VU1's micro code, and 51
instructions of the BIOS, at boot, and wrote them down for the next make,
the BIOS's among them now (`bios-capture.toml`).

### What is left of the step

- **The criterion's run**: the maintainer's run (above) made a game
  without a command line, with three settings changed; the criterion has
  every default accepted.
- **Code a game loads from its data** (Gran Turismo 3's) is compiled only
  once a session has met it: such a game's first session is slow, and its
  folder has to be made again in the wizard; P4.6 is to compile it by
  itself. The recompiler could write that code in parts, as it does an
  executable's.
- **Disc images** other than `.iso` files of 2048-byte sectors (`.bin` and
  `.cue`, `.chd`, `.cso`): PCSX2 recognizes them, PS2Native's reader of a
  disc's files does not read them yet.
- **PCSX2's archive of patches** where PCSX2 is not installed: fetched at
  the user's word, as ADR 0028 has it, or given; the wizard has no way to
  do either yet.
- **The licences** of the libraries the folder carries beside PS2Native's,
  PCSX2's and LLVM's (Qt's, and PCSX2's dependencies'), most of which
  PCSX2's dependency package has not got: to be gathered before the folder
  goes to anyone but the maintainer.
- **A settings file edited by hand** could still give a fixed setting
  another value; the runtime does not refuse one yet.
- **Every executable on the disc is compiled**, a game's other languages
  among them (Agent Under Fire's module four times as large); a choice in
  the first step could leave some out.
- **A game's first session** interprets its micro code and IOP code, which
  no session had met; compiling what it wrote down for the game's next
  start, by itself, is P4.6.
