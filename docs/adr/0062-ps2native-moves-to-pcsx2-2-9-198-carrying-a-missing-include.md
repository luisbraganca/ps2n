# ADR 0062: PS2Native moves to PCSX2 v2.9.198, carrying a missing include

- Date: 2026-11-27
- Status: accepted (the maintainer's words, below).

## Context

At the maintainer's "Follow them in a branch"
([ADR 0061](0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md)),
PCSX2 v2.9.198, nine versions after the one PS2Native followed, was tried in
a worktree and branch of its own (docs/app-log.md, "PCSX2 v2.9.198 tried in
its worktree"):

- **Six of PCSX2's changes touch what PS2Native follows.** One changes how
  PCSX2 keeps the FPU's control registers, and so PS2Native's description of
  PCSX2's structures and the module version. One changes the flags of
  `RSQRT` in microVU. Three make PCSX2 do what PS2Native and the console
  already did (`LWR` and `CTC2` to `FBRST` in its recompiler, `CTC2` to the
  status flag in its interpreter). One is in its interpreter alone.
- **PCSX2's own code does not compile on Linux here** without a missing
  include, which a newer standard library than Ubuntu 22.04's brings in.
- **With PS2Native following them and a one-line patch for the include,**
  every test passed on both systems, and the four timed games' sessions ran
  again as themselves, by stock PCSX2 and by the compiled game, the compiled
  games' EE cycles the same on both versions to the cycle.

The session put the move to the maintainer: "Move PS2Native to PCSX2
v2.9.198, carrying the patch for the missing include until PCSX2's own code
has it?", recommending yes.

## Decision

The maintainer's answer: "3. Yes".

- **The submodule** `third_party/pcsx2` is at v2.9.198, on Windows and inside
  WSL.
- **The worktree's changes are in `main`:** PS2Native's description of
  PCSX2's structures (the FPU's one control register, `FPU_REVISION`) and its
  check, the module version 7, `RSQRT`'s flags, the self-test held to both of
  PCSX2's implementations where they now agree, and the lists of known
  differences with the cause of each change.
- **PS2Native's second change to PCSX2's code:**
  `patches/pcsx2/0002-fullscreenui-settings-includes-sstream.patch`, until
  PCSX2's own code includes `<sstream>` there.
- **Committed and pushed** at the maintainer's word, its date asked with it.

The session's choices, for the maintainer to decide otherwise:

- **Stock PCSX2's trees are built with the include too.** Those trees are
  built with none of PS2Native's changes (`build.ps1 -Pcsx2Patches off`), and
  without this one stock PCSX2 v2.9.198 does not build on Linux here, so
  nothing could be timed against it there. A change that only lets PCSX2's
  source compile here, and changes nothing a run does, says so on a line of
  its own in its header ("Stock PCSX2's tree is built with it too."), and
  `cmake/ps2n-pcsx2-patches.cmake` applies such a change to those trees as
  well. Every other change stays out of them.
- **No release for the move alone,** as for v2.9.189 (ADR 0054): the next
  release makes the runtime's profile of play again and is timed.
- **The maintainer's game folders keep their programs** until they say
  otherwise. Given these programs, each compiles its game again, as at every
  update; the module version being 7, modules compiled before would not be
  loaded.
- **The trial's worktrees are removed** once `main` has everything of them.
- **The places that say which PCSX2 PS2Native follows** say v2.9.198: the
  source comments that name the PCSX2 a file transcribes, where that file is
  unchanged in v2.9.198 or followed; the README; the website; `CLAUDE.md`.
  The speed figures measured against v2.9.189 stay as they were measured.

## Done the same day

- **In `main`,** on Windows and inside WSL (docs/app-log.md, "PCSX2 v2.9.198:
  moved"): the trial's changes, the second change to PCSX2's code, the
  submodule at v2.9.198, and what names the PCSX2 followed.
- **Built,** every tree configured again: the development tree, the tree a
  release is made from, and stock PCSX2's tree, the last with the include
  alone ("built as it is, but for the changes that only let it compile
  here"), on both systems.
- **Every test passes,** none skipped: 556 in the development trees on both
  systems; 554 on Windows and 553 inside WSL in the trees a release is made
  from, the same three comparisons disabled as before.
- **The trial's worktrees and branch removed,** on both systems, their changes
  saved first.
- **Not done:** no release.

## The maintainer's answers to the move as made

The session put two things to them, each with its recommendation:

1. Commit and push the move to v2.9.198. Recommended: yes.
2. Give their game folders these programs after the commit, on Windows and
   inside WSL, with `update-games.ps1` and `update-games.sh`, each game
   compiling again. Recommended: yes.

Their answers: "1. Yes", "2. Yes".

- **Committed and pushed.**
- **Their game folders** are given these programs, their PS2Native folders
  made again from the commit first, as docs/development.md says ("The
  maintainer's game folders brought up to date").
