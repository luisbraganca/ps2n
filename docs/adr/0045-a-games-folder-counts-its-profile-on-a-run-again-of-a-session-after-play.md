# ADR 0045: A game's folder counts its profile on a run again of a session, after play

- Date: 2026-11-21
- Status: accepted (the maintainer's words, below).

## Context

[ADR 0044](0044-a-games-folder-learns-from-its-users-own-play.md) had a
game's folder learn from its user's own play: first the order of its
functions, then a profile sampled while the game plays, each measured
before it goes into a folder. Both were measured on Jak and Daxter on
2026-11-21 ([docs/app-log.md](../app-log.md), "The order of a folder's
functions, built (2026-11-21)" and "A profile sampled while the game plays,
tried (2026-11-21)"):

- **The order**, in every game's folder since: about 0.4%.
- **The profile sampled while the game plays**: 0.2%, within the noise.
  Clang could place about a quarter of its records, and Windows' line
  tables have no discriminators to place the rest by. It goes into no
  folder.
- **The profile counted** (clang's `-fprofile-instr-generate`, then
  `-fprofile-instr-use`), from the first 9,000 frames of another of the
  maintainer's sessions: 7.5%, Jak and Daxter at 1.11 times stock PCSX2's
  time where 1.19. The counting made that run take 2.7 times as long, which
  no one could play. But a game's folder writes every session down, and a
  run again of one is the session at every fingerprint: the counting can be
  done after play, out of sight.

What it costs, on the development computer, for Jak and Daxter (the folders
made with `make-game.ps1`): the modules compiled to count, 98 s; the run
again, 147 s for 9,000 frames, two and a half minutes of play; the modules
compiled with the counts, 63 s; longer on a slower computer. A folder
carries LLVM's `llvm-profdata` and its profile runtime besides (5.9 MB and
0.15 MB on Windows).

The session put it to the maintainer as: "Should a game's folder get its
profile by replaying part of a session after you play, instead of sampling
during play? ... I recommend yes, run once per game after a session of at
least 3 minutes. Unlike new code, it would never stop you playing: Later
just postpones it to the next session's end. Before it goes into folders,
I'd measure it on a second game, since only Jak has been tested."

## Decision

The maintainer's answer, on 2026-11-21: "1. Yes" (with "2. Commit and
push", to committing what had been done since ADR 0044). Read as the
recommendation accepted, whole:

1. A game's folder takes its profile by counting, not by sampling: after a
   session of at least 3 minutes, the folder's compile after the session
   compiles the game's modules to count, runs the session's first 3 minutes
   again with them, without a window, and compiles the modules again with
   the counts, their functions in the order of ADR 0044 as before.
2. Once per game. Unlike new code (ADR 0040), it never keeps a game from
   starting: Later, or closing the window, puts it off to the end of the
   next session, and until then the game plays on its modules without a
   profile.
3. Before it goes into a game's folder, it is built and measured on a
   second game as well as on Jak and Daxter.

It takes the place of ADR 0044's second part: the profile sampled while the
game plays goes into no folder, and stays a measurement (`ps2n-host
-sample-profile`). ADR 0044's rules stand: measured on the maintainer's
sessions in the configuration users get; time counted as before (ADR 0011),
every session run again still the session at every fingerprint; what a
folder learns from its user's play stays in that folder.

The session's choice, for the maintainer to decide otherwise: the second
game is Gran Turismo 3, which has two of the maintainer's sessions, one to
count and one to time.
