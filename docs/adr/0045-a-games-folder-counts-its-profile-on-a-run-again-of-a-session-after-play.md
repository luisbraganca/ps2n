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

## Measured, then built (2026-11-21)

Measured first ([docs/app-log.md](../app-log.md), "The counted profile on a
second game, and further into the first"): counted in clang's front end as
before, on 3 minutes of one of the maintainer's sessions and timed on
another, Gran Turismo 3 took 3.5% less time and Jak and Daxter 7.6%, and
7.2% on a session played two days after the one counted, further into the
game. Counted at the IR level (`-fprofile-generate`), the same 3 minutes of
Jak and Daxter gave 8.4% where the front end gave 6.7%, and were run again in
83 s where 187 s.

Then built into a game's folder (docs/app-log.md, "A game's folder compiles
its game for how it is played, built (2026-11-21)"). Through the folder's
own path on the development computer, after a session of the maintainer's
put into it: Jak and Daxter's round took 174 s and its game 8.0% less time;
Gran Turismo 3's took 95 s and its game 3.6% less; every run the session at
every fingerprint; a compile after it kept what was compiled with the counts.
On Linux under WSL 2 the same path ran through (Jak and Daxter, 336 s there,
not timed), every test passing.

The session's choices within the decision, for the maintainer to decide
otherwise:

- **The IR level.** A folder counts with clang's `-fprofile-generate` and
  compiles with `-fprofile-use`, not the front end's, for more speed and less
  time counting (above).
- **Which 3 minutes.** 10,800 frames (3 minutes at 60 a second); the session
  counted is the newest that lasted that long, its first 10,800 frames run
  again. (Changed on 2026-11-22 at the maintainer's word,
  [ADR 0047](0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md):
  3 minutes of the game's own time, 9,000 frames where the console shows the
  game at 50 frames a second, as a European game's. The runtime writes a
  session's frames a second down when it ends; a session written down before
  it did is taken for one at 60.)
- **How it is run again.** By the folder's own runtime with the folder's own
  options but a window's, on copies of what the session began with (memory
  cards, the console's NVRAM, the BIOS's images, the game's settings and
  patch file), at the console's resolution, on Vulkan where the folder names
  no renderer (a run that draws nothing is not safe for every game: Gran
  Turismo 3's), two hours at most.
- **What is kept.** The counts, with the folder's captures
  (`captures/profile.profdata`), which every later compile takes; code a
  later session meets, or changes, is compiled without counts.
- **A failure** is said in the window; the game plays as it was, and the
  folder does not try again (`work/count/failed.txt` says why).
- **What a folder carries for it.** LLVM's `llvm-profdata` and profile
  runtime (6 MB on Windows), and `runtime/module_counts.cpp`, which hands the
  host a module's counts through the profile runtime's own writer and gives
  that runtime, on Windows, what it asks of a C runtime, from PS2Native's own
  code: no file of Visual Studio's or of the Windows SDK's goes into a
  folder.

Asked, with what was built, whether to commit and push it, and whether to
put the new programs into the maintainer's own game folder of Jak and Daxter
(the session's recommendation) so that its next session of 3 minutes or more
has the round, the maintainer answered on 2026-11-21: "1. Commit and push";
"2. All game folders". Read as: committed and pushed; and the new programs
put into every game folder of the maintainer's on this computer, the
Windows ones and the one inside WSL, each then compiling its game for how it
is played after its next session of 3 minutes or more.
