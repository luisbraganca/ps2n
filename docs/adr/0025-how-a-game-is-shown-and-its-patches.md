# ADR 0025: How a game is shown by default, and PCSX2's patches, as steps G14 to G16

| | |
|---|---|
| Status | Accepted; not begun |
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

## Consequences

- The game log has the steps in its two tables, with their pass criteria.
- What the patches do to the game is compared as everything else is: a
  scripted run with a patch is held to a stock run with the same patch.
- Phase 4's wizard starts from these defaults.
