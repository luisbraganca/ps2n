# ADR 0037: P4.8 done; a game does not play while it compiles

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-18 |
| Decided by | Maintainer (P4.8's run; that a game does not play while it compiles; that it waits for the compile, 2026-11-19); the session (the rest of how, for the maintainer to decide otherwise) |

## Context

P4.8, the folder for others, has for its criterion (the session's
proposal, ADR 0033): on a computer without PCSX2, a game made with the
wizard from a `.chd` image, its patches fetched at the user's word, and the
folder carrying the licence of everything in it. Its parts were built and
checked on this computer on 2026-11-18 (ADR 0036), and the folder for
others was made with `kit.ps1 -Out` from the commit pushed that day
(`fbb2e77`), for the maintainer's run on their Windows 10 computer, P4.3's,
which has no PCSX2.

The maintainer ran it the same evening: Silent Hill 3 made in that folder
with `PS2Native.exe` from its `.chd`, PCSX2's archive of patches fetched at
the settings step's button, and four sessions played from the game's
folder. They brought the folder back to this computer and said:

> Everything went flawlessly, however I think running the game while
> compilation is running in the background shouldn't be allowed.

Since P4.6 (ADR 0034, ADR 0035), when a session ends its launcher starts the
game's own recompile (`ps2n-remake`), at low priority, which compiles what
the session met into modules that the next start puts in place, with a
notification when it begins and another when it is done. A game started
meanwhile played beside it, with the modules it had, while the compile went
on. The folder shows it once: the fourth session began three seconds after
the third had ended, while the recompile of what the third had met was
under way (ready six minutes later, 116 files compiled, eight at a time),
and was closed after 136 frames.

## Decision

1. **P4.8 is done.** The maintainer's run counts for its criterion
   ("Everything went flawlessly").
2. **A game does not play while it compiles** (the maintainer's words).

How is the session's, for the maintainer to decide otherwise:

3. **The game waits for the compile, and plays with it.** Started while
   the folder's recompile compiles, the game's launcher does not start the
   runtime: it waits, in a window of its own that says so ("Compiling what
   your last session met for the first time"), with a moving bar and a
   Cancel button, and when the compile is done it plays, the modules
   compiled put in place at that start. Cancelled, the game is not started
   and the compile goes on; its notification says when it is done.
4. **What counts as compiling.** The recompile holds its lock
   (`work\remake.lock`) from its start until its modules are ready, or could
   not be made, and now lets go of it then, before the notification it shows
   for some seconds after, which the game does not wait for. A recompile
   started less than 20 seconds before, which may not have taken its lock
   yet, is waited for too, so that a game started at once after a session
   ends waits as well; it is found among the programs running by its file,
   the folder's `ps2n-remake.exe`.
5. **Nothing shown for a recompile that finds nothing new**, which is done
   within a moment: the launcher waits a second and a half before it shows
   its window.

## The maintainer's answer (2026-11-19)

The next morning the session put four questions to the maintainer, each
with its recommendation: whether to commit and push the work; whether the
game should wait for the compile, as decision 3 has it, or be refused with
a message and started again later, or be offered to play at once with the
compile stopped until the session ends (the session recommended the wait,
saying that the laptop's last compile had taken six minutes); whether to
put the new launcher into the folders of the maintainer's games made on
this computer before it; and P4.7, Linux, the last step of Phase 4, next.
The maintainer answered:

> 1. Yes 2. Yes 3. Yes 4. Ok

Read as: the work committed and pushed; **the game waits for the compile
and then plays, as decision 3 has it, which is the maintainer's since**,
decisions 4 and 5 staying the session's; the new launcher put into the
folders made before it ([app-log.md](../app-log.md), "P4.8"); and P4.7
next.
