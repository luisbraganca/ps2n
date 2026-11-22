# ADR 0051: The comparison with stock PCSX2 is said as it is; VU1 on a thread of its own is next

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below). Its step was built and
  measured the same day and is put to the maintainer ("The step, built").

## Context

[ADR 0050](0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md)'s
change was built and measured, and with it what had been found while it was
written down ([docs/app-log.md](../app-log.md), "PS2Native's first change to
PCSX2's code"):

- **PCSX2 gives VU1, the console's second vector unit, a thread of its own by
  default** on a computer with three processor cores or more. Every run of
  `ps2n-host` has had that off, stock PCSX2's among them, so that runs
  repeat. "Stock PCSX2's time" in every log, on the website, in the README
  and in 0.2's notes was therefore the time of stock PCSX2 with VU1 on the
  main processor thread, and none of them said so.
- **With its VU thread on, stock PCSX2 takes 40% less time** on Jak and
  Daxter's session, 24% less on Killzone's, 12% less on Gran Turismo 3's and
  2% less on Agent Under Fire's. A compiled game as 0.2 has it takes 1.65,
  1.22, 1.13 and 1.02 times the time of stock PCSX2 so, where 0.98 to 1.00
  (Killzone 0.92) against stock PCSX2 with VU1 on the main thread. With ADR
  0050's change, 1.50, 1.22, 1.13 and 1.02.
- **PS2Native's VU provider cannot simply be put on that thread:** it follows
  PCSX2's VU recompiler as that behaves with the thread off.

## Decision

The session put six decisions to the maintainer, with its recommendation for
each. The maintainer's answers: "1. Yes", "2. Yes", "3. Yes", "4. Yes",
"5. Wait for that step", "6. Leave it as it is".

1. **ADR 0050's change, and what was built for it, is committed and
   pushed.**
2. **The public comparison is corrected.** The website, the README and 0.2's
   notes say that both sides of it ran with VU1 on the main processor thread,
   and give what stock PCSX2 takes with its VU thread on.
3. **The next speed step is VU1 carried out on a thread of its own,** as
   PCSX2 does by default, measured against stock PCSX2 with its VU thread on.
4. **The maintainer's seven game folders get the runtime with ADR 0050's
   change.**
5. **No release with ADR 0050's change alone:** the next release comes with
   the VU thread step.
6. **ADR 0050's record of its first answer stays as it is.** (The session had
   written down the answer's "Yes" and given the rest of it in its own words;
   it asked whether to quote it whole.)

## The correction (point 2)

Made the same day, the session's wording, for the maintainer to decide
otherwise:

- **The website.** The first chart of "Measured" has a third bar for each
  game, stock PCSX2 with its VU thread on ("PCSX2's default": 0.60, 0.88,
  0.98 and 0.76 of stock PCSX2's time with VU1 on the main thread); its
  title, its line of explanation and the axis say which stock PCSX2 the
  ratios are against; its note says that the page had not said so, gives the
  seconds, the compiled games' 1.65, 1.13, 1.02 and 1.22 against stock PCSX2
  so, that Agent Under Fire's runs with the thread on left their session, and
  that no run has a frame limiter. The section's opening says it for every
  number of the page. The roadmap's "No more than PCSX2's time, on Windows"
  says against which stock PCSX2, and its next step is "VU1 on a thread of
  its own". The Linux platform's note has both ratios.
- **The README.** Its status says the same in two sentences for Windows and
  one for Linux.
- **0.2's notes on GitHub.** Two passages added, each marked "Corrected on
  2026-11-22": to the paragraph on speed, and to the Linux limit. Nothing
  else of the release changed: its tag, its two downloads and the rest of
  its notes are as published. The session edited them at the maintainer's
  word (`gh release edit`), and kept the text as published and as corrected
  in the build tree.

## The step (point 3), as the session proposes to take it

For the maintainer to decide otherwise, before or as it goes:

- **Whose thread.** PCSX2's own VU thread, with PCSX2's own machinery for it:
  the queue the main thread fills with VU1's work, the places where the main
  thread waits for VU1, and the way VU1's graphics packets reach the GS. All
  of that is PCSX2's core and stays. What changes is PS2Native's: its VU
  provider and the runtime of its compiled micro code, which follow PCSX2's
  VU recompiler as that behaves with the thread on, as
  [ADR 0005](0005-never-further-from-the-console-than-pcsx2.md) has
  instruction semantics follow it.
- **When it is on.** As in PCSX2: by default where the computer has three
  processor cores or more, off where PCSX2's game database switches it off
  for a game, and the user's to switch in the wizard's settings, on PCSX2's
  own page for it (shown fixed off until now, ADR 0032).
- **What stays as it is.** Every comparison that holds VU1's registers to a
  stock run keeps VU1 on the main thread: every test, the self-tests, a
  session run again to be compared. Runs with the thread on do not all
  repeat (Agent Under Fire's did not on stock PCSX2), so nothing is held to
  them sync point by sync point.
- **What a run with the thread on is held to.** The maintainer's sessions run
  again with it are their sessions at every fingerprint wherever stock
  PCSX2's runs with its VU thread on are; and what else the step finds it
  can hold, reported with it.
- **Its criterion.** With the thread on, each of the four games' timed
  sessions takes no more than stock PCSX2's time with its VU thread on (ADR
  0046's goal, in the comparison a user of both would make), every test
  passing as before; where a game does not get there, the report says how
  far it is and where the time goes.
- **How it is reported.** Measured first on one game, then put to the
  maintainer with what it changed, before it goes into a folder of theirs or
  a release.

## The step, built (2026-11-22)

Built and measured the same day, as proposed above
([docs/app-log.md](../app-log.md), "VU1 on a thread of its own"). It is in
no folder of the maintainer's and in no release: it is put to them first.

- **PS2Native's VU provider carries VU1 out on PCSX2's own VU thread** where
  that thread is on, and does there what PCSX2's VU recompiler does
  differently there (a run carried out whenever it is asked for; VU0's status
  register and VU1's interrupt left to the main thread, which is told where
  a program ended; the thread's own copies of the VIF's TOP and ITOP and of
  FBRST; the EE's cycles left alone). The thread, its queue and the places
  where the main thread waits are PCSX2's, unchanged. Compiled micro code is
  the same either way; what a module and the host share changed with it, so
  a game's folder given these programs compiles its game again, as one
  brought up to date does.
- **Held to PCSX2's VU recompiler** by a sixth mode of the self-test for VU1
  (2,000 random programs carried out as the thread would, what a program's
  end tells the main thread compared with the rest), by nine tests more (the
  seven console-recorded VU programs and the picture a test program's VU1
  draws, with VU1 on the thread), and by the maintainer's sessions run again
  with it: Jak and Daxter's, Gran Turismo 3's and Killzone's are their
  sessions at every fingerprint, as stock PCSX2's runs with its VU thread on
  are; Agent Under Fire's part from the session at frame 1,900, as stock
  PCSX2's do.

What the session chose within the step, for the maintainer to decide
otherwise:

1. **A game that is played has the thread as PCSX2 would give it:** on where
   the computer has three processor cores or more, off where PCSX2's game
   database says so (67 of its entries, none of the five games tried), and the
   user's to switch in the wizard's settings step, on PCSX2's own page (the
   box "Enable Multithreaded VU1 (MTVU)", shown fixed off until now).
2. **Every run that is not played has VU1 on the main processor thread,**
   whatever a game's settings say, unless the run is told otherwise
   (`ps2n-host -pcsx2-vu-thread on`): every test, every comparison of two
   runs, a session run again, and the run on which a game's folder counts
   its profile ([ADR 0045](0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md)).
   Such a run is to repeat, and with the thread on a run need not.
3. **A session written down says when it was played with the thread on,**
   and a run of it again that parts from it says that it was.
4. **The runtime's profile of play is trained with the thread on** in the
   runs that are play, as a game is now played, and without it in one, which
   keeps the other way counted (a training run's sixth part, `vu-thread`).
5. **The thread's setting is decided once for a run,** when the game's disc
   is read and before anything runs, PCSX2's game database heard then, and
   stays so. PCSX2 by itself hears its database only once a game's
   executable has started, and not while the next is loaded, so that the
   thread would go off and on within a run; and
   [ADR 0050](0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md)'s
   change to PCSX2's code, made when PS2Native never used the thread, holds
   only as long as the thread is never switched on while a game runs. The
   change itself is as the maintainer decided it; the note at its top and
   the list of PS2Native's changes say what is true now.

What the step found, besides:

- **A game's run with the thread on need not be one run.** Agent Under
  Fire's is not: a session of it played with the thread on, run again three
  times, gave three different machines at frame 1,900, with the thread and
  without. For such a game a session played with the thread on cannot be run
  again as itself. A folder still counts its profile on a run of it again,
  which then leaves what was played at that point; and such a session is not
  one to compare runs on or to time. The other three games' sessions repeat
  with the thread on. A user, or the maintainer, who wants a session that
  can be run again exactly switches the thread off for that game.
- **A game's own settings file could undo what the host decides,** the core
  reading it over the host's settings: found by the step's checks and put
  right before anything was measured for a release (a run without a window
  has VU1 on the main thread whatever the file says).
- **Two of Linux's tests had never run:** the self-test's programs on blocks
  compiled ahead of time, for the VUs and for the IOP, skipped there for want
  of a capture that only Windows had a script for. Linux has the scripts
  now, and both pass there.

Measured, as a release would have it (the runtime built with link-time
optimization and its profile of play made again with the thread; each game's
folder with its own counts, taken with these programs), on the maintainer's
sessions run again, three rounds in turns:

| | Stock PCSX2 with its VU thread on | The compiled game, VU1 on PCSX2's VU thread | Times its time | The compiled game without the thread, times its time |
|---|---:|---:|---:|---:|
| Jak and Daxter | 33.68 s | 32.35 s | 0.96 | 1.50 |
| Gran Turismo 3 | 18.51 s | 18.76 s | 1.01 | 1.13 |
| Agent Under Fire | 26.31 s | 25.83 s | 0.98 | 1.02 |
| Killzone | 33.12 s | 31.39 s | 0.95 | 1.24 |

- **The criterion is met by three of the four games.** Gran Turismo 3 is
  0.25 s behind in 18.5 s: with VU1 off the main thread, what is left on it
  is the EE's and the IOP's code and the core's devices, where the compiled
  game takes a little more than PCSX2's recompilers (the IOP's code, the
  EE's accesses to devices, costs
  [ADR 0046](0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md)
  listed); PS2Native's faster micro code made up for it while VU1 shared
  that thread.
- **What a player sees and hears is the same:** the last picture and sound
  of every run of Gran Turismo 3 and of Killzone, and the sound of every run
  of Jak and Daxter, are the same with the thread and without, stock PCSX2
  and compiled; every such run is its session at every fingerprint. Agent
  Under Fire's runs with the thread differ from each other, on stock PCSX2
  as compiled.
- **A game's first session,** its code and micro code interpreted, takes 19%
  less time with the thread for Gran Turismo 3 and 32% less for Jak and
  Daxter.
- **A profile counted on a run that left its session** (Agent Under Fire's,
  the thread on in the run counted) was worth as much as one counted on the
  session itself, to within the rounds.
- **Under WSL 2,** Jak and Daxter with the thread takes 0.99 times the time
  of stock PCSX2 with its VU thread on (36.90 s against 37.31 s), with the
  profile of play as 0.2 had it there.
- **Every test passes:** 543 in the development tree on Windows and on
  Linux, 541 and 540 in the release's.

Not tried: a game in a window on Linux with the thread (its window would
have come up on the maintainer's screen), and any game but the five.

Put to the maintainer on 2026-11-22, each with the session's recommendation:
whether the step is committed and pushed; whether their seven game folders
get these programs, each compiling its game again; and whether the next
release, 0.3, is prepared with it at once or after they have played with it.

## Consequences

- The goal of [ADR 0046](0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md),
  compiled games at no more than stock PCSX2's time as users get them, was
  met against stock PCSX2 with VU1 on the main thread and is open again
  against stock PCSX2 as it is by default.
- A speed claim against stock PCSX2, here or anywhere the project speaks,
  says which of the two it is against.
