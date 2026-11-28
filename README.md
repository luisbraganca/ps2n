<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/branding/logos/ps2native-horizontal-dark.png">
    <img src="assets/branding/logos/ps2native-horizontal-light.png" alt="PS2Native" width="420">
  </picture>
</h1>

<p align="center">
  <strong>PlayStation 2 games as native applications, by static recompilation.</strong>
</p>

<p align="center">
  <a href="https://luisbraganca.github.io/ps2n/">luisbraganca.github.io/ps2n</a>
  &middot;
  <a href="https://github.com/luisbraganca/ps2n/releases">Releases</a>
</p>

PS2Native (`ps2n`) converts a PlayStation 2 game into a native application.
The game's code is translated to C++ ahead of time and compiled on your own
computer, by the compiler PS2Native brings, into a folder that plays the
game. The console around that code (graphics, sound, storage, timing) is the
core of [PCSX2](https://pcsx2.net): the translated code takes the place of
PCSX2's recompilers for the console's two processors and its two vector
units.

You provide your own game and your own BIOS dump. Neither is included here,
and nothing generated from them is.

## Download

**PS2Native 0.5**, a pre-release, published on 2026-11-28:
[the release, with its notes](https://github.com/luisbraganca/ps2n/releases/tag/v0.5).

| | File | Runs on |
|---|---|---|
| Windows | `PS2Native-0.5-windows-x64.zip` | Windows 10 (version 1803 or later) or Windows 11, 64-bit |
| Linux | `PS2Native-0.5-linux-x64.tar.xz` | 64-bit Linux with glibc 2.35 or later (Ubuntu 22.04, Linux Mint 21 and later) |

Nothing to install: unpack it and start **PS2Native**. Its wizard takes five
steps: the game's disc image, in any format PCSX2 plays; the BIOS; the
settings, which are PCSX2's own pages for the game; the recompile, a few
minutes; and the game's folder, which plays the game from then on. A game
made with an earlier version is brought up to date from the wizard's first
step ("Games made earlier..."), and compiles itself again before it next
starts.

It needs a processor with SSE4.1, a graphics driver with Vulkan (or another
renderer chosen in the game's settings), and about 1 GB of disk for each game
on Windows, 1.5 GB on Linux: each game's folder has its own copy of the
compiler.

## Status

A preview. It has been tried with eight games: the European releases of
Killzone, 007: Agent Under Fire, Gran Turismo 3: A-Spec, Jak and Daxter: The
Precursor Legacy and Silent Hill 3, and the American releases of SSX Tricky,
Resident Evil – Code: Veronica X and Secret Agent Clank, the last two as far
as their menus. Every other game is untried and may not work.

- **Windows x64.** Everything measured was measured there, on Windows 11.
  PCSX2 gives the console's second vector unit (VU1) a thread of its own by
  default, on a computer with three cores or more, and PS2Native does too
  since 0.3, wherever PCSX2 would. On the maintainer's own sessions played
  back, a compiled game as 0.5 has it takes 0.95 to 1.01 times the time of
  stock PCSX2 2.9.198 with its VU thread on, built as PCSX2's releases are
  (Jak and Daxter 0.98, Gran Turismo 3 1.01, Agent Under Fire 0.98, Killzone
  0.95). 0.4 took 0.93 to 1.00 times the time of stock PCSX2 2.9.189 so; Jak
  and Daxter's rose because stock PCSX2 2.9.198 took about 2% less time on
  its session than 2.9.189, and the compiled game about 3% more than 0.4's.
  0.3 took 0.94 to 1.00 times the time of stock PCSX2 2.9.170 so. 0.2, which kept VU1 on the main
  processor thread, took 1.02 to 1.65 times it (and 0.98 to 1.00 times the
  time of stock PCSX2 with VU1 on its main thread too).
- **Linux x64.** Built and tested only inside WSL 2 (Ubuntu 22.04) so far,
  every test passing there, and games made and played there. One game was
  timed, Jak and Daxter: as 0.5 has it, 0.98 times the time of stock PCSX2
  with its VU thread on, where 0.4 took 0.96 times it, 0.3 0.99 times and 0.2
  about 2.2 times. The menu over a game, new in 0.5, has not yet been opened
  in a game's window there. Nothing is known to work on a Linux machine of
  its own yet.
- **What a compiled game is held to.** A run on PS2Native is compared with
  the same run on stock PCSX2 at every point where the two can be compared,
  and with recordings made on a console: 556 tests.
- **No recompiler for PS2 code is in a game's folder.** Code a session meets
  for the first time (some games load theirs from their data) runs
  interpreted, slower, with a notice over the picture, and is compiled when
  you close the game.

What was done and measured, step by step: the proof of concept
([docs/poc-log.md](docs/poc-log.md)), the first games
([docs/game-log.md](docs/game-log.md), [docs/phase3-report.md](docs/phase3-report.md))
and the application ([docs/app-log.md](docs/app-log.md)).

## Building it

PS2Native builds inside PCSX2's own CMake project, with PCSX2 `v2.9.198` as a
submodule that is never edited (`git submodule update --init --depth 1`).
What PS2Native changes of PCSX2's code is in [patches/pcsx2](patches/pcsx2/),
applied to copies of the files concerned when it is built.

- **Windows:** Visual Studio or its Build Tools with clang-cl, CMake and
  Ninja, and PCSX2's Windows dependency package in `deps\windows-x64`
  ([docs/poc-log.md](docs/poc-log.md) says where it comes from).
  `scripts\windows\build.ps1` builds, and `scripts\windows\test.ps1 -Bios
  <BIOS image>` runs the tests.
- **Linux:** CMake 3.25 or later, Ninja, and clang and lld (LLVM 22.1.3 in
  `deps/llvm-22.1.3-linux` is taken when it is there). `scripts/linux/build.sh`
  builds, PCSX2's dependencies with PCSX2's own script the first time, and
  `PS2N_BIOS=<BIOS image> scripts/linux/test.sh` runs the tests.

The tests that boot the console need a BIOS image of your own, and are
skipped without one. Each script's header says what its options do.
`scripts\windows\kit.ps1` and `scripts/linux/kit.sh` put together the folder
a release is made from.

## Documentation

| Document | What it holds |
|---|---|
| [docs/brief.md](docs/brief.md) | The project's goals and rules |
| [docs/phase1-feasibility.md](docs/phase1-feasibility.md) | The feasibility assessment and the architecture |
| [docs/execution-model.md](docs/execution-model.md) | How compiled code, the interpreter and the PCSX2 core run a program together |
| [docs/poc-log.md](docs/poc-log.md) | The proof of concept: what was done and measured |
| [docs/game-log.md](docs/game-log.md), [docs/phase3-report.md](docs/phase3-report.md) | The first games, and the report on them |
| [docs/phase4-proposal.md](docs/phase4-proposal.md), [docs/app-log.md](docs/app-log.md) | The application: its design, and what was done and measured |
| [docs/adr/README.md](docs/adr/README.md) | The decisions, one file each, and their index |
| [docs/development.md](docs/development.md) | How things are done: building, testing, timing, a release, a newer PCSX2 tried |
| [docs/history.md](docs/history.md) | How the project got here, phase by phase |
| [assets/branding/README.md](assets/branding/README.md) | The logo files, the colours and how to use them |
| [docs/site/README.md](docs/site/README.md) | The website: which files in `docs/` are its own, and where its numbers come from |

## License

PS2Native is free software under the GNU General Public License, version 3
or later ([LICENSE](LICENSE)). It builds on PCSX2 (GPL-3.0 or later,
Copyright PCSX2 Dev Team), which is included as PCSX2 released it, as a
submodule under `third_party/pcsx2`. PS2Native's own changes to PCSX2's code
are the patches in [patches/pcsx2](patches/pcsx2/), each saying what it
changes and why; 0.5 has both, 0.3 and 0.4 the first, 0.2 and 0.1 none. Each
download's `licences` folder has the licence of everything in it, and where
the source of each is.

PS2Native is an independent project. It is not affiliated with the PCSX2
project or with Sony Interactive Entertainment.
