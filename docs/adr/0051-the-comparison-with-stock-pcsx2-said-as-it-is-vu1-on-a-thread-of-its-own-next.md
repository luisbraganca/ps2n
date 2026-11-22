# ADR 0051: The comparison with stock PCSX2 is said as it is; VU1 on a thread of its own is next

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below).

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

## Consequences

- The goal of [ADR 0046](0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md),
  compiled games at no more than stock PCSX2's time as users get them, was
  met against stock PCSX2 with VU1 on the main thread and is open again
  against stock PCSX2 as it is by default.
- A speed claim against stock PCSX2, here or anywhere the project speaks,
  says which of the two it is against.
