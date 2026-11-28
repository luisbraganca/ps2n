# ADR 0066: The wizard's windows carry PS2Native's icon

- Date: 2026-11-28
- Status: accepted (the maintainer's words, below). Built, checked,
  committed and pushed at their word; in no release.

## Context

The maintainer sent two pictures of the wizard making Primal. Its title bar
and its button on the taskbar showed Windows' default icon. They asked:

> why isnt our ps2n icon being used buy the wizard?

- **`ps2n-wizard.exe` carries PS2Native's icon** as its resource 1
  (`assets/ps2n.rc`), so Explorer shows it.
- **Qt gives a window an executable's icon by itself** only when the icon is
  named `IDI_ICON1`. Otherwise the window gets Windows' default application
  icon. PS2Native's other windows take resource 1 themselves: the game's
  window (`host/window.cpp`, ADR 0012) and ps2n-remake's
  (`wizard/remake_main.cpp`). The wizard never had, since it was made (step
  P4.5).
- **On Linux the wizard had no icon at all.** There the icon is a resource of
  Qt's, which only ps2n-remake and a game's launcher were given.

Put to them: "Fix the wizard's icon? ... a few lines in `wizard/main.cpp`, as
the recompile window takes it". Recommended: yes.

## Decision

Their answer: "2. Yes".

The wizard makes PS2Native's icon the application's when it starts
(`QApplication::setWindowIcon`). So every window of it carries the icon, the
settings window among them. It is taken as ps2n-remake takes it:

- on Windows, resource 1 of the executable, at the small and the large size;
- elsewhere, the Qt resource `:/ps2native/icon.png`, now compiled into the
  wizard too (`wizard/targets.cmake`).

## What the session chose within it, for the maintainer to decide otherwise

- **The wizard takes the icon itself,** as the other windows do. The resource
  is not renamed `IDI_ICON1`, which would have Qt take it by itself: that
  would change the code that loads resource 1 in the host and in ps2n-remake,
  for the same result.

## Built and checked the same day

docs/app-log.md, "The wizard's icon (ADR 0066)", has the detail.

- **Built in the tree a release is made from** (`build\windows-clang-module`)
  and inside WSL (`build/linux-clang-module`).
- **On the hidden desktop, the new wizard and 0.5's,** each opened from a copy
  of 0.5's runtime:
  - the new one's window has icons of its own, read back from it
    (`WM_GETICON`): PS2Native's emblem at 32 and 16 pixels;
  - 0.5's has none, so Windows shows the one its window class has, the
    default application icon of the maintainer's pictures.

## Consequences

- **The maintainer's PS2Native folders get it** when they are made again from a
  commit that has it: at an update of their folders, or with the next
  release. A game's own windows already had the icon.

## The maintainer's answers

The session put two things to them, each with its recommendation:

1. Commit and push the fix. Recommended: yes.
2. When their PS2Native folder gets the new wizard. Recommended: with the next
   release, since the icon is the only change and an update now would compile
   their eight games again.

Their answers: "1. Yes", "2. Agree". So it was committed and pushed, and their
folders keep their programs until the next release.
