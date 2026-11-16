# Phase 4 proposal: the user application

| | |
|---|---|
| Status | Decided on 2026-11-16: every recommendation accepted ([ADR 0028](adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md)) |
| Asked for by | [ADR 0027](adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md) |
| Date | 2026-11-16 |
| Starts from | the brief, sections 4 and 10; [phase3-report.md](phase3-report.md), "What Phase 4 has to decide first" |

The brief's Phase 4: the compilation wizard, game recognition and settings,
the compilation pipeline, packaged native applications, meaningful
diagnostics, more platforms. Its wizard has five steps: choose the game
(identified with its region, serial and compatibility), give the BIOS if
one is needed (never distribute one), configure (PCSX2's defaults, every
one acceptable as it is), recompile (with progress and diagnostics), and
package a native application, which may read the original disc image at
run time. Every setting is fixed when the game is built; the game shows no
PCSX2 settings interface; changing a setting means recompiling.

This document proposes how, as decisions for the maintainer, each with a
recommendation, and an order of work. Nothing in it is built yet.

## What constrains it

- **A game has to be compiled on the user's computer.** Its compiled code
  is a translation of the game's code, and of the BIOS's, so no one may
  distribute it (the assessment, section 11). The wizard says so at the
  packaging step.
- **What compiling a game takes today**, measured on Agent Under Fire's
  build of 2026-11-16 (the build's own log): 1,950 s of compile time for
  its code, the BIOS's, the micro code and the IOP's, about 32 minutes of
  one core; just over 3 minutes on this computer's 20 threads, test
  programs included. On a computer with 8 threads it would be around 5
  minutes (an estimate from those numbers, not measured).
- **What compiling needs today.** Generated code includes PCSX2's own
  headers (`R5900.h`, `vtlb.h`, `VU.h`, `COP0.h`, `Config.h`), the C++
  standard library and PCSX2's precompiled header, and is compiled by
  `clang-cl` against Microsoft's runtime: on a user's computer, Visual
  Studio's build tools, several gigabytes, which Microsoft's licence does
  not let PS2Native ship.
- **A compiler that can be shipped.** LLVM's `clang` and `lld`, under
  Apache-2.0 with the LLVM exception: 100 MB and 68 MB here (clang 22.1.3),
  and the compiler's own headers, 39 MB, of which generated code needs the
  SSE intrinsics.
- **New code keeps turning up in play.** Every session of the maintainer's
  met code no build had seen; it runs interpreted once, written down, until
  a build compiles it (ADR 0003). The assessment's idea of the wizard
  playing the game unattended to find it all reaches little: the scripts
  stop where the sessions went on.

## Decision 1: how a user's computer compiles a game

Everything else follows from this one.

1. **A shipped compiler, and game code that needs nothing else
   (recommended).** The wizard brings `clang` and `lld`. Generated code is
   made freestanding: the contract it compiles against (`ps2n/recomp.h` and
   the headers of the instructions' semantics) no longer includes PCSX2's
   headers or any standard library, but describes the machine state it
   touches in structures of its own, which the runtime checks against
   PCSX2's at its own build (layouts compared with `static_assert`). A game
   is compiled into a module (a DLL on Windows, a shared object on Linux),
   which a prebuilt runtime loads: PS2Native's runtime and PCSX2's core,
   built once by the project and shipped with the wizard. So is the BIOS's
   code, from the user's own dump, as a module of its own (ADR 0007). The
   user installs nothing. The cost to measure first: generated code
   reaching the machine's state, and the runtime, across the module's
   boundary, which may take an indirection where today there is none.
2. **The user installs a compiler, and games are built as today.** The
   least work: Visual Studio's build tools on Windows, the distribution's
   compiler on Linux. A large installation before the first game, where
   the brief asks for a straightforward wizard and no emulator to install.
3. **The recompiler writes machine code itself** (through LLVM as a
   library), with no C++ compiler on the user's computer at all. A new back
   end for the generator: not now.

## Decision 2: what a packaged game is

1. **A folder (recommended)**: the runtime as a launcher named after the
   game, with its icon; the game's module; the BIOS's module; the settings,
   fixed when it was built; what the runtime needs of PCSX2's resources
   (shaders, the game's database entries); where the disc image and the
   BIOS image are, read in place. Easy to rebuild in place when new code
   is compiled.
2. **One executable** with everything but the disc and the BIOS image
   inside. Tidier to look at; every recompile rewrites it.

## Decision 3: code met for the first time on a user's computer

1. **The game compiles it itself (recommended)**: what a session had to
   interpret is written down, as today, and the game recompiles its module
   in the background with the toolchain the wizard installed, using it from
   its next start, and says so.
2. **The wizard compiles it when asked**: the game says that a recompile
   would help; the user runs the wizard again. Simpler, and the first step
   towards 1.
3. **The wizard plays the game unattended first**, with scripted input, to
   find what it can. Finds little; can come later as an extra.

## Decision 4: the wizard's interface

1. **Qt 6 (recommended)**, as PCSX2's own interface: already in PCSX2's
   dependency package, one interface for Windows and Linux. Game
   recognition from PCSX2's own code and databases (its game list,
   `GameIndex.yaml`, `RedumpDatabase.yaml`): title, serial, region, CRC, and
   PCSX2's compatibility rating.
2. **A command line first**, the graphical wizard after. Faster to a first
   packaged game; not what the brief describes for users.

The configuration step starts from the window's defaults (G14 to G17:
Vulkan, three times the console's resolution, a picture of 1920 by 1080,
the game's widescreen and frame-rate patches, the overclock a patch asks
for), every one of which can be accepted as it is.

## Decision 5: where a game's patches come from

[ADR 0025](adr/0025-how-a-game-is-shown-and-its-patches.md) takes them from
the installed PCSX2's archive, and leaves a packaged game's to Phase 4. The
repository the archive is made from declares no licence, so PS2Native
should not ship it.

1. **Found or fetched at the user's word (recommended)**: the wizard uses an
   installed PCSX2's archive when there is one, and otherwise offers to
   fetch PCSX2's own release of it; the packaged game keeps the one patch
   file it uses.
2. **The user points the wizard at a patch file**, or there are none.

## Decision 6: speed without a profile

Profile-guided optimization needs a run of the game to train on. Without it
Agent Under Fire takes 1.12 to 1.19 times stock PCSX2's time; Killzone's
play took 1.05 when last measured without one (G10).

1. **Accept that for the first version, and make the generated code faster
   in a step of its own later (recommended).** Both games keep the
   console's pace in the window as they are.
2. **The wizard trains a profile**: a short unattended run, then a second
   compile with it. Twice the compile time, and the run has to reach play.

## Decision 7: platforms

1. **Windows first, the design kept portable (recommended)**: modules as
   shared objects on Linux, nothing tied to one system's loader; Linux as
   soon as the maintainer chooses to run it (ADR 0008). macOS is optional
   (ADR 0001), and on ARM64 the generated code's SSE intrinsics would have
   to be translated first.

## The order of work proposed

Each step has its pass criterion; every comparison with stock PCSX2 (the
test suite, `same-as-stock`, `lockstep`) keeps working throughout, in
development builds as today.

| Step | Work | Pass criterion |
|---|---|---|
| P4.1 The module's cost | A prototype: the test programs and Agent Under Fire built as a module loaded by the runtime, the machine's state reached across the boundary | Every test passes; the game's time measured against today's build, and the cost put to the maintainer |
| P4.2 Freestanding generated code | The contract made independent of PCSX2's headers and the standard library, the layouts checked; the host split into a prebuilt runtime and modules (the game's, the BIOS's, the micro code's, the IOP code's) | A game module compiled with nothing but `clang`, `lld` and PS2Native's own headers; every test passes; a game is still the stock run |
| P4.3 The shipped toolchain | `clang` and `lld` brought along, trimmed to what is needed | A game compiled and run on a computer with nothing else installed (a fresh Windows user, without Visual Studio) |
| P4.4 The packaged game | The folder: launcher, modules, fixed settings, resources, the disc image and BIOS read in place | Killzone and Agent Under Fire run from their folders, at the console's pace, the same runs as from the development host |
| P4.5 The wizard | Qt: the five steps, recognition, defaults, progress, diagnostics, the licence warning at packaging | A game packaged from the disc image and the BIOS with every default accepted, without a command line |
| P4.6 New code in play | Captures written by the packaged game, its module recompiled in the background | A session that meets new code finds it compiled at its next start |
| P4.7 Linux | The runtime, the modules and the wizard on Linux | On the maintainer's own Linux machine (ADR 0004) |

## Decided

The maintainer accepted every recommendation below on 2026-11-16
([ADR 0028](adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md)
has their words and how they were read).

1. How a user's computer compiles a game: **a shipped `clang` and `lld`,
   freestanding generated code in a module loaded by a prebuilt runtime**
   (recommended), or a compiler the user installs.
2. What a packaged game is: **a folder** (recommended), or one executable.
3. New code on a user's computer: **compiled by the game itself in the
   background** (recommended, with the wizard's recompile first), or only
   when the user runs the wizard again.
4. The wizard's interface: **Qt 6** (recommended), or a command line first.
5. Patches: **an installed PCSX2's archive, or PCSX2's release fetched at
   the user's word** (recommended), or a file the user gives.
6. Speed: **accept 1.1 to 1.2 times stock PCSX2's time for the first
   version** (recommended), or train a profile in the wizard.
7. Platforms: **Windows first, the design portable** (recommended).
8. The order of work: **as above, beginning with the prototype of P4.1**
   (recommended).
