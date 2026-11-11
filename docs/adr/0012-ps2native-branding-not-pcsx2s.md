# ADR 0012: What PS2Native builds carries PS2Native's branding, not PCSX2's

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-11 |
| Decided by | Maintainer |

## Context

The brief keeps the name, the abbreviation and the existing branding
(section 7), and the assessment lists `assets/branding/` and `ps2n.rc` among
what carries over from `../ps2-uncompiler` (section 10). The reboot had not
brought them in.

PS2Native runs a game on PCSX2's core
([ADR 0002](0002-static-cpus-on-pcsx2-core.md)), and the core comes with
PCSX2's own logo, name and look. Before anything was changed, it was looked
up in PCSX2 `v2.9.170` where those are in what PS2Native builds:

| Where | What is PCSX2's there | Does it reach someone using PS2Native? |
|---|---|---|
| The executables' icon and file description | Nothing. PCSX2's build gives its icon and the description "PCSX2 PS2 Emulator" only to its own Qt frontend (`setup_main_executable` in `pcsx2/CMakeLists.txt`) | No. `ps2n-host.exe` and `ps2n-recomp.exe` had the system's default icon |
| The `resources` directory beside the host, copied whole from PCSX2 | Three of its 115 files hold the logo: `icons/AppBanner.svg`, `icons/AppIconLarge.ico`, `icons/AppIconLarge.png` | As files on disk only |
| PCSX2's fullscreen interface: start page, pause menu, settings pages, "About PCSX2" (`pcsx2/ImGui/FullscreenUI*.cpp`) | The logo files above, the name written into the code, its colour themes | No. It opens when a frontend asks for it, on a hotkey, or for the achievements. The host does not ask, has no hotkeys and leaves the achievements off. The brief rules this interface out of a game (section 4) |
| What the core draws over a game's picture: messages, performance figures, indicators (`ImGuiManager.cpp`, `ImGuiOverlays.cpp`) | Their look: the colours are written into the code, the font is Roboto, from PCSX2's resources. One line of the performance figures is "PCSX2" and its version, off unless asked for | No. The host has no window, and the pictures a run saves are read from the GS, without what is drawn over them |
| A window's title and icon | Nothing. The frontend makes the window | No. The host makes none yet |
| Names the core gives the system | "PCSX2" as the application's name for the sound output (`Host/CubebAudioStream.cpp`, `Host/SDLAudioStream.cpp`) and for the graphics driver (`GSDeviceVK.cpp`). A Discord presence, off unless asked for | Not today: the host opens no sound device. The graphics driver sees the name; a person does not |
| Logs, license texts, copyright headers, the documentation | PCSX2's name | Yes, and it should: that is attribution |

## Decision

The maintainer's words: "Let's work on the branding, replace PCSX2 logos and
styling with our own. Should we move @../ps2-uncompiler/assets/ to this
project?"

How they were read:

- **What PS2Native builds shows PS2Native's logo, and PCSX2's logo nowhere.**
  That holds the other way round as well: PS2Native's logo is not put on
  PCSX2's interface.
- **The brand kit comes into this repository, as a copy.** The question was
  answered with yes, and with "copy" for "move": the old project stays
  untouched as an archive (`CLAUDE.md`). That part is the session's reading,
  made without asking again.
- **"Styling" is the look of what someone using PS2Native sees.** None of
  PCSX2's is seen today (the table), so there was nothing to replace yet.
  What there will be once the host has a window was asked separately: "What
  the core draws over the picture", below.
- **Attribution is not branding.** PCSX2's name stays in the logs, the
  license texts, the copyright headers and the documentation, and nothing in
  the submodule is edited. The brief asks for that (section 6: "Preserve
  copyright notices and attribution"), and so does the GPL.

## What was done (2026-11-11)

- `assets/` was copied from the old project byte for byte: 32 files, 4.9 MB,
  compared by checksum. [assets/branding/README.md](../../assets/branding/README.md)
  says which file is for what. It and the comment in `assets/ps2n.rc` were
  changed to describe this project, and the README gained the colours,
  measured from the files.
- On Windows, `ps2n-recomp.exe` and `ps2n-host.exe` carry the icon
  (`assets/ps2n.rc`). Checked by reading the icon back out of both.
- The host's build leaves PCSX2's three logo files out of the resources it
  copies (`host/targets.cmake`): 112 files where PCSX2 has 115. They are
  left out rather than replaced by ours under the same names, because only
  PCSX2's interface draws them, beside the word "PCSX2".
- The repository got a `README.md`, headed by the logo.
- The tests pass as they did before, on the rebuilt host with those three
  files gone: 2 unit tests and 447 tests on the core
  (`scripts\windows\test.ps1`).

## What the core draws over the picture

When the host gets a window (step G4 of the [game log](../game-log.md)), the
core can draw over the game's picture: messages ("State saved", a disc
change), and, when asked, performance figures and indicators. Each of these
can be switched off by a setting. Their colours cannot be set: they are
written into PCSX2's code. The maintainer was asked what a game should show
of them. There were three ways to go:

1. A finished game shows none of them. Settings only, no change to PCSX2.
   It fits the brief: the player "should launch the native game without
   needing to understand PCSX2". While a game is being brought up they can
   stay on, in PCSX2's look, for whoever is developing. This was the
   recommendation.
2. Keep them, in PCSX2's look.
3. Restyle them in PS2Native's colours. That takes a patch to the core: the
   first entry of the patch queue, to be carried over every PCSX2 update,
   and a change to "the machine is unmodified PCSX2"
   ([ADR 0002](0002-static-cpus-on-pcsx2-core.md)).

The maintainer's answer (2026-11-11): "1. Recommended approach." The
question had been put as number 1, with the first way as the
recommendation, so both readings of the "1" give the same answer:

- **A finished game shows nothing of what the core can draw over the
  picture.** It is switched off through the core's settings.
- **PCSX2 is not patched for its look.** The patch queue stays empty.
- **While a game is being brought up, it may be shown**, as PCSX2 draws it.

This is not built yet, because nothing can be seen of it without a window;
it is part of making the window. The settings are in `EmuCore/GS`:
`OsdMessagesPos` and `OsdPerformancePos` set to 0 (none), and the `OsdShow`
switches off. Three of those are on in PCSX2's defaults, which the host
takes over as they are: `OsdShowIndicators`, `OsdShowVideoCapture` and
`OsdShowInputRec`.

What PS2Native itself draws, when it draws anything, takes its colours from
the brand kit.

## Consequences

- The window, when it comes, takes its icon from the executable (resource 1)
  and has a title of PS2Native's.
- When the host gets sound, look at what the system's volume controls call
  the game. The core hands over the name "PCSX2" from its own code, so
  changing it would again take a patch; whether it is shown at all depends
  on the system, and has not been looked at.
- Nothing was done for Linux, and nothing was needed: the icon is a Windows
  resource. A window icon there is part of making the window.
- When the PCSX2 submodule is updated, check the list of logo files in
  `host/targets.cmake` against `bin/resources`: a new or renamed logo file
  would be copied.
- Which of PCSX2's other resources a game needs at all is a packaging
  question (Phase 4), not decided here.
