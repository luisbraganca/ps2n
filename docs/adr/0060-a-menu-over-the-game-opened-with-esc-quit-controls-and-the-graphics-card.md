# ADR 0060: A menu over the game, opened with Esc: quit, controls and the graphics card

- Date: 2026-11-25
- Status: accepted (the maintainer's words, below). Built, committed and
  pushed; given to the maintainer's game folders; in no release yet. Reboot and
  the close button asking first (2026-11-26) likewise, at their answers.

## Context

A game made with the wizard plays in a window of PS2Native's own
(`ps2n-host -window`), in which nothing can be changed while it plays:

- the keyboard has PCSX2's default keys for the controller in port 1, and the
  first controller connected is added beside them;
- the graphics card is the one the wizard's graphics page named, or PCSX2's
  first;
- the game ends when its window is closed, without a word.

A game's settings are the wizard's to make, before its code is compiled
([ADR 0032](0032-the-wizard-goes-through-pcsx2s-settings.md)), and the
runtime refuses a game settings file that sets what PS2Native fixes (step
P4.8).

On 2026-11-25, after 0.4 was published, the maintainer wrote:

> I want to add an overlay that opens when pressing Esc, allowing the user to
> Quit (loose progress warning), Edit keybinds and controllers and which GPU
> is in use (if needed and it if makes it easier, ask for a game restart, I
> don't mind).
>
> I think those are the only acceptable changeable settings
> post-compilation.

And, while the session was building it:

> Use a fancy sound when it opens, when it closes and when clicking/hovering
> the menu items.

And, once those sounds were made:

> Use PS2 style sounds

## Decision

1. **Esc opens a menu over the game**, pressed while a game plays in its
   window, and the game waits while it is open. Esc again, or Resume, closes
   it.
2. **What the menu has**, besides Resume:
   - **Quit:** asks first, saying that what has not been saved in the game is
     lost; then the game ends as when its window is closed.
   - **Controls:** what each of the controller's buttons and sticks is on the
     keyboard and on a controller, each changed by pressing the key or the
     button; which controller plays; the defaults back.
   - **Graphics card:** which one draws the game, and choosing another.
3. **The only settings a player changes** once a game is made are those: the
   rest stays as the wizard made it.
4. **Sounds:** when the menu opens, when it closes, and when an item is
   pointed at or chosen, in the manner of the PlayStation 2's own menus.

## What the session chose within it, for the maintainer to decide otherwise

- **Drawn by PS2Native in the game's picture.** The menu is drawn into the
  picture with the ImGui PCSX2's renderer draws its own displays with (Dear
  ImGui, which PCSX2's core carries), by PS2Native's own code
  (`host/menu.cpp`), in the brand's colours: navy panels with the emblem's
  blue down their side, as the notice has them.
  - Not PCSX2's own menus (its fullscreen "Big Picture" interface), which
    [ADR 0012](0012-ps2native-branding-not-pcsx2s.md) keeps out of a game,
    and which have much more than these settings.
  - Not a window laid over the game's, as the notice is
    ([ADR 0035](0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md)):
    a menu takes the keyboard, a controller and the mouse, and drawn in the
    picture it shows over a game covering the screen with every renderer.
- **The game held between two frames.** While the menu is open, the machine
  is held where the window's requests are taken up between two frames.
  - The core is told it is paused, so its sound and vibration stop and its
    frame limiter starts again after, and that it runs again before the
    frame's event test ends: the processors' execution is never left. Nothing
    of the machine changes, so a session in which the menu was opened runs
    again as itself.
  - What was held down of the controller comes up as the menu opens and as it
    closes; in between, the keys and controllers are the menu's.
  - The pace a session writes down
    ([ADR 0055](0055-a-session-writes-down-the-pace-it-kept-and-the-notice-shows-for-any-new-code.md))
    leaves the time the menu was open out.
- **Quit** asks with two items, Quit and Back, Back reached first. The
  window's close button and Alt+F4 close it as before, without asking (until
  the next day: below).
- **Controls:**
  - **The controller in port 1,** the one a session writes down. A second
    player's port is not offered: a session writes down only the first
    controller, so a session played with a second could not be run again as
    itself.
  - **A table:** each of the DualShock 2's inputs on a line, with what it is on
    the keyboard and on a controller in two columns. Choosing one waits five
    seconds for a key, a button or a stick to put there, which replaces what
    was there of its kind. Delete, a controller's Square or a right click
    clears; Esc leaves it as it was. Esc is the menu's, never one of the
    controller's buttons.
  - **"Play with":** a controller connected chosen to play, its inputs the
    mapping PCSX2 makes for it, the keys kept.
  - **"Default controls":** PCSX2's keys again, and the first controller
    beside them, as before.
  - **Kept in the game's folder,** in PCSX2's own format for an input profile:
    `data\PCSX2\inputprofiles\controls.ini`, read when the game starts. Without
    that file the game has the defaults, the first controller connected added
    beside the keys, as before.
- **Graphics card:**
  - The cards PCSX2 lists for the game's renderer, the one drawing marked.
    Windows' software renderer, which Direct3D lists with them ("Microsoft
    Basic Render Driver"), is left out: it is not a graphics card, and a game
    on it crawls.
  - A choice is taken at once: PCSX2 makes its renderer again on that card, as
    it does when its setting is changed while a game runs; where the card
    cannot draw the game, PCSX2 goes back to the one before, and so does the
    setting. No restart. The menu waits while it does, and shows over a black
    picture after it until the game, resumed, draws again: PCSX2's renderer
    made again has no last picture to show.
  - Kept in the game's own settings file (`EmuCore/GS/Adapter`), where the
    wizard's graphics page keeps it.
  - The renderer itself (Vulkan, Direct3D, OpenGL) stays as the wizard made
    it.
- **Moving in the menu:** the arrow keys and Enter, a controller (its D-pad
  or left stick, Cross to choose, Circle to go back), or the mouse. Only Esc
  opens it.
- **The sounds** (`host/menu_sound.cpp`):
  - **PS2 style, PS2Native's own.** In the manner of the PlayStation 2's own
    menus: glassy, crystalline tones in a large, soft, dark room. They are
    made by PS2Native's code when first needed, not files. Nothing of Sony's
    own sounds is used: they are the console's BIOS's, which the project never
    takes anything from.
    - Opening: a low swell and a breath of air rising, glassy bell notes (D,
      A, E, then F sharp, high) blooming over a soft fifth, in a large room.
    - Closing: the same falling away, in a smaller room.
    - An item pointed at, with the mouse, the keys or a controller: a soft
      crystalline tick, with a little of the room.
    - An item chosen: two bright glassy notes up, a faint sparkle over them;
      back a page, or something cleared or given up: two duller notes down.
  - Played through SDL's audio, which PCSX2's core carries, beside the game's
    own sound, at its volume; a sound played again while it still rings does
    not cut it short.
  - Nothing is played where the game's sound is muted: every run without
    anyone to hear it is (`window-check.ps1`, `game.ps1 -Mute`).
  - `ps2n-host -menu-sounds <folder>` writes them as WAV files, to be
    listened to.

## Built and checked the same day

docs/app-log.md, "A menu over the game (ADR 0060)", has the detail.

- **On Windows,** with the development tree's runtime, in a window of 1280 by
  720 on the measurement's hidden desktop, muted, on Direct3D 12 so that the
  window could be photographed (`build\measure\menu-60.ps1`):
  - **The keys:** Esc opened the menu over the game. Controls opened, and D-Pad
    Up's key became P, the controller's D-pad kept beside it, written to
    `controls.ini`. The Graphics card page named the card drawing the game.
    Quit asked, and its Quit ended the session as the close button does.
    Every sound was asked for where it should be (none played, muted).
  - **The replay:** that session, the menu open for 24 seconds in two
    openings, run again without a window: the session at every fingerprint
    (11 of 11; with the final programs, 9 of 9).
  - **The pointer:** the next session started with the player's own controls;
    clicks on Controls and on Default controls put PCSX2's keys back and
    removed the file.
  - **The card:** changed while the menu was open, to Windows' software
    renderer (let into the list for the check only) and back: PCSX2 made its
    renderer again each time, and the game's settings file named the card last
    chosen.
- **Tests:** every test passes: on Windows 556, and 554 in the module tree
  with the same three comparisons disabled; on Linux, inside WSL, 556 and 553;
  none skipped.
- **On Linux** the menu is built and its sounds written, but it was not opened
  in a window: a window there shows on the maintainer's screen (WSLg).
- **Found on the way** (each held up the check until put right): ImGui 1.93
  leaves an invisible button out of keyboard and controller navigation unless
  asked; focusing the page's window at every frame took the focus back out of
  the controls' table; the Enter that chooses a control comes up while the
  control waits, and ImGui kept it held until it was passed on.

## The maintainer's answers

Asked, with the menu built and checked: whether to commit and push; whether
their game folders get these programs, to try it; and whether a controller may
open the menu too (held Select and Start, the session's recommendation). The
maintainer's answers, the same day: "1. Yes", "2. Yes", "3. No".

- **Committed and pushed.**
- **Their game folders** (`build\kit\games` on Windows, `build/PS2Native/games`
  inside WSL) are given these programs, their PS2Native folders made again from
  the commit first, each game compiled again before it next plays.
- **Only Esc opens the menu.** A controller moves in it, but does not open it.

Done the same day:

- **The commit:** `e616401`, at 17:12 UTC, pushed.
- **Their PS2Native folders** made again from it (`build\kit`, and inside WSL
  `build/PS2Native`), their games kept.
- **Their game folders** given its programs by their PS2Native folder's wizard,
  each compiling its game again: the nine of `build\kit\games` (22 s to 324 s)
  and Jak and Daxter's inside WSL (53 s). Where a session can be run again as
  itself, its start was: the session at every fingerprint compared. Asked
  again, the wizard changed nothing; the memory cards and sessions are as they
  were (`build\measure\folders-60.ps1`; the scratchpad's `wsl\p60-linux.sh`).

## Reboot, and the close button asking first (2026-11-26)

The next day, a border on the right in SSX Tricky having proved PCSX2's own
(docs/app-log.md), the maintainer wrote:

> To finish this feature, can you add an additional overlay button for
> "Reboot" that allows rebooting the game? Also show a warning message because
> unsaved progress may be list. This warning should also be shown when closing
> the game through the X button on the window (not sure if you added it there
> as well).

The session had not: the close button closed at once.

1. **Reboot** on the menu's first page, between Graphics card and Quit, asks
   first, saying that what has not been saved is lost, and restarts the game
   from the beginning.
2. **The window's close button**, and Alt+F4, ask the same question as Quit
   before closing.

What the session chose within it, for the maintainer to decide otherwise:

- **A reboot is PCSX2's own reset of the machine** (`VMManager::Reset`, its
  "Reset System"), in the same window, the game starting again from the BIOS.
  It is done as PCSX2 does it: marked when the menu closes, and carried out
  once that frame's event test is over and execution has been left.
- **A session written down marks the frame** (`<frame> reboot`), and a run of
  it again reboots there: such a session still runs again as itself.
- **Reboot's question:** "Reboot <the game>? The game starts again from the
  beginning. Anything you have not saved in it will be lost.", Reboot and Back,
  Back reached first.
- **The close button and Alt+F4** open the menu on Quit's question; Back or Esc
  goes back to the game. Pressed again while that question shows, they are Quit
  chosen there. They close the window at once when the game is not running its
  frames (still starting, or held up): a game that hangs can still be closed.
  The menu's own Quit, and a check, close it without asking.
- **Not while the game saves:** Quit and Reboot are refused while PCSX2 counts a
  memory card busy (for 300 frames after each write), saying to go back to the
  game for a few seconds. PCSX2 refuses a reset then; its own window warns
  before shutting down that the card can be corrupted, and lets the user insist.

Built and checked the same day on Windows (docs/app-log.md, "Reboot, and the
close button asking first"):

- **Reboot:** the game rebooted at the frame it was chosen, the BIOS starting
  again, then the game. The session written down with it ran again as itself,
  rebooting at the same frame: the session at every one of its 19 fingerprints.
- **The close button:** the menu on Quit's question; Esc back to the game; the
  question again, and pressed while it showed, the window closed.
- **The card busy,** in a trial build: Quit, Reboot and the close button pressed
  on the question each refused, with the message.
- **Tests:** every test passes: on Windows 556, and 554 in the module tree; on
  Linux, inside WSL, 556 and 553; none skipped. On Linux it was not opened in a
  window.

Asked whether to commit and push, and whether their game folders get these
programs, the maintainer answered the same day: "1. Yes", "2. Yes".

- **Committed and pushed.**
- **Their game folders** are given these programs as the day before, their
  PS2Native folders made again from the commit first, each game compiled again
  before it next plays.

## Consequences

- **A game's folder gets the menu with this version's programs**, which the
  wizard gives a folder made earlier ("Games made earlier..."). Nothing else of
  the folder changes; the module version stays 6.
- **A player's controls and card stay with the game's folder:** each game has
  its own.
