# ADR 0025: How a game is shown by default, and PCSX2's patches, as steps G14 to G16

| | |
|---|---|
| Status | Accepted; G14 done on 2026-11-14, G15 and G16 on 2026-11-15 |
| Date | 2026-11-14 |
| Decided by | Maintainer, on the session's recommendations |

## Context

On 2026-11-14 the maintainer played the first session of step G13
([ADR 0024](0024-further-into-the-game-before-phase-3-ends.md)) and said of
it: "The game looks good, considering it's using software render, no FPS
patch, no widescreen patch and no upscale." The session's log showed
PCSX2's Direct3D 12 renderer, which PCSX2 chooses for this computer when
the window does not say, at the console's own resolution; and no patch
could be applied, PCSX2's archive of patches (`patches.zip`) not being
among the host's resources.

The session's report put two decisions about it. As they were put:

"**Upscaling next time?** I recommend 3× (about 1080p). It only changes
how the picture is drawn, not the game's code. Alt+Enter gives full
screen."

"**Widescreen and 60 fps patches?** I recommend later, as a step of their
own, not inside this one. This build doesn't have PCSX2's patches at all:
`patches.zip` isn't among the host's files. A patch rewrites parts of the
game's code or data. The compiled code notices changed code and interprets
it until a rebuild compiles the patched version, so it can work. Every
comparison would then need the same patch on the stock side, and a 60 fps
patch changes the timing of everything. Fixing settings like these when a
game is built is Phase 4's subject. If you'd like to see widescreen sooner,
I can try it alone as an experiment."

## Decision

The maintainer's words:

"2. Yes and maybe let's focus more on Vulkan? Later when we have our wizard
(UI) the resolution and renderer can still be changed but I want the
defaults to be:
- 1920x1080
- Vulkan
- Automatically apply Widescreen/FPS patches if available.
3. Sure let's split into several steps."

How they were read:

- **These are the defaults a game is shown with**: now, in a session in
  the window (`game.ps1 -Play`, `ps2n-host -window`); later, in the
  wizard of Phase 4, which can change them.
- **Vulkan is the renderer PS2Native shows a game with, and checks it
  on**: the window's default, and the renderer of the window checks. It is
  also the one renderer PCSX2 has on both platforms ADR 0001 requires. The
  comparisons keep PCSX2's software renderer, which gives the same picture
  on every machine; a hardware renderer's picture depends on the graphics
  card and its driver.
- **1920x1080 is the picture's size, and about the resolution the game is
  drawn at**: the window's picture 1920 by 1080, or as large as the screen
  allows at those proportions, the console's picture drawn at three times
  its resolution (PCSX2's "about 1080p"). Until a widescreen patch makes
  the picture 16:9, a 4:3 picture is shown in the middle of it.
- **A game's widescreen and frame-rate patches are applied when PCSX2 has
  them for that game**, by default. "If available" is read as: for the
  game being played, in PCSX2's database of patches.
- **The work is split into steps** (below), each with its pass criterion,
  measured and checked before the next, inside Phase 3.

## The steps

The session proposes them; the maintainer may order or cut them otherwise.

- **G14, how a game is shown:** the defaults above for the window, Vulkan
  and 1920x1080 at three times the console's resolution, each still
  changeable (`game.ps1 -Renderer`, `-Upscale`). Checked in the window, by
  machine and with the sound off.
- **G15, PCSX2's patches and the widescreen patch:** PCSX2's patches made
  available to the host; a game's widescreen patch applied by default when
  there is one, in the window and in a scripted run that asks for it, the
  stock run of a comparison alike; a patch that changes the game's code
  met as code nothing was compiled from (interpreted, reported, captured,
  compiled by the next build). Where the patches come from is put to the
  maintainer first: PCSX2's archive is made from a repository of its own,
  so it is a new dependency (`CLAUDE.md`: ask before installing software).
- **G16, the frame-rate patch:** the same for a game's frame-rate patch,
  when there is one, with its speed measured: the game then does more work
  every second.

## Full screen (2026-11-14)

A picture of 1920 by 1080 does not fit in a window on a screen of that
size, which is the maintainer's: the taskbar and the window's frame take
some of it. Asked, as it was put: "Your main screen is 1920x1080, so a
1920x1080 picture only fits full screen. When you start the game, should
it open full screen (Alt+Enter switches to a window), or in the largest
window that fits (about 1780x1000)?", with full screen recommended ("On a
larger screen it opens in a 1920x1080 window. My muted machine checks keep
using a small window so they don't cover your screen."). The maintainer's
answer: "Full screen (Recommended)". So a window that would not fit on its
screen covers the screen instead, and Alt+Enter or F11 gives the largest
window that fits.

## Where the patches come from (2026-11-15)

Before G15, the session looked by reading only. The PCSX2 installed on the
maintainer's computer (2.8.2) has an archive of patches with Killzone's
file in it (`SCES-52004_6624A78C.pnach`): its widescreen patch writes one
byte of the game's data, a widescreen mode the game has, and asks for a
16:9 picture; its frame-rate patch ("50/60 FPS", whose note says it "might
need EE Overclock (130%)") changes an instruction of the game's code and a
value of its data, conditionally, every frame. The repository PCSX2 makes
the archive from (`pcsx2_patches`) declares no license.

As it was put: "For the widescreen and frame-rate patches (G15, G16):
where should PS2Native take PCSX2's patches from? Killzone's are in the
archive of the PCSX2 2.8.2 installed on this PC. The repository PCSX2
makes that archive from declares no license.", with reading the installed
PCSX2's archive in place recommended ("nothing downloaded, nothing copied
into the project. Where a packaged game gets its patches stays a Phase 4
question."), against downloading a pinned release of it into the build's
dependencies. The maintainer's answer: "The installed PCSX2
(Recommended)".

So the host reads the patches from the installed PCSX2's archive where it
is, as it reads the BIOS. Where a packaged game gets its patches is left to
Phase 4.

That could not be done without a copy, and the maintainer was asked again.
PCSX2 reads patches only from an archive in its own resources folder or
from loose files in its data folder; a hard link to a file under Program
Files is refused to a user ("Access is denied"), and a symbolic link needs
administrator rights or Windows' Developer Mode. As it was put: "PCSX2
only reads patches from its own resources folder or from loose patch files
in its data folder, and Windows won't let me link to the installed archive
without admin rights or Developer Mode. How should the host get Killzone's
patches from the installed PCSX2?", with copying the game's one file per
run recommended, against turning on Developer Mode or copying the whole
archive at build time. The maintainer asked first: "Only devs will need
developer mode, correct?"; told that only a machine that builds PS2Native
that way would, never a player's, and that the copy needs it nowhere, the
answer: "Copy one file per run (Recommended)". So when the game has booted
the host copies the game's own patch file (774 bytes for Killzone) out of
the installed archive into the run's data folder, in the build tree, where
PCSX2 looks for patch files; nothing goes into the repository.

## Films at 4:3 (2026-11-15)

With the widescreen patch on, Killzone's films, made for 4:3, were stretched
across the 16:9 picture, which is how PCSX2 shows films by default when a
widescreen patch is on. As it was put: "With the widescreen patch on, the
game's films (made for 4:3) are stretched across the 16:9 picture. Show
them at 4:3 in the middle instead?", with 4:3 recommended. The maintainer's
answer: "Films at 4:3 (Recommended)". So with the widescreen patch the host
sets PCSX2's aspect ratio for films to 4:3 (`FMVAspectRatioSwitch`; PCSX2
tells a film by the IPU decoding video), which changes only how the picture
is shown.

## The frame-rate patch (2026-11-15)

What the session decided within G16, for the maintainer to decide
otherwise:

- **Which group is a frame-rate patch.** PCSX2 knows a game's widescreen
  patch by a name of its own ("Widescreen 16:9", its widescreen setting),
  but has no such name for a frame-rate patch: the files call theirs "50/60
  FPS", "60 FPS" and the like. Of the 4,709 patch files in the installed
  archive, 948 have a group whose name has "FPS" in it; 940 have one such
  group, mostly "60 FPS" (338), "50 FPS" (325) or "50/60 FPS" (264), and 8
  have several, which are choices rather than one patch: "60 FPS" beside
  "50 FPS" or "30 FPS", beside "Onscreen FPS Display", "Slow Motion (60
  fps)" or "NTSC Mode/60 FPS". So the host applies the group whose name has
  "FPS" in it where a file has one, by PCSX2's list of enabled patches,
  once it has copied the file; where it has several, none, and its report
  names them.
- **Without PCSX2's EE overclock.** Killzone's frame-rate patch says it
  "might need EE Overclock (130%)": a setting of PCSX2's
  (`EECycleRate`) by which its recompiler counts every block of more than
  five cycles as 1.3 times shorter (`scaleblockcycles` in `iR5900.cpp`), so
  that the EE does more work in a frame. PS2Native does not implement it:
  time is counted as PCSX2's recompiler counts it at its default
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)), and
  the host warns when the setting is given. Implementing it changes how
  time is counted, which is the maintainer's to decide
  ([ADR 0015](0015-a-speed-step-before-phase-3-ends.md)). So the patch is
  applied without it, as stock PCSX2 applies it with its default settings:
  in the first level the game shows 41 new pictures a second, where it
  showed 25 (the game log, "G16"). Put to the maintainer with that
  measurement.

## Consequences

- The game log has the steps in its two tables, with their pass criteria.
- What the patches do to the game is compared as everything else is: a
  scripted run with a patch is held to a stock run with the same patch.
- Phase 4's wizard starts from these defaults.
