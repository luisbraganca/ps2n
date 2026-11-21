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
| P4.7 Linux | The maintainer's own run inside the Ubuntu 22.04 of WSL 2 on this computer ([ADR 0041](adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md); at first their Linux laptop, [ADR 0038](adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md), which is not theirs to use; [ADR 0004](adr/0004-proof-of-concept-environment.md)): a game made with the wizard from its disc image plays from its folder at the console's pace, and what a session meets for the first time is compiled for its next start (the session's proposal, accepted). Linux is then described as tested under WSL 2 until a Linux machine of its own runs it | **Under way** (2026-11-19), at the maintainer's word ([ADR 0037](adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md)), built and tested first on a Linux inside this computer, WSL 2 with Ubuntu 22.04 ([ADR 0038](adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md)): every test passes on Linux (534), the compiled code in the host and in shared objects of its own; the game's window an X11 one, with the notice over its picture; the launcher, the wizard and the recompile working there, and the folder for Linux put together (`scripts/linux/kit.sh`): with it, inside that Linux, Silent Hill 3 made from the maintainer's `.chd` as without PCSX2 in 123 s, played from its folder by its own launcher at the console's pace (49.72 frames a second of 50), what its first session met compiled in the background, and the next session interpreting none of the IOP's code and 0.00% of the EE's cycles. **Done** (2026-11-19), with the maintainer's run inside WSL ([ADR 0041](adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md)): Jak and Daxter made with the Linux wizard from its `.iso` and played from its folder for five sessions, what each met compiled in its window when it ended, the later ones on compiled code alone at about 90% of the console's pace there (Silent Hill 3 99.3%); the transparent parts of characters on WSL's OpenGL are not there on PCSX2's software renderer. The maintainer's words: "I guess it's good", then "Let's create releases on Github" ([ADR 0042](adr/0042-releases-on-github-beginning-with-0-1.md)) |
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
