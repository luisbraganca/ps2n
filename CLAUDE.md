# CLAUDE.md

PS2Native (`ps2n`) converts PlayStation 2 games into native applications by
ahead-of-time static recompilation. This repository is a reboot of
`../ps2-uncompiler`, which stays untouched as an archive.

## Read first

- [docs/brief.md](docs/brief.md): the maintainer's brief, verbatim. It is
  authoritative. Its section 11 (AI development rules) applies to every
  session. If an implementation idea conflicts with it, say so; do not bend
  the goals.
- [docs/phase1-feasibility.md](docs/phase1-feasibility.md): the Phase 1
  assessment, with source references into PCSX2, the old project and the
  reference projects.
- [docs/execution-model.md](docs/execution-model.md): how compiled code, the
  fallback interpreter and the PCSX2 core run a program together. Read it
  before touching `runtime/` or `recompiler/`: how time is counted (exactly
  as PCSX2's EE recompiler counts it, by the blocks it would have formed) is
  a contract between the two.

## Status

**Phase 3 (the first real game: Killzone) was approved on 2026-11-11 and is
in progress.** [docs/game-log.md](docs/game-log.md) has its steps, their pass
criteria and what has been measured; its status table at the top says where
things stand. Phase 4 needs its own approval.

Phase 2 (proof of concept) is done on Windows; its Linux step (P5) is
deferred by the maintainer. Its steps and criteria are in section 14 of the
assessment, and [docs/poc-log.md](docs/poc-log.md) records what was done and
measured. Its last section lists the questions that are open, those waiting
for the maintainer first. Record every decision the maintainer makes as an
architecture decision record under `docs/adr/`.

The game is the maintainer's own copy, used where it is: the disc image and
the files extracted from it are in `C:\ps2\killzone`. Read them, never copy
them, and keep everything generated from them (C++, captures, pictures) in
the git-ignored build tree.

Decisions:

- [ADR 0001](docs/adr/0001-platform-priorities.md): Windows x64 and Linux x64
  are required; macOS, Android and iOS are optional. Build and test nothing
  for an optional platform unless asked.
- [ADR 0002](docs/adr/0002-static-cpus-on-pcsx2-core.md): the game's code is
  translated ahead of time and runs on PCSX2's core as the machine. A BIOS is
  required.
- [ADR 0003](docs/adr/0003-no-jit-in-generated-games.md): no recompiler for
  PS2 code ships in a generated game. PCSX2's VU and IOP recompilers are
  temporary, labeled stand-ins; its EE recompiler is never one. Unseen code
  is interpreted once, reported, captured and rebuilt.
- [ADR 0004](docs/adr/0004-proof-of-concept-environment.md): PCSX2 `v2.9.170`
  as a submodule; the `SCPH-70004` BIOS read in place; Linux is built and
  tested by the maintainer on their own machine, so never report Linux as
  working without their run.
- [ADR 0005](docs/adr/0005-never-further-from-the-console-than-pcsx2.md):
  what counts as correct. On no line of a console-recorded test may stock
  PCSX2 be right and PS2Native wrong, and every remaining difference is
  listed with its cause. Instruction semantics follow PCSX2's recompiler,
  and the console where a recording shows the recompiler wrong.
- [ADR 0006](docs/adr/0006-no-jit-rule-covers-ps2-code-only.md): the no-JIT
  rule covers PS2 program code only. PCSX2's VIF unpack recompiler and its
  software renderer's code generator stay.
- [ADR 0007](docs/adr/0007-bios-code-translated-ahead-of-time.md): the
  BIOS's EE code is to be translated ahead of time, on the user's machine
  from their own dump, as a file of its own beside the game. A BIOS image is
  still required at run time. Not started; it comes after the EE.
- [ADR 0008](docs/adr/0008-linux-run-deferred.md): the first Linux run is
  deferred until the maintainer chooses. Linux is still required.
- [ADR 0009](docs/adr/0009-phase-3-first-game-killzone.md): Phase 3 is
  approved; the first game is Killzone (`SCES-52004`).
- [ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md):
  the fix for PCSX2's VU0 status-flag bug is offered to PCSX2 by the
  maintainer; PS2Native carries it in its patch queue only if that stalls.
- [ADR 0011](docs/adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md):
  time is counted exactly as PCSX2's EE recompiler counts it. A run on
  PS2Native is the stock run, sync point for sync point; a difference is a
  defect to find, unless it comes from an instruction that deliberately
  follows the console (ADR 0005) and is listed with its cause.

## Layout, build and test

```
third_party/pcsx2         PCSX2 v2.9.170, a pinned submodule. Never edit it; changes go in a patch queue (none so far)
third_party/ps2autotests  hardware-verified PS2 test programs, a pinned submodule
cmake/ps2n-inject.cmake   adds PS2Native's targets to PCSX2's own CMake project without touching it
CMakeLists.txt, core/     build-time tools, a project of their own with no PCSX2 dependency: the
                          instruction decoder, disassembler, assembler, ELF/IRX/SNR2 readers
recompiler/               ps2n-recomp, in that same project: analysis and C++ generation for EE executables
runtime/                  what PS2Native adds to the core at run time: the EE provider (dispatcher,
                          fallback interpreter), the record of the blocks PCSX2's recompiler would have formed
                          (rec_blocks), instruction semantics, the contract for generated code
host/                     ps2n-host: console frontend for the PCSX2 core, the test bench
tests/unit/               unit tests of the build-time tools
tests/                    CTest registration for programs booted on the core, the driver script, the recorded
                          baselines; recompiles the test programs into the build tree and links them into the host
tests/programs/           our own test programs, in assembly for core's assembler: selfmod (rewrites its code), bench,
                          timeout (a delay loop that PCSX2 does not run)
tests/sdk/                C test programs built with ps2sdk (prebuilt in bin/), their sources, what stock PCSX2 prints
tests/games/              controller scripts for runs of a game (button presses we wrote; nothing from a game)
scripts/windows, linux    build and test entry points
docs/                     brief, assessment, decisions (adr/), proof-of-concept log, game log
build/, deps/             build trees and downloaded dependency packages; git-ignored
```

- Build: `scripts\windows\build.ps1` (add `-Reconfigure` after changing any
  CMake file). It enters the Visual Studio developer shell by itself.
- Test: `scripts\windows\test.ps1 -Bios <BIOS image>`, optionally
  `-Filter <regex>`. The BIOS is the maintainer's `SCPH-70004` dump in
  `Documents\PCSX2\bios`. Tests are skipped, not passed, without one.
- Speed: `scripts\windows\bench.ps1 -Bios <BIOS image> -Repeat 3` runs
  `tests/programs/bench.s` under each configuration and prints the times.
- The game: `scripts\windows\build.ps1 -Game <its boot executable>` compiles
  it into the host (the build tree remembers it; `-GameCapture <file>` adds
  what runs reported). `scripts\windows\game.ps1 -Image <disc image> -Config
  <configuration> -Frames <n>` runs it, with `-Pad` for a controller script
  (`tests/games/<game>/*.pad`), `-FrameEvery` for pictures along the way and
  `-Capture` to collect what had to be interpreted. `docs/game-log.md`, "How
  to run it", has the commands. A run's files are in
  `build\windows-clang\ps2n\tests\game\<name>\<configuration>`.
- Tests are named `<suite>/<configuration>/<program>`. Suites: `ps2autotests`
  (console recordings), `programs` (our own, `tests/programs/*.s`, built with
  `ps2n-recomp assemble`) and `sdk` (C programs built with ps2sdk, binaries in
  `tests/sdk/bin`). Configurations: `stock-jit`, `stock-interp`
  (PCSX2 as it ships), `ps2n-ee` (our provider, interpreting), `ps2n-static`
  (our provider, the program compiled ahead of time), `lockstep` (the compiled
  run compared with the interpreted one at every event test), `same-as-stock`
  (ADR 0011: the compiled run compared with a stock run at every sync point:
  place, cycle count, registers), `never-further` (ADR 0005: the compiled
  run's output against the stock run's, line by line; CTest runs the two it
  compares first). Also `selftest/tables-and-semantics`: our instruction
  semantics against PCSX2's interpreter and recompiler.
- A failing test's files are in `build\windows-clang\ps2n\tests\<test name>`:
  `core.log` (PCSX2's log), `guest.txt`, `actual.txt`.
- A `lockstep` failure names the event test where the runs part, a
  `same-as-stock` failure the sync point. The execution model's "Comparing
  two runs" says how to go from there (`ps2n-host -sync`, `-sync-against`,
  `-sync-detail`; they work under stock PCSX2 too).
- Generated C++ is in `build\windows-clang\ps2n\generated`. To look at what
  the recompiler makes of one program without building:
  `build\tools-windows\bin\ps2n-recomp.exe info <elf>`. To read code at an
  address, of an executable or of a raw image such as a BIOS:
  `ps2n-recomp.exe disasm <file> --at <address> [--count n] [--base address]`.
- A test that differs from the console recording goes into
  `tests/ps2autotests-known-differences.cmake` only after its cause is known;
  one whose run differs from the stock run, into
  `tests/same-as-stock-known-differences.cmake`, likewise.
- After a run, `ps2n-host` reports what the interpreter did. For a game whose
  image is complete, "instructions inside the executable's code" must be
  zero; when it is not, the line "compiled code handed over to the
  interpreter" names the addresses.
- The Linux scripts have never been run. Only the maintainer can run them.
- Never run stock `pcsx2-gsrunner` here: it writes into the maintainer's own
  `Documents\PCSX2`.

## Standing rules

- Phases are gated: stop at the end of each and ask for approval.
- Never substitute emulation, interpretation or a run-time JIT for static
  recompilation without a documented, approved fallback.
- Investigate root causes before changing code. After repeated failed
  attempts, stop and reassess instead of trying variations.
- Never invent test or benchmark results, and never skip a failing test
  silently. A performance or compatibility claim needs a reproducible
  measurement.
- Do not commit unless asked. Do not push to GitHub or delete repositories
  without explicit authorization. Ask before installing software.
- No BIOS images, game data, or code generated from a game or from a BIOS in
  the repository.
- PCSX2 (GPL-3.0+) may be reused with attribution. AnyPS5 is GPL-2.0-only:
  read it for ideas, never copy its code.
- PCSX2's `AGENTS.md` forbids AI-generated contributions and any automated
  posting to its GitHub. Never open issues or pull requests there, never
  comment there, and never write what is posted there (code, commit
  messages, descriptions, replies). The maintainer may contribute a fix
  themselves; a session may explain and point, no more
  ([ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).

## Development machine

Windows 11 x64 (i5-14600KF, 64 GB). Visual Studio Build Tools 2026 provides
clang-cl, CMake and Ninja, but only inside a developer shell; the command that
enters one is in `../ps2-uncompiler/CLAUDE.md` under "Build and test". The
Vulkan SDK is in `C:\VulkanSDK`. Stock PCSX2 is installed in
`C:\Program Files\PCSX2`, with its data (including the maintainer's BIOS
dumps) in `Documents\PCSX2`. Game workspaces from the old project are under
`C:\ps2\`. Use the BIOS dumps only after the maintainer says which.

## References examined in Phase 1

| Project | Revision |
|---|---|
| PCSX2 | tag `v2.9.170`, commit `a886f8ef` |
| AnyPS5 | `3c9276b7` |
| PS2Recomp | `e5636981` |
| PS2 Web Recomp | `76554ba4` |
| ps2autotests | `97469ffb` |

They were cloned into a temporary directory for that session and are not part
of this repository.
