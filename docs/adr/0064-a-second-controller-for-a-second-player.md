# ADR 0064: A second controller, for a second player

- Date: 2026-11-28
- Status: accepted (the maintainer's words, below). Built, checked, committed
  and pushed at the maintainer's word; their game folders get these programs
  after the commit; in no release.

## Context

A game made with the wizard plays in PS2Native's window with one controller:

- **Port 1** has a DualShock 2, played with PCSX2's default keys and the first
  controller connected (`SDL-0` only), or with the player's own controls set
  in the menu over the game
  ([ADR 0060](0060-a-menu-over-the-game-opened-with-esc-quit-controls-and-the-graphics-card.md)).
- **Port 2 is empty,** as PCSX2 has it by default (`Pad::GetDefaultPadType`).
  A second controller connected does nothing.
- **The menu's Controls page sets port 1's only.** ADR 0060 left a second
  player's port out on purpose: a session written down kept only the
  controller in port 1 (`host/session_record.h`), so a session played with a
  second could not be run again as itself, and a game's folder runs its
  sessions again (its own compile counts what its code does from one,
  ADR 0047; an update of a folder checks its sessions' starts).

PCSX2's core has everything a second player needs: two ports (eight with
multitaps), a DualShock 2 put into a port while a game runs
(`Pad::LoadConfig`, which gives the console the moment of plugging it in), and
its input recording keeps either port.

On 2026-11-28, after 0.5 was published, the maintainer wrote:

> Can you add 2 controllers support? I think currently we only support 1
> controller but I could be wrong

They were right: one controller.

## Decision

A game played in its window takes a second controller, for a second player,
in port 2, as a console with two controllers plugged in.

## What the session chose within it, for the maintainer to decide otherwise

- **Port 2 has a controller only when a second player has something to play
  with:** a controller, or keys of their own. A game played by one player is
  as before, port 2 empty: their sessions, timings and profiles are unchanged.
  Not the other way, a DualShock 2 always in port 2, which every game would
  see, one player or two.
- **It is put in while the game runs, as PCSX2 puts one in:** between two
  frames, at the moment player 2 first has something to play with (a second
  controller connected, keys set for them in the menu, or their own keys kept
  from an earlier session, at the first frame). The console sees a controller
  plugged in. It stays until the game ends, even if the second controller goes
  (put down, its battery flat), as the one in port 1 does; the next start has
  port 2 empty again until player 2 has something to play with.
- **Controllers are given out in the order they connect:** the first to player
  1, beside the keys, as before; the second to player 2. Those connected
  together, at the start, in the order of their numbers (`SDL-0` before
  `SDL-1`). A controller connected while player 1's own is not (one put down
  and another taken up) goes to player 1. A third does nothing until chosen in
  the menu.
  - **Player 1's own controls** (kept in the game's folder) are as before:
    they say which controller is player 1's, and none is added to them.
  - **A second controller goes to player 2** only while player 1 plays with
    another controller that is connected and player 2 with none. So a player
    on the keyboard alone, with a controller left connected, is not made two
    players.
- **Player 2 has no keys by default.** Two players on one keyboard is possible
  by setting player 2's keys in the menu.
- **The menu's Controls page has both players:** "Player 1" and "Player 2" at
  its top (a controller's L1 and R1 go from one to the other); each with the
  table of its inputs on the keyboard and on a controller, the controllers
  connected to choose from ("Play with", marked with the player each plays
  for), and "Default controls", which puts back PCSX2's keys for player 1,
  nothing for player 2, and gives out the controllers connected again in
  order. What player 2 has not yet put into port 2 is said on the page.
- **An input, and a controller, play for one player at a time:** a key or a
  button set for one player is taken from the other's controls; a controller
  chosen to play for one is taken from the other.
- **Kept with the controls** in the game's folder
  (`data\PCSX2\inputprofiles\controls.ini`, PCSX2's own format for an input
  profile): both players'. Port 2 is called a DualShock 2's in the file where
  player 2 has any input, and the runtime has it empty at the start whatever
  the file says, so that a session written down has the moment it was put in.
- **A session written down keeps the second controller too**
  (`host/session_record.h`): `<frame> connect 2` where a controller was put
  into port 2, and `<frame> pad2 <18 bytes>` for its changes, as `pad` lines
  have port 1's. A run of it again puts one in at the same frame and gives it
  back its states: a session played by two runs again as itself. A session
  without such lines, every one written down before, had port 2 empty and runs
  again so.
- **Only two.** PCSX2's multitaps (up to eight) are not offered.

## Built and checked the same day

docs/app-log.md, "A second controller (ADR 0064)", has the detail.

- **In a window,** with the development tree's runtime, on the measurement's
  hidden desktop, muted (`build\measure\players-64.ps1`):
  - player 2's keys set in the menu put a controller into port 2 at that
    frame;
  - player 2's own keys, kept with the controls, put it in at the first frame;
  - a key set for player 2 left player 1's controls;
  - player 2's presses were written down.
- **Run again without a window,** compiled and on stock PCSX2's recompilers:
  each of the three sessions is the session at every fingerprint. Taken out
  of one, port 2 makes the run part from it at frame 200: the console reads
  the controller there.
- **Sessions written down before** run again as themselves with these
  programs.
- **The games' own pages** (Agent Under Fire's and Gran Turismo 3's language
  pages) take player 1's controller only, as on a console. No game's mode for
  two players was reached in these checks: that is the maintainer's to try,
  with two controllers.
- **A controller connected to this computer** was given to player 1 by the
  first check, as it would be to a player. The checks after wrote the controls
  beforehand, so that a check never makes it vibrate.

## Consequences

- **A game's folder takes a second controller with this version's programs,**
  which the wizard gives a folder made earlier ("Games made earlier...").
  Nothing else of the folder changes; the module version stays 7.
- **An earlier version's runtime does not run a session written down with a
  second controller** (it refuses the lines it does not know), and an update
  brings a folder's runtime up to date with its sessions.
- **Code a game runs only with two players** (its two-player modes, the
  console's code that reads port 2) is, the first time, code met for the first
  time: interpreted, with the notice, and compiled when the game closes
  (ADR 0035, ADR 0057).

## The maintainer's answers

The session put two things to them, each with its recommendation:

1. Commit and push the second controller: the code, this record and the
   others, with `CLAUDE.md`'s "Where things stand". Recommended: yes.
2. Give their game folders these programs after the commit, so that they can
   try two controllers: the nine on Windows and Jak and Daxter's inside WSL,
   with `update-games.ps1` and `update-games.sh`, each game compiling again,
   its sessions checked, its memory cards untouched. Recommended: yes.

Their answers, the same day: "1. Yes", "2. Yes".

- **Committed and pushed.**
- **Their game folders** get these programs, their PS2Native folders made
  again from the commit first, as docs/development.md says ("The maintainer's
  game folders brought up to date").
