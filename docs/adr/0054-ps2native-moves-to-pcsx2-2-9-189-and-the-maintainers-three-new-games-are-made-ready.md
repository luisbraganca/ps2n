# ADR 0054: PS2Native moves to PCSX2 2.9.189, and the maintainer's three new games are made ready

- Date: 2026-11-23
- Status: accepted (the maintainer's answers, below).

## Context

[ADR 0053](0053-ps2native-tried-on-pcsx2s-newest-version-2-9-189.md)'s check
was done the night it was asked for ([docs/app-log.md](../app-log.md), "PCSX2
2.9.189 tried"). On PCSX2 v2.9.189, with ten changes of PS2Native's, every test
passes as on v2.9.170 on both systems, a game's folder is made and brought up
to date as before, and the maintainer's sessions run again on it are the
sessions at every fingerprint. The session then put three things to the
maintainer, each with its recommendation: whether PS2Native moves to v2.9.189
(yes), whether that is committed and pushed (yes), and, next, the three games
of theirs PS2Native has never run (Resident Evil – Code: Veronica X, Secret
Agent Clank, SSX Tricky; yes).

## Decision

The maintainer's answers: "1. Yes", "2. Yes", "3. I will, after. Make sure
they're all ready".

1. **PS2Native moves to PCSX2 v2.9.189** (`5977c525`). The submodule moves;
   ADR 0053's ten changes come into the main tree (the count register's
   addition 32 bits wide, so the carry
   [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)
   mirrored is gone; `nextBlockCycles` 32 bits; the IOP jump pair PS2Native
   refused carried out, as the corrected recompiler carries it out); PCSX2's
   Windows dependency package of 2026-11-21 replaces the earlier one in
   `deps`, which is kept beside it; and the places that name the version
   PS2Native follows say v2.9.189, the logs and decisions keeping v2.9.170
   where it was so. The same inside WSL.
2. **Committed and pushed.**
3. **The three games are made ready for the maintainer, who tries them
   after.** How it was read: each is made with the wizard, with every
   default, into the maintainer's own PS2Native folder (`build\kit`, made
   again from the committed tree, its games kept), as the maintainer would
   make it; each is checked on a copy of its folder, so that the maintainer's
   folders are as the wizard made them; what breaks is put right where it
   serves every game, and put to the maintainer where it is not a small
   thing.

The session's choices, for the maintainer to decide otherwise:

- **No release for the move alone.** It reaches users with the next release,
  which makes the runtime's profile of play again on each system and is
  timed, as 0.3 was. Until then the release trees keep 0.3's profile (the
  compiler sets aside what it has of 23 functions of PCSX2's renderers whose
  code changed).
- **The maintainer's seven existing game folders keep their programs:** that
  was not asked. The wizard of their PS2Native folder, once made again, lists
  them under "Games made earlier...", to be brought up to date at their word.
- **The worktree and branch of ADR 0053 are removed** once the main tree has
  everything of them.
- **Linux is moved and tested too,** inside WSL, as every change is.

## Done the same morning

[docs/app-log.md](../app-log.md), "Moved", "The tree a release is made from,
built again whole" and "The maintainer's three new games made ready", has
every number.

- Moved on both systems, every test passing (543 and 541 on Windows, 543 and
  540 on Linux, none skipped), committed and pushed; ADR 0053's worktrees and
  branch removed.
- Found on the way: after the dependency package was swapped under the
  existing trees, their builds mixed objects compiled against both packages,
  and the wizard so built crashed on every new game. The trees were built
  again from nothing; the wizard then ran clean. The maintainer's PS2Native
  folder was made again from that tree.
- The three games made by that wizard into the maintainer's folder, with every
  default: Resident Evil – Code: Veronica X, Secret Agent Clank, SSX Tricky.
  On copies of their folders, each played as its launcher plays it, compiled
  what its first session met, and played again on compiled code; each went
  from its first screens into its menus (two of them into a new game and a
  race); and against stock PCSX2 over their first 1,800 frames, two are the
  stock run at every sync point, and Secret Agent Clank at the same place at
  the same time at every one, VU0's FBRST aside, where PS2Native follows the
  console on purpose, as for Agent Under Fire.

## Consequences

- PS2Native no longer reproduces a defect of PCSX2's: the count register's
  carry into EntryHi is gone from both, and `tests/programs/countcarry.s`
  holds both to the 32-bit addition.
- A game whose IOP code has a jump through a register with an unaligned load
  or store or a GTE instruction in its delay slot, which PCSX2 v2.9.170 sent
  to address 0 and PS2Native refused, runs on both now.
- A game's folder made earlier compiles itself again when it is brought up to
  date, as after every release.
