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
| P4.5 The wizard | A game packaged from the disc image and the BIOS with every default accepted, without a command line | **Under way** (2026-11-17): the maintainer's word that it goes through all of PCSX2's settings, and how ([ADR 0032](adr/0032-the-wizard-goes-through-pcsx2s-settings.md)); PCSX2's own per-game settings pages work in `ps2n-wizard`, compiled unchanged, what PS2Native carries out in PCSX2's place fixed; a game's settings are a file in its folder that the runtime reads (`-packaged`), and a session played so is run again at every fingerprint; the five steps, from the disc image to a folder that plays the game, in `PS2Native.exe` at the root of the folder `kit.ps1` makes: Agent Under Fire made with every default accepted in 336 seconds, and played from its folder at the console's pace. **Done** (2026-11-18): the maintainer made a game the project had never run, Gran Turismo 3, without a command line (three settings changed, the rest accepted), which counts for the criterion ([ADR 0033](adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md)); its code, loaded from its own data, was interpreted in its first session, which wrote it down, and the folder made again from that played at the console's pace. What was left of the step is P4.8's |
| P4.6 New code in play | A session that meets new code finds it compiled at its next start | **Criterion met** (2026-11-18): a game's folder carries its own toolchain, and when a session ends its launcher starts `ps2n-remake`, which compiles what the session met, only what changed, in the background, with an icon and a notification, for the next start ([ADR 0034](adr/0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md)); a fresh folder of Gran Turismo 3: what its first session interpreted (5,871 blocks of its code, 7,846 of the IOP's, 15 of VU1's, the BIOS's kernel) all compiled at the next start, the third session interpreting nothing of it; the code met outside the executables compiled in 16 parts (that compile 151 s to 27 s); the maintainer's fourth game, Jak and Daxter, played from a folder the wizard made for 39 and then 23 minutes at the console's pace, 0.17% and then 0.04% of the EE's cycles interpreted, what each session met compiled about a minute after it |
| P4.7 Linux | The maintainer's own run inside the Ubuntu 22.04 of WSL 2 on this computer ([ADR 0041](adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md); at first their Linux laptop, [ADR 0038](adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md), which is not theirs to use; [ADR 0004](adr/0004-proof-of-concept-environment.md)): a game made with the wizard from its disc image plays from its folder at the console's pace, and what a session meets for the first time is compiled for its next start (the session's proposal, accepted). Linux is then described as tested under WSL 2 until a Linux machine of its own runs it | **Under way** (2026-11-19), at the maintainer's word ([ADR 0037](adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md)), built and tested first on a Linux inside this computer, WSL 2 with Ubuntu 22.04 ([ADR 0038](adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md)): every test passes on Linux (534; two of them, the self-tests on blocks compiled ahead of time, were skipped there and counted so until 2026-11-22: "The tests on Linux, and the self-tests' compiled blocks there"), the compiled code in the host and in shared objects of its own; the game's window an X11 one, with the notice over its picture; the launcher, the wizard and the recompile working there, and the folder for Linux put together (`scripts/linux/kit.sh`): with it, inside that Linux, Silent Hill 3 made from the maintainer's `.chd` as without PCSX2 in 123 s, played from its folder by its own launcher at the console's pace (49.72 frames a second of 50), what its first session met compiled in the background, and the next session interpreting none of the IOP's code and 0.00% of the EE's cycles. **Done** (2026-11-19), with the maintainer's run inside WSL ([ADR 0041](adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md)): Jak and Daxter made with the Linux wizard from its `.iso` and played from its folder for five sessions, what each met compiled in its window when it ended, the later ones on compiled code alone at about 90% of the console's pace there (Silent Hill 3 99.3%); the transparent parts of characters on WSL's OpenGL are not there on PCSX2's software renderer. The maintainer's words: "I guess it's good", then "Let's create releases on Github" ([ADR 0042](adr/0042-releases-on-github-beginning-with-0-1.md)) |
| P4.8 The folder for others | On a computer without PCSX2, a game made with the wizard from a `.chd` image, its patches fetched at the user's word, and the folder carrying the licence of everything in it (the session's proposal, [ADR 0033](adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md)) | **Under way** (2026-11-18), before P4.7, at the maintainer's word ([ADR 0036](adr/0036-the-folder-for-others-how-p4-8-is-done.md)): a disc in any format PCSX2 plays, read through PCSX2's own readers; the executables chosen; PCSX2's archive of patches fetched at the user's word or given; a fixed setting in a game's settings file refused; the licence of everything in the folder, Qt's made from Qt's own SBOMs. On this computer the wizard made Silent Hill 3, a game the project had never run, from the maintainer's `.chd`, as without PCSX2, its patches fetched, in 155 s, once a function the recompiler made of a megabyte of its data was cut into pieces. **Done** (2026-11-18): on the maintainer's Windows 10 computer, which has no PCSX2, Silent Hill 3 made with the wizard from its `.chd`, PCSX2's patches fetched at the button, and played from its folder for four sessions: "Everything went flawlessly" ([ADR 0037](adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md)). At the maintainer's word with it, a game does not play while it compiles: started while its own recompile compiles, its launcher waits for it, in a window that says so, and then plays with what it compiled |

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

### The step done (2026-11-18)

Asked what next, the session recommended counting the maintainer's run
for the criterion, the rest of the list below a step of its own before the
folder goes to anyone else, and then P4.6; the maintainer answered "Agree"
([ADR 0033](adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md)).
P4.5 is done.

### What it leaves for the steps after

For P4.6:

- **Code a game loads from its data** (Gran Turismo 3's) is compiled only
  once a session has met it: such a game's first session is slow, and its
  folder has to be made again in the wizard; P4.6 is to compile it by
  itself. The recompiler could write that code in parts, as it does an
  executable's.
- **A game's first session** interprets its micro code and IOP code, which
  no session had met; compiling what it wrote down for the game's next
  start, by itself, is P4.6.

For P4.8, the folder for others:

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

## P4.6 New code in play

Pass criterion: a session that meets new code finds it compiled at its
next start. As ADR 0028 has it, the game compiles what a session had to
interpret itself, in the background, with the toolchain the wizard brought,
and uses it from its next start; nothing is compiled while the game runs
(ADR 0003). Begun on 2026-11-18, at the maintainer's word (ADR 0033), with
the compile that P4.6 would run after a session like Gran Turismo 3's
first.

### The code met outside the executables, in parts (2026-11-18)

The code a game's sessions met outside its executables, which is all of a
game's own code when its executable is a loader, was translated into one
file. The recompiler now spreads it over parts as it does an executable's
code (`ps2n-recomp compile --outside-parts`, with a unit test, an image
that holds nothing among it), and the wizard and `make-game.ps1` ask for
16, as for the micro code and the IOP code; the development build, whose
games' outside code is small, keeps one file. Gran Turismo 3 made again
from its first session's captures, the four kinds:

| | The make | Of it, the compile | The session run again |
|---|---:|---:|---:|
| The outside code in one file | 151.4 s | 146.5 s (112 files, that one the last, at 146 s) | 28.5 s |
| In 16 parts | 27.4 s | 21.1 s (128 files, 20 at a time) | 28.2 s |

The session run again is the session at every one of its 173
fingerprints from both, ending on the same picture and the same sound,
nothing of the game's code interpreted. Every test passes: 533 in the
usual tree, 534 in the module tree.

### A game compiling what its sessions meet itself (2026-11-18)

As the maintainer chose it (ADR 0034): every game's folder carries a copy
of the toolchain (198.5 MB), and with it `ps2n-remake.exe`, the wizard's
recompile without its window (`wizard/remake_main.cpp`, Qt alone, none of
PCSX2's code), Qt, and `make.txt`, how its modules are made (the title, the
disc image, the BIOS image, the executables), which the wizard and
`make-game.ps1` write. Only the wizard itself stays out of a game's folder.

- **When a session ends**, the launcher starts `ps2n-remake` at below-normal
  priority (`host/launcher.cpp`). It merges what the folder's sessions met
  into the folder's captures; when they are what the modules were compiled
  from (a fingerprint of them, `work\compiled-from.txt`, which every compile
  that ends well writes), it stops there and shows nothing. Otherwise it
  shows PS2Native's icon in the notification area, translates again,
  compiles only the files newer than their objects (the recompiler writes a
  file only when it changes; the objects are kept with a stamp of the
  toolchain that compiled them, and thrown away under another), links the
  four modules into `work\next-<time>`, writes `ready` there last, and says
  so in a notification. One runs at a time in a folder; a session that ends
  while one compiles is taken by another round of it. `work\remake.log`
  says what it did.
- **At the next start** the launcher puts the newest ready modules in place
  before the runtime loads them, and takes every ready folder away.

Checked with a fresh folder of Gran Turismo 3 (`make-game.ps1`, nothing
compiled from any session; 310 MB) and three sessions of 1,500 frames
through its launcher, muted, in a window:

| | First session | Second | Third |
|---|---:|---:|---:|
| The game's code interpreted | 5,871 blocks, 94.6 million instructions | 1,631 blocks met for the first time, 580 million instructions | none |
| The IOP's blocks interpreted | 7,846, all of them | 477 met for the first time; 7,846 compiled | none; 7,846 compiled |
| VU1's blocks interpreted | 15 | none | none |
| The BIOS's kernel interpreted | 98.5 million instructions | 51 instructions, at boot | 51, at boot |
| In the window | 57.13 frames a second | 55.46 | 57.66 |
| The recompile after it | 128 files compiled (no stamp yet), 13 s | 34 of 128 files, 7 s | nothing new, 1 s, nothing shown |

The second session's blocks are none of the first's but one: the code the
game writes over the BIOS's program loader, at `00082000`, which no module
compiles (the game's executable does not hold the address, and the BIOS's
module is compiled from the ROM's own loader), about ten thousand
instructions at boot in every session, Gran Turismo 3's from the first;
left for later. A folder `make-game.ps1` made has no fingerprint and no
stamp yet, so the first recompile after a session compiles everything; one
the wizard made has both. The wizard's folder step checked on Killzone
(`ps2n-wizard --auto`, 120 s, its folder from P4.4 made again): `make.txt`
with both of its executables, the fingerprint and the stamp written, the
toolchain copied with its files' times (the 328 files of a copy have the
kit's sizes and times, so a game's first recompile keeps the objects the
wizard compiled), Qt and `ps2n-remake.exe` beside the runtime, the wizard
itself left out; `ps2n-remake` run then found nothing new in 0.6 s. Every
test passes: 533 in the usual tree, 534 in the module tree.

### Said at once, and over the picture (2026-11-18)

The maintainer tried it: "The compilation notification was really
delayed... It should show right after closing the game showing that is
compiling, than a different one saying it's compiled. Also when the game is
running on interpreted code that can be compiled, there should show an
overlayed notification saying so" (ADR 0035). The first notification was
late because the recompile merged every session's captures before it knew
whether there was anything new (three sessions of Gran Turismo 3, twelve
merges of a 5.6 MB file), and what it showed then was an icon, which
Windows 11 keeps in the notification area's overflow, with no notification
before the end.

- **The recompile merges only the sessions it has not merged**
  (`captures\merged.txt`; one whose kind of capture the folder's captures
  lack is merged again), and shows a notification as soon as it finds new
  code, "Compiling what your session met for the first time, for the
  game's next start", and a different one at the end, "Compiled: the game's
  next start has what your session met for the first time".
- **A notice over the picture** while the game runs code a compile would
  compile, interpreted: PS2Native's own, a window of PS2Native's laid over
  the game's top left corner, letting clicks through (`host/window.cpp`,
  `ShowNotice`), never PCSX2's on-screen display. The runtime looks every
  30 frames, on the CPU thread, at what its providers count (the EE's
  interpreted cycles, the IOP's and the VUs' interpreted blocks), and shows
  it, "Running code met for the first time, interpreted. It may be slower
  now. PS2Native compiles it when you close the game.", for five seconds
  at most once a minute, when at least 1% of a processor's work over the
  last half second was interpreted; in a game's folder only (`-sessions`).
- **The executables say they are PS2Native** (a version resource in
  `assets/ps2n.rc`), which Windows puts at the head of the notifications in
  the place of `ps2n-remake.exe`.

Checked with a fresh folder of Gran Turismo 3 and one session of 900 frames
through its launcher, muted: the notice over the picture 2.7 seconds into
the session (at its boot, the IOP's code interpreted), gone five seconds
later; the window closed, the recompile knew there was new code 0.4
seconds after the launcher had ended, and its first notification was on
the screen a second later; the second 11 seconds after, when the compile
of 128 files was done. Both photographed.

### The maintainer's run with Jak and Daxter (2026-11-18)

The maintainer made a fourth game the project had never run, *Jak and
Daxter: The Precursor Legacy* (`SCES-50361`, PAL), with `PS2Native.exe`,
and played three sessions from its folder around noon (about 11 minutes
together); at 14:29 made it again in the wizard from the same disc image,
with the kit that has the notice (its sessions kept), and played a session
of 20 seconds and two long ones from its launcher, with the patch groups
"Widescreen 16:9", "No-Interlacing" and "NTSC Mode". Like Gran Turismo 3,
it runs code it loads from its data, which no executable on the disc
holds. In the maintainer's words, "Looks good" and "I tested with Jak and
Daxter". From the folder's own logs (`last-run.log`, `work\remake.log`):

| | The first long session | The second |
|---|---:|---:|
| Its modules | compiled from what the sessions before it met | the recompile's after the first |
| In the window | 141,248 frames in 2,358.1 s: 59.89 a second, where the console shows 59.93 | 82,638 frames in 1,380.5 s: 59.86 a second |
| The EE's cycles interpreted | 0.17%: 886 million instructions, none in the executable, all but 51 (the BIOS's, at boot) of code the executable does not hold | 0.04%: 142 million instructions, likewise |
| The IOP's blocks interpreted | 112 of 8,985 | 53 of 9,018 |
| VU1's blocks interpreted | 17 | 24 |
| VU0's blocks interpreted | none | none |
| The recompile after it | new code known within a second of the session's end; 51 of 128 files compiled; ready 58 s after it began | likewise; 51 of 128 files; 63 s |

The first recompile's modules were put in place at the next start, the
second long session's; the second's wait, ready, for the game's next start.

Asked what next, the session recommended P4.8, the folder for others,
before P4.7, Linux, which needs the maintainer's Linux machine for every
run; the maintainer answered "Yep".

## P4.8 The folder for others

Pass criterion: on a computer without PCSX2, a game made with the wizard
from a `.chd` image, its patches fetched at the user's word, and the folder
carrying the licence of everything in it. Begun on 2026-11-18 at the
maintainer's word ("Yep"), with the maintainer's `.chd` of a game the
project had never run, *Silent Hill 3* (`SLES-51434`, PAL; "I do, there's
a chd file for silent hill 3"). How each part is done is the session's, for
the maintainer to decide otherwise ([ADR 0036](adr/0036-the-folder-for-others-how-p4-8-is-done.md)).

### What was built (2026-11-18)

- **A disc in any format PCSX2 plays.** PS2Native's reader of a disc's
  files read `.iso` images of 2048-byte sectors alone. The runtime now
  reads a disc through PCSX2's own readers (`ps2n-host -disc-executables
  <image>`, `-disc-extract <image> <folder> [<path>...]`,
  `host/disc.cpp`): `.iso`, raw `.bin`, `.img` and `.mdf`, `.chd`, `.cso`
  and `.zso`, `.gz`, each sector's 2048 bytes of data as PCSX2's own reader
  of a disc's files asks for them. What is looked for on the disc is still
  PS2Native's (`ps2n/iso9660.h`), made free of C++ exceptions so that the
  runtime compiles it (PCSX2's build switches them off). They are modes of
  their own, which load no module and run no machine, so that the runtime
  in the wizard's folder, which has no game's modules beside it, reads a
  disc too. The wizard, a game's own recompile and `make-game.ps1` use
  them; `ps2n-recomp extract` stays for `.iso` files in development. A
  `.cue` sheet stands for the image its first `FILE` line names, which
  PCSX2 opens (PCSX2 opens no sheet itself). A test of its own,
  `disc/formats`: a small disc of ours written as an `.iso`, a raw `.bin`
  and a `.cso` (`tests/disc/make_images.cpp`), each listed and extracted
  the same, a file the disc has not got and a file that is no disc refused.
- **The executables chosen.** The first step lists the disc's executables,
  each ticked; the one the disc boots cannot be left out. One left out runs
  interpreted, captured, until the game's own recompile has compiled what a
  session met of it (P4.6).
- **PCSX2's archive of patches where PCSX2 is not installed.** The
  settings step offers, at a button, to fetch it from the latest release of
  PCSX2's patches project (the file PCSX2's own builds put among their
  resources), with PCSX2's own downloader (WinHTTP; libcurl on Linux), or
  to take a `patches.zip` the user chooses (`wizard/patches.cpp`). It is
  checked to be a zip of patch files and kept in the wizard's folder,
  `patches\patches.zip`, for the games made after; a game's folder gets the
  game's own patch file from it, as from an installed PCSX2's.
- **A fixed setting refused.** In a folder the wizard made (`-packaged`),
  the runtime will not start when the game's settings file sets what
  PS2Native fixes, and says which, before anything runs; the launcher shows
  it, as any reason it could not play. The list is the one the wizard keeps
  out of the file (`ps2n::game::kFixedSections`, `kFixedKeys`), now shared
  by both. The settings files of the maintainer's four folders set none.
- **The licences.** `kit.ps1` writes `licences\`: PS2Native's and PCSX2's
  (GPL-3.0); PCSX2's own list of its third-party licences
  (`ThirdPartyLicenses.html`), which covers PCSX2's dependency package and
  what PCSX2 builds in, but not SDL and Qt; SDL's; LLVM's; `Qt-NOTICE.txt`;
  where Microsoft's terms for its C++ runtime are, as Visual Studio's own
  `Redist.txt` names them; and `README.txt`, which is which and where the
  source of each is (PS2Native's commit, PCSX2's tag). Qt's notice is made
  from Qt's own SBOMs in PCSX2's dependency package: the nine Qt files the
  folder carries (four libraries, five plugins), the code of others built
  into each (32 components: PCRE2, double-conversion, TinyCBOR, MD4C, the
  memory allocators of Vulkan and Direct3D 12, ...), each with its licence
  and copyright and its own licence file, and every licence text named (17),
  from Qt's source of the same release, `qtbase-everywhere-src-6.12.0.zip`
  (fetched into `deps\qt-6.12.0-licences` when not there, checked against
  the SHA-256 in PCSX2's script for its dependency package). Qt is carried
  under the LGPL, version 3, its libraries files of their own. `kit.ps1`
  says when the tree has changes not committed: the folder for others is
  made from a tree committed and pushed, into a folder of its own (`-Out`),
  without `games\` or `patches\`.

### Checked on this computer (2026-11-18)

- **The formats.** Jak and Daxter's `.iso` written again as a `.cso` (998
  MB, raw deflate blocks of 2048 bytes) and as a raw `.bin` of 2352-byte
  sectors (1.68 GB, with a `.cue`), in the build tree for the check and
  deleted after it: the runtime lists the same executable from all three
  and extracts it byte for byte the same. Silent Hill 3's
  `.chd` (2.2 GB, made for CDs: the codecs `cdlz`, `cdzl` and `cdfl`, units
  of 2448 bytes): its executable, `SLES_514.34` (3.4 MB, 848,094
  instructions), listed in 0.14 s and extracted whole.
- **Silent Hill 3 from its `.chd`, as on a computer without PCSX2**
  (`ps2n-wizard --auto --without-pcsx2 --fetch-patches`, from the folder
  `kit.ps1` made, the wizard's folder without an archive of patches). The
  first step recognized it (`SLES-51434`, CRC `5088CCDB`, PAL-M) and listed
  its one executable; the settings step fetched PCSX2's archive of patches
  (2.18 MB) into `patches\` and took the game's from it, "Widescreen 16:9"
  and "50 FPS", which the wizard enables, with the EE overclock its note
  asks for (130%).
- **What it found.** The recompile did not end: one file, 16.9 MB of C++,
  had not compiled after 29 minutes and 2.6 GB. Silent Hill 3's executable
  is one segment (`00100000` to `0043C380`, read, write and execute), its
  data with its code and none of its sections named, and its code ends near
  `0034C000` (from `00350000` on, not one `jr $ra`): nothing after its last
  function starts another, which took in the data, 187,835 words, as one
  C++ function. The recompiler now cuts a function longer than 16,384
  instructions into pieces (ADR 0036, 7): Silent Hill 3's 8,449 functions
  became 8,462, and the analysis of every executable of the four games run
  before (Killzone's two, Agent Under Fire's nine, Gran Turismo 3's, Jak
  and Daxter's) is the same as before, word for word.
- **Then.** The wizard made the game in 154.7 s (the recompile 149 s, 77
  files; the game's module 57.5 MB), with no box of Windows' for a missing
  library. Its `settings.txt` and `make.txt` name the `.chd`. Started from
  its folder, muted, the runtime played 90 frames from the `.chd`; and the
  game's own recompile after that session took the executable from the
  `.chd` through the folder's runtime and compiled what the session met (40
  of 111 files), ready 13 s after it began.
- **A fixed setting by hand.** Two lines written into that folder's
  settings file (`EnableEE` under `EmuCore/CPU/Recompiler`, `WaitLoop` under
  `EmuCore/Speedhacks`): the runtime stopped after 0.23 s, before anything
  ran, saying "the game's settings file ... sets what PS2Native fixes:
  EmuCore/CPU/Recompiler/EnableEE, EmuCore/Speedhacks/WaitLoop"; with the
  file as the wizard wrote it, it played.
- **The command line with a `.cue`.** `make-game.ps1 -Image` the `.cue`
  beside Jak and Daxter's raw `.bin`: the folder made in 9.5 s, its settings
  naming the `.bin`, the executable read through the kit's runtime, which
  has no game's modules beside it.
- **The licences.** `licences\` as above, `Qt-NOTICE.txt` 198 KB: nine Qt
  files, 32 components, 17 licence texts.
- **Every test passes**: 534 in the usual tree, 535 in the module tree,
  `disc/formats` among them, and the unit tests (the cut of a long function
  among the recompiler's, the reader of a disc's files without exceptions
  among the core's).

### The maintainer's run (2026-11-18)

The maintainer ran the criterion the same evening on their Windows 10
computer, P4.3's, which has no PCSX2 ("It doesn't have PCSX2"): the folder
for others made from `fbb2e77`, on a drive of its own there, with the
`.chd` and the BIOS beside it. They brought the folder back to this
computer (`build\kit-for-others`) and said: "Everything went flawlessly,
however I think running the game while compilation is running in the
background shouldn't be allowed." From the folder's own files, by the
laptop's clock:

- **The wizard.** PCSX2's archive of patches fetched at the settings step's
  button at 23:12 (`patches\patches.zip`, 2.18 MB); the game's own settings
  file enables the groups "50 FPS" and "Widescreen 16:9", and the EE
  overclock the first asks for, nothing else. The game's executable taken
  from the `.chd` at 23:12:36, and the folder written at 23:19:56: about
  seven minutes and twenty seconds for the recompile (149 s on this
  computer).
- **Four sessions** from the game's folder, each written down in its
  `sessions` with what it met for the first time (and each a few places in
  the BIOS at boot, 656 the first):

| Session | Began | Frames | Ended | What it met for the first time |
|---|---|---:|---|---|
| 1 | 23:20:04 | 3,407 | 23:22:00 | 28 places in the game's code; 9,526 blocks of the IOP's code; 31 blocks of VU0's micro code and 281 of VU1's |
| 2 | 23:28:30 | 1,891 | 23:29:14 | 112 blocks of the IOP's code |
| 3 | 23:30:18 | 4,582 | 23:31:54 | 5 places in the game's code; 186 blocks of the IOP's code; 24 blocks of VU0's micro code and 23 of VU1's |
| 4 | 23:31:57 | 136 | 23:32:06 | nothing |

- **The recompiles.** The first session, which met the most, played its
  3,407 frames (68 s of the console's time) in the 116 s from its start to
  its end. The modules the game runs now were finished at 23:29:56 and put
  in place at the third session's start. The recompile after the third
  session (`work\remake.log`) translated the game's code in 28 s and
  compiled 116 of 128 files, eight at a time, ready at 23:38:06, 6 minutes
  13 seconds after it began; its modules wait in `work\` for the next
  start. The reports of the earlier sessions and recompiles were written
  over by the later ones (`last-run.log` and `remake.log` are each the
  last one's).
- **The fourth session** began three seconds after the third had ended,
  while that recompile was under way, and was closed after 136 frames: 3.2
  s in the window, at 41.41 frames a second where the console shows 50,
  nothing of the game's code interpreted.

### A game does not play while it compiles (2026-11-18)

At the maintainer's word ([ADR 0037](adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md)),
a game's launcher (`host/launcher.cpp`) no longer starts the game while
the folder's own recompile compiles. It waits for it in a window of its own
(Windows' task dialog: "Compiling what your last session met for the first
time", a moving bar, Cancel), and plays as soon as the compile is done,
with what was compiled put in place; cancelled, it does not start the
game, and the compile goes on. The recompile (`wizard/remake_main.cpp`)
now lets go of its lock, `work\remake.lock`, as soon as its modules are
ready or could not be made, before the notification it then shows for ten
seconds. The launcher counts as compiling a recompile that holds the lock,
or one started less than 20 seconds before that runs still, which may not
have taken it yet, found among the programs running by its file (the
folder's `ps2n-remake.exe`). It shows its window only after a second and a
half of waiting, which a recompile that finds nothing new does not reach.
The window is drawn at the screen's scale; the launcher looks up Windows'
function for it (the common controls of version 6, which its manifest now
names) and says in a message box where it cannot show it, so that it starts
on any Windows all the same.

Checked on this computer, twice, the second time with the window's text as
it is now, in the Silent Hill 3 folder made here with the new launcher and
recompile put in it, each session 120 frames through the launcher, muted,
in a window of 1280 by 720 (the figures are the second time's):

- **Started before the recompile has taken its lock.** The recompile
  started and held suspended, the launcher 54 ms after it: three seconds
  later its window was up and the runtime not started. The recompile let
  go on, the window stayed until the compile was done, 17 s later, and the
  runtime started 19 ms after it closed.
- **Cancelled.** The launcher started two seconds after a recompile: its
  window up 1.7 s later, closed 1.4 s after that; the launcher ended with
  code 0, no runtime started, no session written, and the compile went on
  to its end.
- **A recompile that finds nothing new**, the launcher 13 ms after it: no
  window; the runtime started 230 ms after the launcher, against 117 ms
  with no recompile at all, one wait of 100 ms more (such a recompile ends
  50 to 60 ms after it starts).
- **No recompile**: the runtime started 117 ms after the launcher, no
  window.

The test suites were not run again: neither program is part of what they
run.

### The step done (2026-11-18)

P4.8's criterion is met with the maintainer's run (ADR 0037). What it
leaves:

- **The folder for others** is to be made again with `kit.ps1 -Out` from a
  tree committed and pushed with the launcher that waits, before it goes to
  anyone.
- **Microsoft's terms** for its C++ runtime are online
  (`https://aka.ms/vs/18/redistribution`, as Visual Studio's own
  `Redist.txt` names them): what they ask of whoever gives the folder to
  others is the maintainer's to read.

### The maintainer's answers (2026-11-19)

Asked four things the next morning, each with the session's
recommendation, the maintainer answered "1. Yes 2. Yes 3. Yes 4. Ok": the
work committed and pushed; the game waits for the compile and then plays,
as above, rather than being refused or played at once with the compile
stopped (the maintainer's since, ADR 0037); the new launcher put into the
folders of the maintainer's games made before it; and P4.7, Linux, next.

The new launcher went into the two such folders that compile what their
sessions meet themselves, Killzone's and Jak and Daxter's, the old ones
kept in `build\launchers-before-adr-0037`. Started in each with the
folder's `work\remake.lock` held as `ps2n-remake` holds it, it showed its
window 1.7 s later and started nothing; closed, it ended with code 0, no
session written. Their recompile stays as it was: the new `ps2n-remake.exe`
takes the executables from the disc through the folder's runtime
(`-disc-extract`, P4.8), which their runtimes are older than. Theirs takes
the same lock, but lets go of it only when it ends, ten seconds after its
last notification, which a game started meanwhile waits for too. The
folders made before P4.6 do not compile by themselves, and keep their
launcher.

## P4.7 Linux

Pass criterion: on the maintainer's Linux machine, a laptop with Linux Mint
and, as the maintainer thinks, NVIDIA's GTX 1660, a game made with the
wizard from its disc image plays from its folder at the console's pace, and
what a session meets for the first time is compiled in the background for
its next start (the session's proposal, accepted: "3. Yes"). Built and
tested first on a Linux inside this computer, WSL 2 with Ubuntu 22.04 (ADR
0038: "1. Sure."). Linux is reported as working only after the
maintainer's run (ADR 0004).

### The Linux inside this computer (2026-11-19)

- **WSL 3.0.5 with Ubuntu 22.04.5**, which the maintainer installed:
  20 processors and 31 GB inside it.
- **PCSX2's build packages** as PCSX2's own Linux build installs them
  (`.github/workflows/linux_build_qt.yml`), and **PCSX2's dependencies**
  built by its own script as its CI builds them (`BUILD_FFMPEG=1`): Qt
  6.12.0, FFmpeg 9.0.1, SDL 3.4.20, shaderc and the rest, in about 8.5
  minutes. Qt 6.12 asks for CMake 3.25 or later, where Ubuntu 22.04 has
  3.22: Kitware's own CMake 3.31.12, its SHA-256 checked. **LLVM's own
  22.1.3** for Linux, the version the Windows builds and the wizard's
  toolchain use (ADR 0030), its SHA-256 the one GitHub publishes for it.
  For the tests, Mesa's Vulkan drivers; for a crash, `gdb`.
- **The repository** inside it is a clone of the Windows one, with git's
  line endings: Git for Windows gives the working tree CRLF ones, with which
  PCSX2's shell scripts do not run. The working tree's changes are carried
  over before each build, only the files whose content changed written.

### Built and tested on Linux (2026-11-19)

- **The build-time tools** and their unit tests: built with no warning,
  both suites pass.
- **The runtime**, with `scripts/linux/build.sh` (rewritten: its options are
  `build.ps1`'s): 918 steps, no error, at the first build.
- **Every test passes on Linux**: 534 in the usual tree (2 skipped, as on
  Windows without the self-test's captures), in 110 s on half the
  processors; 534 in the module tree, the compiled code in shared objects of
  its own (Windows' 535th, the stack probe a module defines for itself, is
  Windows' alone: Linux asks for no such probe). Windows still passes all
  of its own after the changes, 534 and 535.

What it took, Linux's alone:

- **A module of compiled code is a shared object** (`ps2n-game.so` and the
  three others), compiled freestanding with clang (`-nostdlibinc
  -ffreestanding`, every symbol hidden but its entry), linked with no C
  library; it needs no library at all. The host loads it with `dlopen`,
  bound whole as it is loaded. PCSX2's build hides every symbol of its own
  on Linux (`-fvisibility=hidden`), which a hidden symbol's own name cannot
  be exported under: the host's linker gives the 26 of PCSX2's variables
  and functions that compiled code reaches names of PS2Native's,
  `ps2n_pcsx2_` and PCSX2's name (`--defsym`), which the executable
  exports with the runtime's (`ps2n/pcsx2.h`, `tests/tests.cmake`). Every
  symbol the game's module asks for, the host exports.
- **A run without a window has a window never shown where its renderer is
  OpenGL.** PCSX2 picks OpenGL for its null and software renderers where
  the first Vulkan device is a software one (llvmpipe, as under WSL) or
  Intel's, and its OpenGL on Linux makes no context without an X11 or
  Wayland window (`window::CreateHidden`).
- **Every run with OpenGL ended in a crash**, after its last word: PCSX2
  unloads its graphics library when it closes the renderer, before its GS
  thread ends, and libglvnd's `libEGL` (and the drivers it loads under WSL)
  had left destructors of that thread's own data behind, which glibc called
  in code no longer there. The host keeps every library loaded by the time
  the machine is set up (`RTLD_NODELETE`), and has PCSX2 choose its graphics
  device on the main thread, which ends with the process.
- **Two runtimes setting their machines up at once collided** (6 of 534
  tests the first time): PCSX2 makes the GS's memory under one name for
  every process (`/GS.mem`). The host gives that name its process's number,
  as PCSX2's other shared memory has (`host/shm_names.cpp`); serializing
  the machines' setting up instead took the tests from 114 s to 390 s.
- **A difference from microVU on Linux**, found by the self-test in 3 of
  6,000 random micro programs: where a program ends at a branch in a
  branch's delay slot (an "evil jump" with the E bit), TPC is the branch's
  target on Linux and the clip flag on Windows, by which register each
  system's calling convention gives microVU's `arg1regd`
  (`runtime/vu_interp.cpp`).
- **Linux tells the cases of file names apart**: the tests link the BIOS's
  ROM1, ROM2 and EROM under the lower-case extension PCSX2 looks for (the
  maintainer's are `.ROM1` and `.ROM2`).
- **The software renderer's picture** is put together and read back on the
  graphics device, whose arithmetic is the platform's: under WSL 46 of its
  143,360 pixels came out one step from Windows', stock PCSX2's and
  PS2Native's alike. On Linux each configuration's frame is compared with
  stock PCSX2's on the same computer, in the same suite.
- **The game's window** is an X11 one (under Wayland through XWayland, as
  PCSX2's own Linux build draws): the same keys, full screen through the
  window manager (Alt+Enter, F11), the notice over the picture a window
  inside the game's, its text Xft's (loaded when the first is shown; X's
  "fixed" font where there is no Xft). Under WSLg it showed a test program's
  picture on Vulkan and on OpenGL, and the notice over it, photographed.
- **The launcher, the wizard and the recompile build on Linux**: the
  launcher (`wizard/launcher_main.cpp`) Qt's windows where Windows'
  (`host/launcher.cpp`) has Windows' own; the wizard with Qt's private
  platform module, as PCSX2's own Linux build. What the wizard and the
  recompile do on Linux (the toolchain's programs, the modules' names, the
  folder) is next, then the folder made for Linux (`kit.sh`).

### The notice over a full-screen game (2026-11-19)

The maintainer asked whether the notice over the picture (ADR 0035) had
stopped working: Jak and Daxter had slowed down, and closing it the
recompile's notification said it was compiling, but no notice had shown.
Found ([ADR 0039](adr/0039-the-notice-over-a-full-screen-game-exclusive-full-screen-disallowed.md)):

- **The runtime now reports how near the notice's line a run came** (in how
  many half seconds a processor's work interpreted was over it, the first,
  the most in one, and how often it was shown). The maintainer's session of
  12:14 (109,508 frames), run again without a window from its folder's own
  modules, the session at every one of its 2,190 fingerprints: over the
  line in 12 of 3,649 half seconds, the first at frame 2,640, at most
  10.70% of the EE's cycles interpreted in one. The notice was due.
- **Over a game covering the screen, it reached the screen only at a
  session's start.** Its window was there and shown in every check. The
  maintainer watched a scratch copy of Jak and Daxter's folder, its own
  runtime started by its own launcher, interpreting from the start: shown at
  3 s and at 63 s, only the first seen; with PCSX2's
  `ExclusiveFullscreenControl = 0`, both. PCSX2's default lets the graphics
  driver take such a game over (Vulkan's full-screen exclusive), which
  NVIDIA's does once it has run a moment.
- **Exclusive full screen is disallowed by default on Windows**, and the
  game folders on this computer got the line in their settings files.
- The session's own work on the same computer, Linux test suites on all 20
  threads, had slowed the maintainer's earlier session: Linux builds and
  tests now use half the processors, Windows' run at below-normal priority.

### The wizard, the recompile and the folder on Linux (2026-11-19)

- **What the wizard and the recompile do on Linux** (`wizard/platform.h`):
  they compile a module with the toolchain's `clang`, with the flags the
  development build compiles one with (`-O3 -fPIC -msse4.1 -nostdlibinc
  -ffreestanding -fvisibility=hidden`), and link it with the same `clang`,
  which runs `ld.lld` beside it, with no library and no import library
  (`ps2n-game.so` and the three others, whose undefined symbols are the
  runtime's, bound when it loads them). Programs have no `.exe`, and the
  folder's text files Linux's line ends and separators.
- **A BIOS image on another file system than the game's folder** (a hard
  link cannot cross one) is linked into the folder symbolically, its ROM1,
  ROM2 and EROM under the lower-case extensions PCSX2 looks for; Windows,
  which lets no user make a symbolic link, still reads such a BIOS where it
  is.
- **PCSX2's archive of patches** is taken from a PCSX2 installed from a
  distribution's packages or from Flathub, where there is one (an AppImage
  keeps its own inside itself), and otherwise fetched or given, as on
  Windows.
- **The folder** (`scripts/linux/kit.sh`, `kit.ps1`'s counterpart, in
  `build/kit-linux`): `PS2Native` at its root, the launcher, which starts
  the wizard; the runtime, with Qt's windows on X11 (on Wayland through
  XWayland, as the game's window) and the libraries its programs need,
  PCSX2's dependencies and 57 of Ubuntu 22.04's: every one but those
  AppImages leave to the system (pkg2appimage's list, which linuxdeploy
  follows, with which PCSX2 makes its own Linux release on the same Ubuntu
  22.04), from libcurl and OpenSSL to the X11 utilities Qt's plugin needs,
  each library finding the others beside it (`$ORIGIN`, set with
  `patchelf`); what is left to the system is glibc 2.35 or later, libstdc++,
  the graphics driver, X11, fonts and ALSA. The toolchain: LLVM's own
  `clang` and `ld.lld`, their symbol tables stripped (100 MB of 470;
  nothing reads them), with the compiler's headers and the libraries
  `ld.lld` needs (libxml2 and ICU). The licences as `kit.ps1` writes them,
  Qt's notice made from the SBOMs of PCSX2's Linux build of Qt, with the
  texts from Qt's source; and each Ubuntu package's own copyright file,
  with where Ubuntu keeps the source of that very version. The runtime is
  154 MB and the toolchain 401 MB, which a game's folder copies (ADR 0034);
  `--archive` packs the folder into a `.tar.xz`, which keeps who may run a
  program where a copy through a stick formatted for Windows does not.
- **A game made with it inside that Linux**, in an environment of nothing
  but the folder (no library path, the system's programs alone), on half the
  processors: Silent Hill 3 from the maintainer's `.chd`, as on a computer
  without PCSX2, its patches fetched: in 123 seconds, 116 of them compiling
  77 files, 10 at a time. Played from its folder by its own launcher (a
  minute, muted, in a small window without a renderer): 49.72 frames a
  second, the console's 50.00; its IOP code all interpreted (7,784 blocks,
  none compiled yet) and 2.23% of the EE's cycles (the BIOS's kernel), the
  notice due all the time and shown twice. When the session ended, the
  launcher started the folder's recompile, which compiled what it met (128
  files) in two minutes; the next start took its modules, and the same
  minute interpreted none of the IOP's code and 0.00% of the EE's cycles,
  at 49.93 frames a second.
- **Started while its recompile compiled** (the game module's 84 objects
  made again), the launcher waited for it in its window ("Compiling what
  your last session met for the first time", photographed under WSLg), two
  minutes, and then played with what it compiled. Having shown that window,
  it crashed as it ended, after the session: Qt's application, left to go
  with the program's static data, went after Qt's own (`gdb`:
  `~QGuiApplication` from the exit handlers). It goes before `main` returns
  now (`wizard/launcher_main.cpp`), and the same start ends as a session
  does.

### Compiled in a window of its own (2026-11-19)

At the maintainer's word ([ADR 0040](adr/0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md)):
"Compiling shouldn't be done on the background, but yes on a foreground
modal/window (whatever you recommend). Also, the user is able to click
"Later" or close the modal/window (which is the same thing). What this does
is, next time the game runs, it will first compile pending compilations
before actually running." And: "It shouldn't be possible to run the game
without compiling pending compilations first."

- **`ps2n-remake` compiles in a window of its own** (`wizard/remake_main.cpp`),
  shown only when there is something to compile, at normal priority on
  every processor: what it compiles and for what ("Compiling what this
  session met for the first time", "So that Silent Hill 3 runs it at full
  speed from its next start"), how many of the files it has compiled, what
  it is doing, and Later, which closing the window is too. Later stops it;
  the objects compiled are kept. Done after a session, it says "Compiled"
  for three seconds. A compile that fails says why, with where its log is.
  The icon in the notification area and the notifications are gone.
- **The game never starts with anything left to compile**: before the
  runtime, the launcher (`host/launcher.cpp`, `wizard/launcher_main.cpp`)
  runs `ps2n-remake --before-play`, which compiles what is left in the same
  window ("Silent Hill 3 starts when it is done") and, a compile already
  running after a session, waits for it first; the game starts only when it
  ends with 0. Later there, or a compile that fails, leaves the game
  unstarted. The launcher's own wait (ADR 0037) is gone with it.
- **In front**: on Windows the game's window gives, as it closes, the right
  to bring a window to the front, and `ps2n-remake` asks Qt to activate its
  window whatever started it (Qt's `AlwaysActivateWindow`): without that, a
  window started by another program stayed behind the one in front.
- **`work/remake.log`** keeps every run, one before each start and one
  after each session, started again past a megabyte.

Checked on Windows, in a scratch folder of Silent Hill 3 with the new
launcher, recompile and runtime, each start muted in a small window without
a renderer: with nothing left to compile, the game started at once and no
window showed; with the game module's 84 objects to compile again, the
window, in front, and then the game; Later 40 seconds into that compile,
the game not started and 30 objects kept, the next start compiling the
other 54 and then playing; after a session that met what was not compiled,
the window, and Later there, the next start compiling it first; after a
session with only a link left, "Compiled" for three seconds. Inside the
Linux, the same folder of the Linux one: the window before the game and the
game after it; Later (the window asked to close as a window manager's close
button asks) leaving the game unstarted and 37 objects kept, the next start
compiling the other 47 and then playing; and "Compiled" after a session.
The window's secondary lines, first drawn as disabled text, which Qt's
Linux style draws nearly white, have a colour between the text's and the
window's. Every test passes on Windows, 534 in the usual tree and 535 in
the module tree.

The maintainer's answers: "1. Yes 2. Yes to all games 3. Yes" (committed
and pushed; the game folders on this computer given the new programs; the
Linux folder for their laptop made again from the commit). Jak and Daxter's,
Killzone's and Silent Hill 3's folders got the launcher, the recompile and
the runtime; Agent Under Fire's two and Gran Turismo 3's, made before a
game's folder compiled anything itself, the launcher and the runtime, and
compile nothing until they are made again with the wizard. Each folder's
runtime loads its modules.

### The run inside WSL (2026-11-19)

The Linux laptop is not the maintainer's to use. Asked whether the tests
could be made inside this computer's Ubuntu, the session ran Silent Hill 3
from the Linux folder there on PCSX2's OpenGL renderer, which inside WSL is
Mesa's D3D12 driver on this computer's RTX 4070 SUPER: 1,799 frames at
49.67 a second, the console's 50, at three times the console's resolution,
0.00% of the EE's cycles interpreted. Vulkan inside WSL is the CPU's
(llvmpipe), and the first controller does not reach WSL. At the
maintainer's "Yes" (ADR 0041) their own run there counts for the
criterion, and Linux is described as tested under WSL 2 until a Linux
machine of its own runs it.

For the run: the Linux folder made from the committed tree (`f30e7cc`,
`build/PS2Native` inside the Ubuntu); a shortcut on the maintainer's
desktop, "PS2Native (Linux)", which starts its `PS2Native` through
`wslg.exe`, without a console window (checked: it starts the wizard; Qt
says it has no Wayland plugin and takes X11's); in the wizard's settings
OpenGL, PCSX2's patches fetched at the button; the keyboard.

### The maintainer's run (2026-11-19)

Jak and Daxter (`SCES-50361`), made by the maintainer with the Linux wizard
from its `.iso` (OpenGL), played from its folder for five sessions, each
started by its launcher (a second desktop shortcut, "Jak and Daxter
(Linux)"):

| Session | Frames | Renderer | Frames a second (the console's 59.94) | Interpreted |
|---|---|---|---|---|
| 1 | 1,971 | OpenGL | 47.18 | 19.73% of the EE's cycles (Jak's code, loaded from its data); the IOP's and the VUs' code all |
| 3 | 1,133 | OpenGL | 55.39 | 0.00% of the EE's cycles; 176 of the IOP's 7,929 blocks |
| 5 | 3,380 | software | 54.13 | 0.00% of the EE's cycles; none of the IOP's code; 3 blocks of VU1's, 8 times |

(The second and fourth sessions, 4,085 and 4,004 frames, are written down in
the folder; `last-run.log` keeps only the last session's report.) After the
first, its compile window compiled what it met, 128 files on 20 processors,
in 18 seconds; after each later one, what it added. Inside WSL, on compiled
code, Jak and Daxter keeps about 90% of the console's pace, where it keeps
the console's pace on Windows and Silent Hill 3 kept 99.3% inside WSL.

The maintainer: "I guess it's good. Some characters have transparent parts
but I guess it's just OpenGL." OpenGL inside WSL is Mesa's translation to
Direct3D 12; on PCSX2's software renderer, which draws on the processor,
the characters are whole ("Yep software render seems to fix it"). Then:
"Let's create releases on Github." P4.7 is done (ADR 0041), and with it
every step of Phase 4; the first release, 0.1, follows (ADR 0042). The
maintainer published it on 2026-11-19 (tag `v0.1`, a pre-release).

## The speed step (ADR 0043)

At the maintainer's word on 2026-11-20 ("How come our compiled games are
running slower than PCSX2's ...? We need to check why and fix it"; "How
come when our games are running in interpreted mode ... they're far slower
than when running in PCSX2?"), a step of its own: first measure, in the
configuration users get, then make what the profiles show smaller, one
change at a time ([ADR 0043](adr/0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md)).

### How it is measured

A copy of each game's folder is made by `make-game.ps1` from the disc image
and the folder's own captures, into `build\measure\kit\games` (never the
maintainer's own folders), its modules compiled exactly as a game's folder
compiles them, without a profile, but with line tables beside them
(`make-game.ps1 -LineTables`, for measurements only), so that a profile
names their functions and lines (`ps2n-host -profile` now tells the four
modules apart). A session the maintainer played in that game's folder is
run again from the copy (`game.ps1 -Runtime <copy>\ps2n-host.exe -Replay
<session>`), without a window, under stock PCSX2 (`stock-jit`, the same
runtime with PCSX2's recompilers) and as compiled (`ps2n-static`), in turns,
three rounds each on the fast cores, the time taken from frame 1 to the
session's last (`-Marks 1`), and a fourth round of each profiled. Every
run is the session at every fingerprint. Jak and Daxter is run with
nothing drawn; the others on Vulkan at the console's resolution, since
PCSX2's null renderer stops at an assertion when Gran Turismo 3 draws (a
box that came up on the maintainer's screen once; the runs have since been
made on a desktop of their own that nobody sees, `build\measure\hidden.ps1`).

### Compiled code against PCSX2's recompilers (2026-11-20)

| Game, session | Frames | Stock PCSX2 | Compiled | Ratio |
|---|---:|---|---|---:|
| Jak and Daxter, `20261119-110909` (nothing drawn) | 8,357 | 58.94, 58.16, 57.49 s | 72.56, 71.68, 72.18 s | 1.24 |
| Gran Turismo 3, `20261118-102727` | 13,582 | 50.69, 50.90, 50.08 s | 58.98, 58.37, 58.90 s | 1.16 |
| Agent Under Fire, `20261117-185759` | 9,000 | 27.54, 27.53, 27.41 s | 32.38, 31.66, 31.86 s | 1.16 |
| Killzone, `20261117-221825` | 9,300 | 44.93, 45.28, 45.14 s | 48.21, 48.11, 48.32 s | 1.07 |

(Silent Hill 3's sessions are a few seconds each; from boot, without one,
its first 3,000 frames took 3.19 to 3.23 s under stock PCSX2 and 4.32 to
4.33 s compiled: start-up, the IOP's code met there interpreted.)

Where the difference is, by the profiles (seconds of the CPU thread):

| | Jak and Daxter | Gran Turismo 3 | Agent Under Fire | Killzone |
|---|---|---|---|---|
| The game's EE code: PCSX2's recompiler's, compiled | 8.6, 16.0 | 11.6, 13.5 | 10.9, 12.7 | 15.7, 18.1 |
| The IOP's code: PCSX2's recompiler's, compiled | 2.5, 4.4 | 3.9, 6.3 | 1.3, 1.8 | 2.7, 4.6 |
| The micro code: PCSX2's recompiler's, compiled | 16.2, 16.6 | 6.9, 6.7 | 2.7, 2.3 | 6.6, 6.8 |
| Where compiled code reaches memory that is not plain (`memoryFault`, `FindStep`, `FindRunning`) | 1.2 | 2.2 | 2.6 | 0.6 |

- **Every access of compiled code to memory that is not plain** (a device's
  registers) looked its instruction up in its block (`memoryFault` →
  `FindStep` → `FindRunning`), although only the first access of each
  instruction changes anything: 8.0% of Agent Under Fire's run (a loop that
  waits on a GS register), 3.7% of Gran Turismo 3's, 1.7% of Jak and
  Daxter's, 1.2% of Killzone's.
- **The IOP's compiled code takes 1.4 to 1.7 times the time of the code
  PCSX2's IOP recompiler makes**, in every game, and its provider's
  `ExecuteBlock` 0.9 to 1.7% of the run more.
- **Jak and Daxter's own code**: the multimedia instructions (128 bits)
  carried out on the core's copy of the registers (`PMADDH`, `PAND`,
  `PEXTUB`, `PSRAW`, `PADDW`, ... about 15% of its compiled code's samples),
  where PCSX2's recompiler keeps the registers in host registers; and the
  VU0 instructions (17%), with their flags computed by a call
  (`vu0::detail::Flags`, 1.2% of the run).
- **The six loops of `bench.s`** (the site's chart), timed in the module
  tree before the changes, `bench.ps1 -Repeat 3`, the best of three, in
  milliseconds: under stock PCSX2's recompilers, compiled, under PCSX2's
  interpreters, under PS2Native's:

  | Loop | Stock PCSX2 | Compiled | Ratio | PCSX2's interpreters | PS2Native's |
  |---|---:|---:|---:|---:|---:|
  | sum | 70.6 | 65.2 | 0.92 | 1,363.1 | 1,020.6 |
  | mem | 18.9 | 18.1 | 0.96 | 369.9 | 291.4 |
  | call | 29.4 | 28.3 | 0.96 | 608.3 | 493.9 |
  | fpu | 49.6 | 49.4 | 1.00 | 441.2 | 311.8 |
  | mmi | 8.7 | 10.1 | 1.16 | 208.1 | 163.0 |
  | jump | 49.0 | 49.9 | 1.02 | 1,314.5 | 857.0 |

  On the loops compiled code is at stock PCSX2's time or below it, but for
  the 128-bit instructions; and PS2Native's interpreter is faster than
  PCSX2's on every one (it was slower at the proof of concept, before the
  record of blocks: it decodes a block's instructions once now). What a
  game does that the loops do not (devices, the IOP, VU0) is where the gap
  in the games is.
- **What compiled code asks the runtime about at every execution**: a block
  of a page that is not write-protected compares its words through two
  calls (`askAbout`, `checkedAnswer`): 80 million times in Gran Turismo 3's
  run, 40 million in Jak and Daxter's, 23 million in Killzone's. A
  variant's label asks too (counted apart now): 13,366 times in Jak and
  Daxter's, about none in the others: nothing to gain there.

### Code met for the first time (2026-11-20)

The first session of each, run again from a folder made from the disc alone,
so that what it met for the first time is interpreted again, as it was:

| First session | Frames | Stock PCSX2 | PS2Native | Ratio | Interpreted |
|---|---:|---|---|---:|---|
| Gran Turismo 3, `20261118-095532` | 8,678 | 21.48, 21.74, 21.49 s | 108.54, 111.61, 108.66 s | 5.0 | 31.84% of the EE's cycles; the IOP's and VU1's code |
| Jak and Daxter, `20261118-120020` | 2,564 | 14.01, 13.98, 14.03 s | 60.49, 59.64, 60.18 s | 4.3 | 31.83% of the EE's cycles; the IOP's and the VUs' code |

The interpreters alone, against PCSX2's own (`ps2n-ee` and `stock-interp`,
every processor interpreted), on Gran Turismo 3's first 3,000 frames:
PS2Native 7.56 s, PCSX2 16.66 s. **PS2Native's interpreters are 2.2 times
as fast as PCSX2's**; what PCSX2 does with new code is compile it at once,
which no build of PS2Native's ships (ADR 0003). PCSX2's interpreters fetch
every instruction from memory each time (`iopMemRead32`, 21% of that run)
and test for events at every branch; PS2Native's decode a block once and
follow the record of blocks.

Where Gran Turismo 3's first session spends its time (88% of it in
PS2Native's interpreters): the EE's about 62% (`RunRecStep`, `RunRecSteps`,
`ExecuteOwn`, `ProviderExecute`, of which 6.0% in `r5900::info`, an
out-of-line lookup made for every instruction, and 1.3% in `Bios` and
`Physical`, made for every block); the VUs' 14%; the IOP's 9% (2.2% in
`r3000a::decode`, an out-of-line decode made for every instruction, every
time).

### The changes, each timed against the runtime before it (2026-11-20)

Each set of changes built into both trees, every test passing in each (535
and 534); then a copy of each measurement folder with the new runtime in the
place of the old (the same modules, `build\measure\variant.ps1`), the two
run in turns on the same session, two rounds; every run the session at every
fingerprint.

The first set:

- **Compiled code's accesses to memory that is not plain** find their
  instruction's step in a table of the last few thousand, as a COP2
  instruction's is found (`memoryFault`), and look it up in its block only
  when it is not there.
- **The EE's interpreter**: what an instruction is (`r5900::info`) inlined,
  and asked only where it decides something; what the report notes of a
  block (where it lies, the BIOS's place, the start compiled code lacks)
  worked out once for as long as the images in use are the same
  (`NoteBlock`, `NoteMissing`), not at every execution; the steps that
  are only carried out (`kPlain`, decided when the block is formed) taken
  straight to their instruction.
- **The IOP's interpreter**: each instruction decoded once, when its block
  is formed, kept after the block's words (`OpsOf`), where it was decoded at
  every execution.

| Run | Before | After |
|---|---|---|
| Agent Under Fire | 32.15, 32.00 s | 30.65, 31.09 s |
| Gran Turismo 3 | 58.69, 58.68 s | 57.28, 58.28 s |
| Killzone | 48.01, 48.46 s | 47.65, 48.03 s |
| Jak and Daxter | 71.08, 70.60 s | 71.19, 70.76 s |
| Gran Turismo 3's first session | 109.04, 108.51 s | 92.37, 91.98 s |
| Jak and Daxter's first session | 59.57, 60.02 s | 55.26, 54.99 s |

In Jak and Daxter, whose code has many VU0 instructions, the steps of
those and of the accesses took each other's places in the one table; the
second set gives the accesses a table of their own.

The second set: that table; and in the EE's interpreter the plain steps
carried out in the loop over a block's steps, `ExecuteOwn` inlined there (a
call less for each instruction).

| Run | First set | Second set |
|---|---|---|
| Agent Under Fire | 30.96, 31.06 s | 30.71, 31.12 s |
| Gran Turismo 3 | 57.35, 57.30 s | 57.26, 56.98 s |
| Killzone | 47.80, 47.75 s | 47.83, 47.96 s |
| Jak and Daxter | 70.18, 71.23 s | 70.45, 71.71 s |
| Gran Turismo 3's first session | 92.40, 92.36 s | 85.42, 85.78 s |
| Jak and Daxter's first session | 55.17, 54.78 s | 53.83, 53.28 s |

Gran Turismo 3's first session now takes 85.6 s, where it took 108.8 s (21%
less), Jak and Daxter's 53.6 s, where 59.8 s (10% less). The compiled games
gained 1 to 4% (Agent Under Fire most, the loop that waits on a GS register):
less than the profiles had put in `memoryFault` and the lookups under it,
which was not looked into further.

The second set also changed what modules are compiled with: the flags of
VU0's instructions computed inline (`WriteFlags`, where they were a call of
the runtime's, 1.2% of Jak and Daxter's run), and a block of a page that is
not write-protected compared with memory by compiled code itself
(`askAbout` reads the runtime's records, `checkedRecords`), with one call
where there were two. (The runtime still has both functions, for modules
compiled before.) Timed with the folders' modules compiled again from the
same captures (`build\measure\kit2`), against the same runtime on the
modules before (two rounds each):

| Run | Modules before | Modules after |
|---|---|---|
| Agent Under Fire | 30.81, 31.19 s | 30.69, 31.05 s |
| Gran Turismo 3 | 57.23, 57.66 s | 57.45, 57.11 s |
| Killzone | 47.80, 48.22 s | 47.75, 48.08 s |
| Jak and Daxter | 71.15, 71.59 s | 70.78, 71.71 s |

No difference that shows: the flags' work is now on the lines of the VU0
instructions in the profile (21% of the game's compiled code, from 17%).

The third set, in the EE's interpreter: a load or store that may fault
(`kFastmem`) among the plain steps, its memory looked at in the loop
(`PlainFastmem`), where it went through `RunRecStep`; the branch that ends a
block (`RunRecBranch`) inlined; the record's `Find` and `Physical` inlined
(in `rec_blocks.h`), where each was a call for every block. And the host
writes, when asked, the compiled functions whose code a run ran
(`-functions-ran`, `game.ps1 -FunctionsRan`), to place a module's functions
by. Every test passing in both trees (535, 534):

| Run | Second set | Third set |
|---|---|---|
| Gran Turismo 3's first session | 84.90, 85.12 s | 77.27, 77.71 s |
| Jak and Daxter's first session | 54.00, 54.37 s | 52.30, 52.66 s |

Gran Turismo 3's first session takes 77.5 s now, where it took 108.8 s (29%
less); Jak and Daxter's 52.5 s, where it took 59.8 s (12% less). Where the
first takes its time now: the EE's interpreter about 51%, the loop over a
block's steps (`RunRecSteps`) 37% of it all, with the instructions carried
out in it; the VUs' 20%; the IOP's 9%.

The third set also changed what modules are compiled with: the parallel
instructions that only move lanes about (`PEXTLH`, `PCPYLD`, `PAND`,
`PMFHI`, ...) and `PMULTH`, `PMADDH` and `PMSUBH` written with SSE on a
register's sixteen bytes at once (`ops_mmi.h`), where compilers had made
their loops into partial loads of registers just stored whole, and the
other way round. Jak and Daxter's folder compiled again with it
(`build\measure\kit3`), against the folder before (`kit2`), and the
folder before run by the new runtime (`kit2\games\jak-rt3`), to tell the
two apart:

| Jak and Daxter, two rounds each, in turns | Before | After |
|---|---|---|
| Modules and runtime | 71.10, 71.30 s | 69.01, 69.81 s |
| Modules, the runtime after both times | 70.32, 71.13 s | 68.67, 69.55 s |

The modules gave most of it, 2.3%.

### Two ways of compiling the same code (2026-11-20)

Each timed on Jak and Daxter's session, two rounds, the folder's modules
compiled again from the same captures (`build\measure\flags.ps1`,
`relink.ps1`):

- **For this computer's processor** (`-march=native`, with
  `-ffp-contract=off` so that no multiplication and addition become one):
  231.51 and 231.91 s, against 69.76 and 69.35 s, 3.3 times as long, every
  run still the session at every fingerprint. Not looked into further (the
  processor's AVX instructions next to PCSX2's code, compiled without them,
  would be the first thing to look at). Not used.
- **A module's functions in the order a session ran them** (`lld-link
  /ORDER`), from the list the runtime writes of the compiled functions a
  run ran (`ps2n-host -functions-ran`, `game.ps1 -FunctionsRan`), taken from
  another of the maintainer's sessions than the one timed (the first 9,000
  frames of `20261118-120316`: 3,685 of the game's functions; the BIOS's 230
  are in a module of their own and were left out): 68.68 and 68.93 s,
  against 69.86 and 70.60 s, 2.0% less, every run the session at every
  fingerprint. A folder that did this would place its functions by its
  user's own sessions: a profile of their play (ADR 0043, decision 4).
  (Measured again on 2026-11-21, four rounds each on the same objects, it
  is about 0.4%: the two unordered rounds here were slower than every other
  round of the same folder, 68.7 to 69.8 s. "The order of a folder's
  functions, built (2026-11-21)", below.)

### Stores of sixteen bytes, tried and taken out (2026-11-20)

Jak and Daxter's hottest compiled function showed a QMTC2 writing VU0's
register in two stores of eight bytes and the next VU0 instruction reading
it whole, which waits until both stores have left the processor; the
parallel instructions read a register whole just after compiled code put
its bits 0 to 63 back in a store of eight. A fourth set wrote those in one
store of sixteen bytes (QMTC2, QMFC2, SQ, the registers put back before a
parallel instruction) and had the parallel instructions that read only half
of a register read that half alone. Every test passed in both trees (535,
534); Jak and Daxter's folder compiled with it (`build\measure\kit4`),
against the one before (`kit3`), three rounds:

| Run | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Before (kit3) | 69.18 s | 69.57 s | 69.37 s |
| With the fourth set (kit4) | 69.70 s | 70.62 s | 69.77 s |

Its profile said why: the instructions that now read the upper half of a
register alone (`PEXTUB`, `PCPYUD`, `PEXTUH`, `PEXTUW`) took ten to twenty
times the samples they took before. On this processor a load of eight bytes
from the upper half of a store of sixteen waits for the store as well; the
lower half does not. Nothing else in the set gained (the VU0 instructions
took 2,491 samples where they took 2,506). Taken out whole.

### Profile-guided optimization from another of the maintainer's sessions (2026-11-20)

Jak and Daxter's four modules compiled to count what of their code runs
(clang's `-fprofile-instr-generate`, linked with LLVM's profile runtime; a
module is loaded without its C runtime's start, so the host has it write its
counts when the run is over, `WriteGameModuleProfiles` in `host/main.cpp`),
the first 9,000 frames of another of the maintainer's sessions run with
them (`20261118-120316`: 146.93 s, about 2.7 times as long as without the
counting), the counts merged (`llvm-profdata`), and the modules compiled
again with them (`-fprofile-instr-use`); timed against the same build
without the profile on the session timed throughout, three rounds
(`build\measure\batch7.ps1`):

| Run | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Without the profile | 69.70 s | 70.62 s | 69.77 s |
| With it | 65.51 s | 64.13 s | 64.75 s |

7.5% less, every run the session at every fingerprint: Jak and Daxter at
1.11 times stock PCSX2's time (58.2 s), where the same build without the
profile was at 1.20. A session
played with the counting would take 2.7 times as long, which no one could
play; a folder would take its profile another way (sampled while the game
plays, as `ps2n-host -profile` samples), and whether a folder uses its
user's own play for this is the maintainer's to decide (ADR 0043, decision
4).

### The BIOS's kernel compiled for a game's first session (2026-11-20)

A game's folder compiles the BIOS's code from the dump as far as the
analysis finds it, and what its own sessions met of it (its
`captures\bios-capture.toml`): a new game's first session meets much of the
kernel's code only by running it, interpreted (Gran Turismo 3's interpreted
1.27 billion of the kernel's instructions). The wizard now begins a new
game's captures with the BIOS's captures of the other games' folders in the
same games folder whose BIOS is the same image, byte for byte
(`BiosCapturesBeside` in `wizard/pipeline.cpp`): addresses in the BIOS,
which stay on the computer, as everything made from a BIOS does (ADR 0007).
A game's own recompile does not take them again, so that a session of one
game never has another compile before it plays. Of the 646 block starts in
the BIOS that Gran Turismo 3's own sessions met, the other four games'
folders had 600; of Jak and Daxter's 519, 515.

The two first sessions run again from folders made from the disc alone
(`build\measure\kit4`), without and with the other games' capture
(`make-game.ps1 -Captures`, given that capture alone):

| First session | Without | With |
|---|---|---|
| Gran Turismo 3 | 77.99, 77.70 s | 73.57, 73.17, 73.17 s |
| Jak and Daxter | 54.67, 54.96 s | 55.01, 54.80 s |

(A first round of Gran Turismo 3's without it was stopped at 19 s by the
batch before, whose last check stops any runtime still running; another
was run in its place.)

Gran Turismo 3's first session takes 6% less (27.43% of the EE's cycles
interpreted, where 31.84%); Jak and Daxter's, which met little of the
kernel, the same. (kit4's runtime had the fourth set in its interpreter,
which made Jak and Daxter's first session take 54.8 s, where the third set's
took 52.5 s.)

### Where the step stands (2026-11-20)

Compiled games, in the configuration users get (a folder's modules, no
profile), against stock PCSX2's recompilers on the same session:

| Game | Before the step | Now | With a profile of another session |
|---|---:|---:|---:|
| Jak and Daxter | 1.24 | 1.19 | 1.11 |
| Gran Turismo 3 | 1.16 | 1.13 | (not measured) |
| Agent Under Fire | 1.16 | 1.12 | (not measured) |
| Killzone | 1.07 | 1.06 | (not measured) |

(Now: Jak and Daxter from kit3's three rounds, 69.4 s; the others from the
second set, `kit2`, not timed again since the third.)

What is left between compiled code and PCSX2's recompilers, by the
profiles: the game's EE code (Jak and Daxter 15.4 s against 8.6 s; its VU0
instructions each ask the runtime's record of their block what to do,
`cop2Info`, where PCSX2's recompiler knows it when it compiles), the IOP's
compiled code (1.4 to 1.7 times the time of PCSX2's in every game, about 2
s of each session), the VU provider's work around each micro program
(about 1.7 s of Jak and Daxter's), and a profile, which these modules lack
(7.5% of Jak and Daxter's time); the placing of their functions gives
little (about 0.4%, measured on 2026-11-21).

First sessions: Gran Turismo 3's 77.5 s where 108.8 s (29% less), and 6%
less again where other games' folders have met the BIOS's kernel; Jak and
Daxter's 52.5 s where 59.8 s (12% less). Stock PCSX2 runs them in 21.6 s
and 14.0 s, compiling what it meets at once: no build of PS2Native's
compiles PS2 code while a game plays (ADR 0003), and PS2Native's
interpreters are 2.2 times as fast as PCSX2's.

### The maintainer's answer (2026-11-20)

Asked whether a game's folder should learn from its user's own play to
compile its code better, with the session's recommendation (the order of
its functions first, then a profile sampled while the game plays, built and
measured before it ships), the maintainer answered "1. Yes."
([ADR 0044](adr/0044-a-games-folder-learns-from-its-users-own-play.md)),
and "2. Commit and push." to committing what the step had done so far.

### The order of a folder's functions, built (2026-11-21)

ADR 0044's first part, through a game's folder's own path:

- **A session writes what ran.** A session in a game's folder (`ps2n-host
  -sessions`) writes, beside what it had to interpret, the compiled
  functions whose code ran, in the order they first did
  (`sessions\<time>\functions-ran.txt`, as `-functions-ran` writes it).
- **The folder's compile places them.** The compile after a session and
  before a start (`ps2n-remake`, and the wizard's, `wizard/pipeline.cpp`)
  adds each session's list to the folder's (`captures\functions-ran.txt`),
  every function once, in the order they first ran, and links the game's
  module and the BIOS's with their functions in that order: `lld-link
  /ORDER` on Windows (a name a module does not define passed over,
  `/ignore:4037`), `ld.lld --symbol-ordering-file` on Linux, where a
  module's functions are now compiled each in a section of its own
  (`-ffunction-sections`; clang-cl's `/O2` does it on Windows). The list is
  part of what tells a compile whether anything is new: a session that ran
  functions no session had run has the modules linked again, its objects as
  they were (only where a folder has a list, so that one without is as it
  was).
- **`make-game.ps1` the same:** a `functions-ran.txt` among `-Captures` or
  in a session of the folder goes into the folder's list, and the game's and
  the BIOS's modules are linked with it.

Every test passes in both trees on Windows (535, 534) and on Linux under
WSL 2 (534, 534).

What it gives, measured on Jak and Daxter's session, the folder made with
`make-game.ps1` from its captures and the list another session's first
9,000 frames ran (`build\measure\kit6\games\jak-o`, its functions read back
from its symbols in the list's order: 1,999 of the first 2,000 one after
another), against the same objects linked again without the list
(`jak-u`), so that the two differ in where their functions are and in
nothing else; four rounds each, which went first alternating:

| Run | Round 1 | Round 2 | Round 3 | Round 4 | Mean |
|---|---|---|---|---|---|
| Without the order | 69.20 s | 69.38 s | 69.50 s | 69.32 s | 69.35 s |
| In the order sessions ran them | 69.21 s | 69.22 s | 68.90 s | 68.85 s | 69.05 s |

About 0.4% less, every run the session at every fingerprint: much less
than the 2.0% of the link by hand, whose two rounds without the order were
slower than every other round of that folder. Small, as the profile's 7.5%
is not: what a profile gives is mostly not where the functions are.

Through the folder's own path: a session of 900 frames played from a
folder's launcher (`kit6\games\jak`, on a desktop of its own, muted) wrote
the 2,261 compiled functions it ran; its `ps2n-remake` then added them to the
folder's list, compiled the 17 files the session's new code changed, and
linked the game's and the BIOS's modules "in the order the sessions ran
them". On Linux the same (`build/kit-linux-adr44`, no window: the wizard
and `ps2n-remake` on Qt's offscreen platform, the sessions runs without a
window): Silent Hill 3 made with the wizard, a run's 1,639 functions added
to the folder's list, the modules linked with it, the game's module opening
with the list's functions in the list's order (1,489 of 1,490), and a run
on them as the launcher puts them in place.

Found on the way: on Linux, a game's folder's copy of the toolchain did not
keep its files' times (a copy there does not; on Windows it does), so the
folder's first recompile after a session took the wizard's objects for
another toolchain's and compiled them all again (Silent Hill 3: 77 files,
124 s). The copy now keeps them (`wizard/package.cpp`): made again, the
folder's first recompile after a session compiled 0 of the 77 files and
linked the modules in the sessions' order, in 8 s. Every test passes on
Windows with it (535 in the module tree, which builds the wizard).

### A profile sampled while the game plays, tried (2026-11-21)

ADR 0044's second part, measured before anything goes into a folder. The
runtime writes the samples its profiler takes (about a thousand a second)
as a sampled profile in LLVM's text format (`ps2n-host -sample-profile`,
`game.ps1 -SampleProfile`; `host/profile.cpp`, `WriteSampleProfile`): for
each generated function of the four modules, by the name the compiler gives
it, the samples on each of its lines, counted from the line that declares
it, for clang's `-fprofile-sample-use`. Jak and Daxter's folder made with
line tables (`build\measure\kit7\games\jak`), the first 9,000 frames of
another of the maintainer's sessions run on it with the sampling (56.33 s:
the sampling costs nothing that shows, where the counting took 146.93 s),
3,061 functions and 19,047 samples in the profile; the folder made again
with it (`jak-sp`), the two timed on the session timed throughout, four
rounds each, which went first alternating:

| Run | Round 1 | Round 2 | Round 3 | Round 4 | Mean |
|---|---|---|---|---|---|
| Without the profile | 69.29 s | 69.22 s | 68.42 s | 69.73 s | 69.17 s |
| With the sampled profile | 69.02 s | 69.03 s | 68.91 s | 69.20 s | 69.04 s |

0.2%, within the noise, where the counted profile gave 7.5%. Clang says why
(`-mllvm -sample-profile-check-record-coverage=100` on the file that holds
the hottest function): it could place 216 of the profile's 846 records for
that file's functions (19 of 145 for `fn_00721F48`), and the same profile
moved a line either way does no better (300 and 259): not an offset, but
lines it cannot place. My reading, not measured further: clang keys a
sampled profile by line and by discriminator, the number that tells apart
the blocks on one line, and the line tables of Windows' debugging format
(CodeView) have no discriminators, while a generated line is mostly a macro
of several blocks. More samples would not change that.

What does give the counted profile's 7.5% without making play slower:
counting not while the game plays but after, on a run again of the
session the folder has written down (deterministic: the session at every
fingerprint), with modules compiled to count, and compiling the folder
again with the counts. The 7.5% came from 9,000 frames of another session:
a few minutes of a session counted once would do. Not built: it is not
what ADR 0044 named, and is put to the maintainer, with its cost (on the
development computer, for Jak and Daxter, the folders made with
`make-game.ps1`: the folder compiled to count, 98 s; the run again, 147 s
for 9,000 frames, two and a half minutes of play; the folder compiled with
the counts, 63 s; longer on a slower computer), and what it adds to a
game's folder: LLVM's `llvm-profdata` (5.9 MB on Windows) and its profile
runtime (0.15 MB).

### The maintainer's answer (2026-11-21)

Asked whether a game's folder should take its profile by counting on a run
again of part of a session after play, instead of sampling while the game
plays, with the session's recommendation (once per game, after a session of
at least 3 minutes; never keeping the game from starting, Later putting it
off to the next session's end; measured on a second game before it goes into
a folder), the maintainer answered "1. Yes"
([ADR 0045](adr/0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md)),
and "2. Commit and push" to committing what had been done since ADR 0044.
Every test passes with it on Windows (535, 534) and on Linux under WSL 2
(534, 534).

### The counted profile on a second game, and further into the first (2026-11-21)

ADR 0045's measurement before anything goes into a folder, as a folder would
count: the first 3 minutes (10,800 frames) of one of the maintainer's
sessions run again with the modules compiled to count, the counts merged, the
modules compiled again with them, against the same captures compiled without;
in turns, which went first alternating, every run the session at every
fingerprint (`build\measure\batch15.ps1`, the runtime as committed in
`433eb77`):

| Game, the session counted, the session timed | Without | With the counts | Less |
|---|---|---|---|
| Gran Turismo 3, `20261118-102727`, `20261118-095532` (8,678 frames, Vulkan at the console's resolution, four rounds) | 23.61, 24.10, 23.69, 23.82 s | 22.79, 23.12, 23.00, 23.01 s | 3.5% |
| Jak and Daxter, `20261118-120316`, `20261119-110909` (8,357 frames, nothing drawn, four rounds) | 68.99, 69.71, 69.90, 68.67 s | 63.83, 64.37, 64.32, 63.67 s | 7.6% |
| Jak and Daxter, `20261118-120316`, the first 20,000 frames of `20261121-000955` (nothing drawn, three rounds) | 162.90, 163.45, 162.56 s | 151.58, 150.36, 151.56 s | 7.2% |

Gran Turismo 3 gains less (its replay draws as well: PCSX2's null renderer
stops at an assertion on its drawing; why it gains less was not looked into
further). The last row is a session of 2026-11-21, played two days after the
one counted and further into the game: a profile of 3 minutes near a game's
start serves its later play nearly as well. What counting cost (the folders
made with `make-game.ps1`, the compile's time with taking the executables
from the disc and translating them): Jak and Daxter compiled to count in
102 s, its 3 minutes run again in 187 s, compiled with the counts in 67 s;
Gran Turismo 3 in 32 s, 92 s and 24 s.

clang counts in two ways: in its front end (`-fprofile-instr-generate`, what
the 7.5% and the table above had), or where LLVM's optimizer has the code
(`-fprofile-generate`, the IR level), which counts after the first inlining
and counts the targets of calls through a pointer and the sizes of copies
too. The second, on the same 3 minutes of Jak and Daxter, against the first
and against none, three rounds each, in turns, every run the session at
every fingerprint (`build\measure\batch16.ps1`):

| Jak and Daxter, `20261119-110909` | Rounds | Mean | Less |
|---|---|---|---|
| No profile | 69.09, 68.40, 67.42 s | 68.30 s | |
| Counted in the front end | 63.43, 64.09, 63.61 s | 63.71 s | 6.7% |
| Counted at the IR level | 62.39, 62.52, 62.82 s | 62.58 s | 8.4% |

And it counts at less cost: the 3 minutes run again in 83 s where 187 s, the
modules compiled to count in 78 s where 102 s. A folder counts at the IR
level. Its counts of the targets of calls through a pointer are written with
the rest only by the profile runtime's own writer (its buffer leaves them
out, and llvm-profdata then takes the profile for one cut short): a module
made to count hands the host its counts through that writer
(`runtime/module_counts.cpp`), which defines what the profile runtime asks of
a C runtime on Windows, where a module has none (the C runtime's warnings
and its environment empty, memory from an arena of the module's own).

### A game's folder compiles its game for how it is played, built (2026-11-21)

ADR 0045 in a game's folder:

- **When.** After a session, once the folder's compile has compiled what the
  sessions met (ADR 0040), its `ps2n-remake` goes on, in the same window,
  when the folder has no counts yet and has a session that lasted at least
  3 minutes (10,800 frames; the newest such one is counted), or has counts it
  has not compiled with yet. Never before the game starts.
- **The modules made to count.** The game's four modules compiled from the
  C++ the last compile left (`compile_only`), with clang's `-fprofile-generate`,
  into objects of their own (`work\count\objects`), each linked with LLVM's
  profile runtime and `runtime/module_counts.cpp` (`work\count\modules`).
- **The run again.** The folder's own runtime with the folder's own options
  but a window's, on copies of what the session began with (its memory cards,
  the console's NVRAM, the BIOS's images it had, the game's settings and patch
  file: `work\count\run`), the modules made to count loaded in place of the
  folder's (`ps2n-host -modules`), what they counted written when the run is
  over (`-module-counts`), its first 10,800 frames, at the console's
  resolution, on Vulkan where the folder's options name no renderer (a run
  that draws nothing is not safe for every game), how far it has got said
  every 60 frames (`-progress`) for the window's bar.
- **The counts.** The four modules' raw profiles merged by the toolchain's
  `llvm-profdata`, the game's modules compiled again with them
  (`-fprofile-use`, into `work\objects-profiled`) and linked into
  `work\next-<time>`, which the launcher puts in place at the next start; the
  objects are then the folder's own, and the counts are kept with its
  captures (`captures\profile.profdata`), which every later compile takes
  (the objects' stamp, `work\objects\toolchain.txt`, says with which counts
  they were compiled). Code a later session meets, or changes, is compiled
  without counts, as clang does when the counts do not know a function.
- **Later, a game started, a failure.** Later puts the rest off to the end of
  another session, what was done kept. The game started meanwhile has its
  `ps2n-remake --before-play` ask the round to stop (`work\stop-profile`),
  which it does at once: it never keeps the game from starting. When it
  fails, its window says why, the game plays as it was, and the folder does
  not try again (`work\count\failed.txt`).
- **What a folder carries for it.** LLVM's `llvm-profdata` (5.9 MB on
  Windows) and its profile runtime (0.15 MB), and
  `runtime/module_counts.cpp`, in the toolchain (`kit.ps1`, `kit.sh`).

Every test passes in both trees on Windows (535, 534). Through a folder's own
path (`build\measure\batch17.ps1`): Jak and Daxter made with `make-game.ps1`
from the maintainer's folder's captures, as users get it, a session of the
maintainer's that lasted 26,413 frames put into its sessions, then its own
`ps2n-remake` as its launcher starts it after a session: nothing new to
compile, then the round, 174 s on the development computer (compiled to
count, 132 files, 49 s; the 3 minutes run again, 84 s; the counts merged,
under a second; compiled with them, 40 s). Its modules put in place as the
launcher puts them, against the same folder made without, on the session
timed throughout, in turns, every run the session at every fingerprint:

| Jak and Daxter, `20261119-110909` | Rounds | Mean |
|---|---|---|
| Without counts | 69.21, 68.36, 68.09 s | 68.55 s |
| After the folder's own round | 63.00, 62.46, 63.65 s | 63.04 s, 8.0% less |

A compile after another session kept the objects compiled with the counts
(`work\compiled-from.txt` taken away, as when a session met new code: "Compiling
0 of 128 files, the others as they were", the modules linked again), and
started no round again; before the game started there was nothing to do
(`build\measure\batch19.ps1`). Gran Turismo 3 the same way
(`build\measure\batch18.ps1`), a session of the maintainer's that lasted
13,582 frames counted: the round 95 s (compiled to count, 22 s; the 3 minutes
run again, 52 s; compiled with the counts, 21 s), and on its other session
(Vulkan at the console's resolution, four rounds):

| Gran Turismo 3, `20261118-095532` | Rounds | Mean |
|---|---|---|
| Without counts | 23.34, 23.92, 23.94, 23.62 s | 23.71 s |
| After the folder's own round | 22.80, 22.83, 22.91, 22.90 s | 22.86 s, 3.6% less |

On Linux under WSL 2 every test passes in both trees (534, 534), and the
same path runs (a Linux folder of its own, `build/kit-linux-adr45`, no
window: the wizard and `ps2n-remake` on Qt's offscreen platform, half the
processors): Jak and Daxter made with the Linux wizard from its disc image,
the same session of the maintainer's put into its sessions, then its own
`ps2n-remake`: what the session met compiled, then the round, 336 s
(compiled to count, 20 s; the 3 minutes run again, 300 s, on WSL's Vulkan,
which is the processor's, with the BIOS's code a folder made from the disc
alone has not compiled yet interpreted; compiled with the counts, 16 s), the
modules linked with LLVM's profile runtime by `ld.lld` and the C library's
functions it asks for bound when the runtime loads them; the counts kept,
and a run on the modules compiled with them to its last frame. Not timed
there.

The maintainer's answer (2026-11-21): "1. Commit and push"; "2. All game
folders" (ADR 0045): the new programs go into every game folder of the
maintainer's on this computer, each compiling its game for how it is played
after its next session of 3 minutes or more.

Done the same afternoon (`build\measure\update-folders.ps1`, and inside WSL
the same by hand), as the wizard puts the programs into a folder it makes:
the kit made again (`build\kit`, and `build/PS2Native` inside WSL, their
games and patches kept), then in each game folder every file of the kit's
runtime but the wizard's, the launcher under the game's name and the kit's
toolchain; nothing else of a folder touched (sessions, memory cards, NVRAM,
settings, captures). Jak and Daxter, Killzone and Silent Hill 3 had their own
recompile already, and nothing new to compile. Gran Turismo 3 and the two
folders of Agent Under Fire were made before a game compiled what its
sessions met itself: they got `make.txt` too, naming the executables each had
compiled, and each then compiled its game whole with the current toolchain
before its next start (`ps2n-remake --before-play`: 128, 221 and 470 files),
and so has the speed step's changes as well. A run again of the start of one
of each folder's sessions with its new programs was the session at every
fingerprint compared, in all six, and in the folder inside WSL. (`SLES_505.39`,
a folder of P4.4's checks with no launcher and no settings, was left as it
was.)

## Compiled games against PCSX2, as users get them (ADR 0046)

At the maintainer's word on 2026-11-21 ("Speed: match PCSX2", asked what to
work on next): every game's sessions at or below stock PCSX2's time in the
configuration users get, a game's folder with the profile it counts itself
(ADR 0045). First measured so, then the costs the profiles show, one change at
a time, each kept only if it gains
([ADR 0046](adr/0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md)).

### Where things stand (2026-11-21)

Each game's folder made with `make-game.ps1` from the maintainer's folder's
captures, in a kit of its own (`build\measure\kit9`), and counted as a folder
counts: a session of the maintainer's put into its sessions, then its own
`ps2n-remake`, as its launcher starts it after a session
(`build\measure\batch20.ps1`). Jak and Daxter was counted on
`20261118-120316`, Gran Turismo 3 on `20261118-102727`, Agent Under Fire on
`20261117-103006`, one of the maintainer's four sessions in the development
tree (82,844 frames; recorded before a session kept the game's settings and
patch file, its first 3 minutes were run again in the folder all the same,
the session at every fingerprint). Killzone and Silent Hill 3 have no session
of 3 minutes, and were not counted. Each was then timed against stock PCSX2
(the same runtime, `stock-jit`) on a session other than the one counted,
three rounds in turns, every run the session at every fingerprint:

| Game, the session timed | Stock PCSX2 | Compiled, as users get it | Ratio |
|---|---|---|---:|
| Jak and Daxter, `20261119-110909` (nothing drawn) | 56.63, 56.76, 56.24 s | 62.49, 63.02, 62.64 s | 1.11 |
| Gran Turismo 3, `20261118-095532` | 21.65, 21.28, 21.33 s | 22.81, 22.72, 22.85 s | 1.06 |
| Agent Under Fire, `20261117-185759` | 27.34, 27.44, 27.31 s | 29.71, 29.78, 29.57 s | 1.09 |
| Killzone, `20261117-221825` (not counted) | 44.36, 44.27, 44.58 s | 47.08, 47.15, 46.98 s | 1.06 |
| Silent Hill 3, its first 3,000 frames from boot (not counted) | 3.07, 3.16, 3.15 s | 3.92, 3.92, 3.91 s | 1.25 |

(Silent Hill 3's start-up interprets what none of its sessions, of a few
seconds each, has met.)

Where the difference is, by a profiled round of each (the compiled one from a
copy of the folder compiled with line tables and the same counts), in
seconds of the CPU thread, compiled against stock PCSX2:

| | Jak and Daxter | Gran Turismo 3 | Agent Under Fire | Killzone |
|---|---|---|---|---|
| The game's EE code | 11.7, 8.6 | 4.7, 4.3 | 12.3, 10.9 | 18.6, 16.0 |
| The IOP's code, its blocks found, and PCSX2's functions for its stores | 4.0, 3.5 | 2.9, 2.4 | 1.9, 1.8 | 4.9, 3.8 |
| Micro programs found, entered and left | 3.6, 2.2 | under 0.2 | under 0.2 | 2.4, 2.4 |
| Accesses to devices' registers, compiled (`memoryFault`, `FindStep`, `slowRead`) | 0.7 | 0.5 | 1.3 | 0.3 |

- **The counts** took Jak and Daxter's EE code from 15.4 s (2026-11-20, kit3,
  without them) to 11.7 s. With them clang puts the hottest functions inside
  those that call or jump to them, and the profile names the line of the
  call or jump for what it put there: the kinds of generated line say less
  than they did without counts.
- **Jak and Daxter's VU0 instructions and COP2 transfers** still ask the
  runtime what their block does at every execution (1,279 samples, 2.0 s, on
  those lines).
- **The IOP**: with PCSX2's functions for its stores counted on its side
  (compiled IOP code makes them itself), compiled IOP code takes 0.5 s more in
  Jak and Daxter and Gran Turismo 3, 0.1 s in Agent Under Fire, 1.2 s in
  Killzone.
- **Micro programs**: entering and leaving them costs Jak and Daxter 1.4 s
  more than microVU (its VU0 runs a short program at a time); in Killzone the
  two are even.

The changes, in the order these give: COP2 instructions' information decided
when the code is generated (Jak and Daxter); accesses to devices (Agent Under
Fire, Gran Turismo 3, Jak and Daxter); micro programs entered and left (Jak
and Daxter); the IOP's blocks (Killzone, Jak and Daxter, Gran Turismo 3); the
EE's block starts and ends (Agent Under Fire, Killzone).

### What PCSX2's recompiler decides of a COP2 instruction, decided when the code is generated (2026-11-21)

What PCSX2's recompiler decides of a COP2 instruction from the rest of its
block (whether VU0 is brought up to date first, COP2MicroFinishPass; whether
its flags reach VU0's registers, COP2FlagHackPass; whether the cycles so far
are added first) compiled code asked the runtime at every execution
(`cop2Info`: a table of 4,096 places, emptied whenever any block was formed or
thrown away). It depends on the block's range, its start and where the scan
for its end stopped, which the runtime only knows when it forms the block;
but the recompiler knows where a block of compiled code can begin and end
(its leaders), and can work out what every such range decides:

- **One copy of the rules.** The two passes, what follows from them for an
  instruction, and the scan for a block's end are in `core/include/ps2n/rec_cop2.h`,
  which the runtime forms its blocks with (`runtime/rec_blocks.cpp`) and the
  recompiler decides with.
- **Decided where every range agrees.** `ps2n-recomp` goes through every
  range a block of compiled code can have (from each leader, to where the
  scan stops by itself or to a leader before that where a block may exist
  already) and, where all the ranges an instruction is in decide the same,
  writes that into its code (`PS2N_VU0_DECIDED`, `PS2N_COP2_MOVE_DECIDED`): no
  lookup, and the compiler drops what does not apply. Elsewhere the
  instruction asks, as before.
- **Compared before compiled code runs a block.** The image lists what was
  decided (`ImageInfo::cop2`); before compiled code runs a block, the runtime
  compares its own record of each such instruction in it with that
  (`CompiledCodeCanRun`), and where they differ the block is interpreted and
  the host says so after the run. So what was decided is right by
  construction, whatever ranges the recompiler did not foresee.
- **The settings the passes read.** PCSX2's vuFlagHack (on, which PS2Native
  fixes) and its GameDB's FullVU0SyncHack (three games of its GameDB, none of
  the five): the wizard looks the game up and writes `cop2-full-sync=on` in
  `make.txt` where the GameDB sets it, and the folder's compile tells the
  recompiler (`--cop2-full-sync`).
- What a module and the host share changed: `kGameModuleVersion` 3, the
  recompiler 0.11.0.

Every test passes in both trees (535, 534; two of the recompiler's unit tests
new: one block whose COP2 instructions are decided, two whose ranges disagree,
which ask). Measured with the same runtime on the same captures, each game made
twice without counts (`build\measure\batch21.ps1`, `build\measure\kit10`): with
the recompiler's deciding off (`--cop2-decide off`, every COP2 instruction
asking, as before) and on; in turns, which went first alternating, every run
the session at every fingerprint, no block refused:

| Game, the session timed | Decided | Asking | Asking | Decided | Less |
|---|---:|---:|---|---|---:|
| Jak and Daxter, `20261119-110909` | 11,317 | 158 | 68.21, 69.14, 68.60 s | 63.17, 62.73, 63.26 s | 8.2% |
| Gran Turismo 3, `20261118-095532` | 1,093 | 14 | 23.66, 23.57 s | 23.35, 23.70 s | (within the rounds) |
| Agent Under Fire, `20261117-185759` | 28,248 | 589 | 30.44, 30.43 s | 29.97, 29.82 s | 1.8% |
| Killzone, `20261117-221825` | 4,581 | 8 | 46.92, 47.08 s | 45.79, 46.69 s | 1.6% |

(The second and third columns count the COP2 instructions and transfers in
each game's generated code that were decided and that ask.) Kept.

With it, a folder's compile takes what it translates and compiles with into
account when it decides whether there is anything new (`work\compiled-from.txt`
now holds the toolchain's stamp as well): a folder whose programs are brought
up to date compiles itself whole before its next start, where before it kept
the modules of the recompiler it had, which a runtime of another module
version would not load.

### Accesses to devices' registers asking only where they may fault, tried and taken out (2026-11-21)

Every access of compiled code to memory that is not plain asks the runtime
about its step (`memoryFault`: 1.0 s of Agent Under Fire's run, which reads a
GS register in a loop), although after each step's first fault it changes
nothing. The runtime kept, for every word of RAM, how many steps of the blocks
not yet gone for good may still fault there, and the slow paths of loads and
stores asked only where that was not zero. Every test passed in both trees
(535, 534); the same modules run by the runtime before and after it
(`build\measure\batch22.ps1`, kit10's folders), three rounds in turns, every
run the session at every fingerprint:

| Game | Before | After |
|---|---|---|
| Agent Under Fire | 30.17, 29.88, 30.06 s | 30.05, 30.28, 30.00 s |
| Gran Turismo 3 | 23.63, 23.51, 23.70 s | 23.21, 23.42, 23.56 s |
| Jak and Daxter | 62.76, 62.60, 62.98 s | 62.58, 63.36, 62.58 s |
| Killzone | 46.66, 46.71, 46.69 s | 46.93, 46.55, 46.69 s |

Nothing that shows. A profile of Agent Under Fire after it has no samples in
`memoryFault`, and as many again in the read of the GS register around it
(`slowRead`, `gsRead64`, the address table): what the profile had put in
`memoryFault` was the read's own time, which PCSX2's code for the loop spends
too. Taken out whole.

### A micro program's run entered at less cost (2026-11-21)

The VU provider's work at the start of each run of a micro program (VU0 runs
short programs many times a frame in Jak and Daxter):

- **The settings a plan is made with** (the clamp mode, the game fixes, the
  flag hack) were read from PCSX2's settings at every run (`ReadSettings`,
  about twelve fields). They are read when the provider is reset, which the
  core does whenever such a setting changes, and when a block is compiled (for
  the self-test, which changes them between its programs itself).
- **The program and the block a run begins at** were searched for at every
  run (`SearchProg`, and the block manager's search for the pipeline's state).
  Where nothing was written into micro memory since the last search for the
  same start, address and state, the search can only find the same: the
  program found there last (microVU's "quick" reference, which only a write
  clears), and the first block of a list that is only added to whose state
  fits. That is kept (`Vu::StartMemo`) and taken, with what the search's quick
  path does besides (the current program); a state that needs an exact match
  is searched for as before.

Every test passes in both trees (535, 534: the self-test's 20,000 random
micro programs held to microVU, runs sliced over many calls among them). The
same modules run by the runtime before and after it
(`build\measure\batch23.ps1`, kit10's folders), in turns, every run the session
at every fingerprint:

| Game | Before | After |
|---|---|---|
| Jak and Daxter, six rounds | 63.55, 63.40, 64.90, 63.35, 63.69, 62.94 s | 62.27, 62.67, 62.51, 62.13, 62.73, 62.17 s |
| Killzone | 47.02, 46.89, 46.49 s | 46.47, 46.70, 46.47 s |
| Gran Turismo 3 | 23.49, 23.45, 23.36 s | 23.52, 23.46, 23.32 s |
| Agent Under Fire | 29.93, 29.90, 30.05 s | 29.81, 30.04, 29.87 s |

Jak and Daxter 1.5 to 2% less, every round after faster than every round
before; the others within their rounds. Kept.

### Where things stand with the two changes kept (2026-11-21)

As at the start, the folders made again in a kit of their own from the
current module tree (`build\measure\kit11`, `build\measure\batch24.ps1`), the
same sessions counted by each folder's own `ps2n-remake` (again: the code
changed), and timed against stock PCSX2 on the same sessions as before, three
rounds in turns, every run the session at every fingerprint:

| Game, the session timed | Stock PCSX2 | Compiled, as users get it | Ratio | At the start |
|---|---|---|---:|---:|
| Jak and Daxter, `20261119-110909` (nothing drawn) | 57.05, 56.40, 56.43 s | 59.89, 58.82, 58.63 s | 1.04 | 1.11 |
| Gran Turismo 3, `20261118-095532` | 21.44, 21.63, 21.50 s | 22.94, 22.90, 22.84 s | 1.06 | 1.06 |
| Agent Under Fire, `20261117-185759` | 27.45, 27.45, 27.28 s | 29.75, 29.55, 29.45 s | 1.08 | 1.09 |
| Killzone, `20261117-221825` (not counted) | 44.52, 44.82, 44.74 s | 46.94, 46.74, 46.57 s | 1.05 | 1.06 |
| Silent Hill 3, its first 3,000 frames from boot (not counted) | 3.05, 3.13, 3.15 s | 3.90, 3.91, 3.88 s | 1.25 | 1.25 |

By a profiled round of each, in seconds of the CPU thread, compiled against
stock PCSX2: the micro code now takes less than PCSX2's in all four (Jak and
Daxter 15.1 against 15.9); the game's EE code takes 1.2 s more in Jak and
Daxter (9.5, 8.3), 0.3 s in Gran Turismo 3, 1.4 s in Agent Under Fire and in
Killzone; the IOP's (with PCSX2's functions for its stores on its side) 0.55
s, 0.43 s, 0.12 s and 1.27 s; accesses to devices' registers 1.3 s of Agent
Under Fire's (its loop that reads a GS register).

### An IOP block's way out at less cost, tried and taken out (2026-11-21)

Compiled IOP blocks already go on to each other by themselves (a jump to the
next one's function); what each does more than PCSX2's linked blocks is small
and on every block. A way out checked the next block's own record (thrown
away or not) in the place of a count of the blocks thrown away anywhere,
which compiled code read through the import table; blocks thrown away were
kept until the whole record was (2,800 to 10,400 of them over a session); a
block's record was put in one line of the host's cache; and a branch that
does not go back to its own block's start was not asked whether it is a wait
loop. Every test passed in both trees (535, 534); each game made again without
counts (`build\measure\kit12`), against the changes kept so far on the same
captures (kit10's folders with the runtime of the change before), in turns,
every run the session at every fingerprint (`build\measure\batch25.ps1`):

| Game | Before | After |
|---|---|---|
| Jak and Daxter | 62.04, 62.55, 62.49 s | 61.97, 61.95, 62.68 s |
| Gran Turismo 3 | 23.20, 23.39, 23.49 s | 23.38, 23.65, 23.61 s |
| Agent Under Fire | 29.87, 30.01, 29.89 s | 29.87, 30.00, 30.23 s |
| Killzone | 46.31, 46.55, 46.67 s | 46.30, 46.72, 46.62 s |

Nothing that shows. With the counts, clang already makes the jump to a
block's most frequent next block a direct one (the IOP's idle loop jumps to
itself so); what is left of the IOP's time is spread over thousands of small
blocks. Taken out whole.

### What is left, and the maintainer's answers (2026-11-21)

Compiled games take 1.04 to 1.08 times stock PCSX2's time as users get them
(the table under "Where things stand with the two changes kept"); the goal,
no more than its time, is not reached. By the profiled rounds of that
measurement, what is left:

- **In the game's EE code,** 1.2 to 1.4 s more than PCSX2's in three games,
  spread over kinds of line that each take a fraction of a second: the
  128-bit instructions' lines 0.7 s of Jak and Daxter's run (467 samples);
  the starts and ends of blocks 3.8 s of Agent Under Fire's and 3.4 s of
  Killzone's, much of it in loops that wait, which PCSX2's code spends time
  in too.
- **In the IOP's code,** 0.1 to 1.3 s more, over thousands of small blocks.
- **In PS2Native's runtime and PCSX2's core,** the largest part of every
  run, which no change so far has been aimed at as a whole:

| The profiled round | Jak and Daxter | Gran Turismo 3 | Agent Under Fire | Killzone |
|---|---:|---:|---:|---:|
| The compiled run, the processor thread | 59.2 s | 23.1 s | 30.2 s | 47.5 s |
| PS2Native's runtime and PCSX2's core in it | 23.6 s, 39.8% | 11.6 s, 50.4% | 12.3 s, 40.8% | 16.0 s, 33.7% |
| PCSX2's core in the stock run | 23.3 s | 11.1 s | 11.0 s | 16.3 s |

The session put three decisions to the maintainer with the numbers,
recommending yes to the first two and, of what could come next, the runtime
itself built with a profile of play before the smaller changes (the 128-bit
registers kept whole, for Jak and Daxter; loops that wait let off their
blocks' checks at every turn, for Agent Under Fire and Killzone). The
maintainer's answers: "1. Yes", "2. Yes", "3. Ok"
([ADR 0046](adr/0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md)):
committed and pushed; the new programs into the maintainer's game folders;
and the runtime built with a profile next, measured first, against stock
PCSX2 built without one.

The folders the same evening (`build\measure\update-folders.ps1`, as on
2026-11-21 for ADR 0045; inside WSL the same by hand). First tried on a copy
of the Jak and Daxter folder as it was, its old programs and modules
(`build\measure\update-trial.ps1`): given the new programs, its own
`ps2n-remake --before-play` compiled the game whole (the toolchain's stamp in
what it compiled from), and the start of a session run again with it was the
session at every fingerprint compared. Then the six folders: the kit made
again, each folder given the runtime, the launcher and the toolchain, and
each compiling its game whole before its next start:

| Folder | Files compiled | Time |
|---|---:|---:|
| Jak and Daxter | 128 | 53 s |
| Killzone | 138 | 114 s |
| Silent Hill 3 | 128 | 90 s |
| Gran Turismo 3 | 128 | 23 s |
| Agent Under Fire | 221 | 97 s |
| 007 - Agent Under Fire (every language's executable) | 470 | 351 s |

(The Linux folder inside WSL, Jak and Daxter: 128 files, ten at a time, 72 s.)
A run again of the start of one of each folder's sessions with its new
programs was the session at every fingerprint compared, in all six and inside
WSL. None of the folders has counted its profile yet: each does after its next
session of 3 minutes.

### The runtime itself: link-time optimization, and a profile of play (2026-11-21)

At the maintainer's "3. Ok": the runtime, `ps2n-host` (PS2Native's runtime
and PCSX2's core, 34% to 50% of a run's processor thread), built with a
profile of play, measured before anything of it goes into a release.

**What PCSX2's own releases are built with.** Looking at how PCSX2's build
takes such options showed something else first. PCSX2's releases are built
with link-time optimization: its Windows and Linux builds ask CMake for
`CMAKE_INTERPROCEDURAL_OPTIMIZATION`, its Visual Studio projects for whole
program optimization. `scripts\windows\build.ps1` has left that out since
Phase 2 ("minus the Qt interface and link-time optimization"), and
`scripts/linux/build.sh` after it. Every "stock PCSX2" timed so far is the
same runtime run with `-Config stock-jit`: PCSX2's core built without it. So
it was measured beside the profile.

**The trial** (`build\measure\rpgo.ps1`) changed nothing of the repository:
build trees of their own, configured as `build.ps1` configures the module
tree, with other options given to every file compiled, and only `ps2n-host`
built in them. The modules of compiled code are each game folder's own
(kit11's folders, with their counts).

- **Built to count** (`-fprofile-generate`, clang's IR level, linked with
  LLVM's profile runtime): 44 s; the runtime 24.7 MB.
- **The training:** seven runs with it, none of them a session that is timed,
  6 minutes in all, each run the session at every fingerprint compared:
  - the first 3 minutes (10,800 frames) of a session of Jak and Daxter
    (`20261118-120316`), of Gran Turismo 3 (`20261118-102727`) and of Agent
    Under Fire (`20261117-103006`), and Killzone's other session whole (9,300
    frames), from the folders with their counts;
  - Silent Hill 3's first 3,000 frames from boot;
  - two first sessions, from folders made from the disc alone, where the
    game's code is interpreted: Jak and Daxter's (2,564 frames, 31.83% of the
    EE's cycles interpreted) and 3,000 frames of Gran Turismo 3 (64.46%).
- **The counts merged** (`llvm-profdata merge`): 6.6 MB, 25,861 functions, of
  which 5,087 ran in a training run.
- **Built with them** (`-fprofile-use`): 40 s; the runtime 10.5 MB, where 11.3
  MB without.
- **Built with link-time optimization** and no profile (ThinLTO): 50 s; 13.1
  MB. With both: 60 s; 12.2 MB.

**Each alone** (the first set of turns). Every game's session timed in turns,
three rounds, the order turned round every second round, every run the session
at every fingerprint; stock PCSX2 is `stock-jit` on the runtime named, the
compiled game its kit11 folder's modules on it. Means, in seconds:

| Game, the session timed | Stock PCSX2, built as before | Stock PCSX2, link-time optimized | Compiled, the runtime as before | Compiled, link-time optimized | Compiled, built with the counts |
|---|---:|---:|---:|---:|---:|
| Jak and Daxter, `20261119-110909` (nothing drawn) | 57.26 | 54.98 | 58.95 | 58.17 | 56.40 |
| Gran Turismo 3, `20261118-095532` | 21.39 | 20.78 | 22.80 | 22.09 | 21.63 |
| Agent Under Fire, `20261117-185759` | 27.24 | 26.61 | 29.57 | 28.92 | 27.74 |
| Killzone, `20261117-221825` (its folder not counted) | 44.48 | 43.81 | 46.72 | 46.06 | 44.69 |

(The rounds: Jak and Daxter 57.85, 57.84, 56.08; 55.37, 55.42, 54.15; 58.87,
59.14, 58.83; 58.27, 57.55, 58.70; 56.30, 56.24, 56.67. Gran Turismo 3 21.24,
21.50, 21.42; 20.57, 20.85, 20.91; 23.01, 22.65, 22.75; 22.22, 21.96, 22.08;
21.36, 21.89, 21.65. Agent Under Fire 27.21, 27.33, 27.17; 26.51, 26.76,
26.56; 29.51, 29.69, 29.51; 29.04, 28.92, 28.81; 27.69, 27.68, 27.84.
Killzone 44.08, 44.70, 44.65; 43.64, 43.98, 43.81; 46.72, 46.80, 46.65; 46.22,
45.95, 46.01; 44.78, 44.72, 44.56.)

- **Stock PCSX2 built as its releases are takes 1.5 to 4.0% less time** than
  the stock PCSX2 every comparison so far was made with (Jak and Daxter 4.0%,
  Gran Turismo 3 2.9%, Agent Under Fire 2.3%, Killzone 1.5%). Against it, the
  compiled games as users get them today take 1.07 (Jak and Daxter), 1.10
  (Gran Turismo 3), 1.11 (Agent Under Fire) and 1.07 (Killzone) times its
  time, where 1.03, 1.07, 1.09 and 1.05 against stock PCSX2 as built here.
- **Link-time optimization** takes 1.3 to 3.1% off the compiled games' time.
- **The counts** take 4.3 to 6.2% off it (Jak and Daxter 4.3%, Gran Turismo 3
  5.1%, Agent Under Fire 6.2%, Killzone 4.4%).

**Both together, and a game the profile never saw** (the second set of turns,
`build\measure\rpgo-chain2.ps1`). The runtime built with the counts and
link-time optimization (no function's counts failed to fit its code); and,
for each game, a runtime built with counts taken without that game's runs.
Three rounds in turns as before, means in seconds:

| Game | Stock PCSX2, link-time optimized | Compiled, counts | Compiled, counts and link-time optimization | Compiled, counts taken without this game |
|---|---:|---:|---:|---:|
| Jak and Daxter | 55.97 | 56.71 | 55.20 | 56.98 |
| Gran Turismo 3 | 21.04 | 21.78 | 20.72 | 21.68 |
| Agent Under Fire | 26.66 | 27.71 | 26.55 | 27.82 |
| Killzone | 43.70 | 44.89 | 44.14 | 45.20 |

(The rounds: Jak and Daxter 56.33, 55.40, 56.18; 57.33, 56.07, 56.72; 55.19,
55.59, 54.81; 57.54, 56.71, 56.69. Gran Turismo 3 21.06, 21.00, 21.07; 21.99,
21.90, 21.45; 20.51, 20.74, 20.90; 21.51, 21.74, 21.79. Agent Under Fire
26.67, 26.71, 26.59; 27.65, 27.56, 27.92; 26.39, 26.48, 26.79; 27.57, 28.02,
27.88. Killzone 43.71, 43.59, 43.80; 44.94, 44.93, 44.79; 43.98, 44.22, 44.22;
45.19, 45.36, 45.05.)

- **With both, compiled games take 0.99 (Jak and Daxter), 0.98 (Gran Turismo
  3), 1.00 (Agent Under Fire) and 1.01 (Killzone) times the time of stock
  PCSX2 built as its releases are.**
- **A game the profile never saw gets nearly all of it:** with counts taken
  without its runs a game takes from 0.5% less to 0.7% more time than with
  counts that had them.

**Code the training never ran.** Of the runtime's 25,861 functions 5,087 ran
in a training run. With the whole profile the compiler takes the other 20,774
for code that never runs, and compiles it for size: the other renderers,
PCSX2's code for processors without AVX2 (this computer has it), whatever a
game the training did not play would be first to use. `llvm-profdata merge
-sparse` leaves the functions no run ran out of the profile (1.4 MB), and the
compiler then takes them as it does without one. The third set of turns
(`build\measure\rpgo-chain3.ps1`): the runtime with link-time optimization and
the whole profile, with the sparse one, and with a sparse one taken without
the game's runs; three rounds in turns, means in seconds:

| Game | Stock PCSX2, link-time optimized | Compiled, the whole profile | Compiled, the sparse profile | Compiled, a sparse profile taken without this game |
|---|---:|---:|---:|---:|
| Jak and Daxter | 55.40 | 55.05 | 55.09 | 54.70 |
| Gran Turismo 3 | 20.91 | 20.95 | 20.95 | 20.71 |
| Agent Under Fire | 26.84 | 26.55 | 26.46 | 26.79 |
| Killzone | 43.88 | 44.14 | 43.98 | 43.84 |

(The rounds: Jak and Daxter 55.73, 54.74, 55.73; 55.19, 55.25, 54.70; 54.89,
55.39, 55.00; 54.13, 55.30, 54.68. Gran Turismo 3 20.88, 20.76, 21.09; 20.94,
20.93, 20.99; 20.59, 20.79, 21.47; 20.68, 20.59, 20.86. Agent Under Fire
26.93, 26.90, 26.70; 26.48, 26.84, 26.34; 26.21, 26.90, 26.27; 26.78, 26.87,
26.71. Killzone 44.00, 43.56, 44.08; 43.78, 44.22, 44.42; 43.78, 44.01, 44.16;
43.63, 44.08, 43.81.)

The sparse profile gives what the whole one gives, to within the rounds, and
one taken without a game's runs gives that game from 1.1% less to 1.2% more
time than one that had them (in the second set, from 0.5% less to 0.7% more).
**With
it and link-time optimization, compiled games take 0.99 (Jak and Daxter), 1.00
(Gran Turismo 3), 0.99 (Agent Under Fire) and 1.00 (Killzone) times the time
of stock PCSX2 built as its releases are**; in the second set, with the whole
profile, 0.99, 0.98, 1.00 and 1.01.

Two games drawn by PCSX2's software renderer, which no training run used,
compiled, two rounds in turns: Gran Turismo 3 22.16 s on the link-time
optimized runtime without a profile, 21.56 s with the whole profile, 21.61 s
with the sparse one; Killzone 69.00 s, 68.12 s and 68.16 s. Neither profile
costs anything there, and both still gain.

**A game's first session,** where its code is interpreted
(`build\measure\rpgo-first.ps1`): Gran Turismo 3's first session
(`20261118-095532`, 8,678 frames, 31.84% of the EE's cycles interpreted; no
training run ran it) from a folder made from the disc alone, two rounds in
turns, each the session at every fingerprint: 77.36 and 77.11 s on the runtime
as built today, 68.61 and 68.78 s with link-time optimization and the sparse
profile, 11.1% less (stock PCSX2, which compiles as it meets, took 21.6 s on
2026-11-20).

**Built into the build scripts,** off by default: `build.ps1 -RuntimeLto on`
and `-RuntimePgo generate|use -RuntimePgoData <file>` (`build.sh
--runtime-lto`, `--runtime-pgo`, `--runtime-pgo-data`; CMake's
`PS2N_RUNTIME_LTO`, `PS2N_RUNTIME_PGO` and `PS2N_RUNTIME_PGO_DATA`,
`cmake/ps2n-inject.cmake`). They are given to every target of the project
before PCSX2's own exist, and taken off again what is not the runtime where it
is declared (`ps2n_not_the_runtime`: the modules of compiled code, the
launcher, the wizard's and the tests' own programs). A tree built so has the
runtime the trial timed, to the byte's count (15,674,880 bytes with link-time
optimization and the sparse profile, where 11,337,216 today).

**Every test with such a runtime** (`build\measure\rt-check.ps1`): a build
tree of its own given everything the module tree was given, the runtime with
link-time optimization and the sparse profile, built whole in 171 s. 534 of
its 535 tests passed; `ps2autotests/same-as-stock/dma/vif/stcycl` failed, and
run again alone it failed three times in four, its two runs parting at sync
point 31,305.

**Why: a defect of PCSX2's, which link-time optimization moves.** Each of the
test's two runs made six times with its record of sync points written
(`build\measure\rt-stcycl.ps1`): on the new runtime the program runs one of
two ways (61,544 or 61,546 sync points), under stock PCSX2 as compiled; on the
runtime as built today it runs one way every time (61,532). What it prints
differs in its VIF0 cases with a CL of 251 and more, which PCSX2 is already
listed as getting wrong (`tests/ps2autotests-known-differences.cmake`), and
between the new runtime's two ways in eight of them (CL 253 to 255):

- An UNPACK with masks that would run past the end of a VU's memory is carried
  out by PCSX2's interpreted unpack routines (`dVifUnpack`,
  `pcsx2/x86/Vif_Dynarec.cpp`, falls back to them; VU0's memory is 4 KB, and
  with these CLs the writes wrap).
- Their masked write (`VifUnpackSSE_Simple::doMaskWrite`,
  `pcsx2/x86/Vif_UnpackSSE.cpp`) loads the address of `nVifMask` and counts the
  places of its three tables from `nVifMask[2]`: it takes the data mask and the
  write-protect mask from the 128 bytes before `nVifMask`, and the row and
  column values from the data mask's table. (PCSX2's arm64 routine counts from
  `nVifMask` itself. PCSX2's `master` had the same lines on 2026-11-21.)
- What those 128 bytes hold is the linker's to say (each runtime's debugging
  information, `build\measure\rt-neighbours.ps1`). Built as today, `nVifUpk`
  ends there: pointers to the unpack routines, whose low bits are the same on
  every run, so the program prints the same wrong values every time and two
  runs agree. With link-time optimization `nVif` ends there: pointers into the
  heap, whose low bits are not the same from run to run (the values printed
  differ by bit 4 of a data word let through).

So the comparison fails for what stock PCSX2 itself does, in a case PCSX2
gets wrong either way; nothing of PS2Native's is in it. The three comparisons
of two runs of that program (same-as-stock, lockstep, lockstep-no-stack) are
disabled where the runtime is built with link-time optimization, by name and
with this cause (`PS2AUTOTESTS_UNSTEADY_WITH_RUNTIME_LTO`,
`tests/same-as-stock-known-differences.cmake`), and CTest lists them as not
run: **532 of 532 pass, three not run.** Without link-time optimization they
run and pass as before.

**On Linux** (WSL 2, half the processors), the same options through
`build.sh`, in build trees of their own given what the Linux module tree was
given. Not timed there.

- The runtime built to count: 197 s, with its tree's modules.
- One session of the maintainer's Linux folder's game (Jak and Daxter,
  `20261119-225148`, 4,085 frames) run again with it and the folder's own
  modules (`ps2n-host -modules`), the session at every fingerprint, and on
  past its end to 10,800 frames; the counts merged sparse (4,171 functions).
  One game's one session, to see it work: a Linux release's profile needs the
  training runs made there, with each game's folder made on Linux.
- The whole tree built with link-time optimization and those counts: 219 s.
  The names the host exports PCSX2's symbols under (`--defsym`) hold under it.
- Every test: 531 of 531 pass, the same three not run (534).

**What it costs.** For a release, on the computer that has the games, for each
system: the runtime built to count (44 s on Windows), the training runs (seven,
6 minutes), the counts merged (under a second), the runtime built with them and
link-time optimization (about a minute; the whole module tree 171 s): about 8
minutes. The profile stays in the build tree (`build\pgo`): a build from the
repository alone can give the runtime link-time optimization (`-RuntimeLto
on`), not the profile.
The runtime is 4.3 MB larger (15.7 MB, where 11.3 MB). Three comparisons of one
console-recorded program are not run with it, for the defect of PCSX2's above.

With the options off, as by default, every test passes as before from the
same working tree: 535 and 534 on Windows, 534 and 534 on Linux under WSL 2.

### The maintainer's answers (2026-11-22)

The session put seven decisions to the maintainer with these numbers, with
its recommendation for each. Their answers: "1. Yes", "2. Yes", "3. Yes",
"4. Yes, write the text, I'll change it", "5. Yes", "6. Yes", "7. Ok do it"
([ADR 0047](adr/0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md)):

1. **Stock PCSX2 is timed built as its releases are** from now on, with
   link-time optimization and without a profile, and the website's chart is
   corrected to it: the compiled games in their folders at 1.07, 1.10, 1.11
   and 1.07 times its time with the runtime as built before (the first set of
   turns), and at 0.99, 1.00, 0.99 and 1.00 with the runtime built with
   link-time optimization and the sparse profile (the third).
2. **A release's runtime is built with link-time optimization and the profile
   of play,** on each system from training runs made there.
3. **The maintainer's game folders get that runtime,** nothing compiled again.
4. **The maintainer tells PCSX2 of the defect themselves.** The session wrote
   no text for it
   ([ADR 0010](adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)),
   and gave where the defect is, when it shows and how it is seen. They
   opened it the same day: PCSX2's issue 15223.
5. **Committed and pushed.**
6. **A folder counts its profile after a session of 3 minutes of the game's
   own time,** 9,000 frames at 50 frames a second, where it took 10,800 for
   every game.
7. **0.2 next,** with a way to bring the game folders of an earlier version up
   to date.

**The tree the folder is made from, built so** (the session's choice, ADR
0047): `build.ps1 -BuildDir build\windows-clang-module -RuntimeLto on
-RuntimePgo use -RuntimePgoData build\pgo\runtime-windows.profdata`, the
sparse profile of the measurement above, the whole tree compiled again in
86 s. Its runtime is the one timed, to the byte's count (15,674,880 bytes).
Every test in it: 532 of 532 pass, the three comparisons of `dma/vif/stcycl`
not run (190 s). The development tree with the compiled code in the host
stays as it was, and runs them.

**The maintainer's game folders given that runtime**
(`build\measure\runtime-into-folders.ps1`). The kit made again from the tree
built so. First on a copy of the Jak and Daxter folder: given the kit's
`ps2n-host.exe` and nothing else, its own `ps2n-remake --before-play` found
nothing to compile ("nothing new: the modules have all the sessions met", its
modules untouched), and 900 frames of one of its sessions run again were the
session at every one of 18 fingerprints compared. Then the six folders on
Windows, the same way: in each the runtime of 15,674,880 bytes in the place of
the one of 11,337,216, nothing compiled again (the modules those of
2026-11-21), and the start of one of its sessions run again the session at
every fingerprint compared (Silent Hill 3's sessions are of a few seconds: 90
frames, one fingerprint). The second Agent Under Fire folder interprets 1.01%
of the EE's cycles, the BIOS's program loader, as before (its one session is
older than the BIOS's captures in folders). A seventh folder there,
`SLES_505.39`, one of the session's own early checks with programs of its
time, was left as it is.

### Three minutes of the game's own time (2026-11-22)

The maintainer's "6. Yes". A folder counted its profile after a session of
10,800 frames, 3 minutes at 60 frames a second. A European game is shown at
50 unless a patch of it puts the console in its 60 mode: of the maintainer's,
Killzone and Agent Under Fire are shown at 50, for which 10,800 frames are
3.6 minutes, and Killzone's two sessions (9,300 frames, 3.1 minutes) never
counted; Gran Turismo 3 with its "60 FPS" patch, and Jak and Daxter with its
"NTSC Mode" patch enabled, are shown at 60.

- **The runtime writes a session's frames a second down** when the session
  ends (`host/session_record.cpp`): `frames-a-second 50` or `60` in the
  session's `input.txt`, what the console showed most of the session's frames
  at, by the video mode the game had it in (50 in its PAL modes, 60 in every
  other; the frames before the BIOS has set a mode are neither). A session run
  again reads the line and has no use for it.
- **A folder counts by it** (`wizard/remake_main.cpp`, `CountFrames`): a
  session of 180 times that many frames at least, its first 3 minutes run
  again, 9,000 frames at 50. A session written down before the runtime wrote
  the line is taken for one at 60, as before: the maintainer's own folders
  count after their next session of 3 minutes.

Tried: a session of 700 frames in a window of Gran Turismo 3, out of sight,
from a folder brought up to date (below), wrote `frames-a-second 60`, and ran
again as the session at every fingerprint. In the measurement folders of
2026-11-22 (below) the copies of the maintainer's sessions, written down
before the runtime wrote the line, were given it by hand, by what each count
run's EE cycles a frame say (5,897,588 at 50, 4,944,695 at 60): 50 for Jak
and Daxter's session (played before its "NTSC Mode" patch was enabled), Agent
Under Fire's and Killzone's, which counted on 9,000 frames, Killzone's folder
for the first time, on 9,000 of its session's 9,300; 60 for Gran Turismo 3's,
10,800 frames. (Gran Turismo 3's was first given 50, the session's mistake,
and its folder counted on 9,000 frames, 2.5 minutes: counted again and timed
again, below.)

### The runtime's profile made again, by the repository's own scripts (2026-11-22)

The maintainer's "2. Yes": what the trial of 2026-11-21 did by hand is
`scripts\windows\runtime-profile.ps1` (`scripts/linux/runtime-profile.sh`). It
builds the runtime to count in a tree of its own (`build\windows-clang-count`),
runs the training runs named in a file of the build tree
(`build\pgo\training.txt`: a run's name, the disc image, the game's folder
whose modules it runs, the session run again or none, how many frames), each
through `game.ps1` with the modules of the game's own folder (`game.ps1
-Modules`, `ps2n-host -modules`), and puts the counts together without the
functions no run ran. A run of a session that is not the session at every
fingerprint compared stops it.

**The training runs** are those of 2026-11-21 but one: the first 10,800 frames
of a session of Jak and Daxter (`20261118-120316`), of Gran Turismo 3
(`20261118-102727`) and of Agent Under Fire (`20261117-103006`); Killzone's
other session whole (9,300 frames); Silent Hill 3's first 3,000 frames from
boot; and, from folders made from the disc alone, where the game's code is
interpreted, 3,000 frames of each of those sessions of Jak and Daxter and of
Gran Turismo 3. (On 2026-11-21 the folder of Jak and Daxter made from the disc
alone ran the game's first session, which is timed below and so is no longer
trained on.) The folders are those of the measurement below, with their
counts.

**On Windows:** 604 s in all, the tree built whole to count and the seven
runs, each the session at every fingerprint compared; the profile 1,427,352
bytes, 5,093 functions (5,087 on 2026-11-21).

**A profile made again under the same name was not compiled with.** The module
tree, built with the profile of 2026-11-21 under
`build\pgo\runtime-windows.profdata`, did not link once the new one was
written there: nothing told the build that the file had changed, one file of
PCSX2's that is compiled at every configuration took the new profile, and the
linker refuses modules whose profile summaries differ ("linking module flags
'ProfileSummary': IDs have conflicting values"). The build now gives the
compiler a copy of the profile in the build tree named after what it holds
(`cmake/ps2n-inject.cmake`): another profile is another file to every compile
command, everything the runtime is built from is compiled again with it, and
CMake runs again when the profile changes. Built so, the whole module tree in
125 s; its runtime 15,617,024 bytes; every test: 532 of 532 pass, the three
comparisons of `dma/vif/stcycl` not run.

**On Linux** (WSL 2, half the processors). The training runs need each game's
folder made on Linux. Seven were made in a folder of the build tree of their
own (`build/kit-measure`), each by the Linux wizard as a user makes it
(`--auto`), without a window (Qt's offscreen platform): two from the disc
alone (Jak and Daxter 18 s, Gran Turismo 3 13 s), and five that found in
their captures folder what the maintainer's sessions of the game met on
Windows, which is PS2 code, the same on both systems (Jak and Daxter 75 s,
Gran Turismo 3 63 s, Killzone 147 s, Silent Hill 3 169 s, Agent Under Fire,
all nine of its executables, 810 s). The same seven runs, the sessions the
maintainer's own from their Windows folders, drawn by OpenGL: 1,053 s with the
tree built whole to count; **every session written down on Windows, run again
on Linux with modules compiled on Linux, is the session at every fingerprint
compared**; the profile 1,216,368 bytes, 4,750 functions. The module tree
built with it and link-time optimization in 110 s; every test: 531 of 531
pass, the same three not run, and 534 of 534 in the development tree. Nothing
was timed on Linux.

### As the release has it (2026-11-22)

Everything measured again as 0.2 has it (`build\measure\batch26.ps1`): the
runtime built with link-time optimization and the profile the repository's
script made from this tree; each game's folder (kit13) made from the
maintainer's captures and counted by its own `ps2n-remake` on a session other
than the one timed, the first 3 minutes of it (9,000 frames, Gran Turismo 3's
10,800), Killzone's too; two folders made from the disc alone, for first
sessions. Stock PCSX2 is `stock-jit` on the runtime
of a tree of its own built with link-time optimization and no profile
(`build\windows-clang-stock`: 234 s to build, 13,075,968 bytes). Jak and
Daxter with nothing drawn, the others on Vulkan at the console's resolution.
Three rounds in turns, the order turned round every second round, nothing
else running; every one of the 57 runs, and of the 12 of Gran Turismo 3's
second set, the session at every fingerprint written down. Means, in seconds:

| Game, the session timed | Stock PCSX2, built as released | Compiled, as 0.2 has it | Times stock PCSX2's time |
|---|---:|---:|---:|
| Jak and Daxter, `20261119-110909` (8,357 frames) | 55.74 | 54.75 | 0.98 |
| Gran Turismo 3, `20261118-095532` (8,678 frames) | 21.08 | 20.99 | 1.00 |
| Agent Under Fire, `20261117-185759` (9,000 frames) | 27.09 | 26.78 | 0.99 |
| Killzone, `20261117-221825` (9,300 frames) | 43.91 | 40.54 | 0.92 |

(The rounds: Jak and Daxter 56.03, 55.77, 55.43; 54.85, 54.55, 54.85. Gran
Turismo 3 21.08, 21.12, 21.03; 21.13, 21.02, 20.83. Agent Under Fire 26.81,
27.29, 27.17; 26.51, 26.88, 26.94. Killzone 43.55, 44.27, 43.90; 40.19, 40.67,
40.75.)

Gran Turismo 3's row is from a second set of turns
(`build\measure\batch26-gt3.ps1`), its folder counted again on 10,800 frames,
the session's first 3 minutes at 60 frames a second. In the first set, its
folder counted on 9,000 frames (above: "Three minutes of the game's own
time"), it took 20.92 s (20.74, 21.13, 20.89) against stock PCSX2's 20.81 s
(20.58, 20.96, 20.88), 1.01.

**Killzone's counts fit the session timed better than a user's would:** its
two sessions are two runs of one controller script, so the session it counted
on is nearly the play it is timed on. Without counts its folder takes 44.12 s
(44.14, 43.78, 44.45), 1.00 times stock PCSX2's time, as on 2026-11-21.

**What a folder's own counts give** with this runtime (the same folders
compiled again without their counts, in the same turns): Jak and Daxter 58.46
s without (58.45, 57.76, 59.17), 6.3% less with; Gran Turismo 3 21.48 s
(21.49, 21.46, 21.48, the second set), 2.3% less; Agent Under Fire 27.05 s
(27.15, 27.02, 26.99), 1.0% less; Killzone 8.1% less.

**A game's first session,** from a folder made from the disc alone, the game's
code interpreted (31.83% and 31.84% of the EE's cycles), no training run of
the runtime's profile having run either session:

| First session | Stock PCSX2, built as released | PS2Native 0.2 | On 2026-11-20 | Before the speed work |
|---|---:|---:|---:|---:|
| Jak and Daxter, `20261118-120020` (2,564 frames) | 13.09 | 44.83 | 52.5 | 59.8 |
| Gran Turismo 3, `20261118-095532` (8,678 frames) | 20.81 | 69.59 | 77.5 | 108.8 |

(The rounds: Jak and Daxter 12.90, 13.15, 13.21; 44.65, 45.35, 44.49. Gran
Turismo 3 20.62, 20.98, 20.83; 69.09, 69.73, 69.96.) Against 2026-11-20, Jak
and Daxter's first session takes 14.6% less time and Gran Turismo 3's 10.2%
less; against stock PCSX2, which compiles as it meets, 3.4 and 3.3 times its
time.

## 0.2: a game's folder brought up to date (ADR 0048)

The maintainer's "7. Ok do it" (ADR 0047): 0.2, with a way to bring the game
folders of an earlier version up to date. A game's folder has its own copy of
PS2Native's programs, so a game made with 0.1 would stay on 0.1's.

### What was built (2026-11-22)

- **The wizard's first step** has a line and a button, "Games made
  earlier...": the line says how many games of this PS2Native's own `games`
  folder have an earlier PS2Native's programs.
- **The window it opens** (`wizard/update_window.cpp`) lists those games, and
  those of any folder added (a game's folder, or one that holds games: an
  earlier PS2Native's folder, or its `games` folder), each with how its
  programs stand: up to date, made with an earlier PS2Native, or made with a
  later one. Those made earlier are ticked; "Bring up to date" does it.
- **Bringing a folder up to date** (`BringUpToDate`, `wizard/package.cpp`,
  which shares the copying with the making of a folder) gives it every file
  of the runtime a game's folder has, the launcher under the game's name and
  the toolchain in the place of its own, and writes `programs.txt`: the
  version, and a fingerprint of the files the programs were copied from.
  Nothing else of the folder is touched. It is refused while a program of the
  folder runs or its recompile compiles, and for a folder a later PS2Native
  made. Where PCSX2's GameDB plays the game with FullVU0SyncHack, `make.txt`
  gets the line a folder made before 2026-11-21 lacks.
- **The folder's own recompile** (`wizard/remake_main.cpp`) then finds
  everything left to compile before the game next starts, the toolchain being
  another, and its window says why: "Compiling <game> again with PS2Native
  0.2" (`work\brought-up-to-date.txt`, which the wizard leaves and the compile
  takes away).
- **For checks:** `ps2n-wizard --update <folder>` without a window, and
  `--updates <folder> --shots <folder> [--auto]` through the window.
- **The version** is 0.2 (`version.txt`).

### Tried on a folder made by 0.1 as released (2026-11-22)

`build\measure\update-from-01.ps1` and `update-more.ps1`, with the programs as
they are now in a folder of their own (kit14), everything out of sight.

- **A 0.1 folder.** 0.1 as released (the folder its download was made from)
  copied; its own wizard made Gran Turismo 3 from the maintainer's disc image
  (`--auto`, 12 s); the first session the maintainer played of the game
  (`20261118-095532`, 8,678 frames) put into the folder's sessions; and 0.1's
  own `ps2n-remake`, as after a session, compiled what the session met (54 of
  128 files, 22 s).
- **Brought up to date** by the new wizard without a window: 3.3 s. The
  runtime of 15,617,024 bytes in the place of 0.1's of 11,272,192, the
  toolchain of 331 files in the place of 328, `programs.txt` saying 0.2; its
  session and its captures as they were.
- **Its own `ps2n-remake --before-play`,** as its launcher runs it before the
  game starts: "the folder was brought up to date (PS2Native 0.2)", all 128
  files compiled, 30 s.
- **Its session run again whole:** the session at every one of 173
  fingerprints; nothing of the executable's code interpreted, no block of the
  IOP's or of the vector units'; 4.40% of the EE's cycles interpreted
  (1,891,555,820), the BIOS's kernel, to the cycle what the same folder
  interprets with 0.1's programs and the modules 0.1 compiled (the session is
  older than the BIOS's captures in folders, and a folder alone has no other
  game's beside it).
- **Through the window** (`--updates --auto`, a copy of the 0.1 folder as it
  was): listed as "Made with an earlier PS2Native.", ticked, brought up to
  date in 3.0 s, then "Brought up to date: it compiles its game again before
  it next starts."
- **A second time:** "Up to date.", and no file of the folder's root changed.
- **A folder whose runtime is in use:** refused ("is running (ps2n-host.exe):
  close it first."), nothing changed.
- **A session in its window** from a folder brought up to date (700 frames,
  muted, out of sight): written down with `frames-a-second 60`, and run again
  the session at every one of 14 fingerprints.

### A folder's counts after it was brought up to date (2026-11-22)

ADR 0046's point 6, put to the maintainer with this. A folder's counts are of
the code the recompiler made when it counted; where a later recompiler makes
other code for a function, the compiler drops that function's counts. Measured
with the counts the folders of 2026-11-21 took before that day's two changes
(`build\measure\kit9`: before the COP2 information was decided when the code
is generated), given to today's folders in the place of their own and each
compiled again whole by its own `ps2n-remake`; in the same turns as above,
means in seconds:

| Game | Its own counts | An earlier recompiler's counts | No counts |
|---|---:|---:|---:|
| Jak and Daxter | 54.75 | 56.54 | 58.46 |
| Gran Turismo 3 (the second set) | 20.99 | 21.04 | 21.48 |
| Agent Under Fire | 26.78 | 26.85 | 27.05 |

(The rounds with the earlier counts: Jak and Daxter 56.53, 56.60, 56.48; Gran
Turismo 3 21.12, 21.05, 20.96; Agent Under Fire 26.87, 26.77, 26.91.) Jak and
Daxter, whose hot functions are full of COP2 instructions, keeps about half of
what its counts give (3.3% more time than with its own); the other two keep
all of it, to within the rounds.

**Built so, for the maintainer to decide otherwise:** a folder brought up to
date with another toolchain is compiled with the counts it has, and counts
once more after a session (`work\count-again.txt`, which the wizard leaves
where the folder's toolchain changed and the folder has counts; its window
says "Once more, now that its folder was brought up to date"). A game an
earlier PS2Native could not count for is tried again. Tried on a copy of a
folder with its counts, given a toolchain of another time and brought up to
date: its `ps2n-remake` after a session compiled the game again, counted on
its session (the run the session at every fingerprint compared) and compiled
with the new counts, 111 s in all; after the next session it found nothing to
do.

### On Linux (2026-11-22)

Under WSL 2, half the processors, nothing shown (Qt's offscreen platform).

- **The Linux folder** made from the module tree, its runtime built as a
  release's (`scripts/linux/kit.sh`, which says so).
- **A 0.1 folder.** 0.1's Linux download unpacked; its own wizard made Gran
  Turismo 3 from the maintainer's disc image (19 s); the same session of the
  maintainer's put into its sessions; 0.1's own `ps2n-remake`, as after a
  session, compiled what it met (128 files, ten at a time, 34 s).
- **Brought up to date** by the new wizard without a window: 2.1 s;
  `programs.txt` saying 0.2, the toolchain of 336 files in the place of 333.
- **Its own `ps2n-remake --before-play`:** "the folder was brought up to date
  (PS2Native 0.2)", all 128 files compiled, 24 s.
- **Its session run again whole,** drawn by OpenGL: the session at every one
  of 173 fingerprints, in 47 s; no block of the IOP's or of the vector units'
  interpreted. (With nothing drawn the run of this game does not reach its
  end, on any PS2Native: PCSX2's null renderer stops at an assertion of its
  own, `GSState::IsCoverageAlphaSupported(): Not implemented`, some way in.
  Its first 900 frames, twice: the session at every fingerprint compared.)
- **A second time:** "Up to date.", the folder unchanged.

**The maintainer's game folders given the final runtime** (their "3. Yes").
On Windows, the six folders again, the same way as in the morning
(`build\measure\runtime-into-folders.ps1`, tried on a copy first): the
runtime built with the profile made again, 15,617,024 bytes, in the place of
the morning's; nothing compiled again; the start of a session of each run
again the session at every fingerprint compared. Their Windows PS2Native
folder (`build\kit`) made again from the same tree: its wizard is the one
with "Games made earlier...". On Linux, their PS2Native folder made again,
and their one game folder, Jak and Daxter: first a copy given the runtime
alone (its `ps2n-remake --before-play` found nothing to compile, 600 frames
of a session the session at every one of 12 fingerprints compared), then the
folder itself, the same. Their folders have the runtime of 0.2 and, of the
rest, the programs of 2026-11-21: they count by the game's own frame rate
once they are brought up to date, which is theirs to do or to ask for.

### Times on Linux, under WSL 2 (2026-11-22)

Nothing had been timed on Linux. Under WSL 2 Windows places the processors
where it will, and a run's time was not expected to repeat; it does, to
about 1% here. Jak and Daxter's session `20261119-110909` (8,357 frames) run
again with nothing drawn, the folder the one made on Linux for the training
runs, in turns, three rounds, every run the session at every fingerprint
written down; stock PCSX2 is `stock-jit` on the runtime of a tree built with
link-time optimization and no profile (`build/linux-clang-stock`, 209 s to
build). Means, in seconds:

| Jak and Daxter on Linux | Seconds | Times stock PCSX2's time |
|---|---:|---:|
| Stock PCSX2, built as released | 76.53 | 1.00 |
| Compiled, without counts, the runtime built without either (as 0.1's) | 94.05 | 1.23 |
| Compiled, without counts, the runtime as 0.2 has it | 85.57 | 1.12 |
| Compiled, with the folder's own counts, the runtime as 0.2 has it (a second set: stock PCSX2 75.85) | 79.38 | 1.05 |

(The rounds: 77.05, 76.76, 75.77; 94.22, 94.36, 93.58; 85.15, 85.65, 85.90;
and, the folder counted by its own `ps2n-remake` on 9,000 frames of the
maintainer's session `20261118-120316`, 406 s in all for the round, stock
PCSX2 76.71, 75.69, 75.15 and the compiled game 79.44, 79.47, 79.24.)

- **The runtime as 0.2 has it takes 9.0% off the compiled game's time on
  Linux,** and the folder's own counts 7.2% more.
- **On Linux the compiled game is not at PCSX2's time:** 1.05 times it as 0.2
  has it, where 0.98 on Windows for the same session. Without counts the two
  systems are further apart (1.12 on Linux with the runtime as 0.2 has it,
  1.05 on Windows). Why is not known yet: the compiled code is the same C++
  on both, compiled for another calling convention and into shared objects.
  One game, one session, inside WSL: it says where Linux stands, and that it
  is worth measuring on its own.

### The maintainer's answers, and the release (2026-11-22)

The session put five decisions to the maintainer with what this chapter and
the one before it measured, with its recommendation for each. Their answers:
"1. Yes", "2. Yes, prepare the release", "3. Yes", "4. Do all seven",
"5. After"
([ADR 0049](adr/0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md)):

1. **Committed and pushed.**
2. **0.2 is released from that commit:** both downloads made from it, and the
   release a draft on GitHub with its notes, for the maintainer to read and
   publish.
3. **A folder brought up to date with another toolchain counts its profile
   once more after a session,** as it was built.
4. **The session brings the maintainer's own game folders up to date,** the
   six on Windows and the one inside WSL, with the release's programs.
5. **Linux's speed after 0.2:** why a compiled game takes more than stock
   PCSX2's time there is looked for once the release is out.

Neither system's profile of play is made again for the release: the runtime's
source is as it was when each was made. Of `host`, `runtime`, `core` and
`cmake`, the one file newer than the profiles is `cmake/ps2n-inject.cmake`,
changed in how the build hands the profile to the compiler (a copy named
after what it holds), not in what the compiler is given.

### 0.2 made and published (2026-11-22)

From the commit `4b45373`, pushed (`build\measure\release-02.ps1`, and the
same steps inside WSL), the working tree as committed:

| | Windows | Linux, under WSL 2 |
|---|---|---|
| The module tree at the commit | built in 47 s, the runtime 15,617,024 bytes | nothing to build: only documents had changed since it was built, the runtime 42,344,672 bytes |
| Every test in it | 532 of 532 pass, the three comparisons of `dma/vif/stcycl` not run (189 s) | 531 of 531, the same three not run (133 s) |
| The folder for others | 487 files, 307,600,007 bytes | 592 files, 599,363,362 bytes |
| The download | `PS2Native-0.2-windows-x64.zip`, 115,248,300 bytes | `PS2Native-0.2-linux-x64.tar.xz`, 144,150,520 bytes |
| Its SHA-256 | `134e0d5fd1fc88b31eb07fd9199b59718fe0c6bc09177cc8841c82189286d7b9` | `fe7face23c112258d40b172e85573049652800b696488f80014bdfd317ebb0da` |

Both folders say their runtime is built with link-time optimization and with
a profile of play, and their licences name the commit.

**Each download unpacked elsewhere and tried,** with nothing but the system
on `PATH` (on Linux Qt's offscreen platform as well, so that nothing shows):
its runtime read the maintainer's `.chd` (`-disc-executables`: "boot
SLES_514.34"); its wizard made Jak and Daxter from its disc image with every
default (`--auto`: 17 s on Windows, 12 s on Linux, the disc's executable being
small and the game's code coming from its data); the wizard, asked to bring
that folder up to date, found it up to date; and the maintainer's session
`20261119-110909` run again whole from it was the session at every one of the
167 fingerprints written down, on both systems, 58.6% of the EE's cycles
interpreted, as in a game's first session (275 s on Windows, 285 s on Linux,
other work running beside both: a check, not a timing).

**The draft.** `gh release create v0.2 --draft --prerelease --target` the
commit, with both downloads and the notes; GitHub's sizes and SHA-256 of the
two files are those above.

**Published.** Asked whether they would publish it themselves, as they had
0.1, or have the session do it, and whether the website's move to 0.2 should
then be committed and pushed with this record, the maintainer answered
"1. Yes, publish" and "2. Yes do it". The session published the draft (`gh
release edit v0.2 --draft=false`): 0.2 is public since 2026-11-22 10:17 UTC,
a pre-release, the tag `v0.2` on `4b45373`, both files answering at their
addresses. The website's downloads, its roadmap and what it says of each
system then moved to 0.2 (`docs/index.html`, `docs/site/README.md`).

### The maintainer's game folders brought up to date (2026-11-22)

The maintainer's "4. Do all seven" (`build\measure\folders-02.ps1`; first on a
copy of the Jak and Daxter folder, which went as its folder did after it).
`build\kit` made again from the released tree, then each of the six folders:
its memory cards copied aside; `build\kit\runtime\ps2n-wizard.exe --update
<folder>` ("Made with an earlier PS2Native. Brought up to date (PS2Native
0.2)."); its own `ps2n-remake --before-play`, which compiled the game again
whole, and its modules put in place as the launcher puts them; the start of
one of its sessions run again with its new programs; and the wizard asked
again ("Up to date.", nothing changed).

| Folder | Brought up to date in | Files compiled | In | The start of a session run again |
|---|---:|---:|---:|---|
| Jak and Daxter | 2.0 s | 128 | 53 s | 900 frames of `20261119-110909`: the session at every one of 18 fingerprints |
| Killzone | 1.8 s | 138 | 126 s | 900 frames of `20261117-190101`: 18 of 18 |
| Silent Hill 3 | 2.0 s | 128 | 89 s | 90 frames of `20261118-171819`: 1 of 1 |
| Gran Turismo 3 | 2.0 s | 128 | 23 s | 900 frames of `20261118-095532`: 18 of 18 |
| Agent Under Fire | 2.1 s | 221 | 94 s | 900 frames of `20261117-185759`: 18 of 18 |
| 007 - Agent Under Fire (nine executables) | 2.0 s | 470 | 323 s | 900 frames of `20261117-224454`: 18 of 18 |
| Jak and Daxter, inside WSL | | 128, ten at a time | 45 s | 600 frames of `20261119-225148`: 12 of 12 |

(The times to compile are a whole `ps2n-remake --before-play`: its analysis
and code generation, then the files.) Nothing of a game's executables was
interpreted but the 4 instructions Agent Under Fire always has; its second
folder interprets 1.01% of the EE's cycles, the BIOS's program loader, as
before. In every folder the memory cards are the bytes they were, every
session is there, and nothing is left to compile. The folder inside WSL
(`~/ps2n/build/PS2Native`, made again from the released tree too) went the
same way. None of the seven has counts: each counts after its next session of
3 minutes of the game's own time, Killzone's among them now. A seventh folder
in `build\kit\games`, `SLES_505.39`, one of the session's own early checks, is
not a game's folder to the wizard (no `make.txt`) and stays as it is.

## Linux's speed (ADR 0049, ADR 0050)

The maintainer's "5. After" (ADR 0049): why a compiled game takes more than
stock PCSX2's time on Linux is looked for once 0.2 is out. What was known:
under WSL 2, Jak and Daxter's session `20261119-110909` (8,357 frames, nothing
drawn) took 76.5 s on stock PCSX2 and 79.4 s compiled, with the folder's own
counts: 1.05 times stock PCSX2's time, where Windows takes 55.7 s and 54.8 s,
0.98.

### The runtime's profile, on Linux too (2026-11-22)

`ps2n-host -profile` looked at the CPU thread on Windows only. On Linux now
(`host/profile.cpp`): a thread of the profile's own wakes every millisecond
and, when the CPU thread has run for most of it by its own processor time,
sends it a signal, whose handler notes where it was and the six words on top
of its stack; a thread that waits is left alone. The names come from the
symbol tables of the files the process has loaded, so there are no lines: the
report says where the time went by kind of code and by function, for compiled
code by function and offset, and for the system's functions by what called
them, as far as the stack's top says, with where in the host's file the call
returns to, for `llvm-symbolizer --inlines` to say what was compiled into
there. `tests/guest_test.cmake -DPROFILE=1` asks for it, as on Windows. (A
first version used a timer of the thread's own processor time: this kernel
looks at those at its tick, 250 times a second.)

### Where the time goes, Linux beside Windows (2026-11-22)

One profiled run of each, as 0.2 has them, in seconds (on Windows a sample is a
millisecond of the clock, on the fast cores; on Linux a millisecond the thread
ran, which it did for 99.9% of the run):

| Seconds | Windows, stock | Windows, compiled | Linux, stock | Linux, compiled |
|---|---:|---:|---:|---:|
| The whole run | 56.2 | 56.2 | 75.1 | 79.8 |
| Code PCSX2 generated | 27.3 | | 27.8 | |
| The game's compiled code | | 9.3 | | 11.4 |
| The compiled micro code | | 15.7 | | 17.4 |
| The IOP's compiled code | | 3.5 | | 4.7 |
| PS2Native's runtime and PCSX2's core | 21.9 | 20.3 | 23.6 | 21.6 |
| The system's libraries and the kernel | 5.5 | 5.8 | 21.7 | 22.8 |
| Not identified | 1.5 | 1.6 | 1.8 | 1.8 |

- **Under WSL 2 more than a quarter of either run is one function of the
  system's, `sem_post`,** which wakes a thread that waits: 19.6 s of stock
  PCSX2's 75.1 s, 20.7 s of the compiled game's 79.8 s. On Windows its
  counterpart, `ZwReleaseSemaphore`, takes 3.3 s and 3.6 s.
- **Whose calls they are** (the stack's top, then `llvm-symbolizer`): 17.7 s
  of stock PCSX2's and 18.4 s of the compiled game's come from
  `vifCode_STRow`, through `VU_Thread::WriteRow` and `VU_Thread::KickStart`
  (`pcsx2/Vif_Codes.cpp:667`, `pcsx2/MTVU.cpp:542` and `:427`): PCSX2's VU
  thread, which neither run uses. The rest, about 2 s, wakes the GS thread
  for its packets.
- **The compiled code takes longer on Linux, kind by kind,** where the code
  PCSX2's recompilers generate takes the same: the game's 23% more, the micro
  code 11%, the IOP's 34%.

### What costs more inside WSL 2 than on Windows itself: a probe (2026-11-22)

The same small measurements built by the same compiler (clang 22.1.3) for both
and run on this computer (`build\measure\probe\probe.c`): calls through a
pointer to code packed tight and to code a page apart, in an order that
repeats and in one that does not; reads of a byte a page; and waking a thread
that waits.

| | Windows | WSL 2 |
|---|---:|---:|
| A call through a pointer, 2,048 places packed tight | 6.4 ns | 6.9 ns |
| The same, a page apart: 2,048 pages | 26.6 ns | 26.2 ns |
| 8,192 pages (32 MB of code) | 106 ns | 115 ns |
| 131,072 pages (512 MB) | 145 ns | 192 ns |
| A call whose place cannot be foreseen, of 1,024 | 6.7 ns | 7.0 ns |
| A read of one byte a page, 131,072 pages | 9.1 ns | 9.9 ns |
| Waking a thread that waits: the call alone | 1.2 µs | 7.2 µs |
| The same, until the thread has answered | 4.7 µs | 22 µs |

- **Waking a thread costs six times as much inside WSL 2:** 7.2 µs where
  1.2 µs. At that price the 17.7 s are about 2.4 million wakes, 290 a frame,
  which on Windows come to 3 s: what its profile has.
- **Code and data cost the same on both** until they spread over more than
  the 8 MB the processor's second table of pages reaches; beyond, code costs
  8% more inside WSL 2 at 32 MB and a third more at 512 MB.
- A busy thread inside WSL 2 runs on a fast core: a loop that only the clock's
  rate bounds took 1.17 ns a turn there, 5.1 GHz (a slow core's 4.0 GHz would
  make it 1.5 ns).

### PCSX2 wakes its VU thread at every STROW and STCOL of VIF1 (2026-11-22)

PCSX2 can carry VU1 out on a thread of its own (its "MTVU" speed hack), which
PS2Native has fixed off (`vuThread`, `host/game_settings.h`): VU1's micro code
is compiled code's, on the CPU thread. The thread exists all the same, asleep.
And two of VIF1's commands, STROW and STCOL, which set the row and the column
an unpack fills in, are mirrored into that thread's own copy of them whether
it is in use or not: `vifCode_STRow` and `vifCode_STCol` (`pcsx2/Vif_Codes.cpp`),
and a write of the row and column registers (`pcsx2/Vif.cpp`), call
`VU_Thread::WriteRow` and `WriteCol` (`pcsx2/MTVU.cpp`), which put a command
into the thread's queue and wake it (`KickStart`). The thread wakes, copies
16 bytes and goes back to sleep. Every other use of the thread in those files
is under `THREAD_VU1`. Jak and Daxter sends about 290 of these commands a
frame.

**A trial without it,** PCSX2's source not touched: a copy of the submodule in
the build tree (`build/pcsx2-trial`), where the ten calls are made only when
the thread is in use (`&& THREAD_VU1`), built in trees of its own
(`build.sh --pcsx2-source`, which the Linux script now has as `build.ps1` has
`-Pcsx2Source`). On Linux, three rounds in turns, every run the session at
every one of its 167 fingerprints:

| Jak and Daxter, Linux under WSL 2 | Seconds | Rounds |
|---|---:|---|
| Stock PCSX2 | 75.43 | 74.78, 75.69, 75.82 |
| Stock PCSX2 without the wakes | 54.06 | 53.98, 54.04, 54.17 |
| The compiled game | 79.52 | 79.31, 79.76, 79.49 |
| The compiled game without the wakes | 56.73 | 56.83, 56.69, 56.66 |

- **Without the wakes both take 28% less time under WSL 2:** 21.4 s and
  22.8 s less, more than the 17.7 s and 18.4 s the calls themselves took.
- **Stock PCSX2 inside WSL 2 then takes what it takes on Windows** (54.1 s;
  Windows, with its own wakes, 55.7 s), and in its profile the code PCSX2
  generates, PCSX2's core and the compiled micro code take what they take on
  Windows (27.0 s, 21.9 s, 15.7 s). What was slower in them was what a wake
  leaves behind in the processor's caches, 30,000 times a second.
- **The compiled game's distance to stock PCSX2 stays:** 1.05 times its time
  with the wakes and without. What is left of it is in the game's compiled
  code (11.1 s where Windows 9.3 s) and the IOP's (4.4 s where 3.5 s).

### What Linux's compiler made of the same code (2026-11-22)

One generated file of the game (`game_outside_part003.cpp`, 452 functions)
compiled by the same compiler for both systems, as each one's folder compiles
it, without counts (`clang --driver-mode=cl --target=x86_64-pc-windows-msvc`
makes Windows's on Linux): 8.8% more code for Linux. Two causes, both in how
Linux passes values between functions:

- **The 128-bit instructions' lanes.** `ops_mmi.h` writes them as loops over a
  register's lanes, held in a small struct (`Lanes`, 16 bytes) that a helper
  returns by value, and leaves it to the compiler to make host SIMD of the
  loops. Windows returns 16 bytes through memory. Linux returns them in two
  general registers, and the compiler, left to put the lanes together again
  out of two 64-bit values, did not: in `fn_00721160`, PSRLH and PMINH came
  out a lane at a time, with eight 16-bit stores read back as one register,
  where Windows's build has `psrlw` and `pminsw`. That function took 0.63 s
  of the run on Linux and 0.31 s on Windows. **Kept:** `Lanes` has a
  destructor that does nothing, with which it goes through memory on Linux
  too; Linux's build then has the same instructions, and the function takes
  0.31 s. (Windows's build of the file is the same size to the byte with it.)
- **The registers a call keeps.** Windows's convention keeps xmm6 to xmm15 for
  the caller, Linux's none, so compiled code that holds FPU and VU0 values in
  them saved them around every call of the runtime: the worst function,
  `fn_007C324C`, was 7,175 instructions on Linux where 5,149 on Windows, with
  1,080 saves of an SSE register where 12. **Tried and taken out:** the
  runtime's 33 functions that compiled EE code calls given Windows's
  convention on Linux (clang's `ms_abi`). The function came to 5,185
  instructions and the game's module to 36.4 MB from 38.1 MB, but what
  compiled code no longer saved, the runtime's functions now saved themselves
  at every call: in the profile the game's code took 0.9 s less with both
  changes, and the runtime's functions for loads, stores and VU0 calls 0.3 s
  more; the whole run 80.09 s against stock PCSX2's 76.34 s (1.049), where
  79.52 s against 75.43 s before (1.054). The calls are on ways seldom taken,
  and the compiler had put the saves there.

### The measurement folder's own micro code (2026-11-22)

The Linux run interpreted 28 of VU1's 1,776 blocks (4.7 million of 1,738
million block runs, 0.7 s), where the Windows run interprets none: the Linux
folder was made from the Windows folder's captures, and the plans of those
blocks differ between the two systems (60,508 steps where 60,489). A user's
folder compiles them after its first session, as anything a session meets;
the measurement's had not played. So 20,000 frames of the session it counted
on, not the one timed, were run again from it, what it interpreted put into
its captures (1,750 of VU1's blocks, from 1,722) and compiled (17 of 128
files); the timed run then interprets none.

### Code on large pages, tried and taken out (2026-11-22)

The modules' code and the runtime's own moved onto the processor's 2 MB pages
when a run starts (memory of no file that asks for large pages, moved over the
file's with `mremap`: 32 of the game's 34 MB, 2 of the IOP's 4.7 MB, 10 of the
runtime's 13 MB). Three rounds in turns: stock PCSX2 76.14 s, and 75.23 s
with the runtime's code on large pages; the compiled game 78.40 s, 78.20 s
with its modules' code on them, 78.90 s with the runtime's as well. Nothing
for the compiled game: taken out.

### Where Linux stands (2026-11-22)

| Jak and Daxter, Linux under WSL 2 | Stock PCSX2 | Compiled | Times stock PCSX2's time |
|---|---:|---:|---:|
| As 0.2 has it | 75.43 s | 79.52 s | 1.05 |
| With the lanes, and the folder's own micro code | 76.14 s | 78.40 s | 1.03 |
| Without PCSX2's wake of its VU thread (a trial: not in) | 54.06 s | 56.73 s | 1.05 |

(The rounds of the second line: 75.79, 75.79, 76.84 and 78.05, 78.83, 78.31,
every run the session at every one of 167 fingerprints. The third line's
change is built in since, at the maintainer's word: "PS2Native's first change
to PCSX2's code", below, has it timed again.)

- **What is left, 3%, is in the game's compiled code and the IOP's,** and its
  cause was not found. The IOP's blocks are the same instructions on both
  systems and do the same work (324 million block runs of 1,810 million
  instructions), in 4.4 s where 3.5 s. Looked at and ruled out: the calling
  convention and the pages the code is on (above); what the kernel does against
  speculation (nothing on jumps in a program's own code); which of the
  processor's cores the run is on (a fast one); what lies beside the IOP's and
  the EE's registers in the runtime's data (nothing another thread writes).
- **The larger matter is not Linux's:** PCSX2's wake of a thread that is not
  in use, which costs both runs 28% inside WSL 2. It is put to the maintainer
  (below), with what it costs on Windows.

### The wake on Windows, game by game (2026-11-22)

The same trial on Windows (`build\measure\vif-trial.ps1`: `build\pcsx2-trial`,
the trees `build\windows-clang-trial` and `-trial-stock`, kit13's folders with
those runtimes), each game's timed session, two rounds in turns, every run
the session at every fingerprint written down. Means, in seconds:

| | Stock PCSX2 | Without the wakes | The compiled game | Without the wakes |
|---|---:|---:|---:|---:|
| Jak and Daxter | 56.20 | 51.40 | 55.10 | 50.21 |
| Gran Turismo 3 | 21.10 | 20.72 | 21.00 | 20.97 |
| Agent Under Fire | 26.76 | 27.45 | 26.94 | 26.93 |
| Killzone | 43.89 | 43.99 | 40.69 | 40.81 |

(The rounds, in that order of columns: 56.14, 56.25; 51.25, 51.54; 54.56,
55.64; 49.89, 50.53. 21.10, 21.09; 20.62, 20.81; 21.03, 20.96; 20.85, 21.08.
26.70, 26.82; 27.27, 27.63; 26.76, 27.11; 26.78, 27.07. 43.89, 43.88; 43.68,
44.29; 40.67, 40.70; 40.65, 40.96.)

- **Jak and Daxter takes 9% less without the wakes on Windows too,** stock
  PCSX2 (8.5%) and the compiled game (8.9%) alike: more than the 3.3 s and
  3.6 s the calls themselves take there. Against stock PCSX2 as it is
  released, the compiled game without them takes 0.89 times its time, where
  0.98.
- **The other three games do not change:** they send few of these commands.
  (Their stock PCSX2 from the trial's tree differs from the other tree's by
  up to 2.6% either way, Agent Under Fire's the wrong way: two builds of the
  same code, without a profile, laid out differently.)
- **PCSX2's source today has the same lines** (`master` read on 2026-11-22),
  and none of its issues or pull requests mentions them (three searches).

**Put to the maintainer:** whether PS2Native's own builds leave those wakes
out, which would be the first change it carries to PCSX2's code (the
submodule as it is, the change applied to a copy when the runtime is built,
stock PCSX2 timed without it as before); and whether PCSX2 is told. Answered
the same day, yes to both ("The maintainer's answers", below).

### The tests, with what is kept (2026-11-22)

What is kept of the day: the profile on Linux (`host/profile.cpp`, and the
host starting it there), the lanes' struct (`ops_mmi.h`), `build.sh
--pcsx2-source`. Every test with it: on Windows 532 of 532 in the tree
releases are made from (the three comparisons of `dma/vif/stcycl` not run) and
534 of 534 in the development tree; on Linux 531 of 531 and 534 of 534. On
Windows the runtime and the modules of that tree are the size they were, to
the byte.

### The maintainer's answers (2026-11-22, ADR 0050)

To the four decisions put with the above: "1. Yes", with their reason; "2.
Yes, write an issue as a guide I'll write it myself."; "3. Yes"; "Stop."

- **PS2Native's builds leave PCSX2's wakes of its VU thread out,** the first
  change PS2Native carries to PCSX2's code: a patch in this repository,
  applied to a copy of PCSX2's source when the runtime is built, the
  submodule as it is, stock PCSX2 timed as released. Built next.
- **The maintainer tells PCSX2 themselves.** Asked for an issue written as a
  guide, the session wrote none (ADR 0010) and gave where it is, how it is
  seen and what limits it (ADR 0050 has all three).
- **Committed and pushed.**
- **The last 3% on Linux is left for now.**

### What "stock PCSX2" has had switched off: its VU thread (2026-11-22)

Found while the answers were written down:

- **PCSX2 switches its VU thread on by default** on a computer with three
  processor cores or more, on Windows and on Linux
  (`VMManager::SetHardwareDependentDefaultSettings`, `pcsx2/VMManager.cpp`).
  With it on, the thread is in use and the STROW and STCOL commands are its
  work: the wakes cost where it is off. That is PS2Native, always, and stock
  PCSX2 on a computer of two cores or where its user switched it off.
- **Every run of `ps2n-host` has had that thread off, stock PCSX2's among
  them** (`host/main.cpp`: "VU1 stays on the CPU thread so that runs
  repeat"). "Stock PCSX2's time" in these logs is the time of PCSX2's
  recompilers with VU1 carried out on the CPU thread, as PS2Native carries
  it out. Against stock PCSX2 with its VU thread on, as most of its users
  have it, no compiled game has been timed. Put to the maintainer, with a
  measurement to come.

## PS2Native's first change to PCSX2's code (ADR 0050)

The maintainer's "1. Yes" (ADR 0050): PS2Native's builds leave out PCSX2's
wakes of its VU thread where that thread is not in use, the first change
PS2Native carries to PCSX2's code. Until now the core was PCSX2 as released
(ADR 0002); ADR 0010 had said that a first patch would be recorded with how it
is applied and how it is dropped again.

### The change, carried by the build (2026-11-22)

- **The change** is one patch, `patches/pcsx2/0001-the-vu-thread-is-not-woken-where-it-is-not-in-use.patch`:
  the ten calls of `VU_Thread::WriteRow` and `WriteCol` in `pcsx2/Vif.cpp`
  and `pcsx2/Vif_Codes.cpp` are made only while the thread is in use
  (`THREAD_VU1`), as the trial had them. It says at its top what it changes,
  why, and when it was made; `patches/pcsx2/README.md` lists what the folder
  holds.
- **How a build tree gets it** (`cmake/ps2n-pcsx2-patches.cmake`, included
  once PCSX2's own CMake file has finished, when its targets exist): each
  file a patch changes is copied into the tree (`ps2n-pcsx2/src`), the
  patches are applied to the copies by git, and PCSX2's own targets are given
  the copies to compile in the place of the files they came from, each as the
  file it came from (what PCSX2's build says of that file alone, and its own
  folder searched first for what it includes by name). The submodule is not
  touched, and nothing else of PCSX2's build changes: a tree built before
  compiles two files again and links, 8 s for the development tree and 49 s
  for the release's with its link-time optimization. The assessment had it
  so ("a patch queue ... applied at configure time", 9.5).
- **Line endings.** The copies are patched in a small repository of their
  own in the build tree, whose one attribute has git read and write them with
  Unix line endings: a first try on Windows did not apply, the submodule's
  files having Windows line endings in this checkout and the patch Unix
  ones. The patch has Unix line endings on every system (`.gitattributes`),
  and so have the copies.
- **What stops a configuration, saying which patch and why:** a patch that
  does not apply; a patch that leaves a file as it was; a patch that changes
  a file no target of PCSX2's compiles. A header is such a file: the files
  beside it include it by name from their own folder and would not see a
  copy, so this way carries changes to compiled sources only (`.cpp`, `.c`).
- **Which trees have it:** every one but the tree stock PCSX2 is timed on,
  which is built with `build.ps1 -Pcsx2Patches off` (`build.sh
  --pcsx2-patches off`) and remembers it. In every other tree the tests'
  "stock" configurations therefore run PCSX2 with the change.
- **A runtime says which it is:** the second line of `ps2n-host -help` is
  "PCSX2's core in it has PS2Native's changes: 0001-..." or "PCSX2's core in
  it is as PCSX2 has it, with none of PS2Native's changes". And a release
  says so: `kit.ps1` and `kit.sh` put the patches into the folder's
  `licences/PCSX2-changes` and have `licences/README.txt` say that PCSX2's
  core in it is changed, as the GPL asks of a changed version; they warn
  when a tree's runtime was not built with the patches as they are.
- **Stock PCSX2 with its VU thread on** can now be timed: `ps2n-host
  -pcsx2-vu-thread` (`game.ps1 -Config stock-jit -Pcsx2VuThread`,
  `-DPCSX2_VU_THREAD=1` through `tests/guest_test.cmake`), refused with
  PS2Native's VU provider on VU1.

### The tests with it (2026-11-22)

- **Windows.** The three trees built: the release's (`build\windows-clang-module`,
  with the change), the development tree (with it), and the tree stock PCSX2
  is timed on (`build\windows-clang-stock`, built with `-Pcsx2Patches off`);
  each runtime's `-help` says which it is, and the submodule has no file
  changed. Every test: 532 of 532 in the release's tree (the three
  comparisons of `dma/vif/stcycl` not run, as before) and 534 of 534 in the
  development tree. A folder made from the release's tree in a scratch place
  (`kit.ps1 -Out`) has the patch in `licences\PCSX2-changes`, and its
  `licences\README.txt` says that PS2Native changed PCSX2's core in it.
- **Linux, under WSL 2.** The same three trees, the build's step unchanged
  there (the release's tree compiled the two copies and linked in 32 s):
  531 of 531 in the release's tree (the same three comparisons not run) and
  534 of 534 in the development tree.

### Timed on Windows (2026-11-22)

`build\measure\patch-01.ps1`: kit13's folders, each game as 0.2 has it (its
modules compiled with its own counts), each game's timed session, three
rounds in turns, the order turned round every second round. The runtimes:
stock PCSX2's, built again without the change (`build\windows-clang-stock`);
0.2's own; and the release tree's with the change, built as a release's is,
with 0.2's profile of play (the functions of the two changed files have no
counts in it until the profile is made again for a release). Means, in
seconds:

| | Stock PCSX2 | The compiled game as 0.2 has it | With the change | Times stock PCSX2's time, with the change |
|---|---:|---:|---:|---:|
| Jak and Daxter | 55.90 | 55.31 | 50.23 | 0.90 |
| Gran Turismo 3 | 20.96 | 20.91 | 20.83 | 0.99 |
| Agent Under Fire | 26.80 | 26.79 | 26.79 | 1.00 |
| Killzone | 43.92 | 40.79 | 40.71 | 0.93 |

(The rounds, in that order of columns: 56.17, 55.88, 55.65; 55.24, 55.58,
55.10; 50.48, 50.09, 50.13. 20.89, 21.08, 20.90; 20.78, 20.91, 21.04; 20.75,
20.95, 20.80. 26.82, 26.68, 26.91; 26.87, 26.59, 26.92; 26.73, 26.78, 26.86.
43.88, 43.96, 43.92; 41.09, 40.74, 40.54; 40.73, 40.64, 40.75. Every one of
these 36 runs is its session at every fingerprint written down: 167, 173, 180
and 186.)

- **Jak and Daxter takes 9.2% less time with the change,** 50.23 s where
  55.31 s, as the trial had it: 0.90 times the time of stock PCSX2 as these
  logs time it, where 0.99. The other three do not change (0.4%, 0.0% and
  0.2%, within the spread of their rounds).
- **The wake is gone from the profile:** in Jak and Daxter's compiled run
  with the change, `ZwReleaseSemaphore` takes 0.5 s (the GS thread's packets)
  where 3.6 s, and `vifCode_STRow` 0.06 s.

### Stock PCSX2 with its VU thread on (2026-11-22)

What the section "What 'stock PCSX2' has had switched off" owed: the same
sessions on stock PCSX2 with its own VU thread carrying VU1 out
(`game.ps1 -Config stock-jit -Pcsx2VuThread`), as PCSX2 has it by default on
this computer (the installed PCSX2's own settings file here has
`vuThread = true`). In the same turns as the timings above, three rounds,
means in seconds:

| | Stock PCSX2, VU1 on the CPU thread | Stock PCSX2, its VU thread on | | The compiled game, with the change | Times the time of stock PCSX2 with its VU thread on |
|---|---:|---:|---:|---:|---:|
| Jak and Daxter | 55.90 | 33.49 | 0.60 | 50.23 | 1.50 |
| Gran Turismo 3 | 20.96 | 18.43 | 0.88 | 20.83 | 1.13 |
| Agent Under Fire | 26.80 | 26.27 | 0.98 | 26.79 | 1.02 |
| Killzone | 43.92 | 33.33 | 0.76 | 40.71 | 1.22 |

(The rounds with the VU thread on: 33.45, 33.41, 33.62; 18.36, 18.39, 18.53;
26.44, 26.25, 26.11; 33.28, 33.47, 33.25.)

- **With its VU thread on, stock PCSX2 takes 40% less time on Jak and
  Daxter's session,** 24% less on Killzone's, 12% less on Gran Turismo 3's
  and 2% less on Agent Under Fire's: what a second processor core gives a
  game whose micro code is a large part of its run.
- **So the compiled games stand at 1.02 to 1.50 times the time of stock
  PCSX2 as most of its users have it,** where the logs, the website and
  0.2's notes say 0.98 to 1.00 (Killzone 0.92) against stock PCSX2 with VU1
  on the CPU thread. Both are true of what they compare; only the first is
  what someone who has PCSX2 beside PS2Native would see. These are runs
  without a frame limiter, nothing drawn or drawn at the console's
  resolution: how fast each can go. At the console's pace all of them play at
  full speed on this computer either way.
- **Runs with the VU thread on do not all repeat.** Jak and Daxter's, Gran
  Turismo 3's and Killzone's were their sessions at every fingerprint, all
  nine. Agent Under Fire's three were the session at 37 fingerprints and
  parted from it at frame 1,900, each with another fingerprint there: its
  time above is of runs that were no longer the session. It is why every
  run here has VU1 on the CPU thread.
- **PS2Native's VU provider cannot simply be put on that thread.** It
  follows PCSX2's VU recompiler as that behaves with the thread off: it
  takes a program's start from VU0's status register and clears it there at
  the program's end (`runtime/vu_interp.cpp`), where the recompiler with the
  thread on does neither (`recMicroVU1::Execute`, and the `THREAD_VU1` cases
  of `microVU_Branch.inl`); switched on as it is, it would carry nothing out.
  Carrying VU1 out on a thread of its own is a step of its own, put to the
  maintainer.
- **The CPU thread of stock PCSX2 with the VU thread on,** profiled once on
  Jak and Daxter (34.1 s): PCSX2's core 55.5%, the code PCSX2 generates
  36.5%, Windows 8.0%.

### Timed on Linux, under WSL 2 (2026-11-22)

A scratch script (`patch-01.sh`, kept in `build\measure\linux-tools`): Jak and
Daxter's timed session, nothing drawn, three rounds in turns on half the
processors, from the measurement folder as it was kept this morning (the
lanes, its own micro code, its counts). The runtimes: stock PCSX2's tree,
built again without the change; the release tree's as it was this morning,
put aside before the change; and the release tree's with the change. Means,
in seconds:

| Jak and Daxter, Linux under WSL 2 | Seconds | Rounds |
|---|---:|---|
| Stock PCSX2 | 76.01 | 76.31, 75.61, 76.10 |
| Stock PCSX2, its VU thread on | 36.13 | 36.22, 36.39, 35.79 |
| The compiled game before the change | 79.13 | 78.71, 79.52, 79.16 |
| The compiled game with the change | 57.34 | 57.19, 57.40, 57.43 |

(Every one of the twelve runs is the session at every one of its 167
fingerprints; the compiled runs interpret none of VU1's blocks.)

- **With the change the compiled game takes 27.5% less time under WSL 2,**
  57.34 s where 79.13 s, as the trial had it (56.73 s where 79.52 s). Against
  stock PCSX2 as these logs time it, which pays for the wakes there, that is
  0.75 times its time, where 1.04 without the change (1.03 this morning: the
  same two builds a few hours apart, 78.40 s and 76.14 s then).
- **With its VU thread on, stock PCSX2 takes 36.13 s there,** less than half
  its time with VU1 on the CPU thread (0.48), and near what it takes on
  Windows (33.49 s), where with the thread off the two systems are 36% apart
  (76.01 s and 55.90 s). Going by the times, and not looked at in a profile:
  a thread that is at work is seldom asleep when a command comes, so little
  is left of what waking it costs. Against stock PCSX2 so, the compiled game
  with the change takes 1.59 times its time under WSL 2.

### Where the compiled games stand to stock PCSX2, both ways (2026-11-22)

With the change, on the sessions above, a compiled game's time as a multiple
of stock PCSX2's:

| | Stock PCSX2 with VU1 on the CPU thread, as every comparison here has had it | Stock PCSX2 with its VU thread on, as it is by default |
|---|---:|---:|
| Jak and Daxter, Windows | 0.90 | 1.50 |
| Gran Turismo 3, Windows | 0.99 | 1.13 |
| Agent Under Fire, Windows | 1.00 | 1.02 (runs that left the session) |
| Killzone, Windows | 0.93 | 1.22 |
| Jak and Daxter, Linux under WSL 2 | 0.75 | 1.59 |

**Put to the maintainer, with these numbers:** whether the change and what
was built for it are committed and pushed; whether the website, the README
and 0.2's notes say which stock PCSX2 they compare with, and what it takes
with its VU thread on; whether the next speed step is VU1 carried out on a
thread of its own, held to stock PCSX2 with its VU thread on; and whether the
maintainer's own game folders get the runtime with the change now, a release
with it now or with that step.

### The maintainer's answers, and the comparison corrected (2026-11-22, ADR 0051)

To the six decisions put with the above: "1. Yes", "2. Yes", "3. Yes",
"4. Yes", "5. Wait for that step", "6. Leave it as it is".

- **Committed and pushed,** the change and what was built for it.
- **The public comparison corrected.**
  - The website: the first chart of "Measured" has a third bar for each game,
    stock PCSX2 with its VU thread on (0.60, 0.88, 0.98 and 0.76 of stock
    PCSX2's time with VU1 on the main thread), and its title, its
    explanation, its axis and its note say which stock PCSX2 the ratios are
    against, with the seconds and the compiled games' 1.65, 1.13, 1.02 and
    1.22 against stock PCSX2 as it is by default; the section's opening says
    it for every number of the page; the roadmap and the Linux platform's
    note likewise. Looked at as a browser draws it, at a desktop's width and
    a phone's.
  - The README's status says it for Windows and for Linux.
  - 0.2's notes on GitHub: two passages added, each marked "Corrected on
    2026-11-22", to the paragraph on speed and to the Linux limit (`gh
    release edit`, at the maintainer's word). The release's tag, its two
    downloads and the rest of its notes are as published; the text as
    published and as corrected is kept in `build\release`.
- **Next, VU1 on a thread of its own,** as PCSX2 does by default, measured
  against stock PCSX2 with its VU thread on (ADR 0051 has how the session
  proposes to take the step).
- **The maintainer's seven game folders get the runtime with the change;** no
  release with the change alone.

### The maintainer's game folders given the runtime with the change (2026-11-22)

At the maintainer's "4. Yes" (ADR 0051), from the committed tree (`004a1d6`),
the wizard's own way, as for 0.2 (`build\measure\folders-03.ps1`, which makes
`build\kit` again and runs `folders-02.ps1`; inside WSL the scratch
`release-02.sh theirs`):

- **The maintainer's own PS2Native folders made again,** on Windows
  (`build\kit`) and inside WSL (`~/ps2n/build/PS2Native`), their games left
  as they were: each now has the patch in `licences/PCSX2-changes`.
- **A trial first,** on the copy of the Jak and Daxter folder kept from the
  morning's (at 0.2's programs): `ps2n-wizard --update` said "Made with
  another build of PS2Native 0.2. Brought up to date", the version being the
  same and the programs' fingerprint another; the folder's own `ps2n-remake
  --before-play` compiled the game again (52 s), a toolchain header having
  changed (`ops_mmi.h`'s lanes); 900 frames of a session run again were the
  session at every fingerprint.
- **The six folders on Windows,** each the same way, its memory cards copied
  aside first and the same bytes after: Jak and Daxter (compiled again in
  52 s), Killzone (113 s), Silent Hill 3 (89 s), Gran Turismo 3 (22 s),
  Agent Under Fire (96 s) and 007 - Agent Under Fire (321 s). The start of
  one of each folder's sessions run again with its new programs is the
  session at every fingerprint compared (18 of them; Silent Hill 3's 90
  frames, 1); asked again, the wizard says "Up to date" and changes nothing;
  nothing is left to compile. None has counts yet.
- **The one inside WSL,** Jak and Daxter: compiled again in 27 s, 600 frames
  of its longest session the session at every one of 12 fingerprints.
- **Each folder's runtime says what it is:** the second line of its
  `ps2n-host -help` names the change.

## VU1 on a thread of its own (ADR 0051)

The maintainer's "3. Yes" (ADR 0051): the next speed step is VU1 carried out
on a thread of its own, as PCSX2 does by default, measured against stock
PCSX2 with its VU thread on. Where things stood: with that thread on, stock
PCSX2 takes 33.49 s for Jak and Daxter's session where the compiled game
takes 50.23 s (1.50 times its time), 33.33 s where 40.71 s for Killzone's
(1.22), 18.43 s where 20.83 s for Gran Turismo 3's (1.13).

### What the thread is, and what PS2Native's VU provider does differently on it (2026-11-22)

- **The thread and its machinery are PCSX2's core, and stay.** The main
  thread puts VU1's work into a queue (`VU_Thread`, `pcsx2/MTVU.cpp`: a
  program to run, with the VIF's TOP and ITOP and FBRST as they are then;
  what is written into VU1's micro and data memory; an unpack) and waits for
  the thread where it needs VU1's state. The thread takes the work and calls
  whichever VU1 provider is installed (`CpuVU1`): it is there whether PCSX2's
  VU recompiler is the provider or PS2Native's, and with the setting off it
  sleeps.
- **What PCSX2's VU recompiler does differently where the thread carries VU1
  out** (the places of `THREAD_VU1` in `pcsx2/x86/microVU*`), the provider
  now does too (`runtime/vu_interp.cpp`, `OnVuThread`):
  - a run is carried out when it is asked for, whatever VU0's status register
    says of VU1 (`recMicroVU1::Execute`);
  - where a program ends, pauses or stops at a T bit, VU0's status register
    is left alone and VU1's interrupt is not raised: the main thread is told
    (`VU_Thread::mtvuInterrupts`, as `mVUEBit` and `mVUTBit` tell it) and
    does both (`Get_MTVUChanges`);
  - XTOP and XITOP read the thread's own copy of the VIF's TOP and ITOP, and
    a pair with the T bit the thread's own copy of FBRST;
  - the floating-point control register is set at every run's start and put
    back at its end, not only where it differs from the EE's;
  - the EE's cycles are left alone at a run's end;
  - a reset and a shutdown let the thread finish first;
  - and VU0, where it reaches VU1's registers, waits for the thread
    (`WaitForVu1`, microVU's `mVUwaitMTVU`).
- **Compiled micro code reads TOP, ITOP and FBRST through three pointers**
  that whoever runs the program sets (`vu::Ctx`), so that the same compiled
  block serves both ways. What a module and the host share changed with it:
  the module version is 4, and a game's modules are compiled again. A block's
  plan did not change: the micro code captured so far is as good as it was.
- **The host.** `ps2n-host -pcsx2-vu-thread` now goes with PS2Native's VU
  provider (`game.ps1 -Pcsx2VuThread` with `ps2n-static`), and is refused
  with what reads VU1's registers from the main thread (`-sync`,
  `-sync-against`, `-trace`, `-trace-against`, `-vu-report`). What the host
  asks of the provider while a game runs (how much of the VUs' work was
  interpreted, for the notice; whether it stopped at something) it now asks
  in a way that is safe from another thread, and before a run's report it
  lets the thread finish. Every run without a window and without the option
  has VU1 on the CPU thread, as before; what a game played in a window has
  is below ("Where the thread is on").

### Held to PCSX2's VU recompiler, and the tests (2026-11-22)

- **The self-test has a mode more for VU1: as on PCSX2's VU thread.** The
  setting on, both sides run on the self-test's own thread as that thread
  would run them, the T bit's enable and TOP and ITOP in the thread's copies,
  and what the program's end tells the main thread compared with everything
  else: 2,000 random programs, 21,016 instruction pairs, none differing from
  PCSX2's VU recompiler. With a fault planted (the end not told), 1,662 of
  the 2,000 differ, each saying so ("what the end told the main thread:
  microVU 08, PS2Native 00").
- **Nine tests more, with VU1 on PCSX2's VU thread:** the seven
  console-recorded VU programs on PS2Native's providers
  (`ps2autotests/ps2n-static-vu-thread`), held to the recordings as
  `ps2n-static` is; and the picture a test program's VU1 draws, its packets
  reaching the GS from that thread, by PS2Native's VU provider and by
  PCSX2's VU recompiler (`frames/software/ps2n-static-vu-thread/vu`,
  `stock-jit-vu-thread`).
- **Every test passes with the step as it ends,** in the four trees: on
  Windows 543 of 543 in the development tree and 541 of 541 in the release's
  (the three comparisons of `dma/vif/stcycl` not run there, as before), on
  Linux 543 of 543 and 540 of 540, none skipped ("The tests on Linux",
  below). The 534 of before are among them.

### Timed on Windows, with the profile of play as 0.2 had it (2026-11-22)

`build\measure\vut-01.ps1`. The release's tree built with the step (every
test in it passing: 541 of 541, the three comparisons of `dma/vif/stcycl`
not run), a folder for others made from it (`build\measure\kit15`), and in it
a copy of each game's folder of kit13 brought up to date by that folder's
wizard and compiled by its own `ps2n-remake --before-play` with the counts
it has: what a user's folder goes through (Jak and Daxter compiled in 56 s,
Gran Turismo 3 in 27 s, Agent Under Fire in 110 s). Stock PCSX2 from the
folders of the timing before (`rpstock`, `rpstockt`). Each game's timed
session, three rounds in turns, the order turned round every second round;
means in seconds:

| | Stock PCSX2, VU1 on the CPU thread | Stock PCSX2, its VU thread on | The compiled game, VU1 on the CPU thread | The compiled game, VU1 on PCSX2's VU thread | Times the time of stock PCSX2 with its VU thread on |
|---|---:|---:|---:|---:|---:|
| Jak and Daxter | 55.94 | 33.44 | 50.89 | 32.89 | 0.98 |
| Gran Turismo 3 | 20.97 | 18.39 | 20.77 | 18.65 | 1.01 |
| Agent Under Fire | 26.79 | 26.23 | 27.01 | 25.92 | 0.99 |
| Killzone | 43.78 | 33.41 | 41.04 | 31.76 | 0.95 |

(The rounds, in that order of columns: 56.06, 55.47, 56.28; 33.24, 33.28,
33.79; 50.91, 51.18, 50.58; 32.71, 32.97, 33.00. 21.01, 20.95, 20.94; 18.51,
18.27, 18.39; 20.91, 20.73, 20.68; 18.63, 18.58, 18.75. 26.75, 26.77, 26.84;
26.24, 26.22, 26.24; 27.26, 26.91, 26.85; 26.14, 25.81, 25.80. 43.71, 44.04,
43.60; 33.30, 33.58, 33.36; 41.24, 41.06, 40.81; 32.52, 31.24, 31.53.)

- **With VU1 on PCSX2's VU thread a compiled game takes 0.95 to 1.01 times
  the time of stock PCSX2 with its VU thread on,** where in the same turns it
  takes 1.03 to 1.52 with VU1 on the CPU thread: Jak and Daxter 35% less time
  than without the
  thread, Killzone 23% less, Gran Turismo 3 10% less, Agent Under Fire 4%
  less. Gran Turismo 3 is 0.26 s behind stock PCSX2 so (1.4%).
- **The runs are the sessions where stock PCSX2's are.** Jak and Daxter's,
  Gran Turismo 3's and Killzone's runs with the thread on, stock and
  compiled, are their sessions at every fingerprint (167, 173 and 186), all
  eighteen, nothing interpreted. Agent Under Fire's part from the session at
  frame 1,900 on stock PCSX2 and on the compiled game alike, as before: its
  two times are of runs that were no longer the session.
- **One thing is not as a release would have it:** the runtime's profile of
  play is 0.2's, in which the functions this step changed (the VU
  provider's way into and out of a program among them) have no counts. With
  VU1 on the CPU thread the compiled games take up to 1% more than in the
  timing before for it (50.89 s where 50.23 s, 41.04 s where 40.71 s, 27.01 s
  where 26.79 s; Gran Turismo 3 20.77 s where 20.83 s).
- **A first run of the same folder,** one round alone before these: Jak and
  Daxter 32.07 s with the thread, 50.76 s without, stock PCSX2 33.37 s and
  56.55 s.

### Timed on Linux, under WSL 2 (2026-11-22)

The scratch `vut-01.sh`, as the timings before on Linux: the release's tree
built with the step (link-time optimization, the profile of play as 0.2 had
it there), the Linux folder made from it, and a copy of the measurement's
folder of Jak and Daxter brought up to date by that folder's wizard and
compiled by its own `ps2n-remake --before-play` with the counts it has (128
files, 70 s at ten at a time). Stock PCSX2 from the tree left as it was
built for ADR 0050, as released. The session of the Windows timing, nothing
drawn, three rounds in turns, the order turned round in the second; seconds:

| | Rounds | Mean |
|---|---|---:|
| Stock PCSX2, VU1 on the CPU thread | 75.88, 74.06, 74.22 | 74.72 |
| Stock PCSX2, its VU thread on | 37.41, 37.35, 37.18 | 37.31 |
| The compiled game, VU1 on the CPU thread | 56.14, 56.01, 56.21 | 56.12 |
| The compiled game, VU1 on PCSX2's VU thread | 36.80, 37.16, 36.75 | 36.90 |

- **With VU1 on PCSX2's VU thread the compiled game takes 0.99 times the
  time of stock PCSX2 with its VU thread on** under WSL 2, where 1.50 with
  VU1 on the CPU thread (56.12 s against 37.31 s), and 34% less time than
  without the thread.
- **All twelve runs are the session at every one of its 167 fingerprints,**
  nothing of the micro code interpreted.
- **The same caveat as on Windows:** the profile of play is 0.2's. And it
  was timed before the step's last changes to the host (how the thread's
  setting is decided at a run's start, below), which are on no run's path;
  it was not timed again.

### Where the thread is on: a game played in a window, by default (2026-11-22)

- **In a window, as in PCSX2.** The host leaves the setting as the settings
  have it: PCSX2's default for the computer (on where it has three processor
  cores or more), a game's own setting over it, and PCSX2's game database
  over both (67 of its entries switch the thread off, none of them one of
  the five games tried). In the wizard's settings step PCSX2's own box for
  it, "Enable Multithreaded VU1 (MTVU)" on the Emulation page, is the
  user's: it was shown fixed off until now, and `vuThread` is no longer among
  the settings a game's settings file may not set (`host/game_settings.h`).
- **Every other run has VU1 on the CPU thread,** unless `ps2n-host
  -pcsx2-vu-thread on` asks for the thread (`game.ps1 -Pcsx2VuThread`): every
  test and comparison, a session run again, the run a game's folder counts
  its profile on. A run without a window is to repeat.
- **The host's word holds over a game's own settings file.** The core reads a
  game's settings over the host's, and reads them again when the game's
  executable starts, without telling the host. So the host puts what it
  decided into the configuration each time the core has read its settings,
  and into the game's settings as the core holds them in memory, never into
  the file (`OnSettingsLoaded`, `host/main.cpp`). The check below found it:
  before, a settings file that said `vuThread = true` put a run without a
  window on the thread, a comparison with a stock run among them, and one
  that said `false` undid `-pcsx2-vu-thread on`.
- **Decided once for a run.** PCSX2 hears its game database only from the
  moment a game's executable starts, and not while the next one is being
  loaded: for a game the database switches the thread off for, the thread
  would be on, off, on and off again within one run. PS2Native's change to
  PCSX2's code (ADR 0050) keeps the thread's copy of the VIF's row and
  column up to date only while the thread is in use, so the thread must
  never be switched on while a game runs; it was written when PS2Native
  never used the thread, and its note says so. So the host decides when the
  game's disc is read, before anything runs, the database heard then too,
  and the thread stays as decided for the run: such a game has it off from
  the start. A run's report says how many of VU1's runs were on the thread.
  The note at the top of the change, and the list of PS2Native's changes
  (`patches/pcsx2`), say what holds now; the change itself is as it was.
- **A session written down says when it was played with the thread on**
  (`vu-thread on` in its record), and a run of it again that parts from it
  says so.

Checked (`build\measure\vut-04.ps1`, `vut-05.ps1`; a game's window on the
measurement's own desktop, which nobody sees, muted):

- **Jak and Daxter played in a window,** 1,500 frames from its folder
  (kit15's, the release tree's runtime as the step ends), no word on the
  thread anywhere: VU1 on PCSX2's VU thread, all 171,118 of its runs, the
  session's record saying so, none of its micro code interpreted. That
  session run again
  without a window is the session at every one of its 30 fingerprints, with
  VU1 on the CPU thread and with the thread on.
- **The same as a folder the wizard made** (`-packaged`, a game settings
  file): the file saying nothing of the thread, VU1 on the thread (all
  129,196 runs) and the record saying so; the file saying `vuThread =
  false`, none of VU1's runs on the thread and the record saying nothing.
- **A game PCSX2's database switches the thread off for** (a line more for
  this game in a copy of the database, in a copy of its folder), played in a
  window the same way: none of VU1's runs on the thread, from the start, the
  record saying nothing of it, and PCSX2's own log saying that it set its
  switch off.
- **A session of the maintainer's run again without a window from a copy
  whose settings file names the thread,** 600 frames: the file saying
  `true`, VU1 on the CPU thread and the run the session; with
  `-pcsx2-vu-thread on`, on the thread whether the file says `true` or
  `false`, the run the session each time; and compared with a stock run
  (`same-as-stock`, 300 frames), the file saying `true`, both runs with VU1
  on the CPU thread and the same at every one of 1,005,323 sync points.
- **The wizard's settings step,** saved as pictures: the box for the thread
  is live on PCSX2's Emulation page, beside the ones still shown fixed (the
  EE's cycle skipping).

### A game whose run does not repeat with the thread on: Agent Under Fire (2026-11-22)

The timings above had Agent Under Fire's session, played without the
thread, part from itself at frame 1,900 when run again with it. Looked at
the other way round (`vut-04.ps1`, parts `aut` and `autagain`):

- **A session played with the thread on.** Its folder (kit15's, the release
  tree's runtime) played in a window by the script into its first mission
  (`tests/games/agent-under-fire/start.pad`), 4,600 frames, VU1 on PCSX2's
  VU thread as a window has it by default: its record says so, and none of
  VU1's micro code was interpreted.
- **That session run again without a window, three times:** twice with the
  thread on, once with VU1 on the CPU thread. Each is the session at its
  first 37 fingerprints and parts from it at frame 1,900, each with another
  state there (fingerprints `FEB3B93A71327CF7`, `1573A15D895E2AE7` and
  `5BCE05B31427FA0C`, where the session had `20919A98E1AFBE3C`).
- **So with the thread on this game's run is not one run:** the same input
  gave three different machines at frame 1,900, two of them with the thread
  on both times. Between frames 1,850 and 1,900 from power-on, something of
  what the main thread sees depends on when the thread did its work; what,
  was not looked for. Jak and Daxter's, Gran Turismo 3's and Killzone's
  sessions repeat with the thread on, at every fingerprint.
- **What follows for a game like it:** a session played with the thread on
  cannot be run again as itself, with the thread or without. A run of it
  again still runs (it is the same input from the same start), so a folder
  still counts its profile on it, but on a run that leaves what was played
  where the game's run first depends on the thread; and such a session is no
  session to compare runs on or to time. The sessions written down until now
  were all played without the thread, and are what they were.

### The tests on Linux, and the self-tests' compiled blocks there (2026-11-22)

- **Two tests had never run on Linux.** `selftest/vu-compiled` and
  `selftest/iop-compiled` run the self-test's random programs on blocks
  compiled ahead of time, from a capture that is made on the system and
  built in; without it they are skipped, and the Linux trees never had one.
  Every count of Linux's tests until now ("534, all passing") had those two
  skipped, not passed: ctest counts a skipped test among those that did not
  fail. It was in the test runs' own logs and in no report.
- **Linux has the scripts for them now:** `scripts/linux/vu-selftest.sh` and
  `scripts/linux/iop-selftest.sh`, the counterparts of the Windows ones (the
  programs run with every block interpreted and written down, the tree
  built with the capture). On Linux the IOP's 1,500 programs give 6,366
  different blocks, all compiled.
- **With both captures in both trees,** every test of the development tree
  passes there, 543 of 543, none skipped, and 540 of 540 in the release's
  tree (the three comparisons of `dma/vif/stcycl` not run, as on Windows),
  the nine with VU1 on PCSX2's VU thread among them.

### The runtime's profile of play made again, with the thread (2026-11-22)

A release's runtime is built with a profile of play (ADR 0047), and 0.2's
was counted with VU1 on the CPU thread: what the VU provider does on the
thread, and what the main thread does beside it, had no counts.

- **A training run can have VU1 on the thread:** a sixth part of its line,
  `vu-thread` (`scripts\windows\runtime-profile.ps1`,
  `scripts/linux/runtime-profile.sh`). Such a run of a session need not be
  the session; the script says so where it is not, and goes on.
- **Made again on Windows** (`-Training build\pgo\training-vut.txt -Out
  build\pgo\runtime-windows-vut.profdata`; 731 s with the runtime built to
  count): the seven runs of 0.2's profile, from the measurement's folders
  with the step's programs, six of them with the thread (Jak and Daxter's,
  Gran Turismo 3's and Killzone's play, Silent Hill 3's boot, and the first
  sessions of Jak and Daxter and of Gran Turismo 3, their micro code
  interpreted, VU1's on the thread), and Agent Under Fire's play without it,
  whose run with the thread leaves its session, which keeps the other way
  counted too. Every run of a session was the session at every fingerprint
  compared (216, 216, 216, 186, 60 and 60). The profile has 5,105
  functions.
- **Not made again on Linux:** the Linux timing above is with 0.2's profile
  there, and the profile is made again on each system for a release, from
  the tree released.

### Timed on Windows, as a release would have it (2026-11-22)

`build\measure\vut-06.ps1`, then `vut-01.ps1 -Parts time -Variant rv`, alone
on the computer. Two things are as a release's would be that were not in the
first timing above:

- **The runtime** is the release's tree's as the step ends, built with
  link-time optimization and the profile of play made again with the thread
  (every test passing in it: 541 of 541).
- **Each game's folder has its own counts, taken with these programs.** A
  folder brought up to date counts once more after its next session of three
  minutes (ADR 0049): each of the four folders did, by its own `ps2n-remake`
  as after a session, on its count session, never the one timed: Jak and
  Daxter in 196 s, Gran Turismo 3 in 95 s, Agent Under Fire in 396 s,
  Killzone in 345 s, each run it counted on the session at every fingerprint
  compared.

Stock PCSX2 from the folders of the timings before, as released (`rpstock`,
`rpstockt`). Each game's timed session, three rounds in turns, the order
turned round in the second; means in seconds:

| | Stock PCSX2, VU1 on the CPU thread | Stock PCSX2, its VU thread on | The compiled game, VU1 on the CPU thread | The compiled game, VU1 on PCSX2's VU thread | Times the time of stock PCSX2 with its VU thread on |
|---|---:|---:|---:|---:|---:|
| Jak and Daxter | 55.99 | 33.68 | 50.37 | 32.35 | 0.96 |
| Gran Turismo 3 (its second set) | 20.94 | 18.51 | 20.92 | 18.76 | 1.01 |
| Agent Under Fire | 26.91 | 26.31 | 26.85 | 25.83 | 0.98 |
| Killzone | 44.21 | 33.12 | 40.95 | 31.39 | 0.95 |

(The rounds, in that order of columns: 56.04, 56.03, 55.90; 33.70, 33.83,
33.52; 50.39, 50.38, 50.35; 32.20, 32.49, 32.37. 20.86, 20.84, 21.12; 18.48,
18.50, 18.56; 20.89, 20.95, 20.93; 18.79, 18.71, 18.79. 26.69, 27.05, 26.99;
26.07, 26.42, 26.45; 26.72, 26.92, 26.91; 25.78, 25.91, 25.79. 43.99, 44.44,
44.20; 32.76, 33.36, 33.24; 40.30, 41.38, 41.16; 31.31, 31.46, 31.41.)

- **With VU1 on PCSX2's VU thread a compiled game takes 0.95 to 1.01 times
  the time of stock PCSX2 with its VU thread on:** three of the four games
  less than it (4%, 2% and 5% less), Gran Turismo 3 1.4% more. With VU1 on
  the CPU thread the same folders take 1.50, 1.13, 1.02 and 1.24 times that
  stock PCSX2's time in these turns, and 0.90, 1.00, 1.00 and 0.93 times the
  time of stock PCSX2 with VU1 on the CPU thread.
- **The step's criterion (ADR 0051) is met by three games and not by Gran
  Turismo 3,** which is 0.25 s behind in 18.5 s; where its time goes is
  below.
- **What a player sees and hears is the same.** The last picture and the
  sound at the last frame are the same in all twelve runs of Gran Turismo 3
  and in all twelve of Killzone, and the sound in all twelve of Jak and
  Daxter (nothing drawn): stock PCSX2 and compiled, with the thread and
  without. All thirty-six are their sessions at every fingerprint.
- **Agent Under Fire's runs with the thread are no longer its session,** as
  before, stock PCSX2's three and the compiled game's three: each of the six
  ends with another picture and another sound; its six without the thread
  end with the same. Its two times with the thread are of runs that differ.
- **Gran Turismo 3 was timed twice.** The measurement's script first had its
  folder count on 9,000 frames, having given its copy of the session 50
  frames a second by hand, where this game is shown at 60 and three minutes
  are 10,800 (the same slip as in 0.2's measurement, "As the release has
  it"): timed so, 18.72 s with the thread (18.69, 18.82, 18.64) against stock
  PCSX2's 18.40 s (18.11, 18.63, 18.45), 1.02, and 21.08 s against 20.87 s
  without. Counted on its 10,800 frames (the run it counted on the session
  at all 216 fingerprints compared), the row above. A user's folder is not
  open to the slip: the runtime writes a session's frames a second down
  itself.
- **With VU1 on the CPU thread nothing was lost:** 50.37 s for Jak and Daxter
  where 50.23 s before the step, 20.92 s where 20.83 s, 26.85 s where 26.79 s,
  40.95 s where 40.71 s ("Timed on Windows", in the chapter before), each
  within what two sets of turns differ by.

### Where Gran Turismo 3's time goes with the thread (2026-11-22)

One profiled run of each side with the thread on (`vut-01.ps1 -Parts
profile`): the compiled game's main thread 19.1 s, stock PCSX2's 18.8 s. With
VU1 on a thread of its own, what the main thread carries is the EE's and the
IOP's code and the core's devices. In seconds of the main thread:

| | The compiled game | Stock PCSX2 |
|---|---:|---:|
| The game's EE code (PCSX2's: its EE recompiler's) | 4.4 | 4.2 |
| The EE's event test | 1.8 | 2.0 |
| The IOP's code, with its stores into IOP memory | 2.8 | 2.4 |
| The EE's accesses to devices (the runtime's and the core's functions for them) | 0.7 | 0.2 |
| System instructions carried out by the runtime, and the provider's loop | 0.6 | 0.1 |
| The sound | 2.6 | 2.8 |
| The films' decoder (IPU) | 2.1 | 2.1 |

- **The compiled side takes more** for the IOP's code, for the EE's accesses
  to devices (while its films play, this game reads the IPU's registers in a
  loop, and compiled code goes through the runtime for them), for the system
  instructions and for the EE's code itself; and less for the event test and
  the sound. Net, 0.3 s of 18.8 s.
- **They are costs ADR 0046 listed,** two of them tried then and taken out
  for want of a gain (accesses to devices asking only where they may fault,
  an IOP block's way out). With VU1 on the main thread they were made up for
  there: the thread takes 2.43 s of this session off stock PCSX2's main
  thread and 2.16 s off the compiled game's, PS2Native's compiled micro code
  being the cheaper of the two.

### What a profile is worth when its run left the session (2026-11-22)

A folder counts its profile on a run again of a session (ADR 0045), and a
session of a game like Agent Under Fire played with the thread on cannot be
run again as itself. Measured the other way round (`vut-06.ps1`, parts
`copy`, `drift` and `timedrift`): a copy of Agent Under Fire's folder counted
once more on the same session as its folder above, but with the thread on in
the run it counts on, which left the session at frame 2,300 of its 9,000
(the other's run was the session at all 180 fingerprints compared). Its
timed session then, the thread on, three rounds in turns:

| The folder's counts are of | Rounds | Mean |
|---|---|---:|
| a run that was its session | 26.03, 26.14, 25.85 | 26.01 |
| a run that left its session at frame 2,300 | 25.39, 25.95, 25.66 | 25.67 |

No loss shows: the difference is the other way, and within what this game's
runs with the thread differ by (no two of them are the same run; the first
folder took 25.83 s in the turns above).

### A game's first session, with the thread (2026-11-22)

`vut-06.ps1`, part `timefirst`: the first sessions the maintainer played of
Gran Turismo 3 and of Jak and Daxter, from folders made from the disc alone
(the game's code interpreted where the game loads it from its data, the
micro code interpreted), the release tree's runtime, two rounds in turns;
seconds:

| | VU1 on the CPU thread | VU1 on PCSX2's VU thread | |
|---|---:|---:|---:|
| Gran Turismo 3's first session (8,678 frames) | 68.44 (68.26, 68.62) | 55.56 (56.03, 55.09) | 19% less |
| Jak and Daxter's (2,564 frames) | 41.56 (41.24, 41.88) | 28.23 (28.41, 28.05) | 32% less |

All eight runs are the session at every fingerprint, the micro code of both
vector units interpreted at once on two threads. (As 0.2 has them: 69.6 s
and 44.8 s. Stock PCSX2 with VU1 on the CPU thread takes 20.8 s and 13.1 s;
with its VU thread on it was not timed on these.)

### Where the step stands, and what is put to the maintainer (2026-11-22)

- **Built, tested and measured;** in no folder of the maintainer's and in no
  release, and not committed.
- **Not tried:** a game in a window on Linux with the thread (inside WSL its
  window would have come up on the maintainer's screen; without a window the
  thread was run and timed there); any game but the five; the maintainer's
  own play.
- **Left as it is:** what in Agent Under Fire depends on the thread; Gran
  Turismo 3's 1.4%; the Linux profile of play, which a release makes again.
- **Put to the maintainer:** whether it is committed and pushed; whether
  their seven game folders get these programs (each compiles its game
  again); and whether the next release, 0.3, is prepared with it, after they
  have played with it or at once.

### The maintainer's answers (2026-11-22, ADR 0052)

"1. Yes", "2. Yes", "3. Yes", and "Do whatever needs doing from your side
(from your "Not done" category)":

1. **The step is committed and pushed.**
2. **The maintainer's seven game folders get these programs,** the release's
   own, made from 0.3's tree, each compiling its game again before it next
   starts.
3. **0.3 is prepared with it:** both downloads from the committed and pushed
   tree, the runtime's profile of play made again on each system, each
   download tried, a draft on GitHub that the maintainer publishes. The
   session had recommended it after the maintainer had played with the step;
   their answer is read as now, the draft leaving them free to play first.
   The version is 0.3 (`version.txt`).
4. **What the session had not done is done** (below, as it is done): its last
   edits carried into the Linux tree and the website looked at again (it
   renders as before, the changed passages where they belong); the Linux
   runtime's profile of play made again with the thread; and a game in a
   window on Linux with the thread.

## 0.3 (ADR 0052)

### Committed and pushed (2026-11-22)

At the maintainer's "1. Yes": the step, this log, ADR 0052 and the version,
0.3, in the commit `d10604d`, pushed.

### The maintainer's game folders given 0.3's programs (2026-11-22)

At their "2. Yes", from the release's own programs (below), the wizard's own
way, as for 0.2 (`build\measure\folders-03.ps1`, `folders-02.ps1`; inside WSL
the scratch `release-03.sh theirs`):

- **Their PS2Native folders made again** from the tree released, on Windows
  (`build\kit`) and inside WSL (`~/ps2n/build/PS2Native`), their games left
  as they were.
- **A trial first,** on the copy of the Jak and Daxter folder kept for it:
  brought up to date, compiled again (56 s), 900 frames of a session the
  session at every one of 18 fingerprints.
- **The six folders on Windows,** each the same way, its memory cards copied
  aside first and the same bytes after: Jak and Daxter compiled again in
  56 s, Killzone in 123 s, Silent Hill 3 in 98 s, Gran Turismo 3 in 24 s,
  Agent Under Fire in 103 s, 007 - Agent Under Fire in 352 s. The start of
  one of each folder's sessions run again with its new programs is the
  session at every fingerprint compared (18 of them each; Silent Hill 3's 90
  frames, 1); asked again, the wizard says "Up to date" and changes nothing;
  nothing is left to compile. None has counts yet, so none counts again.
- **The one inside WSL,** Jak and Daxter: brought up to date and compiled
  again in 27 s (128 files at ten at a time), 600 frames of its longest
  session the session at every one of 12 fingerprints, its memory cards the
  same bytes after; asked again, up to date.

### 0.3 prepared (2026-11-22)

At their "3. Yes", from `d10604d`, as 0.2 was (`build\measure\release-03.ps1`;
inside WSL the scratch `release-03.sh`, a copy of which is kept as
`build\measure\release-03-linux.sh`):

- **The runtime's profile of play made again on each system** from the tree
  released, the runs that are play with VU1 on PCSX2's VU thread, Agent Under
  Fire's without: on Windows the seven runs of `build\pgo\training-vut.txt`
  (475 s, 5,105 functions); on Linux the same seven from the Linux
  measurement's folders, each brought up to date by that tree's wizard and
  compiled again first (Agent Under Fire's nine executables in 611 s at ten
  at a time), in 1,168 s, 4,763 functions. Every run of a session was the
  session at every fingerprint compared. 0.2's profiles are kept beside them
  (`runtime-windows-02.profdata`, `runtime-linux-02.profdata`).
- **Every test passes in the trees the downloads are made from,** none
  skipped: on Windows 541 of the 544 (the three comparisons of
  `dma/vif/stcycl` not run), on Linux 540 of 543 likewise, and 543 of 543 in
  Linux's development tree.
- **The downloads:** `PS2Native-0.3-windows-x64.zip`, 115,257,596 bytes,
  SHA-256 `5e646b4f6ebce5bb0e1596fe039812a57c8945d597b5b0289bd48b43225fa67d`;
  `PS2Native-0.3-linux-x64.tar.xz`, 144,293,140 bytes, SHA-256
  `7ddad1842833d93f23fcc0fd239563cb7d644d7f9ff370d35096df2b5821bce3`. Each
  folder's licences name the commit, and say that PCSX2's core in it carries
  PS2Native's change number 1, with the patch in `licences/PCSX2-changes`.
- **Each tried unpacked elsewhere,** with nothing but the system on the
  path: its runtime reads Silent Hill 3's `.chd`; its wizard makes Jak and
  Daxter from its disc image with every default (16 s on Windows, 12 s on
  Linux, the game's code to be met in its first session), and asked again
  finds it up to date; the maintainer's session `20261119-110909` run again
  whole from that folder, as a first session (58.6% of the EE's cycles
  interpreted), is the session at every one of its 167 fingerprints (282 s on
  Windows, 215 s on Linux).
- **A game's folder made with 0.2 brought up to date,** on each system: a
  copy of the Jak and Daxter folder 0.2's own download made, given 0.3's
  programs by 0.3's wizard ("Made with PS2Native 0.2. Brought up to date
  (PS2Native 0.3)"), compiled again by its own `ps2n-remake` before it plays
  (10 s and 7 s, 77 files), the start of that session run again the session
  at every fingerprint compared (18 and 12), and, asked again, up to date.
- **The release's runtime timed again,** VU1 on PCSX2's VU thread on both
  sides, two rounds in turns, the same folders as in "Timed on Windows, as a
  release would have it" with the release's runtime
  (`build\measure\release-03-time.ps1`; inside WSL `vut-01.sh time`, Jak and
  Daxter's folder there); seconds:

  | | Stock PCSX2, its VU thread on | The compiled game, VU1 on PCSX2's VU thread | Times its time |
  |---|---:|---:|---:|
  | Jak and Daxter | 33.74 (33.90, 33.58) | 32.61 (32.97, 32.24) | 0.97 |
  | Gran Turismo 3 | 18.37 (18.26, 18.48) | 18.42 (18.22, 18.61) | 1.00 |
  | Agent Under Fire | 26.82 (26.98, 26.66) | 25.54 (25.45, 25.63) | 0.95 |
  | Killzone | 32.99 (32.82, 33.16) | 31.07 (30.68, 31.46) | 0.94 |
  | Jak and Daxter under WSL 2 | 35.90 (35.78, 36.02) | 35.50 (35.41, 35.59) | 0.99 |

  As the three rounds with the runtime before it, to within the rounds.
  Every run is its session at every fingerprint but Agent Under Fire's, whose
  runs with the thread are not one run, stock PCSX2's as the compiled game's.
  These are the numbers 0.3's notes give.
- **A game in a window on Linux with the thread,** inside WSL on this
  computer, the window on the maintainer's screen for its 28 seconds: a copy
  of the maintainer's Linux Jak and Daxter folder, brought up to date as
  above, its runtime started with the folder's own settings, muted, for
  1,500 frames. VU1 on PCSX2's VU thread as PCSX2 has it by default there
  (all 129,017 of its runs), drawn on OpenGL, none of the micro code
  interpreted, 56.51 frames a second where the console shows 59.94 (inside
  WSL, as before, a little under the console's pace); the session's record
  says `vu-thread on`. Run again without a window, with VU1 on the CPU thread
  and with the thread on, it is the session at every one of its 30
  fingerprints both times.
- **The draft:** a pre-release on GitHub, "PS2Native 0.3", its tag `v0.3`
  to be made on `d10604d` when it is published, with both downloads and its
  notes (`build\release\notes-0.3.md`), which say against which stock PCSX2
  each speed figure is, that Agent Under Fire's run with the thread on is not
  the same twice, and how a game made with 0.1 or 0.2 is brought up to date.
  It is published at the maintainer's word; until then the website's
  downloads and the README stay 0.2's.

### 0.3 published (2026-11-22)

The maintainer's answers to "Publish 0.3?" and "Then the website and the
README moved to 0.3, and the record committed and pushed?": "1. Publish",
"2. Yes".

- **Published** at 21:26 UTC (`gh release edit v0.3 --draft=false`), a
  pre-release, the tag `v0.3` made on `d10604d`, both downloads as they were
  made (GitHub gives their sizes as 110 MB and 138 MB).
- **The website's downloads moved to 0.3:** the section's title, text,
  buttons, file names, sizes and SHA-256s, the hero's button and line, the
  page's descriptions and its closing words; the roadmap has "0.3, the third
  release", and its "VU1 on a thread of its own" gives the numbers 0.3's notes
  give; the platforms' notes name 0.3. The README's Download and Status the
  same.

## PCSX2 2.9.189 tried (ADR 0053)

### What PCSX2 changed since the pinned version (2026-11-22)

At the maintainer's "Do 2" (ADR 0053), PS2Native was built and tested on PCSX2
v2.9.189 (`5977c525`, 2026-11-21), the newest when they were asked, nineteen
nightly versions after the pinned v2.9.170 (`a886f8ef`, 2026-11-10). PCSX2's
39 commits between the two were read in a clone of PCSX2 in the build tree
(`build\pcsx2-2.9.189`, its last 300 commits). Most are its renderers'
(depth buffers as targets and depth feedback in the Vulkan renderer, the
texture cache's choice of a target, Direct3D 12 and Metal), its interface's
(the big-picture interface's controller settings, the achievements' sidebar),
its game database's (699 lines; none of the five games' entries, nor those of
the maintainer's three untried games) and its translations'. What reaches
what PS2Native follows:

- **The count register** (`recMFC0`, `pcsx2/x86/iCOP0.cpp`), from v2.9.173,
  PCSX2's pull request 15187, its correction of the maintainer's issue 15186:
  a 32-bit addition, nothing carried into EntryHi.
- **`VURegs::nextBlockCycles`** (`pcsx2/VU.h`), from v2.9.174: 32 bits again,
  "an offset, not an absolute cycle" (save states of earlier versions refused).
  microVU writes it 32 bits wide, as before; its macro mode still reads 64 bits
  there (`xSUB(rax, ptr64[&VU0.nextBlockCycles])`, `microVU_Macro.inl`): the
  field and the four bytes of padding after it, which nothing writes.
  (*Corrected on 2026-11-23:* there is no padding after it. The four bytes
  that read takes past the field are the low half of `Mem`: "The 64-bit read
  of nextBlockCycles", below.)
- **microVU's XGKICK under the "XGKick Sync" game fix** (`microVU_Lower.inl`),
  from v2.9.174: the cycle it began at kept in 64 bits. PS2Native's VU provider
  refuses that game fix, so nothing follows from it.
- **PCSX2's VU1 interpreter** (`VU1microInterp.cpp`), from v2.9.174: its
  stalls counted against a 64-bit cycle. PS2Native follows microVU; only the
  tests' `stock-interp` runs have that interpreter.
- **The IOP recompiler's JR and JALR** (`rpsxJR`, `rpsxJALR`,
  `pcsx2/x86/iR3000Atables.cpp`), from v2.9.183, PCSX2's pull request 15198,
  which closed the maintainer's issue 15192 (ADR 0023): the jump's target kept
  in a host register of the IOP's own kind, so a delay slot that gives up every
  host register no longer loses it.
- **Its Windows dependencies:** DirectX-Headers and the Agility SDK 1.620.2,
  its CMake asking for DirectX-Headers 1.620.1 or later; Qt the same 6.12.0.
  Its Linux dependencies did not change.

### The branch (2026-11-22)

A git worktree beside the repository, `..\ps2n-pcsx2-189`, on a branch of its
own, `pcsx2-2.9.189`, from `4126455`; inside WSL the same as
`~/ps2n-pcsx2-189`. There the submodule is moved to `5977c525`, and on
Windows `deps\windows-x64` is PCSX2's rolling dependency package of
2026-11-21 (SHA-256 `c74f3608e7aba8f10aea7b71e829667de2d282ab1d98ca4e8ba04749697a4da6`,
the digest GitHub gives for it); inside WSL `~/ps2n`'s dependencies are used.
The main tree and its build trees are as 0.3 left them. PS2Native's changes,
ten files, nothing committed (`build\measure\p189.patch`):

- `runtime/include/ps2n/pcsx2_layout.h`: `nextBlockCycles` an `int32_t`,
  which the build's check against PCSX2's own structure holds
  (`runtime/pcsx2_check.cpp`).
- `runtime/ee_provider.cpp`: where microVU's macro mode reads 64 bits of it,
  the field zero-extended, which is what that read gives. The values
  PS2Native's VU provider writes there are never negative, so it gives what
  it gave. (*Corrected on 2026-11-23:* that read gives the field and the low
  half of `Mem`, not the field alone: "The 64-bit read of nextBlockCycles",
  below.)
- `runtime/include/ps2n/ops_cop0.h`: MFC0 of the count register a 32-bit
  addition. `tests/programs/countcarry.s` now expects "EntryHi moved by 0"
  under every configuration, and its known difference under PCSX2's
  interpreter is gone (`tests/programs-known-differences.cmake`);
  `host/selftest.cpp`'s note follows.
- `core/include/ps2n/r3000a.h`: the refusal of a JR or JALR whose delay slot
  gives up every host register taken out. PS2Native's IOP provider and the
  code it compiles already take the target before the delay slot, as the
  corrected recompiler does. The unit test that held the refusal holds the
  pair carried out (`tests/unit/core_tests.cpp`), and the IOP self-test's
  random programs include such pairs now (`host/selftest_iop.cpp`), so its
  capture for `selftest/iop-compiled` is made again on each system.

PS2Native's own change to PCSX2 (`patches/pcsx2/0001-…`) applies to v2.9.189
as it is. The runtime's profile of play, made with v2.9.170, is taken as it
is: the compiler sets aside what it has of 23 functions whose code changed, all
of them in PCSX2's Vulkan renderer and texture cache. A release on v2.9.189
would make the profile again, as every release does.

### Built and tested (2026-11-22)

`build\measure\p189-01.ps1` on Windows, `p189-linux.sh` inside WSL (a copy
kept as `build\measure\p189-linux.sh`):

| Tree | Built in | Tests |
|---|---:|---|
| Windows, development | 258 s | 543 of 543 |
| Windows, the one a release is made from | 393 s | 541 of 544: the three comparisons of `dma/vif/stcycl` disabled, as in 0.3's |
| Linux, development | 474 s | 543 of 543 |
| Linux, the one a release is made from | 264 s | 540 of 543: the same three disabled |

None skipped: the counts 0.3's trees give. The IOP self-test's capture made
again in 29 s on Windows, 25 s on Linux. `programs/stock-interp/countcarry`
passes as an ordinary test now, where it was an expected difference.

### The maintainer's sessions played back (2026-11-22)

The folder for others made from the Windows tree (`kit.ps1 -Out build\kit189`;
inside WSL `kit.sh`), and with it, as a user's computer would have it
(`build\measure\p189-02.ps1`):

- **A game made fresh:** its wizard made Jak and Daxter from its disc image
  with every default in 23 s, and asked again, found it up to date. The
  maintainer's session `20261119-110909` run again whole from that folder, as
  a first session, is the session at every one of its 167 fingerprints
  (330 s).
- **0.3's measurement folders brought up to date:** copies of the five
  folders 0.3's timing used (`build\measure\kit15\games`: the VU thread step's
  programs on PCSX2 v2.9.170, each with its own counts but Silent Hill 3's),
  given the new programs by the new wizard and compiled again by their own
  `ps2n-remake` before they play: Jak and Daxter 128 files in 86 s, Gran
  Turismo 3 128 in 41 s, Agent Under Fire 221 in 163 s, Killzone 138 in 108 s,
  Silent Hill 3 128 in 100 s. Inside WSL the Linux measurement folder of Jak
  and Daxter likewise, 128 files in 160 s.
- **Each game's timed session run again whole,** stock PCSX2 and the compiled
  game, from the folder on v2.9.170 and from its copy on v2.9.189, VU1 on the
  CPU thread and then with PCSX2's VU thread on (Agent Under Fire not with the
  thread: its runs with it do not repeat). Inside WSL, Jak and Daxter's: all
  eight runs the session at every one of its 167 fingerprints, the EE's
  cycles the same on both versions (41,554,654,904), and with the thread on
  all 27,162,857 of VU1's runs on PCSX2's VU thread on both. On Windows every
  run likewise:

  | Game | Session | Fingerprints | Runs | The session at every one |
  |---|---|---:|---:|---:|
  | Jak and Daxter (nothing drawn) | `20261119-110909` | 167 | 8 | 8 |
  | Gran Turismo 3 | `20261118-095532` | 173 | 8 | 8 |
  | Agent Under Fire | `20261117-185759` | 180 | 4 | 4 |
  | Killzone | `20261117-221825` | 186 | 8 | 8 |
  | Silent Hill 3 | `20261118-171819` | 1 | 8 | 8 |

  The compiled games' EE cycles are the same on both versions, to the cycle
  (Jak and Daxter 41,554,654,904, Gran Turismo 3 42,919,556,668, Agent Under
  Fire 53,078,292,439, Killzone 54,847,762,075). Silent Hill 3's session is a
  few seconds long, one fingerprint: it says little. Not a timing: one run of
  each, the computer busy with the other system's work meanwhile.

The kit's runtime started from the kit's own `runtime` folder with `-help`
says it cannot load `ps2n-game.dll` (`.so` on Linux): there is no game there.
0.3's download says the same; from a game's folder it answers, and says that
PCSX2's core in it has PS2Native's change 0001.

### Where it stands, and what is put to the maintainer (2026-11-23)

On PCSX2 v2.9.189, with the branch's ten changes, PS2Native passes every test
it passes on v2.9.170, on both systems; a game's folder is made and brought up
to date as before; and the maintainer's sessions run again on it are the
sessions at every fingerprint, stock PCSX2's runs and the compiled games', with
VU1 on the CPU thread and on PCSX2's VU thread, as on v2.9.170. Nothing was
timed.

Moving would take: the submodule moved to v2.9.189 and the branch's changes
into the main tree; PCSX2's newer Windows dependency package in `deps` (and
`docs/poc-log.md`'s record of it); and the places that name v2.9.170 as the
version PS2Native is built on brought up to date (52 files name it, 35 of them
code and tests whose notes say which PCSX2 a semantic follows; the logs and
decisions keep it where it was true). A release on it makes its profile of
play again and is timed, as 0.3 was. Put to the maintainer: whether PS2Native
moves to v2.9.189.

### Moved (2026-11-23)

The maintainer's answers: "1. Yes", "2. Yes", "3. I will, after. Make sure
they're all ready" (ADR 0054). In the main tree (`build\measure\p189-03.ps1`):

- **The branch's ten changes** applied (`build\measure\p189.patch`, the patch
  the Linux worktree had); **the submodule** moved to v2.9.189 (`5977c525`,
  fetched from the clone in the build tree); **PCSX2's dependency package of
  2026-11-21** in `deps\windows-x64`, the earlier one kept as
  `deps\windows-x64-2.9.170`, its archive beside it.
- **The version named where PS2Native says which PCSX2 it follows:** v2.9.189
  in the notes of 26 files (the code's, the README's, the website's), in the
  tests' notes on what stock PCSX2 gives, in the execution model's two lists
  of rules and in what it says the IOP provider does not carry out, in
  `docs/poc-log.md`'s record of the dependency package and in
  `docs/site/README.md`. The logs and decisions keep v2.9.170 where it was so.
- **Both trees built again and tested:** the development tree in 234 s, 543
  of 543; the IOP self-test's capture made again (19 s); the tree a release is
  made from in 272 s, 541 of 544, the three comparisons of `dma/vif/stcycl`
  disabled as before. None skipped. (Both were built again whole after the
  commit: below.)
- **Committed and pushed** at the maintainer's "2. Yes" (`6bc8a40`).
- **Inside WSL** (`build\measure\p189-move.sh`): `~/ps2n` at the commit, its
  PCSX2 at v2.9.189 (fetched from the worktree's clone of it); its Linux
  dependencies as they were. The development tree 543 of 543, the IOP
  self-test's capture made again, the tree a release is made from 540 of 543
  (the same three disabled); none skipped.
- **ADR 0053's worktrees and branch removed** on both systems, once the main
  trees had everything of them (on Windows its two links to the main tree's
  LLVM and Qt sources taken away first, by themselves).
- **The module version, found missing the same afternoon:** the move changed
  what a module of compiled code and the runtime share, the place of what
  follows `VURegs::nextBlockCycles`, but not `kGameModuleVersion`. At 32 bits
  the field follows `clipflag` directly, so `Mem`, the address of the VU's
  memory, and everything after it are eight bytes nearer: byte 1200 of the
  structure, where 1208 (PS2Native's description of both versions, compiled
  and printed; the build's check holds each to its PCSX2). `pcsx2_layout.h`'s
  comment had said the field had four bytes of padding after it, which is why
  the version was left. A copy of the maintainer's Jak and Daxter folder, its
  modules compiled by 0.3, given the new runtime for a measurement, stopped
  within 300 frames (with VU1 on PCSX2's VU thread and without; stock PCSX2
  from the same copy ran on). With this morning's runtime beside the libraries
  it was built with, those modules stopped the same way
  (`build\measure\jakmix.ps1`), so it was the modules, not the libraries. The
  runtime should have refused them. No folder of the maintainer's met it: a
  folder brought up to date compiles its game again before it next plays. The
  version is 5 since (`runtime/game_module.h`).

### The tree a release is made from, built again whole (2026-11-23)

The wizard of the maintainer's PS2Native folder, made again from that tree,
crashed (heap corruption, 0xC0000374) on all three new games: at or after its
settings step, Resident Evil – Code: Veronica X's folder made before it did,
the other two leaving empty folders. 0.3's own wizard took the same disc
through its settings step without a fault, the new one crashed there on Jak
and Daxter too, and the wizard built the night before in ADR 0053's worktree
had made Jak and Daxter whole. The cause was the build, not PCSX2: swapping
the dependency package under the existing trees recompiled only what PCSX2 or
PS2Native had changed (ninja does not track the package's headers, and 7-Zip
gives its files the archive's dates, older than the objects), so the trees
mixed objects compiled against both packages. The tests still passed; the
wizard did not. Windows' crash dumps, read with dbghelp
(`build\measure\dumpstack.ps1`), showed the corruption found when one of the
wizard's group boxes was deleted at a change of step.

Built again from nothing with its settings (`build\measure\p189-06.ps1`), the
tree passes 541 of 544 again; the development tree, cleaned and built again,
543 of 543. (A `cmake --build --target clean` had lost the release tree's
settings, and the build after it had made a default tree; that one went
nowhere.) The new wizard then takes Secret Agent Clank and Jak and Daxter
through its settings step without a fault. The maintainer's PS2Native folder
was made again from that tree; the three folders the crashed runs had left in
its `games` were removed, and the crash dumps Windows had kept of them.
Nothing in the commit changes: no binary is in the repository.

### The maintainer's three new games made ready (2026-11-23)

Each made by the wizard of their PS2Native folder (`build\kit`) from its disc
image with every default, as they would make it (`build\measure\games3.ps1`):

| Game | Serial | Made in | PCSX2's patches |
|---|---|---:|---|
| Resident Evil – Code: Veronica X | `SLUS-20184` | 43 s | its widescreen patch, applied; three other groups, not |
| Secret Agent Clank | `SCUS-97623` | 58 s | none |
| SSX Tricky | `SLUS-20326` | 61 s | none; PCSX2's game database rounds its EE's FPU to nearest |

They are the first discs from the USA PS2Native has run, on the maintainer's
European BIOS. Their folders are as the wizard made them; each was checked on
a copy (`build\measure\kit-new3`):

- **Played as its launcher plays it** (its own settings: a window, Vulkan,
  VU1 on PCSX2's VU thread), muted, out of sight, 5,400 frames, a controller
  script pressing START and then CROSS every 300 frames
  (`build\measure\press-start.pad`): each ran to its end. Its first session
  interpreted what a first session does (the IOP's code; Secret Agent Clank's
  code it loads from its data, 4.7% of the EE's cycles, as Gran Turismo 3's);
  the folder's own compile took it in (9, 58 and 10 s); the second session ran
  on compiled code, the notice "code met for the first time" in none of its
  half seconds.
- **Pictures** (no window, every 450 frames): Resident Evil – Code: Veronica X
  from its memory card notice through its logos, its intro film and title
  screen to a new game's opening text; Secret Agent Clank through its logos
  and title screen to its main menu, then a notice that a new game needs a
  memory card (the run has none); SSX Tricky through its intro film and title
  screen, the choice of rider, event, difficulty and venue, to a race's
  loading screen.
- **Against stock PCSX2** (same-as-stock, the first 1,800 frames, the same
  input): Resident Evil – Code: Veronica X and SSX Tricky the stock run at
  every sync point (SSX Tricky's 11,370,788), with the same picture and sound
  hardware. Secret Agent Clank at the same place at the same time at every
  sync point, and in every register but one: VU0's FBRST, from sync point
  2,623,746 on, `0x404` in the stock run and `0` in the compiled one, as in
  Agent Under Fire (docs/game-log.md, "G13"). The game resets VU0 and VU1 the
  way Sony's library does while the register's two D-bit enables are set, and
  PS2Native follows the console there (ADR 0005). Written out on both sides at
  that sync point and at the run's last (`build\measure\syncdetail.ps1`), it
  is the only register that differs.

### The 64-bit read of nextBlockCycles (2026-11-23)

Found while looking for why the module version had not changed ("Moved",
above). Since v2.9.174 PCSX2's `VURegs` has `u32 clipflag; s32
nextBlockCycles; u8* Mem;`, the field at byte 1196 and `Mem`, the address of
VU0's memory, at 1200. In v2.9.170 the field was 64 bits, at 1200 after four
bytes of padding, and `Mem` at 1208. These offsets come from PS2Native's
description of both versions, compiled and printed; the build's check holds
each to its PCSX2 (`runtime/pcsx2_check.cpp`, `clipflag`, `nextBlockCycles`,
`Mem`, `Micro`).

microVU's macro mode reads 64 bits at the field in two places (`COP2_Interlock`
and `mVUSyncVU0`, `pcsx2/x86/microVU_Macro.inl`, the same lines in both
versions). Where PCSX2's VUSyncHack or FullVU0SyncHack game fix is on, it
subtracts them from how far the EE is ahead of VU0 (`cpuRegs.cycle` and
`VU0.cycle`, both 64 bits) and compares the whole result with 4, to decide
whether VU0 runs on before a COP2 instruction. In v2.9.170 that was the field.
In v2.9.189 it is the field with the low half of `Mem` as its high half.
Unless that half is zero, the comparison then comes out the same whatever the
cycles: always under 4 where the half is below 2^31, never where it is above.
So it depends on where the system put PCSX2's memory. That is a defect of
PCSX2's since v2.9.174, in how its recompiler reads the field, not in what it
computes with it.

PS2Native's runtime subtracts the field alone, zero-extended (`Cop2Prologue`,
`runtime/ee_provider.cpp`). That is what v2.9.170's code gave, not what
v2.9.189's gives. It was written so on the belief that the four bytes after
the field were padding nothing writes; the comments now say what is there,
and the code is as it was.

Which games: PCSX2's game database gives VUSyncHack to 25 entries and
FullVU0SyncHack to 3, and none of them is one of the maintainer's eight games
(`SLES-50539`, `SCES-50294`, `SCES-50361`, `SCES-52004`, `SLES-51434`,
`SLUS-20184`, `SLUS-20326`, `SCUS-97623`). No test program has an entry in
it, so no test and no session has run that path on either side. Put to the
maintainer: whether PS2Native reads the field as v2.9.189 does (as it
reproduced the count register's carry, ADR 0013) or keeps the field alone,
and whether PCSX2 is told, which is theirs to do (ADR 0010).

Where the system put that memory, as PCSX2's log of three runs here gives it
(the area that holds the EE's, the IOP's and the VUs' memory): VU0's and
VU1's memory began at `0x0000022511000000`, `0x0000012311000000` and
`0x000002AB94740000`. So that half was below 2^31 in two runs and above it in
the third: in stock PCSX2 v2.9.189, under those game fixes, the comparison
would have come out one way in two of the runs and the other way in the
third.

Decided the same day (ADR 0056, "Agree"): PS2Native keeps the field alone,
listed in the execution model's "What is not reproduced". Telling PCSX2 is
the maintainer's.

## The pace a session kept, and the notice for any new code (ADR 0055)

### Built (2026-11-23)

At the maintainer's answers (ADR 0055), in `host/main.cpp`:

- **The pace written down** (`KeepPace`): for a session in the window in
  `pace.txt` beside its `input.txt`, and for any run in the file `-pace`
  names (`game.ps1 -Pace`; `PACE` in `tests/guest_test.cmake`). Every 300
  frames a line: the frame; the seconds since the first frame; the frames a
  second over the stretch; its longest frame on the CPU thread; the longest
  frame PCSX2 showed (its performance figures' maximum over each half second,
  read every 30 frames); what the game itself drew a second, at the console's
  pace; and the part of the EE's cycles, of the IOP's blocks and of the vector
  units' blocks an interpreter carried out. What the game drew is PCSX2's count
  of the game's new pictures (`PerformanceMetrics::GetInternalFPS`) to each
  frame shown (`GetFPS`), over the same half second, averaged over the stretch
  and multiplied by the console's frames a second, so that a run without a
  window says it too; -1 where PCSX2 cannot tell. The run's report gives the
  worst: the stretches under 99% of the console's frames a second, the
  slowest, the longest frames and the fewest pictures the game drew, each
  with where.
- **The notice for any new code** (`WatchNewCode`): shown when in the last
  half second the EE interpreted any instruction but the BIOS's at boot, or
  the IOP or a vector unit any block; still for five seconds, at most once a
  minute. The report's line says in how many half seconds such code ran, the
  most of each processor's work in one, and how often the notice was shown.
- **The module version** 5 (`runtime/game_module.h`; "Moved", above).

Every test passes: on Windows 543, and 541 with the same three disabled; on
Linux, inside WSL, 543 and 540; none skipped.

### Jak and Daxter's session run again with its pace (2026-11-23)

The maintainer's session of 09:47 UTC (44½ minutes, "laggy as hell,
specially during boss fight"; then "the boss fight was laggy even when
playing complied") was run again whole from a copy of their folder brought up
to date (`build\measure\jaklag`, `jakpace.ps1`). It ran as played (VU1 on
PCSX2's VU thread, Vulkan at three times the console's resolution), with
`-pace`, and was the session at every one of its 3,205 fingerprints, in 563
s.

What the game itself drew a second, at the console's 59.94:

- **Minutes 1 to 38:** 60 in every minute (one stretch of 300 frames at 51.9,
  at minute 31.8).
- **Minute 39.4 to the end:** about 30, in bursts of 15 to 30 seconds (minutes
  40.2 to 40.6, 41.0 to 41.5, 42.0 to 42.6, 44.0 to 44.5), and 35 to 50
  between them; minute 43 at 59.

Of the 534 stretches:

| Pictures a second | Stretches |
|---|---:|
| 59 or more | 484 |
| 50 to 59 | 21 |
| 40 to 50 | 4 |
| 30 to 40 | 15 |
| under 30 | 10 (the boot's two, the intro's one, seven in those bursts) |

The emulation never fell behind: no stretch ran slower than 2.7 times the
console's pace. Nothing interpreted weighed: at most 0.205% of the EE's
cycles in a stretch.

The game's settings enable PCSX2's patch "NTSC Mode", which shows this
European game at 60 frames a second where a European console shows it at 50.
A picture so has a sixtieth of a second of the console's time; where one
takes longer, the game shows it for two frames, 30 a second. That is the
boss fight's lag: the game's own, in the console's time as PCSX2 counts it,
which the compiled game counts as stock PCSX2 does. Stock PCSX2 was not run
on the session.

### The window check (2026-11-23)

Copies of two folders were given the new runtime and run in a window on the
measurement's hidden desktop, muted, 3,600 frames each, with the START and
CROSS script (`build\measure\pacecheck.ps1`).

- **Secret Agent Clank, as the wizard made it (never played):**
  - Pace: 59.87 frames a second where the console shows 59.93; its first
    stretch, its start, at 59.14.
  - New code: met in all 119 half seconds. It is its first session: up to
    10.1% of the EE's cycles, and all of the IOP's and the micro code's
    blocks, none of them compiled yet.
  - The notice: shown once (the run lasted a minute).
  - The game drew about 30 pictures a second in its menus, which is its own
    rate.
- **Jak and Daxter's copy:**
  - Pace: 58.37 frames a second; its first two stretches, its start, at 49.5
    and 54.5.
  - New code: met in 113 of 119 half seconds, at most 0.04% of the EE's
    cycles. The script took the game where the maintainer's sessions had not.
  - The notice: shown once.

Each session's `pace.txt` was written beside it, 12 lines. It could not be
read while the session ran ("being used by another process": the C runtime's
`fopen_s` opens it unshared), so the runtime now opens it with
`FileSystem::OpenSharedCFile`, letting others read. Built again with that
(`build\measure\p55-03.ps1`), both sessions' files were read while they ran,
and each run was otherwise the same as before, to the EE cycle (17,707,512,539
and 18,149,644,064). The three games the maintainer has not played yet were
then brought up to date from that tree, each compiling its game again (39,
54 and 58 s).

### Code Jak and Daxter keeps interpreting (2026-11-23)

The run again of the maintainer's session used modules compiled from
everything the folder's sessions met, that session among them
(`captures\merged.txt`). It still interpreted EE code in 427 of its 534
stretches: none in minutes 13 to 20, at most 0.205% of the EE's cycles in
one.

It wrote a capture of 1,006 pieces of code; its session's had 4,162:

- 531 of them are in the folder's capture word for word, so the folder's
  compile had taken them in.
- The other 475 are not, although the run is the session.

So part of what Jak and Daxter loads from its data stays interpreted however
often its folder compiles. Why has not been looked for. With the notice for
any new code, a session of it would show the notice about once a minute
through most of its length. Put to the maintainer, who made finding out why
the next step (ADR 0056: "2. Yes").

### The maintainer's answers (2026-11-23)

ADR 0056: "1. Ok 2. Yes 3. Agree 4. Commit and push."

- Their Jak and Daxter folder is to play the game at a 130% EE cycle rate,
  brought up to date so that its next session writes its pace down.
- Why part of Jak and Daxter's loaded code stays interpreted is the next
  step.
- PS2Native keeps `nextBlockCycles` alone.
- Their other earlier folders keep their programs until then (not answered,
  as recommended).

### Why Jak and Daxter's loaded code stays interpreted (2026-11-23)

Looked for the same afternoon, at the maintainer's word (ADR 0056, "2. Yes").

The runtime captures the code a game runs outside its executables in two
ways:

- **A block run where no image has compiled code** ("outside its executable,
  where nothing was compiled").
- **The words over a compiled function's whole range,** where that function
  is in memory's place but memory holds other words ("in the place of the
  function").

The recompile puts every piece captured outside the executable into one image
of its own, a word at each address (`codeOutside`,
`recompiler/src/analysis.cpp`). Where two pieces disagree, "the first is
kept": the piece with the lowest start. That image has no variants. Each of
the executable's functions can have up to 253 versions, chosen at run time by
what memory holds; the code outside it can have one.

Jak and Daxter puts different code at the same addresses as it goes through
its world. Here is each folder's capture, translated as its compile
translates it, into a scratch folder (`build\measure\outside-conflicts.ps1`):

| Folder | Sessions | Pieces captured | Code outside the executable | Words captured with another value |
|---|---:|---:|---|---:|
| Jak and Daxter | 15 | 65,322 | 9,356 functions, 447,935 instructions | 1,006,464 notes at 83,085 addresses |
| Gran Turismo 3 | 2 | 26,274 | 6,297 functions, 158,869 instructions | 0 |
| Killzone, Silent Hill 3 | 3, 7 | 9, 8 | 2 functions, 24 instructions each | 0 |
| Agent Under Fire (two folders) | 1, 1 | 1, 4 | none | 0 |

Gran Turismo 3 loads its code from its data too, and leaves it where it put
it.

In Jak and Daxter, every version at those 83,085 addresses (about 330 KB)
but the first runs interpreted:

- it is captured again at the end of each session;
- it makes the folder compile after the session, for nothing. Its own log
  has a compile after nearly every session since 2026-11-21.

So its captures grow with every session.

Compiling every captured version would not settle it. A piece "in the place
of the function" holds the whole range of the function compiled there
before, most of it memory around the code that ran:

- At `0x011a13e4`, the busiest start, 419 versions were captured, 417 of
  them 752 words long.
- By their first eight words they fall into 16 groups, most of which are not
  code at all: zeros, colours, tags for the graphics hardware.
- Within the largest group, only the first 59 words agree.

All the pieces together hold 1.65 million words, against the 0.45 million
instructions compiled outside the executable now. Put to the maintainer: how
to fix it.

## Code run outside an executable, every version compiled (ADR 0057)

### Built (2026-11-23)

At the maintainer's "Yes implement the recommendation" (ADR 0057):

- **What is captured.** A block the interpreter runs outside the executable,
  the BIOS's files and the executables a program loads itself, where no
  compiled code ran it, goes into the capture as a `[[block]]`
  (`RunRecBlock`; `host/main.cpp`). It holds the words the runtime compares
  with memory before compiled code runs that block: from its start to where
  the scan for its end stopped, past a block PCSX2 cut short there. Where
  memory held other code at the same address at other times, every version
  is captured.
  - Before, such code was captured as the words over the whole range of the
    compiled function in its place (`[[code]]`), most of it memory around the
    code that ran, and only one version a run.
- **What is compiled** (`codeOutside`, `ps2n-recomp compile --outside`):
  - The first image of code run outside the executable is what it was: every
    `[[code]]` piece of earlier captures, the first word kept where two
    disagree, and the blocks that agree with it.
  - A block that disagrees goes into the first further image it agrees with
    (`<id>_outside_2` and on). Its end is followed from as well as its start,
    so that what the runtime compares for a block never runs on into another
    version's words.
  - The first image lists the others (`ImageInfo::layers`, module version 6).
  - With parts, as a folder's compile asks, each further image is a file of
    its own. Without them (the development build), all are in the one file.
- **What runs** (`blockStart`):
  - Every image is in use. A block the image compiled code asked from does
    not hold is identified against the others, and run by the one that holds
    it (`Block::owner`).
  - The label that asked keeps that answer (`Known::other`), and the
    dispatcher enters that image there from then on.
  - The same is tried where a BIOS image's code is not what memory holds, for
    the program's own version of code in a place of the BIOS's.
- **The notice over the picture** (ADR 0055) counts what a run records for
  the next compile (`ee::Stats::new_code`): a block start an executable's
  compiled code missed, code in the place of a function, a version of a
  block run outside the executable. The BIOS's own block starts are not
  counted.
  - It no longer counts every instruction interpreted. Code compiled code
    cannot carry out exactly as PCSX2's would is interpreted every time
    without being new, and no compile takes it in.
- **Tests.**
  - `tests/programs/overlay.s` copies two routines in turns to the same
    address outside its executable. Its first build reports what
    `overlay.capture.toml` holds: six blocks, two versions at one address,
    since PCSX2 had formed one routine's first block shorter where the
    other's had begun. Rebuilt with it, nothing is interpreted
    (`programs/rebuilt/overlay`).
  - Every rebuilt test program must now interpret nothing outside its
    executable either.
  - Unit tests of the layering and of `[[block]]`.
  - Every test passes: on Windows 556, and 554 with the same three disabled;
    on Linux, inside WSL, 556 and 553; none skipped.

### Measured on Jak and Daxter (2026-11-23)

The maintainer's session of 09:47 UTC (44½ minutes) was run again whole from
copies of their folder (`build\measure\jak57`, `p57-03.ps1` to
`p57-05.ps1`, a PS2Native folder of its own, `build\measure\kit57`). Each
ran as played (VU1 on PCSX2's VU thread, Vulkan at three times), and each run
was the session at every one of its 3,205 fingerprints.

| Programs, and what the folder had compiled | EE cycles interpreted | Instructions interpreted outside the executable | Recorded for the next compile |
|---|---:|---:|---|
| These, from its captures as they were | 580,691,693 (0.07%) | 389,564,108 | 23,549 blocks |
| ... with those blocks (the first round) | 87,583 | 63,445 | 761 blocks, all of them recorded before |
| ... with those blocks (as built) | 48,071 | 32,929 | nothing |

- **5eade8c's programs on the folder as it was** (the run of the afternoon)
  had interpreted code in 427 of the 534 five-second stretches and recorded
  894 pieces in a function's place and 112 blocks. The second row's run did
  the same in its stretches. Neither of the last two had code interpreted in
  any stretch, to the thousandth of a percent.
- **The first round's 761 blocks** were blocks PCSX2 had cut short, compared
  past their last instruction. They were found once a block was captured over
  the span compared and its end made a boundary.
- **What is left** is known code the runtime interprets because compiled code
  cannot carry it out exactly as PCSX2's would: 14,750 times a label handed
  its block to the interpreter, at 377 places. It is never recorded, and the
  notice does not count it. Counted as interpreted instructions, as this
  afternoon's rule counted, it would have shown the notice's code in 435 of
  the session's 5,341 half seconds; counted as what is recorded, in none.
- **The code:** 205 further images, 17 MB of C++ against the first image's
  75 MB. The game's module grew from 36.5 MB to 44.0 MB. The copy compiled
  them in 62 s (225 of its 336 files).
- **Times:** the runs took 546 to 583 s, and 848 s while Linux built beside
  them. That is not a timing.

### Gran Turismo 3, Killzone and Agent Under Fire (2026-11-23)

- **Gran Turismo 3.**
  - Its folder's captures (26,274 pieces) were translated by 5eade8c's
    recompiler and by the new one. The code it ran outside its executable is
    the same 6,297 functions and 158,869 instructions. Of the 82 C++ files,
    two differ, by the table of further images (five lines each, empty).
  - Its session of 2026-11-18 09:55 was run again from a copy brought up to
    date, on Vulkan at the console's resolution (PCSX2's null renderer
    asserts on its AA1 drawing).
  - The run was the session at every one of its 173 fingerprints, recorded
    nothing, and found no new code in any of its 288 half seconds.
  - The code it patches into the BIOS's program loader (`00082000`) had been
    interpreted every session, 10,135 instructions in this one, and captured
    again every time. Now it runs compiled from the game's own image, and the
    run interprets only the BIOS's 51 instructions at boot.
- **Killzone:** its session of 2026-11-17 22:18, run again from a copy, was
  the session at every one of its 186 fingerprints. It recorded nothing, and
  interpreted the BIOS's 51 instructions at boot.
- **Agent Under Fire** (the folder with all nine of its executables): its
  session of 2026-11-17 22:44 was the session at every one of its 180
  fingerprints, and recorded nothing.
  - It interpreted 0.56% of the EE's cycles, 219 million instructions of the
    BIOS's kernel after boot, as the folder's own last session did on 0.3's
    programs (the same count). That folder has no capture of what was
    interpreted of the BIOS (`captures\bios-capture.toml`). The maintainer's
    other Agent Under Fire folder and Killzone's have one (22,374 bytes), and
    their runs interpret only the 51 instructions at boot.
  - Under this afternoon's rule the notice would have counted that; now it
    does not.

### The maintainer's answers (2026-11-23)

ADR 0058: "1. Yes", "2. Yes", "3. Yes".

- Committed and pushed.
- Their game folders are brought up to date with these programs, their
  PS2Native folders made again from the committed tree first.
- 0.4 is prepared once they have played a session or two with the updated
  folders.

### The maintainer's folders brought up to date (2026-11-23)

From `2cc8125`, the wizard's own way (`build\measure\folders-58.ps1`; inside
WSL the scratchpad's `wsl\p58-linux.sh`, after `release-03-linux.sh theirs`):

- **Their PS2Native folders,** on Windows and inside WSL, were made again from
  the tree, their games kept.
- **The nine game folders on Windows** were given its programs and compiled
  their games again. Agent Under Fire's folder with all nine of its
  executables took 319 s, the others 23 to 163 s.
- **Six of them have a session played with VU1 on the CPU thread.** Its first
  900 frames (Silent Hill 3's 90), run again with the new programs, were the
  session at every fingerprint compared.
- **Resident Evil – Code: Veronica X and Secret Agent Clank** have no session
  yet. SSX Tricky's one session was played with VU1 on PCSX2's VU thread.
- **Asked again,** the wizard found every folder up to date and changed nothing.
  The memory cards are as they were, and every session is there.
- **Jak and Daxter inside WSL:** brought up to date and compiled in 34 s. The
  first 600 frames of its longest session were the session at every
  fingerprint compared.
  - Those 600 frames ran 15,157 blocks outside the executable by
    interpreter, which its earlier captures could not keep as versions. They
    are recorded as blocks now.
- **The maintainer's next Jak and Daxter session,** on either system, records
  them, shows the notice, and compiles once more after it.
- **`SLES_505.39`,** a folder a session made for P4.4's check, is left as it
  is.

## 0.4 (ADR 0059)

### What it holds, and how it is made (2026-11-25)

The maintainer, having played with their folders brought up to date: "I did
some play tests, everything looks smooth. Let's release 0.4."

Since 0.3, 0.4 holds:

- PCSX2 v2.9.189 (ADR 0054);
- the pace a session keeps written down, and the notice for any new code
  (ADR 0055);
- every version of code run outside an executable compiled, the runtime
  picking the one memory holds (ADR 0057; module version 6).

It is made as 0.3 was:

1. The version 0.4, committed and pushed.
2. The runtime's profile of play made again on each system, the training
   folders brought up to date first.
3. The trees the downloads come from built with it and tested.
4. Both downloads made and tried, a folder of 0.3's download brought up to
   date among the tries.
5. The release's runtime timed again.
6. A draft for the maintainer to publish.

### Made and tried (2026-11-25)

From `a243df4` (`build\measure\release-04.ps1`, `release-04-all.ps1` and
`release-04-time.ps1` on Windows; the scratchpad's `wsl\release-04.sh` in WSL).

**The training folders.**
- **Windows:** kit15's seven were brought up to date by `build\kit`'s wizard and
  compiled by their own `ps2n-remake --before-play`, in 14 to 161 s each. That
  wizard is `2cc8125`'s, whose programs are this tree's but for the version they
  say. The folders' modules had been module version 4.
- **Linux:** `build/kit-measure`'s seven, with `build/kit-linux` made at the
  commit, in 8 to 802 s each. Agent Under Fire's nine executables took the 802 s,
  on ten threads beside the Windows build.

**The runtime's profile of play.**
- **Windows:** 925 s. Its counting tree, last built before PCSX2's dependency
  package changed under it (ADR 0054), was moved aside as
  `build\windows-clang-count-2.9.170` and built from nothing within those 925 s.
- **Linux:** 959 s. Its dependencies had not changed, so its counting tree was
  built on from where it was.
- **Every training run of a session** was the session at every fingerprint
  compared: 216 over 10,800 frames, 186 over Killzone's whole session, and 60
  over 3,000 frames of the first sessions.
- **The release trees** compiled every object again with the new profiles: 754 on
  Windows, 736 on Linux.

**The tests,** none skipped:
- Windows' release tree, 554 of 554;
- Linux's release tree, 553 of 553; its development tree, 556 of 556;
- with the runtime built with link-time optimization, the three comparisons of
  `dma/vif/stcycl` are disabled and did not run.

**The downloads.**

| File | Bytes | SHA-256 |
|---|---:|---|
| `PS2Native-0.4-windows-x64.zip` | 115,300,159 | `d957095a1d00fe908ac1c14f60feb65ea2e0e2443b720661d281fb08bab70402` |
| `PS2Native-0.4-linux-x64.tar.xz` | 144,266,640 | `808596f90de792320b5d6c49e80e111eccd9127b1d7fafdbc8468504f2c7801b` |

Their licences name PS2Native 0.4, commit `a243df4`, and PCSX2 v2.9.189 with
PS2Native's one change.

**Each download, unpacked elsewhere:**
- **Its runtime** read Silent Hill 3's `.chd` with nothing but the system on
  `PATH`.
- **Its wizard** made Jak and Daxter from the disc image (15 s on Windows, 11 s on
  Linux, the executable alone), and then found that folder up to date.
- **The maintainer's session `20261119-110909`,** run again whole from that
  folder, was the session at every one of its 167 fingerprints on both systems:
  - 202 s on Windows, 215 s on Linux;
  - the same 41,554,654,904 EE cycles on both, 58.6% of them interpreted, as in a
    first session.
- **The pace record on Linux:** that run wrote down its pace (`-pace`), the first
  Linux run of ADR 0055's record. It wrote 27 stretches and the runtime's summary
  of the worst of them. A run without a window goes as fast as the machine does,
  so only what the game drew and the interpreted parts mean what they say.
- **The update path:** a copy of the Jak and Daxter folder 0.3's own download made
  was brought up to date by 0.4's wizard and compiled again (9 s on Windows, 7 s
  on Linux). The start of the session run again from it was the session at every
  fingerprint compared (18 over 900 frames, 12 over 600), and the wizard, asked
  again, found it up to date.

**The maintainer's PS2Native folders,** `build\kit` and inside WSL
`build/PS2Native`, were made again from the release's tree. Their game folders
are as they were, with `2cc8125`'s programs:
- on Windows, by their programs and their files' dates;
- inside WSL, by fingerprints of their programs, modules and memory cards.

**Since the update of 2026-11-23,** the maintainer's one session is SSX Tricky's
of the morning:
- 90,377 frames, with VU1 on PCSX2's VU thread;
- 59.93 frames a second over its 301 stretches, the lowest 58.95 in the first;
- nothing interpreted in any stretch;
- after it, the folder counted its profile once more (ADR 0049) and compiled the
  game with the counts.

**The release's runtime, timed.**

Stock PCSX2's trees were built as PCSX2's releases are, at v2.9.189: link-time
optimization, no profile, none of PS2Native's changes. Windows' was built from
nothing, the earlier one, v2.9.170's, kept as `build\windows-clang-stock-2.9.170`.

The comparisons:
- **On Windows:** copies of kit15's four folders, with their own counts as taken
  for 0.3, ran on the release's runtime (`<game>-r4`) and on stock PCSX2's
  (`<game>-s4`).
- **Inside WSL:** both took `jak-vut`'s modules.
- **How:** two rounds in turns, both sides with VU1 on PCSX2's VU thread, the
  maintainer's timed sessions, alone on the computer.

| Game | Stock PCSX2 v2.9.189 | 0.4 | Ratio | 0.3's (against v2.9.170) |
|---|---:|---:|---:|---:|
| Jak and Daxter | 34.38, 33.69 s | 31.84, 31.69 s | 0.93 | 0.97 |
| Gran Turismo 3 | 18.10, 18.80 s | 18.28, 18.65 s | 1.00 | 1.00 |
| Agent Under Fire | 26.28, 26.56 s | 25.58, 25.80 s | 0.97 | 0.95 |
| Killzone | 33.06, 33.75 s | 31.09, 31.41 s | 0.94 | 0.94 |
| Jak and Daxter, under WSL 2 | 37.45, 37.50 s | 36.21, 36.05 s | 0.96 | 0.99 |

- **Agent Under Fire's runs,** on both sides, part from the session at frame
  1,900: its runs with the thread on do not repeat (ADR 0051).
- **Every other run** was the session at every fingerprint.
- **Killzone's counts** were taken on a run of the same controller script as its
  timed session, so they fit it better than a user's would.

**The draft.** `gh release create v0.4 --draft --prerelease` on `a243df4`, with
both files and `build\release\notes-0.4.md`; the tag is made when it is
published.

### Published, and the maintainer's folders given its programs (2026-11-25)

The maintainer's answers: "1. Yes, publish yourself", "2. Yes do that", "3. Yes
please. When you say my game folders you mean build/kit/games?"

- **Published** at 12:42 UTC (`gh release edit v0.4 --draft=false`), a
  pre-release, the tag `v0.4` on `a243df4`. GitHub gives the files' sizes as
  110 MB and 138 MB.
- **The website and the README** moved to it: the downloads, the eight games
  tried, Phase 5's paragraph, the platforms' lines and the games tile. The charts
  are dated measurements and stay as they were.
- **Their game folders:** on Windows, the nine of `build\kit\games`
  (`SLES_505.39` aside, a folder a session made for P4.4's check, which no round
  has updated); inside WSL, Jak and Daxter's in `build/PS2Native/games`. Each was
  given 0.4's programs by the wizard of their PS2Native folder, made from the
  release's tree, and compiled its game again (`build\measure\folders-59.ps1`;
  the scratchpad's `wsl\p59-linux.sh`, kept in `build\measure`).

| Folder | Compiled in | Its start run again |
|---|---:|---|
| Jak and Daxter | 66 s | 900 frames, 18 of 18 fingerprints |
| Killzone | 115 s | 900 frames, 18 of 18 |
| Silent Hill 3 | 88 s | 90 frames, 1 of 1 |
| Gran Turismo 3 | 23 s | 900 frames, 18 of 18 |
| Agent Under Fire | 95 s | 900 frames, 18 of 18 |
| 007 - Agent Under Fire (nine executables) | 317 s | 900 frames, 18 of 18 |
| Resident Evil – Code: Veronica X | 38 s | no session yet |
| Secret Agent Clank | 55 s | no session yet |
| SSX Tricky | 58 s | its sessions played with VU1 on PCSX2's VU thread |
| Jak and Daxter, inside WSL | 27 s | 600 frames, 12 of 12 |

- **Asked again,** the wizard found every folder up to date and changed nothing.
- **Memory cards and sessions** are as they were.
- **Counting once more:** Jak and Daxter's folder on Windows and SSX Tricky's had
  counted their profiles; each counts once more after a session of 3 minutes
  (ADR 0049).
- **Jak and Daxter inside WSL,** not played since 2026-11-23, still ran 15,157
  blocks outside its executable by interpreter in those 600 frames. Its next
  session records them, and the compile after it takes them in.

## A menu over the game (ADR 0060)

### Built (2026-11-25)

At the maintainer's words ("I want to add an overlay that opens when pressing
Esc, allowing the user to Quit (loose progress warning), Edit keybinds and
controllers and which GPU is in use ...", then "Use a fancy sound when it opens,
when it closes and when clicking/hovering the menu items."):

- **Opening it** (`host/main.cpp`, `HoldForMenu`): Esc in the game's window
  asks for the menu; between two frames, where the window's requests are taken
  up (`OnGuestFrame`), the machine is held while it is open.
  - The core is told it is paused (`VMManager::SetState`): its sound and the
    controllers' vibration stop, its frame limiter starts again after.
  - It is told it runs again before the frame's event test ends, so the
    processors' execution is never left (`Cpu->ExitExecution()` is not called)
    and nothing of the machine changes.
  - What was held of the controller comes up as the menu opens and as it closes
    (`Pad::ResetAllControllerInputs`): the session written down has that
    release at that frame, and its replay gives it back there.
  - The time the menu is open is left out of the frames' times
    (`s_latest_frame_time`): the pace a session writes down (ADR 0055) and the
    run's report are the game's. The report says how often the menu was opened
    and for how long.
- **Drawing it** (`host/menu.cpp`): each of the menu's frames is one task of
  the GS thread, the menu drawn with the ImGui of PCSX2's renderer and the
  game's last picture presented under it (`GSPresentCurrentFrame`), about 60 a
  second. Four pages, each a window of its own in the middle of the picture:
  - the first: the game's name, Resume, Controls, Graphics card, Quit;
  - Quit: "Quit <the game>? Anything you have not saved in the game will be
    lost.", Quit and Back, Back reached first; its Quit closes the window as
    its close button does (`window::RequestClose`);
  - Controls and Graphics card, below.
  - The keys and controllers move in it as ImGui moves (arrows, Enter, Tab; a
    controller's D-pad or stick, Cross, Circle back); the mouse points and
    clicks, the window passing it on (`Listener::pointer_moved`,
    `pointer_button`, `wheel`; on Linux X11's motion and button events). Esc
    goes back a page, and out from the first.
- **Controls:** the controller in port 1 (the one a session writes down).
  - A line for each of the DualShock 2's inputs (its two motors aside), with
    what it is on the keyboard and on a controller, as PCSX2 writes a binding,
    a controller's button as its glyph (PCSX2's icon fonts).
  - Choosing one waits five seconds for a key, a button or a stick, taken by
    PCSX2's input hook (`InputManager::SetHook`) as PCSX2's own fullscreen
    interface takes it: held, and made the binding when let go, in the place of
    what the line had of that kind. Esc leaves it as it was. Delete,
    Backspace, a controller's Square or a right click clears one side.
  - "Play with" a connected controller: its inputs the mapping PCSX2 makes for
    it (`GetGenericBindingMapping`), the keys kept, its motors the vibration.
  - "Default controls": PCSX2's keys, and the first controller beside them.
  - Each change is taken by the core at once (`VMManager::ReloadInputBindings`)
    and kept: `inputprofiles\controls.ini` in the core's data folder (a game's
    folder: `data\PCSX2\inputprofiles\controls.ini`), PCSX2's own format for an
    input profile (`Pad::CopyConfiguration`), read when the game starts. "Default
    controls" removes it. While a game has its own, the first controller
    connected is no longer added by itself (`MapFirstController`).
- **Graphics card:** the card drawing the game and its graphics interface, as
  the renderer names them; the cards PCSX2 lists for that interface
  (`GSGetAdapterInfo`), Windows' software renderer left out. A choice goes into
  the core's settings and its configuration, and PCSX2 makes its renderer
  again on that card (`MTGS::ApplySettings`, then PCSX2's `GSreopen`). If the
  card then drawing is the one chosen, it is written into the game's settings
  file (`EmuCore/GS/Adapter`); otherwise PCSX2 has gone back to the card before,
  and so does the setting.
- **Sounds** (`host/menu_sound.cpp`), at the maintainer's "Use PS2 style
  sounds" after a first set (bell-like notes in a small room): in the manner of
  the PlayStation 2's own menus, made by the code when first needed, nothing of
  Sony's. Played through SDL's audio (one device; for each sound one to four
  streams taken in turn, mixed by SDL, so that a sound played again while it
  rings is not cut short) beside the game's sound, at its volume
  (`StandardVolume`), none where it is muted.
  - The tones: glassy bells by frequency modulation (a modulator three or two
    times the note's frequency, its depth dying away faster than the note), the
    right side 4 cents above the left so that the two shimmer; soft held tones
    with a slight vibrato; a low sine swell; breaths of band-passed noise; then
    Jezar's Freeverb (eight combs, four allpasses) with a short pre-delay.
  - Opening: a swell from 68 to 104 Hz and air rising from 220 to 3,400 Hz,
    bells on D6, A6, E7 and F♯6 90 ms apart over a soft D5–A5 fifth, in a large
    room; closing: the air falling, a swell from 104 to 60 Hz, bells on E7, A6
    and D6; an item reached: a soft high tick (about 3.1 kHz, 22 ms) with a
    little room; chosen: A6 then E7 and a faint A7; back, cleared or given up:
    E6 then B5, duller.
  - `ps2n-host -menu-sounds <folder>` writes them as WAV files: opening 3.59 s
    (within 20 dB of its loudest for its first 1.6 s), closing 2.16 s, chosen
    1.38 s, back 1.24 s, reached 0.41 s (26 dB down after 0.1 s), each ended
    where it has died away 56 dB; at most 0.32 of full scale.
- **Checks** (`scripts\windows\window-check.ps1`): the keys Escape, Delete,
  Tab and Space, and `click`, `rclick` and `move` steps for the pointer. A
  session that does not end is stopped with its whole tree (`taskkill /T`): it
  had left `ps2n-host` running on the hidden desktop.

### Checked (2026-11-25)

On Windows, with the development tree's runtime (Agent Under Fire's code built
in), each session in a window of 1280 by 720 on the measurement's hidden
desktop, muted, on Direct3D 12 so that a photograph of the window shows the
picture (`build\measure\menu-60.ps1`). On that desktop the window is drawn
about 12 times a second with the menu, and the game at 35 frames a second of
50: the steps are a second apart.

- **The keys:** Esc opened the menu over the dimmed picture of the game's
  language screen; Down and Enter opened Controls; D-Pad Up's key set to P, the
  controller's D-pad kept beside it (`Up = SDL-0/DPadUp`, `Up = Keyboard/P` in
  `controls.ini`); Esc back, the focus on Controls; the Graphics card page named
  the RTX 4070 SUPER and Direct3D 12; Esc twice to the game; the menu opened
  again, and Quit, asked, ended the session as the close button does. The run's
  report: the menu opened twice, for 24.3 s; its sounds asked for: opening 2,
  closing 2, reached 7, chosen 5, back 2 (muted: none played).
- **The replay:** that session, 556 frames with the menu open for 24.3 s in
  two openings, run again without a window: the session at every one of its
  11 fingerprints.
- **The next session** started with the player's own controls ("the controls
  are the player's own"); the pointer clicked Controls, then Default controls,
  which put PCSX2's keys back and removed `controls.ini`.
- **The card changed:** with Windows' software renderer let into the list for
  the check (a build of its own), the menu's Graphics card page changed to it:
  PCSX2 made its renderer again there ("The game is drawn by Microsoft Basic
  Render Driver from now on"), then back to the RTX, which the game's settings
  file then named. The menu showed over a black picture after each change.
- **Found on the way:**
  - ImGui 1.93 leaves an invisible button out of keyboard and controller
    navigation unless asked (`ImGuiButtonFlags_EnableNav`): the menu drew, Esc
    worked, the arrows did nothing.
  - Focusing the page's window at every frame took the focus back out of the
    controls' table (a child window): the window is focused only when no
    window is.
  - The Enter that chooses a control comes up during the wait, which took it:
    ImGui kept it held and ignored the next Enter. What comes up without having
    gone down during the wait now goes on to the menu.
- **With the final programs** (the PS2-style sounds; ImGui's keys let go when
  the window loses the keyboard while the menu is open): the keys' session
  again, the menu open 24.2 s in two openings, the same steps carried out; its
  replay the session at every one of its 9 fingerprints.
- **Tests:** every test passes: on Windows 556, and 554 in the module tree with
  the same three comparisons disabled; on Linux, inside WSL, 556 and 553; none
  skipped.
- **On Linux** the menu and its window's pointer are built, and the Linux
  runtime writes the same sounds (the same lengths to the sample), but no game
  was opened in a window there: a window inside WSL shows on the maintainer's
  screen.
