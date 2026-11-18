# ADR 0035: A game says when it runs new code, and when it compiles it

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-18 |
| Decided by | Maintainer |

## Context

ADR 0034 had a game compile what its sessions met for the first time
itself when its window closes, with an icon in Windows' notification area
while it compiles and a notification when it is done. Trying it, the
maintainer said: "The compilation notification was really delayed... It
should show right after closing the game showing that is compiling, than a
different one saying it's compiled. Also when the game is running on
interpreted code that can be compiled, there should show an overlayed
notification saying so."

The first notification was late for two reasons: the recompile merged the
captures of every session the folder had ever had before it knew whether
there was anything new, and what it showed then was an icon, which Windows
11 keeps out of sight in the notification area's overflow, with no
notification until the end.

ADR 0012 has a finished game show nothing of what PCSX2's core can draw over
the picture, which is switched off by settings.

## Decision

As read:

1. **Two notifications for a compile of the game's own**: one as soon as
   the window is closed and the session is found to have met new code,
   saying that it is being compiled; and a different one when it is done,
   saying that the next start has it (or that it could not be compiled).
   The recompile merges only the sessions it has not merged before, so that
   it knows at once.
2. **A notice over the game's picture while it runs interpreted code that
   can be compiled**: what a session meets for the first time and the next
   compile would compile. It is PS2Native's own, drawn in a window of
   PS2Native's laid over the game's, never with PCSX2's on-screen display,
   which stays off (ADR 0012 stands for what the core draws). The session's
   choices, for the maintainer to decide otherwise: it is shown in a game's
   folder (where closing the window compiles), when over the last half
   second at least 1% of the EE's, the IOP's or a vector unit's work was
   interpreted; for five seconds, at most once a minute; in the window's
   top left corner, in the brand's colours, letting clicks through.
