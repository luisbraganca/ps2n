# ADR 0049: 0.2 is released; a folder brought up to date counts once more; Linux's speed after it

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below).

## Context

What ADR 0047's answers asked for was built and measured the same day
([docs/app-log.md](../app-log.md), "As the release has it" and "0.2: a game's
folder brought up to date"):

- **Compiled games as 0.2 has them, on Windows,** take 0.98 (Jak and Daxter),
  1.00 (Gran Turismo 3), 0.99 (Agent Under Fire) and 0.92 (Killzone, whose
  counts fit its timed session better than a user's would; 1.00 without
  counts) times the time of stock PCSX2 built as its releases are.
- **The wizard brings a game's folder made with an earlier PS2Native up to
  date**
  ([ADR 0048](0048-a-games-folder-of-an-earlier-version-is-brought-up-to-date-by-the-wizard.md)),
  tried on a folder made by 0.1 as released, on Windows and on Linux.
- **On Linux,** timed for the first time, under WSL 2, Jak and Daxter takes
  1.05 times stock PCSX2's time (1.23 as 0.1 had it). Linux is not at PCSX2's
  time, and why is not known.
- **The maintainer's own game folders** have 0.2's runtime (ADR 0047) and, for
  the rest, the programs of 2026-11-21: they still count a session from
  10,800 frames whatever the game's frame rate, so Killzone's does not count.
- **One point was open:** what becomes of a folder's counts when the folder
  is brought up to date (ADR 0046's point 6), which ADR 0048 put with its
  measurement.

## Decision

The session put five decisions to the maintainer, with its recommendation for
each. The maintainer's answers: "1. Yes", "2. Yes, prepare the release",
"3. Yes", "4. Do all seven", "5. After".

1. **Committed and pushed.**
2. **0.2 is released from that commit,** as
   [ADR 0042](0042-releases-on-github-beginning-with-0-1.md) has a release
   made: both downloads from the committed and pushed tree, the runtime built
   with link-time optimization and the profile of play (ADR 0047), and the
   release created on GitHub as a draft with its notes, which the maintainer
   reads and publishes.
3. **A folder brought up to date with another toolchain counts its profile
   once more after a session,** as it counted the first time, and is compiled
   with the counts it has until then (ADR 0048, where it was put: Jak and
   Daxter kept about half of what its counts give across a change of the
   recompiler, Gran Turismo 3 and Agent Under Fire all of it).
4. **The session brings the maintainer's own game folders up to date,** all
   seven: the six on Windows and the one inside WSL. (The session had
   recommended that the maintainer bring the first up to date with the
   wizard's new button, as its first use outside a check.)
5. **Why a compiled game takes more than stock PCSX2's time on Linux is looked
   for after 0.2 is out.** The release's notes say where Linux stands.

## The session's choices, for the maintainer to decide otherwise

- **0.2 is a pre-release,** as 0.1 is: tried with the same five games, on
  Windows 11 and, on Linux, under WSL 2 only.
- **The profile of play is the one made on 2026-11-22** on each system by the
  repository's scripts ([docs/app-log.md](../app-log.md), "The runtime's
  profile made again"): nothing of the runtime's source changed after it was
  made, so it is the released tree's.
- **The maintainer's folders get the programs of the release itself,** the
  wizard's own way (`ps2n-wizard --update`), from the folder made from the
  released commit. The session then has each folder compile its game (its own
  `ps2n-remake --before-play`, out of sight) and runs the start of one of its
  sessions again, so that each game starts at once and is known to play. What
  each counts waits for a session of the maintainer's, as it does for any
  user.
- **The website's downloads stay 0.1's until 0.2 is published:** a draft's
  files are not public.
