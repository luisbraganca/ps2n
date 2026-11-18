# ADR 0036: The folder for others: how P4.8 is done

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-18 |
| Decided by | Maintainer (the step next, its disc image); the session (how, for the maintainer to decide otherwise) |

## Context

ADR 0033 made P4.8, the folder for others, a step of its own, done before
the folder `kit.ps1` makes goes to anyone but the maintainer: the licences
of everything it carries; disc images other than `.iso` files of 2048-byte
sectors; PCSX2's archive of patches where PCSX2 is not installed, fetched
at the user's word or given (ADR 0028); a fixed setting edited by hand into
a game's settings file refused by the runtime; a choice, in the wizard's
first step, of the executables compiled. Its criterion: on a computer
without PCSX2, a game made with the wizard from a `.chd` image, its patches
fetched at the user's word, and the folder carrying the licence of
everything in it.

On 2026-11-18, asked what next, the session recommended P4.8 before P4.7,
Linux, which needs the maintainer's Linux machine for every run; the
maintainer answered "Yep". Asked for a `.chd` image to check with, the
maintainer answered: "I do, there's a chd file for silent hill 3"
(`C:\ps2\silent-hill-3`, *Silent Hill 3*, `SLES-51434`, a game the project
had never run, read in place as the others are).

What there was: PS2Native's reader of a disc's files read an `.iso` alone,
and threw C++ exceptions, which PCSX2's build switches off in the runtime;
PCSX2's own readers, in the runtime, open `.iso`, raw `.bin`, `.img` and
`.mdf`, `.chd`, `.cso`, `.zso` and `.gz` (not `.cue` sheets). The wizard
kept the fixed settings out of a game's settings file, but the runtime took
whatever the file said. PCSX2's own builds take the archive of patches from
the latest release of PCSX2's patches project. PCSX2's dependency package
carries Qt's own SBOMs, which name the code of others built into each Qt
library and plugin; PCSX2's list of its third-party licences leaves out Qt
and SDL.

## Decision

1. **P4.8 is next, before P4.7** (the maintainer's "Yep"), and its `.chd`
   is the maintainer's Silent Hill 3. Its criterion is run on the
   maintainer's Windows 10 computer, P4.3's, which has no PCSX2 (asked,
   the maintainer answered: "You mean my other machine that has windows
   10? It doesn't have PCSX2").

The rest is the session's, as it was done, for the maintainer to decide
otherwise:

2. **A disc is read through PCSX2's own readers**, by the runtime: `ps2n-host
   -disc-executables <image>` and `-disc-extract <image> <folder> [<path>...]`
   (`host/disc.cpp`), modes of their own that load no module and run no
   machine. What is looked for on the disc stays PS2Native's
   (`ps2n/iso9660.h`, made free of exceptions so that the runtime compiles
   it): the same rule for every format. The wizard, a game's own recompile
   and `make-game.ps1` use them; `ps2n-recomp extract` stays for `.iso`
   files in development. A `.cue` sheet stands for the image its first
   `FILE` line names, which PCSX2 opens.
3. **PCSX2's archive of patches, where PCSX2 is not installed**: the
   wizard's settings step offers to fetch it, at a button, from the latest
   release of PCSX2's patches project
   (`https://github.com/PCSX2/pcsx2_patches/releases/latest/download/patches.zip`,
   the file PCSX2's own builds include), with PCSX2's own downloader, or to
   take a `patches.zip` the user chooses; it is kept in the wizard's folder
   (`patches\patches.zip`) for the games made after, and checked to be a zip
   of patch files. ADR 0028's "PCSX2's release fetched at the user's word"
   read as that file. A game's folder gets the game's own patch file from
   it, as from an installed PCSX2's.
4. **A fixed setting in a game's settings file is refused**: the runtime,
   in a folder the wizard made (`-packaged`), will not start when the file
   sets one, and says which (`ps2n::game::kFixedSections`, `kFixedKeys`,
   the list the wizard keeps out of the file).
5. **The executables compiled**: the first step lists them, each ticked;
   the one the disc boots cannot be left out. One left out runs
   interpreted, captured, until the game's own recompile has compiled what a
   session met of it.
6. **The licences**: `kit.ps1` writes `licences\` with PS2Native's and
   PCSX2's (GPL-3.0); PCSX2's own list of its third-party licences
   (`ThirdPartyLicenses.html`); SDL's; LLVM's; `Qt-NOTICE.txt`, made from
   Qt's SBOMs in PCSX2's dependency package (each Qt library and plugin the
   folder carries, the code of others built into it, its licence and
   copyright), with each component's own licence file and every licence
   text named, from Qt's source of the same release (fetched into
   `deps\qt-<version>-licences` when not there, checked against the SHA-256
   in PCSX2's script for its dependency package); where Microsoft's terms
   for its C++ runtime are, as Visual Studio's own `Redist.txt` names them;
   and a `README.txt` saying which is which and where the source of each
   is, PS2Native's commit among them. The folder for others is made from a
   tree whose changes are committed and pushed, into a folder of its own
   (`kit.ps1 -Out`), without `games\` or `patches\`; `kit.ps1` says when
   the tree has changes not committed.
7. **A function of at most about 16,384 instructions.** Making Silent Hill
   3 found an executable that is one segment, its data with its code and
   none of its sections named, whose code ends near `0034C000`: nothing
   after it starts a function, and its last function took in a megabyte of
   data, 187,835 words, written as one C++ function that clang had not
   compiled after 29 minutes and 2.6 GB. The recompiler now cuts a function
   longer than 16,384 instructions into pieces, after an unconditional jump
   or a return and its delay slot within 1,024 instructions of the limit,
   or else at the limit, never in a delay slot; each piece is a function of
   its own, as a function whose end the analysis cannot see is (its
   boundaries are a matter of speed, never of correctness,
   `recompiler/src/analysis.h`). No executable of the games run before has
   a function that long: the analysis of each is the same as before, word
   for word.
