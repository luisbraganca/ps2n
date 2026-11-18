# ADR 0038: P4.7 built and tested on a Linux inside the development computer first

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-19 |
| Decided by | Maintainer |

## Context

P4.7, Linux, the last step of Phase 4, is next (ADR 0037, "4. Ok"). Nothing
of PS2Native has been compiled on Linux. Phase 2's Linux scripts
(`scripts/linux`) were written on Windows from PCSX2's own Linux build and
never run (ADR 0008), and much has been added since that is Windows only:
the game's window and the notice over its picture (`host/window.cpp`), a
game's launcher and its waiting window (`host/launcher.cpp`), how the
runtime loads a game's modules and how they reach PCSX2's core (by the names
Windows' compiler gives its symbols, `ps2n/pcsx2.h`), the wizard's
packaging and the recompile's commands (`wizard/package.cpp`,
`wizard/pipeline.cpp`), and the folder the toolchain travels in
(`kit.ps1`). Qt, PCSX2's core and its readers of a disc are made for Linux
too. ADR 0004 has Linux built and tested by the maintainer on their own
machine, and never reported as working without their run; the development
computer had no Linux.

The session asked: "May I build and test on a Linux inside this computer
(WSL 2 with Ubuntu 22.04)?", and recommended it: without it the Linux code
would be written without being compiled, each error found only on the
maintainer's machine, one at a time; with it, the maintainer's machine is
needed only for what a Linux inside Windows cannot show (the window on its
graphics card, the sound) and for the criterion's run. Ubuntu 22.04
because PCSX2 makes its own Linux releases on it (`linux_build_qt.yml` at
`v2.9.170`), so that what is built there runs on later distributions too.
The maintainer answered:

> 1. Sure.

## Decision

1. **WSL 2 with Ubuntu 22.04 on the development computer**, where sessions
   build PS2Native for Linux and run its tests before the maintainer's
   runs. The session installs in it the packages PCSX2's Linux build needs,
   as the question said it would; Windows' part of the installation needs
   the maintainer (an administrator's consent, a restart, the Linux user's
   name and password).
2. **ADR 0004 stands.** Linux is reported as working only after the
   maintainer's run on their own machine, which P4.7's criterion is.

## The two other questions (2026-11-19)

The same message asked what the maintainer's Linux machine is, and
proposed P4.7's criterion ("on your Linux machine, a game made with the
wizard from its disc image plays from its folder at the console's pace, and
what a session meets for the first time is compiled in the background for
its next start"). The maintainer answered, a little later, that the machine
is a laptop: "Linux Mint. I think GTX 1660"; and "3. Yes".

3. **The Linux machine** is a laptop with Linux Mint, its version and
   desktop not known yet, and, as the maintainer thinks, NVIDIA's GeForce
   GTX 1660. Linux Mint 21 is made from Ubuntu 22.04 and Linux Mint 22
   from Ubuntu 24.04: what is built on Ubuntu 22.04 runs on either.
4. **P4.7's criterion** is the session's proposal: on that machine, a game
   made with the wizard from its disc image plays from its folder at the
   console's pace, and what a session meets for the first time is compiled
   in the background for its next start.
