# ADR 0032: The wizard goes through PCSX2's settings

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-17 |
| Decided by | Maintainer; points 2 to 5 on the session's recommendations |

## Context

The brief's wizard has a configuration step: "Present applicable PCSX2
configuration options", "Use PCSX2 defaults wherever possible", "The user
must be able to accept every default without manually configuring
anything" (its section 4, step 3), and a packaged game exposes no settings
interface: "All game configuration is finalized during the compilation
process". The Phase 4 proposal had that step start from the window's
defaults (ADR 0025, steps G14 to G17), and listed no more of it
([phase4-proposal.md](../phase4-proposal.md), Decision 4; ADR 0028).

At the start of step P4.5, the wizard, the maintainer said: "Wait, just to
make sure we're in sync, the wizard should go through all the PCSX2
settings. When I mentioned default settings it's just that I wanted some
default settings to differ from PCSX2: Widescreen/FPS patches applied,
1080x1920 resolution and Vulkan engine. But yes we can now start to work on
it."

The runtime today starts from PCSX2's defaults and the game's own fixes
from PCSX2's database, sets the window's defaults (ADR 0025), and fixes
what PS2Native needs (VU1 on the core's own thread, so that runs repeat;
none of PCSX2's hotkeys; none of what the core draws over the picture,
ADR 0012). Its compiled code has been held to PCSX2 at those settings, and
besides at every EE cycle rate (G17) and in the VUs' clamping modes None
and Normal (the self-test). The session put four questions, each with a
recommendation; the maintainer chose:

1. "Some PCSX2 settings change how the PS2's processors are emulated:
   rounding and clamping modes, speed hacks (cycle skip, wait loops, VU1 on
   its own thread, instant VU1), game fixes. [...] How should the wizard
   treat these settings?" Chosen: "Shown, fixed for now (Recommended)".
2. "PCSX2's on-screen display (FPS counter, messages, indicators): ADR 0012
   decided a finished game shows nothing the emulator core draws over the
   picture. Should the wizard offer these settings?" Chosen: "Keep ADR
   0012: not offered (Recommended)".
3. "Should the wizard reuse PCSX2's own pages inside PS2Native's wizard, or
   have pages of PS2Native's own?" Chosen: "PCSX2's own pages if they fit
   (Recommended)": the session first checks that they build into the
   wizard without changing PCSX2; if not, PS2Native's own pages.
4. "What would not be shown: settings of PCSX2's program rather than of a
   game (its interface, game list, folders, update checks, debugging), and
   for now hotkeys (save states etc., which PS2Native hasn't been made to
   support) and RetroAchievements. Agreed?" Chosen: "Agreed
   (Recommended)".

## Decision

As read:

1. **The wizard's configuration step goes through every setting of
   PCSX2's that applies to a game**: how it is emulated, drawn, heard and
   controlled, its memory cards, its patches and its fixes. Each starts at
   PCSX2's own default, but for the window's defaults (ADR 0025): the
   game's widescreen and frame-rate patches applied where PCSX2 has them,
   with the EE overclock a frame-rate patch asks for (G17); PCSX2's Vulkan
   renderer; a picture of 1920 by 1080, the game drawn at three times the
   console's resolution to fill it ("1080x1920 resolution" read as the
   1920 by 1080 of ADR 0025). Every default can be accepted as it is.
2. **Settings that change how the PS2's processors are emulated are shown,
   fixed** at the value PS2Native follows, each with a line saying why. One
   becomes changeable when PS2Native follows it and it has been checked
   against PCSX2, as the EE overclock was (G17).
3. **ADR 0012 stands**: what the core can draw over the picture is not
   offered.
4. **PCSX2's own settings pages**, its per-game settings, inside
   PS2Native's wizard, if they build into it without changing PCSX2;
   otherwise pages of PS2Native's own.
5. **Not shown**: the settings of PCSX2's program rather than of a game
   (its interface, game list, folders, update checks, debugging); and for
   now its hotkeys (save states and the like, which PS2Native has not been
   made to support) and RetroAchievements.

The settings chosen are fixed in the game's folder when it is packaged, as
the brief has it; the runtime's own (ADR 0011, ADR 0012, VU1's thread)
still apply over them.

## The aspect ratio (2026-11-18)

Making a third game with the wizard, *Gran Turismo 3: A-Spec*, whose patch
file has no widescreen group, the maintainer set its aspect ratio to 16:9
(the game's settings file the wizard wrote: `AspectRatio = 16:9`), and
said: "Also an fyi, this setting on the wizard should always be as
widescreen (16:9) overriding PCSX2's defaults."

Read as: "this setting" is the aspect ratio (Graphics, Display, "Aspect
Ratio"); no picture came with the message, and it is the setting the
maintainer had changed so. Its default, for every game, is "Widescreen
(16:9)" in the place of PCSX2's "Auto Standard (4:3/3:2 Progressive)",
which only a patch that asks for 16:9 makes wide; it can be changed for a
game, as every default can. The films stay at 4:3 (G15). A game drawn at
4:3 with no widescreen patch is shown stretched unless its aspect ratio is
changed in the wizard.
