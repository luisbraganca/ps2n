# ADR 0039: The notice over a full-screen game: exclusive full screen disallowed

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-19 |
| Decided by | Maintainer (that it be fixed); the session (how, for the maintainer to decide otherwise) |

## Context

ADR 0035 has a notice of PS2Native's own shown over a game's picture, in a
window of PS2Native's laid over the game's, while the game runs code met for
the first time, interpreted: when over the last half second at least 1% of a
processor's work was interpreted, for five seconds, at most once a minute.
On 2026-11-19 the maintainer played Jak and Daxter from its folder and
asked:

> Did the overlay noticing the game was being interpreted (and hence slowed)
> stop working? It stopped working for Jak and Daxter. And I know it was
> interpreting because the game sure was slow and after closing I got a
> notification saying it is compiling.

The session's first reading was that the session had interpreted little
(0.08% of the EE's cycles over the whole of it) and that the slowness came
mostly from the session's own work on the same computer (Linux test suites
in WSL on all 20 threads, step P4.7), and it said that the notice had only
ever been checked in a window of 1280 by 720, never over a game covering the
screen. The maintainer answered: "You're right, maybe it's a full screen
bug? Fix it", and after another session: "it happened again, Jak and Daxter
never showed the in-game notification, the game slowed down and when I
closed it I got the compiling tray icon notification."

What was found:

- **The notice was due.** The runtime now reports how near the notice's
  line a run came (decision 3). The maintainer's session of 12:14 (109,508
  frames), run again without a window from its folder's own modules, the
  session at every one of its 2,190 fingerprints: over the line in 12 of
  3,649 half seconds, the first at frame 2,640 (about 44 s in), at most
  10.70% of the EE's cycles interpreted in one.
- **It is shown, and over a full-screen game it reaches the screen at a
  session's start, not later.** In every run made to check it, the notice's
  window was there and shown (Windows' own list of windows), at the top left
  of a game covering the 1920 by 1080 screen, on PCSX2's Vulkan renderer on
  the maintainer's NVIDIA GeForce RTX 4070 SUPER. The maintainer watched:
  one shown 3 s into a run, seen; a scratch copy of Jak and Daxter's folder,
  its own runtime started by its own launcher, with modules that do not hold
  the game's code, so that it interprets from the start (run A): the notice
  shown at 3 s and again at 63 s, "Only the first" seen.
- **With exclusive full screen disallowed, both were seen** (run B: the same
  with PCSX2's `ExclusiveFullscreenControl = 0` in the game's settings
  file): "Both".

PCSX2's default for that setting, "Automatic", lets the graphics driver
decide (Vulkan's `VK_EXT_full_screen_exclusive`, `VKSwapChain.cpp`): a game
covering the screen is then taken over by NVIDIA's driver once it has run a
moment, and from then on nothing Windows lays over the picture reaches the
screen, PS2Native's notice among them. A notice shown at a session's start,
before that, shows.

## Decision

At the maintainer's "Fix it"; how is the session's, for the maintainer to
decide otherwise:

1. **Exclusive full screen is disallowed by default on Windows**
   (`EmuCore/GS/ExclusiveFullscreenControl = 0`): among the defaults the
   wizard shows and a game's folder starts from (`ps2n::game::SetDefaults`),
   and in every run of the runtime in a window. A game's own settings file
   may allow it again (the wizard's graphics page, "Exclusive Fullscreen
   Control"), and then the notice may not show after a session's start.
   Linux has no such control (PCSX2 only says it cannot, when it is set),
   and its notice is a window inside the game's.
2. **The game folders already made on this computer** that have a settings
   file of their own (Agent Under Fire, Gran Turismo 3, Jak and Daxter,
   Killzone, Silent Hill 3) have the line added there, which their own
   runtimes, older than this, take as they are (the originals kept in
   `build\gamesettings-before-adr-0039`).
3. **The runtime's report says how near the notice's line a run came**: in
   how many half seconds a processor's work interpreted was over it and the
   first of them, the most of each processor's work interpreted in one, and
   how often the notice was shown (`last-run.log` in a game's folder), in a
   session run again too, where none is shown.
