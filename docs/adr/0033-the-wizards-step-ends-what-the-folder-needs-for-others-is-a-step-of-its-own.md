# ADR 0033: The wizard's step ends; what the folder needs for others is a step of its own

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-18 |
| Decided by | Maintainer, on the session's recommendation |

## Context

Step P4.5, the wizard, has for its pass criterion "A game packaged from the
disc image and the BIOS with every default accepted, without a command
line" ([phase4-proposal.md](../phase4-proposal.md)). On 2026-11-18 the
maintainer made *Gran Turismo 3: A-Spec*, a game the project had never run,
with `PS2Native.exe`, without a command line, changing three settings and
accepting the rest; Agent Under Fire had been made with every default
accepted by the wizard's own check (`--auto`) the day before
([app-log.md](../app-log.md), "P4.5"). What the step's log still listed as
left of it: the licences of the libraries the folder carries, disc images
other than `.iso` files, PCSX2's archive of patches where PCSX2 is not
installed, a fixed setting edited by hand into a game's settings file,
and a choice of the executables compiled; none of them changes anything
for the maintainer, whose discs are `.iso` files and who has PCSX2
installed, and each matters once the folder goes to someone else.

Asked what to do next, the session recommended, in this order: count the
maintainer's run as P4.5's criterion; move the rest of P4.5's list into a
step of its own, done before the folder goes to anyone else, which changes
Phase 4's plan and so is the maintainer's to decide; then P4.6 (new code
in play), beginning with the game's code that a session finds outside its
executables compiled in parts, as an executable's is, since P4.6 would run
that compile after every session that meets such code, and Gran Turismo
3's took 146 of its 151 seconds in one file. The maintainer answered:
"Agree".

## Decision

As read:

1. **P4.5's criterion is met** by the maintainer's run of 2026-11-18; the
   step is done.
2. **What was left of P4.5 is a step of its own, P4.8, the folder for
   others**, done before the folder `kit.ps1` makes goes to anyone but the
   maintainer: the licences of everything the folder carries; disc images
   other than `.iso` files of 2048-byte sectors (`.bin` and `.cue`, `.chd`,
   `.cso`); PCSX2's archive of patches where PCSX2 is not installed,
   fetched at the user's word or given (ADR 0028); a fixed setting edited
   by hand into a game's settings file refused by the runtime; and a
   choice, in the wizard's first step, of the executables compiled. Its
   pass criterion, the session's proposal: on a computer without PCSX2, a
   game made with the wizard from a `.chd` image, its patches fetched at
   the user's word, and the folder carrying the licence of everything in
   it.
3. **P4.6 is next**, beginning with the game's code a session finds
   outside its executables compiled in parts.
