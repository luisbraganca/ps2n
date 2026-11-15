# ADR 0025: How a game is shown by default, and PCSX2's patches, as steps G14 to G17

| | |
|---|---|
| Status | Accepted; G14 done on 2026-11-14, G15, G16 and G17 on 2026-11-15 |
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
- **G17, the EE overclock a patch asks for** (added on 2026-11-15, at the
  maintainer's answer below, "The EE overclock a frame-rate patch asks
  for"): applied with the patch that asks for it, and time counted as
  PCSX2's recompiler counts it with it.

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
  measurement, whose answer, below, has the host apply it since G17.

## The EE overclock a frame-rate patch asks for (2026-11-15)

With G16 done, as it was put: "Killzone's frame-rate patch says it "might
need EE Overclock (130%)": a PCSX2 setting that lets the main processor do
30% more work per frame. PS2Native doesn't have that setting, so the patch
runs without it, which is also what stock PCSX2 does by default. Result: 41
new pictures a second in the first level, where the patch aims for 50. What
should I do about the overclock?", with measuring first recommended ("Count
the pictures stock PCSX2 shows with its 130% setting, in the same kind of
run as today's. [...] Nothing changes in how PS2Native counts time, and you
decide with the number."), against building it now ("It changes how time
is counted, which is your call") and leaving it. The maintainer's answer:
"Sure apply recommended PCSX2 patches".

How it was read: what a patch recommends is applied with it. Where the
frame-rate patch the host applies says it needs an EE overclock, the host
applies that overclock, PCSX2's EE cycle rate setting (`EECycleRate`) at the
percentage the patch names, as a player of stock PCSX2 who follows the
patch's note would. For the compiled game that means counting time as
PCSX2's recompiler counts it with that setting, where until now it was
counted only as at the default
([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)): a
change to how time is counted, which the answer decides. A comparison then
gives stock PCSX2 the same setting.

What the archive asks for: 866 of its 4,709 patch files say a patch of
theirs might need an EE overclock, 835 of them in a group whose name says
FPS; they name 130% (418 times), 180% (181) or 300% (23), or no figure, in
words that vary ("Might need EE Overclock (130%).", "Might need EE overclock
at 180%.", "Unlocked at 60 FPS. Might need enable EE Overclock to be
stable."). A patch file has no setting for it: PCSX2 reads only the picture's
aspect ratio and its interlacing from one. (PCSX2's own database of game
settings sets the EE cycle rate for 55 games; not for Killzone.)

It is a step of its own, G17, begun with the measurement that was
recommended, which its comparisons need anyway: what stock PCSX2 shows at
130%. Should that be no more than without, it goes back to the maintainer
before anything is built. It was more: over the first 53 seconds of play,
46.2 new pictures a second where 41.0 without the overclock, and 50 in 21
of those seconds where 7 (the game log, "G17"). So the step went on.

What the session decided within G17, for the maintainer to decide
otherwise:

- **Which note asks for it, and how much.** A group's note is its lines in
  the patch file under its name: its description, its comments (no group's
  name in the archive speaks of an overclock; 11 files do before their
  first group, which the host does not read). A line of a
  group the host applies that speaks of an overclock or of the cycle rate
  asks for 130%, 180% or 300% where it names one of them (the first, where
  it names two: "130-180%"), for the setting it gives where it gives one
  ("to 2"), and for 130%, the mildest, where it names none; where lines ask
  for several, for the most. The host sets PCSX2's setting to it (1, 2 or
  3) when it applies the patch, and its report says so.
- **A setting given is kept.** `ps2n-host -ee-cycle-rate` (`game.ps1
  -EeCycleRate`), -3 to 3, sets PCSX2's setting for any run; given, a
  note does not change it, and the report says what the note asked for and
  that it was left out. `-EeCycleRate 0` runs the frame-rate patch as G16
  did.
- **Time is counted as PCSX2's recompiler counts it with the setting**: a
  block's cost (`scaleblockcycles`) and what a flush keeps of it
  (`scaleblockcycles_clear`), at every rate PCSX2 has. The runtime reads the
  setting whenever it throws its record of blocks away, where PCSX2 throws
  its blocks away whenever the setting changes; a patch's setting arrives
  when the game's executable starts, before any of its blocks is formed.
  PCSX2's VU recompiler scales VU0's block cycles by the same setting,
  which PS2Native's VU provider already followed.
- **At the end of a block, a table.** PCSX2's recompiler works a block's
  cost out once, when it forms it. The compiled game first called the
  formula at the end of every block, which at 130% took an eighth of the
  level run; it now reads the cost from a table the runtime makes when it
  reads the setting.
- **Tested at five rates.** The comparison with stock PCSX2 at every sync
  point, for every test program of the `programs` and `sdk` suites, at
  130%, 180%, 300%, 75% and 50% on both sides (`same-as-stock-ee130` and
  the like; 60% is counted by the formula of 50%, with another factor).

Done on 2026-11-15: Killzone's frame-rate patch is applied with 130% in the
window. The level run with both patches and the overclock is the stock run
with them at every one of 103,579,794 sync points, the FPU's sign of a zero
aside, and the interpreted run, and takes 1.00 times stock PCSX2's time
with the same setting, its play 0.98 (the game log, "G17").

## Consequences

- The game log has the steps in its two tables, with their pass criteria.
- What the patches do to the game is compared as everything else is: a
  scripted run with a patch is held to a stock run with the same patch.
- Phase 4's wizard starts from these defaults.
