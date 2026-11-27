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
  a contract between the two. Its last two sections are the same for the
  vector units' micro programs and for the I/O processor's code.
- [docs/phase3-report.md](docs/phase3-report.md): what Phase 3 did, how it
  compares with stock PCSX2, what it leaves, and what Phase 4 has to decide
  first.
- [docs/phase4-proposal.md](docs/phase4-proposal.md): Phase 4's design as
  the maintainer decided it (ADR 0028), and its steps, P4.1 to P4.7, with
  their pass criteria; a step P4.8, the folder for others, was added by
  ADR 0033 ([docs/app-log.md](docs/app-log.md) has all eight).
- [docs/development.md](docs/development.md): how things are done, one
  section to a task. Read the section for what you are about to do.
- [docs/adr/README.md](docs/adr/README.md): every decision the maintainer
  has made, each with its digest. Read the record itself before changing
  what it decides.
- [docs/history.md](docs/history.md): how the project got here, phase by
  phase.

The table under "Before you do this, read that" says which of them a task
needs.

## Where things stand

As of 2026-11-27. This section is replaced when it changes, never added to.

- **Phase 5, Maintenance,** since 2026-11-22 (ADR 0053). Phases 1 to 4 are
  done.
- **Released:** 0.4, a pre-release, on 2026-11-25: the tag `v0.4` on
  `a243df4`.
- **Being released:** 0.5 (ADR 0063). `version.txt` says 0.5.
- **In `main` and in the maintainer's game folders, in no release:** the menu
  over the game, opened with Esc (ADR 0060; `e616401`, `224d896`).
- **PCSX2 followed:** v2.9.198 (ADR 0062; v2.9.189 until then), with two
  changes of PS2Native's own (`patches/pcsx2`): ADR 0050's, and a missing
  include, which stock PCSX2's trees are built with too. The module version is
  7. Committed and pushed at the maintainer's word; their game folders given
  these programs.
- **Games tried, eight:** Killzone, Agent Under Fire, Gran Turismo 3, Jak and
  Daxter and Silent Hill 3, European releases; Resident Evil – Code: Veronica
  X, Secret Agent Clank and SSX Tricky, from the USA. The maintainer's discs
  are in `C:\ps2\<game>`. Their game folders are in `build\kit\games` and,
  inside WSL, in `build/PS2Native/games`.
- **Systems:** Windows x64, on two computers of the maintainer's. Linux x64 is
  tested under WSL 2 (Ubuntu 22.04) on the development computer only, and
  described so.
- **Tests:** 556 in the development trees on both systems; 554 (Windows) and
  553 (Linux) in the trees a release is made from, where three comparisons
  are disabled; none skipped.
- **Speed, as 0.4 has it,** on the maintainer's sessions, against stock PCSX2
  v2.9.189 built as its releases are, with its VU thread on: Jak and Daxter
  0.93 times its time, Gran Turismo 3 1.00, Agent Under Fire 0.97, Killzone
  0.94; under WSL 2, Jak and Daxter 0.96.
- **Maintenance tools** (ADR 0061): the release's scripts in the repository,
  this file cut; committed and pushed (`b542370`).
- **Dates:** from 2026-12-11, read whether PCSX2's issue 15185 has a reply,
  and tell the maintainer (ADR 0010); issue 15223, opened on 2026-11-22,
  likewise from 2026-12-22.

## Rules for every session

They hold whatever the task. The decision each comes from is in
[docs/adr/README.md](docs/adr/README.md).

### The maintainer decides

- Phases are gated: stop at the end of each and ask for approval.
- Do not commit unless asked. Do not push to GitHub or delete repositories
  without explicit authorization. Ask before installing software.
  A new dependency is software too.
- Record every decision the maintainer makes as an architecture decision
  record under `docs/adr/`, with their exact words and how they were read,
  and give it its entry in the index. What a session decides within a step
  is listed in the record as its choice, "for the maintainer to decide
  otherwise".
- A change to how time is counted, or to what is compared with stock PCSX2,
  goes to the maintainer first (ADR 0015, ADR 0019).
- A step that changes what users get is measured first, and put to the
  maintainer before it goes into a game's folder or a release.
- Whether PS2Native moves to a newer PCSX2 is the maintainer's to decide,
  after the results (ADR 0053). So is each change PS2Native carries to
  PCSX2's code; one that would alter a result of a run needs their word on
  that too (ADR 0050).
- The maintainer's own game folders are never measured on or tried on:
  copies are, in `build\measure`. They keep their programs unless the
  maintainer says otherwise, and their memory cards, sessions and settings
  stay as they are.
- Artifacts of PCSX2's renderer that stock PCSX2 shows too are left to
  PCSX2. A setting of PCSX2's own that removes them is put to the maintainer
  (ADR 0026).

### Commits and GitHub

- Commit messages never reference Claude: leave out the `Co-Authored-By`
  and `Claude-Session` lines a session adds by default
  ([ADR 0020](docs/adr/0020-no-claude-references-in-commit-messages.md)).
  Naming the file `CLAUDE.md` is fine.
- Pushing `main` publishes the website, which is `docs/` (ADR 0014). When a
  number or a state the page repeats changes, `docs/index.html`, the date at
  its foot and `README.md` change with it (`docs/site/README.md`).

### PCSX2

- PCSX2's `AGENTS.md` forbids AI-generated contributions and any automated
  posting to its GitHub. Never open issues or pull requests there, never
  comment there, and never write what is posted there (code, commit
  messages, descriptions, issue texts, replies). The maintainer may
  contribute a fix themselves; a session may explain and point, no more
  ([ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).
  A commit message here never names one of PCSX2's issues or pull requests
  by its address or as `PCSX2/pcsx2#` and a number: GitHub would post a
  reference there.
  Asked by the maintainer for a draft of an issue, or for one "as a guide",
  a session writes none, and says where the defect is and how it is seen.
- Never edit the submodule `third_party/pcsx2`. A change PS2Native carries is
  a patch in `patches/pcsx2`; a session's own trial is made on a copy in the
  build tree, or in a worktree (ADR 0050, ADR 0061).
- PCSX2 (GPL-3.0+) may be reused with attribution. AnyPS5 is GPL-2.0-only:
  read it for ideas, never copy its code.
- What PS2Native builds carries PS2Native's branding and never PCSX2's logo,
  and PS2Native's logo is not put on PCSX2's interface. PCSX2's name stays
  wherever it is attribution. A finished game shows nothing of what the core
  can draw over the picture: that is switched off by settings, never restyled
  by patching PCSX2. The notice and the menu over a game are PS2Native's own
  (ADR 0012).
- Never run stock `pcsx2-gsrunner` here: it writes into the maintainer's own
  `Documents\PCSX2`.

### Games, the BIOS and the repository

- No BIOS images, game data, or code generated from a game or from a BIOS in
  the repository.
  Everything made from them (C++, captures, pictures, profiles) stays in the
  build tree, which git ignores.
- The maintainer's games, their BIOS dumps and PCSX2's archive of patches are
  used where they are: read them, never copy them.
- Nothing from a game or a BIOS goes on the website, into `tests/games` or
  into a release. PCSX2's patches are never shipped (ADR 0014, ADR 0028,
  ADR 0042).
- `build\` is never deleted or cleaned whole. Beside the build trees it holds
  the maintainer's game folders, with their memory cards and sessions, the
  profiles of play and the measurement folders. (New with ADR 0061, kept at
  the maintainer's word.)

### The computer is the maintainer's

- A game the maintainer may be playing keeps its pace. Before anything heavy,
  look whether one runs from `build\kit\games`, and read the answer before
  starting. Windows work runs at below-normal priority, Linux work on half
  the processors, long scripts on a desktop of their own.
- Never stop a process you did not start. On 2026-11-21 a check stopped
  every `ps2n-host` it found, and closed the maintainer's game.
- A window is checked with the sound off, or on the hidden desktop: the
  sound comes out of the maintainer's speakers.

### What a session may claim

- Never invent test or benchmark results, and never skip a failing test
  silently. A performance or compatibility claim needs a reproducible
  measurement. A speed claim against stock PCSX2 says which stock PCSX2:
  with VU1 on the main processor thread, as every comparison of two runs has
  it, or with its VU thread on, PCSX2's default where there are three cores
  or more, which is much faster on some games (ADR 0051).
- A skipped test is not a passed one. Without a BIOS the tests that boot
  programs are skipped, and ctest counts a skipped test among those that did
  not fail: look for "Skipped" before saying that every test passed.
- Linux is described everywhere as tested under WSL 2 (Ubuntu 22.04) on the
  development computer, until a Linux machine of its own runs PS2Native. It
  is never reported as working beyond that (ADR 0004, ADR 0041).
- A test goes into a list of known differences only after its cause is
  known, and every difference left is listed with its cause.

### What counts as correct

- Never substitute emulation, interpretation or a run-time JIT for static
  recompilation without a documented, approved fallback.
  No recompiler for PS2 code ships in a generated game; the rule covers PS2
  program code only (ADR 0003, ADR 0006).
- On no line of a console-recorded test may stock PCSX2 be right and
  PS2Native wrong. Instruction semantics follow PCSX2's recompiler, and the
  console where a recording shows the recompiler wrong (ADR 0005).
- Time is counted exactly as PCSX2's EE recompiler counts it. A run on
  PS2Native is the stock run, sync point for sync point. A difference is a
  defect to find, unless it comes from an instruction that deliberately
  follows the console and is listed with its cause (ADR 0011).
- Investigate root causes before changing code. After repeated failed
  attempts, stop and reassess instead of trying variations.
- Windows x64 and Linux x64 are required; macOS, Android and iOS are
  optional. Build and test nothing for an optional platform unless asked
  (ADR 0001).

### When two sessions work at once

- They agree before timing anything, say "go" and "done" around every
  timing, and name the files they touch. Whichever commits second rebases
  onto the first (ADR 0018, ADR 0021).

## Before you do this, read that

| Before | Read |
|---|---|
| Touching `runtime/` or `recompiler/` | `docs/execution-model.md`; ADR 0005, ADR 0011 |
| Building, or changing a build tree's settings | `docs/development.md`: "Build", "The runtime itself" |
| Running the tests, or saying what they showed | `docs/development.md`: "Test", "The tests", "When a test fails", "Known differences"; `docs/execution-model.md`, "Comparing two runs" |
| Timing a run, or making a speed claim | `docs/development.md`: "Timing and profiling a run", "Compiled games timed against stock PCSX2"; ADR 0047, ADR 0051 |
| Changing what a module and the runtime share, or the generated code's headers | `docs/development.md`, "The compiled code in modules of its own". The module version changes with it (`kGameModuleVersion`) |
| PCSX2's code: a change, a newer version, a defect found | `docs/development.md`, "A newer PCSX2 tried, and the move to it"; `patches/pcsx2/README.md`; ADR 0010, ADR 0050, ADR 0053, ADR 0054 |
| Making a release | `docs/development.md`, "A release"; ADR 0042, ADR 0047, ADR 0049 |
| Giving the maintainer's game folders new programs | `docs/development.md`, "The maintainer's game folders brought up to date" |
| Running anything long, or opening a window | `docs/development.md`, "Batches on a computer someone is playing on" |
| Working on Linux | `docs/development.md`: "Linux", "Linux inside WSL, from Windows"; ADR 0038, ADR 0041 |
| The wizard, a game's folder, its own compile, the launcher | `docs/development.md`, "The PS2Native folder, a game's folder and the wizard"; `docs/phase4-proposal.md`; ADR 0028, ADR 0032, ADR 0040, ADR 0045, ADR 0048 |
| The menu, the notice, anything drawn over a game | ADR 0012, ADR 0035, ADR 0055, ADR 0060 |
| A new game, a game's captures, playing one | `docs/development.md`: "A game compiled into the host", "Playing a game, and running a session again"; ADR 0024, ADR 0026, ADR 0057 |
| Micro code, IOP code, the BIOS's code | `docs/execution-model.md`, its last two sections; `docs/development.md`, their sections; ADR 0017, ADR 0022, ADR 0023 |
| The website or the README | `docs/site/README.md`; ADR 0014 |
| Wondering why something is as it is | `docs/adr/README.md`, by subject; `docs/history.md`; the logs: `docs/poc-log.md` (Phase 2), `docs/game-log.md` (Phase 3), `docs/app-log.md` (Phases 4 and 5) |

## Layout

```
third_party/pcsx2         PCSX2, a pinned submodule. Never edit it
patches/pcsx2             PS2Native's changes to PCSX2's code, one patch to a change
third_party/ps2autotests  test programs recorded on a console, a pinned submodule
cmake/                    how PS2Native's targets join PCSX2's own CMake project
core/, recompiler/        the build-time tools, with no PCSX2 dependency: decoder, assembler, readers, ps2n-recomp
runtime/                  what PS2Native adds to the core at run time: the EE, VU and IOP providers, instruction
                          semantics, what a module of compiled code and the runtime share
host/                     ps2n-host: the runtime's program, the test bench, the window a game is played in, the menu
wizard/                   ps2n-wizard, the application a game is made with; ps2n-remake, a game folder's own compile
tests/                    unit tests, test programs, recorded baselines, lists of known differences
scripts/windows, linux    build, test, kit, game, and the long ones: release, update-games, time-games, try-pcsx2
assets/                   branding
docs/                     the brief, the logs, the decisions (adr/), how things are done (development.md); and the
                          website, which GitHub Pages serves from this folder
version.txt               PS2Native's version
build/, deps/             git-ignored. build/ holds the build trees and much that cannot be made again: see the rules
```

`docs/development.md`, "Layout", says what is in each.

## Build and test

- **Build:** `scripts\windows\build.ps1`; add `-Reconfigure` after changing
  any CMake file. It enters the Visual Studio developer shell by itself.
- **The trees:** `build\windows-clang`, for development, the compiled code in
  the host; `build\windows-clang-module`, the compiled code in modules, with
  the wizard: what a release and the PS2Native folder are made from
  (`-BuildDir`); `build\windows-clang-stock`, stock PCSX2 as its releases are
  built, for timings.
- **Test:** `scripts\windows\test.ps1 -Bios <BIOS image>`, with `-Filter
  <regex>` and `-BuildDir`. The BIOS is the maintainer's `SCPH-70004` dump in
  `Documents\PCSX2\bios`. A failing test's files are in
  `<tree>\ps2n\tests\<test name>`.
- **Linux:** in WSL 2 with Ubuntu 22.04, in the clone `~/ps2n`, never on
  `/mnt/c`: `scripts/linux/build.sh`, `PS2N_BIOS=<BIOS image>
  scripts/linux/test.sh`. From Windows: `scripts\windows\wsl.ps1`.

## Development machine

Windows 11 x64 (i5-14600KF, 64 GB). Visual Studio Build Tools 2026 provides
clang-cl, CMake and Ninja, but only inside a developer shell; the command that
enters one is in `../ps2-uncompiler/CLAUDE.md` under "Build and test". The
Vulkan SDK is in `C:\VulkanSDK`. Stock PCSX2 is installed in
`C:\Program Files\PCSX2`, with its data (including the maintainer's BIOS
dumps) in `Documents\PCSX2`. Game workspaces from the old project are under
`C:\ps2\`. Use the BIOS dumps only after the maintainer says which.

## Keeping this file short

Every session reads all of this file, so it holds only what every session
needs: the rules, where things stand, and where to read. It had grown to
24,000 words before ADR 0061 cut it.

- **"Where things stand"** is replaced when it changes.
- **A decision** gets its record in `docs/adr/` and its entry in
  `docs/adr/README.md`. It comes here only where it makes a rule for every
  session.
- **What was done and measured** goes into the log (`docs/app-log.md`), and
  a short paragraph of it into `docs/history.md`.
- **How something is done** goes into `docs/development.md`.
