# ADR 0048: A game's folder made with an earlier PS2Native is brought up to date by the wizard

- Date: 2026-11-22
- Status: accepted in its outline (the maintainer's "7. Ok do it",
  [ADR 0047](0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md));
  how it is done is the session's choice, listed below for the maintainer to
  decide otherwise. One point was put to them, what becomes of a folder's
  counts, and answered the same day ("3. Yes",
  [ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md)).

## Context

A game's folder carries its own copy of PS2Native's programs: the runtime,
its launcher, and the toolchain it compiles new code with
([ADR 0034](0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md)).
It plays and compiles wherever it is, and nothing outside it changes it. So a
game made with 0.1 stays on 0.1's programs when its user takes 0.2: nothing a
later version does faster reaches a game made before it, short of making the
game again and carrying its saves over by hand.

On 2026-11-21 and 2026-11-22 the session brought the maintainer's own folders
up to date with scripts of the build tree
([docs/app-log.md](../app-log.md)): each folder given the new programs, and
its own recompile compiling the game again whole before its next start,
because a folder's compile counts its toolchain in what it compiled from
([ADR 0046](0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md)).
Users had no such way. Asked what to do next, the maintainer chose 0.2 with
one ("7. Ok do it").

## Decision

1. **The wizard brings a game's folder up to date.** Its first step has a
   button, "Games made earlier...", which opens a window that lists game
   folders with how the programs of each stand to this PS2Native's: up to
   date, made with an earlier PS2Native, or made with a later one. It lists
   the games of this PS2Native's own `games` folder, and those of any folder
   the user adds: a game's folder, or a folder that holds games (an earlier
   PS2Native's folder, or its `games` folder). The first step also says how
   many games of its own folder were made with an earlier PS2Native.
2. **Bringing a folder up to date gives it this PS2Native's programs** in the
   place of its own: every file of the runtime a game's folder has, the
   launcher under the game's name, and the toolchain. Nothing else of the
   folder is touched: its sessions, its saves and the console's settings, the
   game's own settings and patch file, what its sessions met, its modules and
   what it compiled.
3. **The game then compiles itself again before it next starts,** by its own
   recompile, as it compiles what a session met
   ([ADR 0040](0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md)):
   the toolchain being another, the folder's compile finds everything left to
   compile. Its window says why, once: the folder was brought up to date, and
   the game is compiled again with the newer programs. Later there leaves the
   game unstarted, as always. Until then the folder holds the earlier modules,
   which the newer runtime refuses to load: the game never runs on a mix.
4. **A folder says which PS2Native's its programs are:** `programs.txt` in it,
   written when the wizard makes a folder and when it brings one up to date,
   has the version and a fingerprint of the runtime and toolchain the programs
   were copied from. A folder that does not say (0.1's did not) is an earlier
   PS2Native's. One whose version is later than this PS2Native's is left as it
   is, and the window says so.
5. **Nothing is changed while the game runs or compiles.** The folder is
   refused then, and the window says to close the game first.
6. **Nothing is brought up to date unasked.** A game's folder is its user's,
   and bringing it up to date costs a compile of some minutes before its next
   start: the user ticks the games and presses the button.

## The session's choices, for the maintainer to decide otherwise

- **Where it is offered:** a button on the wizard's first step, and one line
  beside it. No window opens by itself when the wizard starts.
- **What "up to date" is:** the fingerprint of the files a folder gets from
  this PS2Native (their names, sizes and times) is the one the folder wrote
  down. Two builds of the same version are told apart by it too, which is what
  a development folder needs; for a release it comes to the version.
- **`make.txt` gets one line** where PCSX2's GameDB has the game played with
  FullVU0SyncHack and the folder was made before the recompiler decided COP2
  instructions with it (ADR 0046). The game is found in the GameDB by the name
  of the executable its disc boots.
- **Modules an earlier compile left for the next start stay** where they are:
  the compile before the next start makes newer ones, which the launcher
  takes.
- **A folder made by `make-game.ps1`** says nothing of its programs, and is
  listed as made with an earlier PS2Native.
- **The version is 0.2** (`version.txt`) from this work on.
- **For checks:** `ps2n-wizard --update <folder>` brings the games at a folder
  up to date without a window, and `--updates <folder> --shots <folder>
  [--auto]` opens the window, saves it as a picture, and with `--auto` brings
  the games ticked up to date and saves it again.

## Put to the maintainer: a folder's counts after it was brought up to date

ADR 0046's point 6, open until this. A folder that has counted its profile
([ADR 0045](0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md))
keeps `captures\profile.profdata`, and every compile after takes it. The
counts are of the code the recompiler made then: where a later recompiler
makes other code for a function, the compiler drops that function's counts.
No folder of 0.1's has counts (0.1 did not count), so the question is for the
versions after 0.2, and for the maintainer's own folders.

Measured on 2026-11-22 ([docs/app-log.md](../app-log.md), "0.2: a game's
folder brought up to date", "A folder's counts after it was brought up to
date"), with counts taken before the recompiler decided COP2 instructions
when the code is generated, given to today's folders: Jak and Daxter, whose
hot functions are full of such instructions, keeps about half of what its
counts give (56.54 s, where 54.75 s with counts of its own and 58.46 s
without any); Gran Turismo 3 and Agent Under Fire keep all of it, to within
the rounds.

**The session's recommendation, built so:** a folder brought up to date with
another toolchain is compiled with the counts it has, and counts once more
after a session, as it counted the first time (its window says "Once more,
now that its folder was brought up to date"; Later puts it off, and it never
keeps the game from starting). It costs the user one round of a few minutes
for each version that changes the toolchain, and keeps a game from slowly
losing what its counts gave. A game an earlier PS2Native could not count for
is tried again. The alternative is to count once for ever, and take what the
old counts still give.

**The maintainer's answer, the same day: "3. Yes"**
([ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md)).
A folder brought up to date with another toolchain counts its profile once
more after a session.

## Checked

On a folder made by 0.1 as released, with 0.1's own wizard, a session of the
maintainer's compiled into it by 0.1's own recompile, on Windows and on Linux
under WSL 2: brought up to date in 2 to 3 seconds; its own recompile then
compiled the game again whole before its next start (128 files, 24 to 30 s);
and the session run again from it was the session at every one of 173
fingerprints, with nothing of the game's code interpreted that the folder had
compiled before. [docs/app-log.md](../app-log.md) has the rest: the window,
a folder in use refused, a second time finding nothing to do, a session in a
window, and the counts taken once more.
