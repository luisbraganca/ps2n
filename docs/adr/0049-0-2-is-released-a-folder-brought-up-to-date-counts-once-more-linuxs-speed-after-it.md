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

## Published

With both downloads made and tried and the draft on GitHub, the session asked
two things more: whether the maintainer would publish the draft themselves,
as they had 0.1, or have the session publish it; and whether the website's
move to 0.2 should then be committed and pushed, with the record of the
release. The maintainer's answers: "1. Yes, publish", "2. Yes do it". The
session read the first as its word to publish, and published the draft the
same day: 0.2 is public since 2026-11-22, the tag `v0.2` on the commit the
downloads were made from. The website's downloads moved to 0.2 after it.

## Linux's speed, looked for (point 5)

Looked for the same day, once 0.2 was out
([docs/app-log.md](../app-log.md), "Linux's speed"):

- **Most of what a run takes more inside WSL 2 than on Windows is not
  PS2Native's and not the compiled game's alone:** PCSX2's core wakes its VU
  thread at every STROW and STCOL command of VIF1, whether that thread is in
  use or not, and waking a thread costs six times as much inside WSL 2 as on
  Windows. Jak and Daxter's session does it about 2.4 million times: 28% of
  the run under WSL 2, for stock PCSX2 and for the compiled game alike. A
  trial without those wakes, on a copy of PCSX2's source, took 54.1 s where
  75.4 s (stock PCSX2) and 56.7 s where 79.5 s (the compiled game). On Windows
  the same trial took 9% off Jak and Daxter's run, stock and compiled, and
  nothing that shows off the other three games', which send few of those
  commands. Nothing of it is in: a change to PCSX2's code is the maintainer's
  to order
  ([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)), and it
  is put to them with these numbers. (Answered the same day:
  [ADR 0050](0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md).)
- **Of the compiled game's own distance to stock PCSX2 there, 1.05 times its
  time, two parts were found and put right:** the 128-bit instructions' lanes,
  which Linux's calling convention kept the compiler from making host SIMD of
  (`ops_mmi.h`), and, in the measurement, micro code the Linux folder had not
  compiled. With both, 1.03.
- **The rest, 3%, was not found,** and is left for now (ADR 0050). It is in
  the game's compiled code and the IOP's, the same instructions as on Windows
  for the same work. Tried and taken out, for no gain: the runtime's
  functions given Windows's calling convention on Linux, and the code on
  large pages.
- **The runtime's profile works on Linux** (`ps2n-host -profile`), which is
  how the above was seen.

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
