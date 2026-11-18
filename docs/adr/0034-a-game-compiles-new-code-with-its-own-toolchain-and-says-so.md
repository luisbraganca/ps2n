# ADR 0034: A game compiles new code with its own toolchain, and says so

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-18 |
| Decided by | Maintainer; point 2 on the session's recommendation, point 1 against it |

## Context

Step P4.6, new code in play, has for its pass criterion "A session that
meets new code finds it compiled at its next start". ADR 0028 chose how:
"The game compiles it itself: what a session had to interpret is written
down, as today, and the game recompiles its module in the background with
the toolchain the wizard installed, using it from its next start, and
says so" ([phase4-proposal.md](../phase4-proposal.md), Decision 3).

The session laid out how it would do that within ADR 0028: when a session
that met new code ends, the game's launcher starts the compile in the
background, at low priority, nothing being compiled while the game runs;
the wizard's own recompile does it, without its window; only what changed
is compiled again; and the new code is used from the next start, a game
started before the compile has ended playing as it was. It put two
questions to the maintainer, the two choices that show:

1. "When a session meets new code, the game compiles it after the window
   closes. But the compiler (about 250 MB) is in PS2Native's folder, not
   in each game's folder. Where should a game's compile come from?" The
   recommendation, "PS2Native's folder (Recommended)": the game's folder
   remembers where PS2Native's folder is and uses its compiler; if that
   folder is moved or deleted, the game still plays, and its log says
   nothing could be compiled. The other: "A copy in each game": "Every
   game folder carries its own compiler: about 250 MB more per game, but
   it compiles wherever the game is moved." Chosen: "A copy in each game".
2. "How should a game tell you that it is compiling, after you close its
   window, what your session met for the first time?" Chosen: "A Windows
   notification (Recommended)": "An icon in the notification area while it
   compiles (about half a minute for Gran Turismo 3 here), then a
   notification that the next start will have it." The other was "Only in
   the game's log".

## Decision

As read:

1. **A game's folder carries its own toolchain**: a copy of PS2Native's
   folder's `toolchain` (LLVM's `clang-cl` and `lld-link`, PS2Native's
   recompiler, headers and the source a module is linked with), made when
   the game is made, about 250 MB more per game; and with it what runs the
   compile, the wizard's recompile without its window, and the libraries
   it needs. A game so compiles wherever its folder is, with the toolchain
   of the runtime it was made with, whatever becomes of PS2Native's folder.
2. **It says so with Windows' notification area**: an icon while it
   compiles, and a notification when the next start will have the new
   code. A session that met nothing new starts no compile and shows
   nothing.
3. **The rest is as the session laid it out** (above), for the maintainer
   to decide otherwise.
