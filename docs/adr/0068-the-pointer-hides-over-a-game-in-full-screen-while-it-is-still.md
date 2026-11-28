# ADR 0068: The pointer hides over a game in full screen while it is still

- Date: 2026-11-28
- Status: accepted (the maintainer's words, below). Built, checked,
  committed and pushed at their word; in no release. Their game folders are
  given its programs after the commit, at their word.

## Context

- **The pointer stayed over the picture.** A game's window covers the screen
  at Alt+Enter or F11, or by itself where a window of its size would not fit
  (ADR 0025; ADR 0039: the window without its frame, never exclusive full
  screen). The pointer stayed where it was, over the game, until it was moved
  off.
- **The mouse is no game's control in PS2Native.** The window passes the
  pointer, its buttons and its wheel to PS2Native's menu over the game alone
  (ADR 0060), which takes them only while it is open. Its Controls page sets
  keys and controllers, and leaves the mouse to the menu. The wizard has none
  of PCSX2's controller pages. So no click and no move of the mouse reaches a
  game.
- **PCSX2's own frontend** has a setting, "Hide Cursor In Fullscreen", off by
  default: on, the pointer is hidden all the while a game runs in full
  screen, and shows when it is paused. It also hides it when the mouse's moves
  are bound to a controller's input.

## Decision

The maintainer's words, on 2026-11-28:

> Can you hide the mouse when on full-screen? I'm not sure what's the best
> policy to avoid messing with Mouse1 or mouse swipes if it's used as
> keybinds. Maybe hide it when clicking the game view? And show it if the
> mouse moves?

How they were read:

- **In full screen,** as asked. In a window the pointer stays as it was.
- **Their worry, clicks and moves of the mouse used as controls:** none is one
  in PS2Native (above). Hiding the pointer takes nothing from a game. A click
  and a move reach what they reached before: the menu while it is open, and
  nothing else.
- **Their two ideas are kept:** a click on the picture hides the pointer at
  once, and moving it shows it again.
- **One is added:** the pointer also hides by itself once it has been still
  for two seconds. A player with a controller or the keys never clicks the
  picture. Without it, the pointer would stay in the middle of the picture
  for the whole game.

## What the session chose within it, for the maintainer to decide otherwise

- **Two seconds still.** Long enough for a pointer being moved about not to
  vanish between two moves, short enough for it to be gone soon after it
  stops.
- **The menu over the game is used with the pointer.** While it is open,
  neither the time nor a click hides the pointer. Hidden when the menu opens,
  it stays hidden until it is moved, and no item of the menu shows as pointed
  at by a pointer no one sees. After the menu closes, it hides once it has
  been still for two seconds.
- **Any move shows it,** however small. The window moving under a pointer that
  is still is not a move: when it covers the screen and when it stops.
- **Any of its three buttons hides it,** the right and the middle as well as
  the left. The wheel neither hides nor shows it.
- **Both systems follow one rule,** written once (`host/pointer_hiding.h`)
  with a unit test of its own (`unit/host`, among the unit tests that need no
  PCSX2). On Windows the window answers "none" when Windows asks it for the
  pointer's shape (`WM_SETCURSOR`); on Linux the window is given a pointer
  with no pixels (X11).
- **A check can ask the window whether the pointer is hidden,** as it asks for
  the picture on display (`window::kPointerMessage`, the `pointer` step of
  `scripts\windows\window-check.ps1`).
- **If the mouse is ever made a game's control** (PCSX2 can bind its buttons
  and its moves to a controller's inputs), the rule changes for that game:
  the pointer hidden all the while the game has the mouse, as PCSX2 hides it
  when the moves are bound. Nothing of that is built.

## Built and checked the same day

docs/app-log.md, "The pointer hidden in full screen (ADR 0068)", has the
detail.

- **The rule's unit test,** eight cases, passes on Windows and inside WSL.
- **On the hidden desktop,** a muted session of Agent Under Fire was played in
  its window by a script, and the window was asked 22 times whether the
  pointer was hidden. Every answer was the one expected: shown in a window,
  even after a click; covering the screen, hidden once still for two seconds,
  shown when moved, hidden at once at a click; shown while the menu was open,
  hidden again after it closed; shown in a window again.
- **That session runs again as itself** without a window, at every one of its
  29 fingerprints: nothing of the game's run changed.
- **Every test of the development tree passes:** 556, none skipped.
- **Not seen on a screen.** The hidden desktop has no pointer to see, and a
  check on the maintainer's own screen would take their mouse. On Linux, a
  window opened inside WSL appears on that screen: there the build and the
  unit test are the check.

## Consequences

- **Nothing a game runs changes.** The pointer is no input a session keeps,
  and nothing was timed.
- **The maintainer's folders get it** when they are given programs made from a
  commit that has it, at their word.

## The maintainer's answers

The session put three things to them, each with its recommendation:

1. Keep the rule as built. Recommended: keep it. Otherwise their two ideas
   alone, without the two seconds, or PCSX2's way: hidden all the while a game
   runs in full screen, shown only in the menu.
2. Commit and push. Recommended: yes.
3. Give their game folders these programs now, after the commit. Recommended:
   yes, since they asked for it to play with; the wizard's icon (ADR 0066)
   comes with it. Otherwise with the next release.

Their answers, the same day:

> 1. Ok, we might need to change the rule in the future. I'm not sure how it
> works on PCSX2, but if it allows to bind mouse movement or mouse clicks, we
> may need to change things.
> 2. Yes
> 3. Sure

How they were read:

- **The rule stays as built,** to be looked at again if the mouse ever becomes
  a game's control.
- **What PCSX2 allows,** looked up for them in its source. When one of a
  controller's inputs is set, a click binds that button of the mouse, and a
  turn of the wheel binds the wheel, always. A move binds the mouse's moves
  only where "Enable Mouse Mapping" is ticked, which it is not by default
  (`pcsx2-qt/Settings/InputBindingWidget.cpp`). With the moves bound, PCSX2
  hides the pointer and reads the mouse by how far it moves, not by where it
  is (`InputManager::UpdateHostMouseMode`). PS2Native lets none of these be
  set (Context). If it ever does, a game with the mouse's moves bound has the
  pointer hidden all the while it is played; for one with only its buttons
  bound, the rule is looked at again then.
- **Committed and pushed;** their game folders are given these programs after
  the commit.
