# ADR 0061: Maintenance tools: a newer PCSX2 tried by one command, the release's scripts in the repository, CLAUDE.md cut to what a session needs

- Date: 2026-11-27
- Status: accepted (the maintainer's choices, below).

## Context

The menu over the game was finished the day before
([ADR 0060](0060-a-menu-over-the-game-opened-with-esc-quit-controls-and-the-graphics-card.md)).
The maintainer asked: "What should we work on next?"

The session looked at where things stood before answering:

- **The menu** is in `main` and in the maintainer's game folders, and in no
  release.
- **Every update recompiles every game** and has a folder with counts count
  once more, whether or not anything a game's code is made from changed: the
  wizard tells one toolchain from another by its files' sizes and times, and
  each link of the runtime writes the host's import library again.
- **Games:** all eight in `C:\ps2` are made. Two have no session of the
  maintainer's yet.
- **Two players:** only the controller in port 1 is connected.
- **PCSX2** was at v2.9.198; PS2Native follows v2.9.189.
- **The release procedure** lived outside the repository: 109 scripts in
  `build\measure`, which git ignores, more in a session's scratch folder, and
  the order and the traps in a session's private notes.
- **`CLAUDE.md`** had 24,048 words, read whole by every session: 16,757 of
  history and decisions, 6,660 of build and test instructions, 600 of
  everything else. The brief's section 11 asks for concise documentation for
  future sessions.

It put four options, recommending the first:

1. **Release 0.5,** the menu, after one fix: an update recompiling a game only
   when it must.
2. **More games,** and the compatibility table the brief asks for.
3. **A second controller.**
4. **Maintenance tools:** "The brief's list for this phase; nothing a player
   sees. One command that tries PCSX2's newest version and reports (PCSX2 is at
   v2.9.198, we follow v2.9.189). The release and folder-update scripts moved
   into the repository (they sit in a git-ignored folder). CLAUDE.md, 24,000
   words read at every session's start, cut to what a session needs."

## Decision

The maintainer chose the fourth: "Maintenance tools".

Reading what PCSX2 had changed since v2.9.189 (18 commits, 34 files), the
session found six changes in code PS2Native mirrors, and asked how far the
trial of v2.9.198 should go: follow them in a branch, as with v2.9.189
([ADR 0053](0053-ps2native-tried-on-pcsx2s-newest-version-2-9-189.md)), or
report only. The maintainer chose "Follow them in a branch", the session's
recommendation.

So:

- **A newer PCSX2 is tried by one command,** which writes a report. It never
  moves anything: whether PS2Native moves is the maintainer's to decide after
  the results, as before.
- **A release, an update of the maintainer's game folders and a timing against
  stock PCSX2** are each a script in the repository, described in the
  repository.
- **`CLAUDE.md` is cut to what a session needs,** by moving, not deleting.
- **v2.9.198 is tried in a worktree and branch of its own,** PS2Native
  following PCSX2's changes there. Nothing changes in `main` and nothing is
  committed there. The results go to the maintainer.

The plan was put to the maintainer the same day and approved as it stood.

## The session's choices, for the maintainer to decide otherwise

**The scripts**

- **Their names:** in `scripts\windows`, `common.ps1` (what the others share),
  `hidden.ps1`, `wsl.ps1`, `update-games.ps1`, `time-games.ps1`, `release.ps1`,
  `try-pcsx2.ps1`; in `scripts/linux`, `common.sh`, `sync.sh`,
  `update-games.sh`, `release.sh`, `try-pcsx2.sh`.
- **What is this computer's or a game's stays out of the repository.** Disc
  images, sessions and folders come from parameters and from lists in the
  build tree, as `build\pgo\training.txt` did already.
- **Put right while they were rewritten:**
  - a release stops where the tree has changes not committed, is not pushed or
    does not say the version; a rehearsal goes through it on any tree, makes no
    draft and never touches the maintainer's PS2Native folder;
  - both systems make a release in one order, with a first PS2Native folder of
    the tree's own for the training folders, where Windows had borrowed the
    maintainer's;
  - Linux's test step goes through `test.sh`, and so catches a failure of the
    tools' unit tests;
  - a Linux batch waits for a game played on Windows;
  - the clone inside WSL is never overwritten where it has changes of its own;
  - a folder's session to check with is chosen by a rule, not by a table of the
    maintainer's games;
  - a tree's tests are counted from what ctest said of the tree itself, skipped
    and disabled apart. The tools' two unit tests are not added to them.
- **Publishing** a release stays a command typed at the maintainer's word. No
  script has it.

**The trial of a newer PCSX2**

- **In a worktree and branch of its own,** as ADR 0053 did it, with build
  trees of its own: a tree built before PCSX2's dependency package changed
  mixed objects and crashed the wizard last time.
- **A watch list,** `scripts/pcsx2-watch.txt`: PCSX2's files that PS2Native
  follows or compiles, each with what follows it.
- **It stops where PCSX2's dependency scripts changed,** until the package is
  fetched at the maintainer's word.
- **It says so** when PS2Native's description of PCSX2's structures changed in
  the worktree and the module version did not.

**The cut of `CLAUDE.md`**

- **Three new documents** take what leaves it, sentence for sentence, only
  links adjusted: `docs/history.md` (how the project got here),
  `docs/adr/README.md` (the index of the decisions) and `docs/development.md`
  (how things are done).
- **The rules stay.** An independent read of the file found 40 rules that bind
  a session whatever its task, 23 of them only inside the history, a decision's
  digest or a how-to. All 40 are in the short file.
- **One rule is new, and the maintainer's to strike:** `build\` is never
  deleted or cleaned whole. It holds their game folders, with their memory
  cards and sessions, the profiles of play and the measurement folders, and no
  rule said so.

**Not in this step:** a release (0.5 waits for the maintainer's word); the
wizard telling a toolchain by its contents; a compatibility table; a check of
PCSX2 run on a schedule; builds on GitHub's machines.

## Done the same day

- **The scripts** were each tried, on copies and in rehearsals, none of the
  maintainer's own folders touched (docs/app-log.md, "Maintenance tools (ADR
  0061)", "Each script tried"):
  - the folder update, on copies of three Windows folders and two inside WSL,
    one of them a copy of the maintainer's own Linux folder: each brought up to
    date and compiled, the start of a session run again as itself, the wizard
    asked again changing nothing, the memory cards the bytes they were;
  - a release, rehearsed whole on both systems but for the draft: every test
    passing in the trees the downloads are made from (554 on Windows, 553 and
    556 on Linux, none skipped), both downloads made and unpacked, each wizard
    making Jak and Daxter, and a session of the maintainer's run again whole
    from that folder as itself, the same machine on both systems to the cycle;
  - a timing, one round of one game on each system, every run the session (one
    round is not a timing: its numbers are quoted nowhere);
  - the check of PCSX2 v2.9.198, and the trial made whole on v2.9.189, the
    version followed, as a control: built, every test passing as in the main
    tree, and four games' sessions run again as themselves from the worktree's
    own PS2Native folder, by stock PCSX2 and by the compiled game.
- **Found by those runs, and put right:** inside WSL the copy that brings a
  worktree's changes over (`sync.sh`) deleted the worktree's `.git` file and
  its link to the clone's `deps`, which it set aside only as folders; and the
  console's test programs are cloned into a trial worktree from the clone's own
  copy. Only a trial worktree was harmed, and it was made again. The patterns
  that count tests had not allowed for the labels ctest writes after a test's
  status on Linux; no count was wrong, none of the day's logs having a skipped
  test.
- **The rehearsals left** the runtime's profile of play made again on both
  systems from this tree, the trees a release is made from built with it, and
  the training folders brought up to date. The maintainer's PS2Native folders
  keep their programs.
- **`CLAUDE.md`** went from 23,888 words to 2,627. Of its 144 items, paragraphs
  and bullets one by one, every one is in the four files but the website's
  note, whose pointer was changed on purpose; the 40 rules and four more are in
  the short file; every relative link leads somewhere.- **PCSX2 v2.9.198, tried in its worktree** (`..\ps2n-pcsx2-198`, the branch
  `pcsx2-2.9.198`, and `~/ps2n-pcsx2-198` inside WSL; nothing in `main`
  changed, nothing committed; docs/app-log.md, "PCSX2 v2.9.198 tried in its
  worktree"):
  - PS2Native follows PCSX2's six changes to what it follows. One changes
    PS2Native's description of PCSX2's structures (the FPU keeps one control
    register), so the module version is 7. Three make PCSX2 agree with what
    PS2Native already did, as the console does (`LWR`, `CTC2` to `FBRST` in
    the recompiler; `CTC2` to the status flag in the interpreter), and the
    self-test now holds PS2Native to both of PCSX2's implementations there.
  - PCSX2's `FullscreenUI_Settings.cpp` does not compile on Linux here without
    `#include <sstream>`: a one-line patch in the worktree's `patches/pcsx2`,
    PS2Native's second change to PCSX2's code if the maintainer decides it is
    carried.
  - Every test passes on both systems, none skipped (556, and 554 and 553 in
    the trees a release is made from), the lists of known differences
    corrected with the cause of each change: `cpu/ee/lsu` and
    `cpu/vu0_macro/transfer` off, `cpu/ee_fpu/transfer` off the interpreter's,
    and `vu/lower/integer` on it, where v2.9.198's correction of `CTC2`
    uncovers the defect of the maintainer's issue 15185.
  - The four timed games' sessions run again whole on v2.9.189 and v2.9.198,
    by stock PCSX2 and by the compiled game, are each the session at every
    fingerprint, the compiled games' EE cycles the same on both versions to
    the cycle; inside WSL, Jak and Daxter's likewise.
  - Whether PS2Native moves to it is put to the maintainer.
## The maintainer's answers

The session put four things to them, each with its recommendation, yes to
all four:

1. Commit and push this step, with the records of 2026-11-26 not yet
   committed.
2. Keep the one new rule: `build\` is never deleted or cleaned whole.
3. Move PS2Native to PCSX2 v2.9.198, carrying the patch for the missing
   include until PCSX2's own code has it.
4. Remove the worktree of Phase 3, `..\ps2n-vu0`, and its local branch `perf`,
   which held nothing `main` lacked.

Their answers: "1. Yes", "2. Yes", "3. Yes", "4. Yes".

- **Committed and pushed.**
- **The rule stays** in `CLAUDE.md`.
- **The move** is a step of its own, made in `main` from the worktree's
  changes and recorded with it; its commit waits for the maintainer's word.
- **The worktree of Phase 3 and its branch** are removed.