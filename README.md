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

**PS2Native 0.2**, a pre-release, published on 2026-11-22:
[the release, with its notes](https://github.com/luisbraganca/ps2n/releases/tag/v0.2).

| | File | Runs on |
|---|---|---|
| Windows | `PS2Native-0.2-windows-x64.zip` | Windows 10 (version 1803 or later) or Windows 11, 64-bit |
| Linux | `PS2Native-0.2-linux-x64.tar.xz` | 64-bit Linux with glibc 2.35 or later (Ubuntu 22.04, Linux Mint 21 and later) |

Nothing to install: unpack it and start **PS2Native**. Its wizard takes five
steps: the game's disc image, in any format PCSX2 plays; the BIOS; the
settings, which are PCSX2's own pages for the game; the recompile, a few
minutes; and the game's folder, which plays the game from then on. A game
made with 0.1 is brought up to date from the wizard's first step ("Games made
earlier...").

It needs a processor with SSE4.1, a graphics driver with Vulkan (or another
renderer chosen in the game's settings), and about 1 GB of disk for each game
on Windows, 1.5 GB on Linux: each game's folder has its own copy of the
compiler.

## Status

A preview. It has been tried with five games, the European releases of
Killzone, 007: Agent Under Fire, Gran Turismo 3: A-Spec, Jak and Daxter: The
Precursor Legacy and Silent Hill 3. Every other game is untried and may not
work.

- **Windows x64.** Everything measured was measured there, on Windows 11. On
  the maintainer's own sessions played back, a compiled game takes 0.98 to
  1.00 times the time stock PCSX2 takes with its recompilers (Killzone 0.92,
  with counts of a session that is nearly the one timed). That is against
  stock PCSX2 with the console's second vector unit (VU1) on the main
  processor thread, as PS2Native runs it. PCSX2 itself gives VU1 a thread of
  its own by default, on a computer with three cores or more, and so set it
  takes 2% to 40% less time on those sessions: a compiled game then takes
  1.02 to 1.65 times its time. Carrying VU1 out on a thread of its own is
  PS2Native's next step.
- **Linux x64.** Built and tested only inside WSL 2 (Ubuntu 22.04) so far,
  every test passing there, and games made and played there. One game was
  timed, Jak and Daxter: as 0.2 has it, 1.05 times stock PCSX2's time, and
  about 2.2 times the time of stock PCSX2 with its VU thread on. Most of what
  it takes more there than on Windows was PCSX2 waking a thread PS2Native
  does not use, which the next release leaves out (0.75 and 1.59 then).
  Nothing is known to work on a Linux machine of its own yet.
- **What a compiled game is held to.** A run on PS2Native is compared with
  the same run on stock PCSX2 at every point where the two can be compared,
  and with recordings made on a console: 534 tests.
- **No recompiler for PS2 code is in a game's folder.** Code a session meets
  for the first time (some games load theirs from their data) runs
  interpreted, slower, with a notice over the picture, and is compiled when
  you close the game.

What was done and measured, step by step: the proof of concept
([docs/poc-log.md](docs/poc-log.md)), the first games
([docs/game-log.md](docs/game-log.md), [docs/phase3-report.md](docs/phase3-report.md))
and the application ([docs/app-log.md](docs/app-log.md)).

## Building it

PS2Native builds inside PCSX2's own CMake project, with PCSX2 `v2.9.170` as a
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
| [docs/adr/](docs/adr/) | The decisions, one file each |
| [assets/branding/README.md](assets/branding/README.md) | The logo files, the colours and how to use them |
| [docs/site/README.md](docs/site/README.md) | The website: which files in `docs/` are its own, and where its numbers come from |

## License

PS2Native is free software under the GNU General Public License, version 3
or later ([LICENSE](LICENSE)). It builds on PCSX2 (GPL-3.0 or later,
Copyright PCSX2 Dev Team), which is included as PCSX2 released it, as a
submodule under `third_party/pcsx2`. PS2Native's own changes to PCSX2's code
are the patches in [patches/pcsx2](patches/pcsx2/), each saying what it
changes and why; 0.2 and 0.1 have none. Each download's `licences` folder has
the licence of everything in it, and where the source of each is.

PS2Native is an independent project. It is not affiliated with the PCSX2
project or with Sony Interactive Entertainment.
