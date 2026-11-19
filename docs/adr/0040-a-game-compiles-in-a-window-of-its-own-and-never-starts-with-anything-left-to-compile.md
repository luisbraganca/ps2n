# ADR 0040: A game compiles what a session met in a window of its own, and never starts with anything left to compile

- Date: 2026-11-19
- Status: accepted (the maintainer's words, below). It replaces the
  background compile of [ADR 0034](0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md),
  the icon and notifications of [ADR 0035](0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md)
  (its notice over the picture stays), and the launcher's wait of
  [ADR 0037](0037-p4-8-done-a-game-does-not-play-while-it-compiles.md).

## Context

Since step P4.6 (ADR 0034), when a session has ended, the game's launcher
starts the folder's own recompile, `ps2n-remake`, which compiles what the
session met for the first time in the background, at low priority, with an
icon in the notification area and a notification when it begins and another
when it is done (ADR 0035). A game started while that compile runs waits for
it in a window of the launcher's, and plays when it is done (ADR 0037).

On 2026-11-19, with the Linux folder put together and the maintainer's run
on their Linux machine next, the maintainer wrote:

> Let's change things a bit: Compiling shouldn't be done on the background,
> but yes on a foreground modal/window (whatever you recommend). Also, the
> user is able to click "Later" or close the modal/window (which is the same
> thing). What this does is, next time the game runs, it will first compile
> pending compilations before actually running. Can we do this change? I
> think it improves the user experience.

And, while the session was making the change:

> It shouldn't be possible to run the game without compiling pending
> compilations first.

## Decision

1. When a session has ended and met code for the first time, the game
   compiles it at once, in a window of its own in front of the others, not
   in the background. The window says what it compiles and for what, and how
   far it has got, and has a button, **Later**. Closing the window is Later.
2. Later stops the compile. What it had compiled is kept; the rest is
   compiled before the game next starts.
3. The game never starts with anything left to compile. Started, its launcher
   has the folder's recompile compile what is left first, in the same window
   ("<the game> starts when it is done"), and starts the game when that is
   done. Later there, or that window closed, leaves the game unstarted, and
   the next start compiles it first again.

## What the session chose within it, for the maintainer to decide otherwise

- **A window of its own**, not a dialog over another (the game's window has
  closed by then). It shows only when there is something to compile: a
  session that met nothing new shows nothing, and a start with nothing left
  starts the game at once. Its button reads Later while it compiles, and
  Close once it has ended.
- **At normal priority, on every processor**, as the wizard's compile; the
  background compile ran at below-normal priority.
- **When the compile after a session is done**, the window says so
  ("Compiled") for three seconds and closes. When the compile before a start
  is done, the game starts at once.
- **A compile that fails** says why in the window, with where its log is
  (`work/remake.log`). As the maintainer's second sentence has it, the game
  does not start before what its sessions met is compiled: a compile that
  keeps failing keeps the game from starting until it is put right (the game
  made again with the wizard, or a corrected PS2Native). The other way would
  let the game start after a failed compile, interpreting what it could not
  compile.
- **The icon in the notification area and the notifications go**. The notice
  over the picture while the game runs code met for the first time (ADR
  0035) stays.
- **ADR 0037's wait is the start compile itself**: a game started while the
  compile after a session runs waits for it without a window of its own
  (that compile's window is there), then compiles what is left, if Later was
  chosen there, in the start window.
- **On Windows**, the game's window gives, as it closes, the right to bring a
  window to the front, which Windows would otherwise not let the compile's
  window take; the launcher gives the start compile the right it was started
  with.

## Consequences

- P4.6's criterion (a session that meets new code finds it compiled at its
  next start) holds, the compile now in its window. P4.7's ("what a session
  meets for the first time is compiled in the background for its next
  start") is read as compiled for its next start, in its window.
- A game's folder made before this change keeps the launcher and recompile
  it was made with. It gets these when it is made again with the wizard, or
  when its launcher and `ps2n-remake` are copied over.
- `wizard/remake_main.cpp` (the window, `--before-play`), `host/launcher.cpp`
  and `wizard/launcher_main.cpp` (the start compile first, the game only on
  its success), `host/window.cpp` (Windows' foreground right).

## The maintainer's answers (2026-11-19)

Asked whether to commit and push it, whether to give the game folders on
the development computer the new launcher, recompile and runtime, and
whether to make the Linux folder for their laptop again from that commit:
"1. Yes 2. Yes to all games 3. Yes". Read as: all three. The folders of
Jak and Daxter, Killzone and Silent Hill 3 got the three programs; the two
of Agent Under Fire and the one of Gran Turismo 3, made before a game's
folder compiled anything itself (no toolchain, no `make.txt`), got the
launcher and the runtime, and compile nothing until they are made again
with the wizard. Each folder's runtime loads its modules. The programs they
had are kept in the build tree (`build\game-programs-before-adr-0040`).
