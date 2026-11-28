# ADR 0065: The project's files cleaned up, Jak and Daxter's folders and Silent Hill 3 deleted

- Date: 2026-11-28
- Status: accepted (the maintainer's words, below). Done the same day; this
  record committed and pushed at their word.

## Context

The build tree, which git ignores, had grown to about 205 GB on Windows. Inside
WSL the clone took 59 GB, in a disk file of 64 GB. Most of it was what trials,
measurements and checks had left behind since Phase 3:

- the build trees of trials;
- every game run's files since 2026-11-10, 44 GB in the development tree;
- 34 measurement folders that no list names, among them seventeen copies of
  the PS2Native folder with games in it (`kit` to `kit14`, and three more);
- the files of releases 0.2 to 0.4 and of a rehearsal.

`CLAUDE.md` says that `build\` is never deleted or cleaned whole (ADR 0061). It
holds the maintainer's game folders, with their memory cards and sessions, the
profiles of play and the measurement folders.

On 2026-11-28 the maintainer wrote:

> Everything related to this project is taking a lot of space. Can you do some
> clean-up? Games, old tests that aren't used anymore... Also fyi, I finished
> Jak and Daxter 100% so, unless you still use my recordings for tests, you can
> delete it.

Told what had been deleted and kept, they wrote:

> Also I don't think silent hill 3 is used by me, if it's not useful for tests,
> feel free to delete it. If you only use it because it's a different file type,
> don't worry I'm preparing Primal game that will also be CHF file type

## How it was read

- **"Clean-up":** what nothing reads any more is deleted, folder by folder,
  never a tree whole, on Windows and inside WSL. Something stays if a script, a
  list in the build tree, a build tree's settings or a procedure of
  docs/development.md reads it.
- **"Games":** the copies of games in the build tree, not the maintainer's
  discs.
- **"Unless you still use my recordings":** two of their Jak and Daxter
  sessions are still used:
  - `20261119-110909` is the session Jak and Daxter is timed on against stock
    PCSX2, on Windows and inside WSL. A trial of a newer PCSX2 runs it again,
    and 0.5's downloads were tried with it.
  - `20261118-120316` is two of the training runs of the runtime's profile of
    play, on each system.

  So their Jak and Daxter folders were deleted, on Windows and inside WSL,
  after those two sessions were copied, byte for byte, to
  `build\measure\sessions\jak`. The lists that named them now name the copies.
  The disc image stays in `C:\ps2\jak-and-daxter`, since every one of those
  runs plays it. So do the measurement folders made from it.
- **Silent Hill 3, "if it's not useful for tests":** it was used twice, both
  times as the one disc image in another format, a `.chd`, among the
  maintainer's games:
  - each release checks that its downloads' runtime reads such a disc
    (`-Disc`, `--disc`);
  - its boot, 3,000 frames without a session, was one of the profile's
    training runs on each system.

  Their game folder of it was used by nothing. "CHF" is read as CHD. Primal's
  disc image (`C:\ps2\primal`, a `.chd`) was read by 0.5's runtime as Silent
  Hill 3's is, its boot executable found, so it takes over the first use now.
  So Silent Hill 3 was deleted whole, its disc image among it:
  - their game folder of it, and the copy of its memory cards;
  - the measurement folders made from it, on both systems;
  - its run among the lists of training runs;
  - `C:\ps2\silent-hill-3`.

## What the session chose within it, for the maintainer to decide otherwise

- **Kept:**
  - the build trees in use, on both systems: the development tree, the tree a
    release is made from, stock PCSX2's, and the one that counts for the
    profile;
  - the captures the trees are built with (`build\bios`, `build\vu`,
    `build\iop`, `build\game\aut`, `build\game\killzone`);
  - the profiles of play (`build\pgo`);
  - the measurement folders the lists name (`build\measure\kit15`, and
    `build/kit-measure` inside WSL);
  - their other game folders, with their sessions, memory cards and settings:
    seven, and Primal's, which they made that morning;
  - the old scripts and logs of `build\measure`, small, some named by
    docs/development.md.
- **Kept, though no script reads them:**
  - the copies of their memory cards that each update of their folders made,
    0.7 GB, Jak and Daxter's cards among them, as they were in its folder
    (`build\measure\update-games\saves`). Silent Hill 3's were deleted with
    that game;
  - 0.5's files, where its download made the game folder the next release
    brings up to date (`-Earlier`);
  - 0.1's own folder, kept to try updates on (docs/development.md).
- **Not touched, outside the build tree:**
  - their other discs and the folders extracted from them in `C:\ps2`, 43 GB
    with Primal's: the build reads Agent Under Fire's executables there;
  - the old project `ps2-uncompiler`, 1.9 GB, an archive;
  - PCSX2's dependency package in `deps`, which the build needs.
- **Deleted as an old test:** `SLES_505.39` in `build\kit\games`, an early
  check's folder that the updates of their folders had left out
  (docs/development.md said so).
- **Deleted from the measurement folder in use (`kit15`):** 37 folders no list
  names, among them older timings' twins and trials' copies. Most of their
  files were hard links to the measured folders. The twins a timing needs,
  `-release` and `-stock`, are made again by `time-games.ps1` at every timing.
- **Six training runs** for the profile of play from now on, Silent Hill 3's
  boot gone. Primal's boot, from a measurement folder made from its disc, may
  take that place once Primal plays.
- **Deleted inside WSL too:** LLVM's download, 1.9 GB. The toolchain unpacked
  from it stays.
- **The default lists of training runs** (`build\pgo\training.txt`, on both
  systems) now hold the runs 0.4 and 0.5 were trained on. They had named
  folders that are gone.
- **WSL's disk file compacted** with WSL's own command, `wsl --manage
  Ubuntu-22.04 --compact`, after `fstrim`, with nothing running in WSL. Space
  freed inside WSL comes back to Windows only so.

## Done the same day

docs/app-log.md, "The project's files cleaned up (ADR 0065)", has the detail.

- **Free space on C:** 258.2 GB before, 461.3 GB after the first answer, 470.1
  GB after Silent Hill 3. WSL's disk file went from 63.9 GB to 35.0 GB.
- **Inside WSL,** 59 GB used before, 32 GB after.
- **Every disc, folder and session the lists name** is there, on both systems.
- **WSL after its disk was compacted:** the clone at its commit, nothing
  differing from it; `git fsck` finds no fault; the toolchain and the four
  build trees are there.

## Consequences

- **Inside WSL, `build/PS2Native/games` has no game.** `update-games.sh` finds
  none and says so.
- **Jak and Daxter is still timed and trained on,** from the copies of its two
  sessions. A release's check of its downloads can use the timed one
  (`-Session`, `--session`).
- **A release's check that its runtime reads a `.chd`** uses Primal's disc
  image (`-Disc`, `--disc`).
- **Silent Hill 3 is still among the games tried,** as the records and the
  website say. It cannot be made or played again here without its disc image.
- **Old game runs' files are gone.** What they showed is in the logs. A run made
  again writes its files again.
- **A deleted tree is built again from nothing when it is wanted,** for
  example `-Module imports`'s `build\windows-clang-imports`.

## The maintainer's answer

Asked whether to commit and push this record, with the log and the documents
that named the deleted folders (recommended: yes), they answered "1. Yes".
