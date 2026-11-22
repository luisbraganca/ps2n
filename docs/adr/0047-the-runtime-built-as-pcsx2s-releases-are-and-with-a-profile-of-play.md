# ADR 0047: The runtime built as PCSX2's releases are, and with a profile of play; stock PCSX2 timed as released; 0.2 next

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below).

## Context

ADR 0046's third answer had the runtime itself, `ps2n-host` (PS2Native's
runtime and PCSX2's core, 34% to 50% of a compiled run's processor thread),
built with a profile of play, measured first, against stock PCSX2 built
without one. The measurement ([docs/app-log.md](../app-log.md), "The runtime
itself: link-time optimization, and a profile of play") found:

- **PCSX2's own releases are built with link-time optimization,** which the
  build here had left out since Phase 2. Every "stock PCSX2" timed until then
  was PCSX2's core built without it. Built as its releases are, stock PCSX2
  takes 1.5 to 4.0% less time on the maintainer's sessions.
- **Against stock PCSX2 built so, compiled games as users had them took 1.07
  to 1.11 times its time,** where the numbers given the day before, and shown
  on the website, said 1.04 to 1.08.
- **With the runtime built with link-time optimization and a profile of
  play,** compiled games take 0.99 (Jak and Daxter), 1.00 (Gran Turismo 3),
  0.99 (Agent Under Fire) and 1.00 (Killzone) times that stock PCSX2's time.
  The profile is the counts of what the runtime ran in seven runs of the
  maintainer's sessions, none of them a session timed, without the functions
  no run ran (`llvm-profdata merge -sparse`), which the compiler then takes as
  it does without a profile.
- **A game the profile never saw gets nearly all of it:** trained without a
  game's runs, the runtime gave that game within 1% of the same time.
- **A game's first session,** where its code is interpreted, takes 11.1% less
  time (Gran Turismo 3's: 68.7 s where 77.2 s).
- **What it costs:** about 8 minutes for a release on each system, on the
  computer that has the games (the runtime built to count, the training runs,
  the runtime built again); the profile stays out of the repository, as
  everything that comes from playing a game does; the runtime is 4.3 MB
  larger.
- **A defect of PCSX2's showed with it:** PCSX2's interpreted masked VIF
  unpack on x86 reads its mask tables 128 bytes low
  (`VifUnpackSSE_Simple::doMaskWrite`, `pcsx2/x86/Vif_UnpackSSE.cpp`). What it
  then writes depends on what the linker put before those tables: built with
  link-time optimization, two runs of the console-recorded program
  `dma/vif/stcycl` differ from each other, under stock PCSX2 too. Its three
  comparisons of two runs are not run with such a runtime, by name and with
  that cause.
- **Killzone's folder never counts its profile** (ADR 0045): a session counts
  from 10,800 frames, 3 minutes at 60 frames a second, and the game runs at
  50, where its sessions of 3.1 minutes are 9,300 frames.

## Decision

The session put seven decisions to the maintainer with these numbers. The
maintainer's answers: "1. Yes", "2. Yes", "3. Yes", "4. Yes, write the text,
I'll change it", "5. Yes", "6. Yes", "7. Ok do it".

1. **Stock PCSX2 is timed built as its releases are,** with link-time
   optimization, from now on, and the website's chart is corrected to it. It
   is timed without the profile of play: that profile is PS2Native's, and
   PCSX2's releases have none. Numbers measured against stock PCSX2 built
   without link-time optimization stay in the logs as they were measured,
   with their dates.
2. **A release's runtime is built with link-time optimization and the profile
   of play,** on each system from training runs made on that system. The
   profile is made again for a release from the tree released, and stays in
   the build tree.
3. **The maintainer's game folders on this computer get that runtime now.**
   Only the runtime changes; nothing is compiled again.
4. **The maintainer tells PCSX2 of the defect themselves.** Asked to write the
   text for them to change, the session wrote none: what is posted to PCSX2 is
   the maintainer's to write, not a session's
   ([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)), and
   PCSX2's team asked that no LLM-generated issues be sent. It gave where the
   defect is, when it shows and how it is seen, and that an AI session found
   it. Meanwhile PS2Native changes nothing of PCSX2 for it; the three
   comparisons stay off for a runtime built with link-time optimization, and
   run where it is built without.
5. **What was built and measured is committed and pushed.**
6. **A folder counts its profile after a session of 3 minutes of the game's
   own time:** 9,000 frames of a game that runs at 50 frames a second, 10,800
   at 60. It changes ADR 0045's "10,800 frames"; the rest of ADR 0045 stands.
7. **0.2 is next, with a way to bring the game folders of an earlier version
   up to date.** The alternative put beside it, more speed first, to get
   clearly under PCSX2's time, was not chosen. How folders are brought up to
   date is a record of its own when it is built.

The session's choices within these, for the maintainer to decide otherwise:

- **Which trees are built so.** The tree the folder and the releases are made
  from (`build\windows-clang-module`, `build/linux-clang-module`) has the
  runtime as releases have it. The development tree with the compiled code in
  the host stays as before, and runs the three comparisons of
  `dma/vif/stcycl`. Stock PCSX2 is timed on a runtime of a tree of its own,
  built with link-time optimization and no profile.
- **What the profile is trained on:** the maintainer's own sessions and games,
  as in the measurement (play from folders with their counts, a boot, and
  first sessions from folders made from the disc alone, where the game's code
  is interpreted), a session that is timed never among them.
- **0.1's download is not made again.** The next release is the first with
  such a runtime.
