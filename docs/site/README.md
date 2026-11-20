# The website

PS2Native's website is one page, at <https://luisbraganca.github.io/ps2n/>.
GitHub Pages serves it from this repository's `docs` folder
([ADR 0014](../adr/0014-the-website-is-served-from-docs.md)), which is also
where the project's documentation is. This file says which is which, where
the page's numbers come from, and how to look at the page before it is
pushed.

## What belongs to the site

| In `docs/` | What it is |
|---|---|
| `index.html` | The page |
| `404.html` | What Pages shows for an address that holds no file. It stands alone: its style is inside it |
| `.nojekyll` | An empty file. It tells Pages to serve the folder as it is, without running Jekyll over it |
| `site/site.css`, `site/site.js` | The page's style and its script |
| `site/brand/` | Copies of six files of the brand kit (below) |
| `site/README.md` | This file |

Everything else in `docs/` is documentation: the brief, the assessment, the
logs, the execution model and `adr/`. It is read on GitHub, and the page
links to it there. Pages publishes those files too, as the text they are;
nothing links to them on the site.

Do not move or rename the documentation for the site's sake. Do not delete
`.nojekyll`: without it Pages builds the folder with Jekyll, which turns
every Markdown file into a page of the site and reads `{{` and `{%` in them
as its own template language.

There is no build step and no dependency. The page loads nothing from
another host: no fonts, no libraries, no analytics.

## Looking at it

```powershell
scripts\windows\site.ps1 -Open
```

serves `docs` at `http://localhost:8377/ps2n/` as Pages does, the page for a
missing address included. Elsewhere any static file server over `docs` does.
Opening `docs/index.html` as a file works too, except that the logo does not
put itself together and `404.html` finds nothing.

The site goes live when `main` is pushed: Pages publishes the folder a minute
or so later. Until then nothing of it is public.

## Where the page's numbers come from

Every number on the page was measured, and the page gives the date. They are
copies: **when a log changes, change the page**, and the date at the foot of
the page with it. A number that has no line in a log does not go on the
page.

| On the page | From |
|---|---|
| The phases and their states | `CLAUDE.md`, "Status"; Phase 3's end and Phase 4's start, [ADR 0027](../adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md); Phase 4's design, [ADR 0028](../adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md) |
| Phase 4's first step: a game's code as a module costs no time that shows, Agent Under Fire as long as built in, every test passing | [app-log.md](../app-log.md), "P4.1" |
| Phase 4's second step: the compiled code needs only a compiler and PS2Native's own headers, four modules, every test passing, Agent Under Fire the stock run at every point compared | [app-log.md](../app-log.md), "P4.2" |
| Phase 4's third step: LLVM's own compiler, a folder of 254 MB, Agent Under Fire compiled from its disc image on a Windows 10 laptop with nothing else installed in about 8 minutes, the same code as the development build's, played at close to the console's pace (within 3% over the session) | [app-log.md](../app-log.md), "P4.3"; [ADR 0030](../adr/0030-the-wizards-compiler-is-llvms-own-release.md) |
| Phase 4's fourth step: a game is a folder that plays it, started from a program named after the game, no console window; Killzone and Agent Under Fire played from their folders at the console's pace; the development build's sessions, the maintainer's among them, played back from the folders the same at every point compared | [app-log.md](../app-log.md), "P4.4"; [ADR 0031](../adr/0031-microsofts-c-runtime-travels-in-the-folder.md) |
| Phase 4's fifth step, done on 2026-11-18: the wizard goes through all of PCSX2's settings for a game, PCSX2's own pages; a game made from its disc image into a folder that plays it, every default accepted, in about five and a half minutes for Agent Under Fire | [app-log.md](../app-log.md), "P4.5" and its row in the table at the top; [ADR 0032](../adr/0032-the-wizard-goes-through-pcsx2s-settings.md); [ADR 0033](../adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md) |
| Phase 4's fifth step: Gran Turismo 3 made by the maintainer on 2026-11-18, its code loaded from its own data; its first session slower than the console, the code interpreted; made again from what that session wrote down, the console's pace | [app-log.md](../app-log.md), "P4.5", "The maintainer's run (2026-11-18)" |
| Phase 4's sixth step, its criterion met on 2026-11-18: a game's folder carries its own compiler; when a session ends the game compiles what it met for the first time by itself, in the background, with a notification when it begins and another when it is done; its next start has it; a notice over the picture while a game runs code met for the first time | [app-log.md](../app-log.md), the table at the top and "P4.6"; [ADR 0028](../adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md); [ADR 0034](../adr/0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md); [ADR 0035](../adr/0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md) |
| Phase 4's eighth step, begun on 2026-11-18: a game made from its disc image in any format PCSX2 plays, PCSX2's patches fetched at the user's word where PCSX2 is not installed, fixed settings edited by hand refused, the licence of everything in the folder; Silent Hill 3 made from a `.chd`, as on a computer without PCSX2, in two and a half minutes (154.7 s) | [app-log.md](../app-log.md), "P4.8" and its row in the table at the top; [ADR 0036](../adr/0036-the-folder-for-others-how-p4-8-is-done.md) |
| Phase 4's eighth step, done on 2026-11-18 on the maintainer's Windows 10 laptop, which has no PCSX2: Silent Hill 3 made there from its `.chd`, PCSX2's patches fetched at the maintainer's word, played from its folder; a game no longer starts while it compiles what a session met, it waits for the compile, saying so, and then plays with it | [app-log.md](../app-log.md), "P4.8", "The maintainer's run (2026-11-18)" and "A game does not play while it compiles (2026-11-18)"; [ADR 0037](../adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md) |
| Phase 4's last step, Linux, begun on 2026-11-19 on a Linux inside the development computer (WSL 2 with Ubuntu 22.04): PS2Native builds there, every one of its 534 tests passes, the compiled code in shared objects of its own; the folder for Linux put together there, with which Silent Hill 3 was made from its disc image and played from its folder, compiling what its first session met by itself; working on Linux is the maintainer's run's to say. The Linux platform's line: under way, built and tested on a Linux inside the development computer, a game made and played there with the folder for Linux | [app-log.md](../app-log.md), "P4.7", "Built and tested on Linux (2026-11-19)" and "The wizard, the recompile and the folder on Linux (2026-11-19)"; [ADR 0038](../adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md) |
| The notice over the picture of a game covering the screen, which on Windows showed only at a session's start, now shown whenever it is due | [app-log.md](../app-log.md), "The notice over a full-screen game (2026-11-19)"; [ADR 0039](../adr/0039-the-notice-over-a-full-screen-game-exclusive-full-screen-disallowed.md) |
| Phase 4's last step done on 2026-11-19 with the maintainer's run inside the Linux of the development computer (their Linux laptop not theirs to use): Jak and Daxter made with the Linux wizard and played from its folder, what each session met compiled for the next, the later sessions at about 90% of the console's pace there; every step of Phase 4 done. The Linux platform's line: tested under WSL 2 (Ubuntu 22.04) only, by the maintainer too | [app-log.md](../app-log.md), "P4.7", "The run inside WSL (2026-11-19)" and "The maintainer's run (2026-11-19)"; [ADR 0041](../adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md) |
| A game no longer compiles in the background: what a session met for the first time compiled at once, in a window of its own that can leave it for later; a game never starts with anything left to compile, compiling it first, in the same window | [app-log.md](../app-log.md), "Compiled in a window of its own (2026-11-19)"; [ADR 0040](../adr/0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md) |
| Phase 4's sixth step: Jak and Daxter played by the maintainer on 2026-11-18 from a folder the wizard made, its code loaded from its data; 39 minutes and then 23 at the console's pace, 0.17% and then 0.04% of the main processor's work interpreted, each session's new code compiled about a minute after it | [app-log.md](../app-log.md), "P4.6", "The maintainer's run with Jak and Daxter (2026-11-18)" |
| Steps G0 to G18 and their states | [game-log.md](../game-log.md), the table at the top |
| 1,003,452 instructions, 22,668 functions | game-log.md, "What is on the disc" |
| 1.8 s, 152.4 MB, 119 s | game-log.md, "G2" |
| 463 places, 5,079 instructions, then none | game-log.md, "G3", "One rebuild is enough" |
| 38,478,822 moments, 3 GB, 4,199 frames, 47,705,744 event tests, 1.11% | game-log.md, "G3", "4,200 frames, with the button press" |
| The first chart: 22.7 s and 23.4 s, 11.3 s and 12.3 s, the three runs of each, 1.03 and 1.09 | game-log.md, "G10", "Where it stands (main tree, 2026-11-13)" |
| The first chart's note, 2026-11-20: the development build with profile-guided optimization; a game's folder, four games' sessions run again, 1.07 to 1.24 and then 1.06 to 1.19 times stock PCSX2's time; a profile of another session, Jak and Daxter 1.19 to 1.11 | [app-log.md](../app-log.md), "The speed step (ADR 0043)", "Where the step stands (2026-11-20)" |
| Phase 4: 0.1 published on GitHub by the maintainer on 2026-11-19, a pre-release for Windows and Linux; the speed step, 1.06 to 1.19 from 1.07 to 1.24, Gran Turismo 3's first session 29% faster and 6% more with the BIOS's code other games' folders met | app-log.md, the line on 0.1 under "P4.7" and "The speed step (ADR 0043)"; [ADR 0042](../adr/0042-releases-on-github-beginning-with-0-1.md), [ADR 0043](../adr/0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md) |
| 25.7 s and 15.1 s, 1.12 and 1.35 for the first 4,200 frames on 2026-11-11 | game-log.md, "G3", "4,200 frames, with the button press" |
| 1.35 and 0.97 for the run into the first level, its play 0.95 times stock PCSX2's; the changes named in the first chart's note | game-log.md, the table at the top: G5, G6 ("seven changes"), G7, G8, G9, G10, G11, G12 |
| 1.21 to 1.23 times for the run into the first level | game-log.md, "G6", "What each change gave" |
| G4: "played by the maintainer in a window, with sound and a controller, on Windows" | game-log.md, "G4", "A window, sound and a controller" |
| 59 of 63 test programs | game-log.md, "The timing difference", "What was measured" |
| 453 tests, 42 cases, 153 s | game-log.md, "G4", "The compiled game against stock PCSX2" |
| The longer run: three and a half minutes of the first level, 93,221,937 moments, 1,750 pictures compared | game-log.md, "G4", "The compiled game against stock PCSX2" |
| 51 instructions interpreted in the first 4,200 frames, at boot; the BIOS kernel "Static"; G8's 1.18 times | game-log.md, "G8": "The game's code in kernel memory, compiled with the game", "What is still interpreted, and why", "The time" |
| G10's 1.01 times, its play as long as stock PCSX2's | game-log.md, "G10", "The FPU's arithmetic on SSE registers (main tree, 2026-11-13)" |
| G11: 6,088 programs | game-log.md, "G11", "What the vector units run, measured" |
| G11: 589 different blocks | game-log.md, "G11", "The translator: a block's plan, made once" |
| G11: 20,000 random programs; nothing interpreted in the run into the first level, none on PCSX2's recompiler; still the stock run; 0.97 and 0.95 times stock PCSX2's time | game-log.md, "G11", "Where G11 stands" |
| "What is static": the vector units' micro programs static | game-log.md, "G11", "Where G11 stands"; [ADR 0003](../adr/0003-no-jit-in-generated-games.md) |
| "What is static": the IOP's modules static, no stand-in in use; the machine's socket for both processors | game-log.md, "G12", "Where G12 stands", and "Open", "No stand-in is left"; ADR 0003 |
| G12: 8,023 blocks; 4,000 random programs; nothing interpreted in the run into the first level, none on PCSX2's recompiler; still the stock run, the second processor's registers included; the times against stock PCSX2's | game-log.md, "G12": "The IOP's code compiled ahead of time", "The self-test", "Where G12 stands" |
| G13: the first session's 26 blocks of micro code and 791 of the second processor's code, most of them in the memory card's modules; sessions written down and run again against stock PCSX2 | game-log.md, "G13", "The first session" and "Sessions written down, and run again" |
| G14: Vulkan, three times the console's resolution, a picture of 1920 by 1080, full screen where a window that size does not fit | game-log.md, "G14" |
| G15: the widescreen patch from the installed PCSX2, applied by default; Killzone in 16:9, drawn wide by the game itself | game-log.md, "G15" |
| G16: the frame-rate patch applied by default; 41 new pictures a second in the first level where 25; the console's pace in the window; the code the patch changes compiled ahead of time; 1.00 times stock PCSX2's time with the same patch | game-log.md, "G16" |
| G17: the EE overclock a patch asks for applied with it, time counted as PCSX2 counts it at that setting; 46 new pictures a second in the first level where 41; still the stock run; 1.00 times stock PCSX2's time with the same setting | game-log.md, "G17" |
| G18: Agent Under Fire, four programs that load one another, all compiled, the one in memory told by its code; nothing interpreted into its first mission; still the stock run; the console's pace in the window | game-log.md, "G18" |
| G18: the maintainer's first five missions; the fourth mission's wait loop, 7.6 and then 1.8 times stock PCSX2's time, 0.27 of the console's | game-log.md, "G18", "The maintainer's second session" and "Compiled code checks such a block itself" |
| G13: ended with the phase, Killzone into its first level and Agent Under Fire's first five missions, the sessions run again still the stock run | game-log.md, the table at the top (G13) and "G18" |
| The longer run: "the second processor's place and registers compared too since 2026-11-14" | game-log.md, "G12", "The IOP's place and registers held to the stock run" and "Where G12 stands" |
| 40 of 55, 39, 38, and "on no line" | [ADR 0005](../adr/0005-never-further-from-the-console-than-pcsx2.md) |
| 204 instructions, 4,077,475 and 36,976,118 cases | [poc-log.md](../poc-log.md), "Increment 5" |
| The six loops: the ratios and the milliseconds, "a few percent from one build to the next" | game-log.md, "G10", "Where it stands (main tree, 2026-11-13)" and "The website's two charts, measured again" |
| The six loops in the proof of concept: 1.47 and 1.15 | poc-log.md, "P4 Measurement" |
| 63 test programs, compiled and interpreted the same | poc-log.md, the table at the top, P1 |
| PCSX2 `v2.9.170` | [ADR 0004](../adr/0004-proof-of-concept-environment.md) |

The code under "One loop, both sides" is real. The left side is what
`ps2n-recomp disasm <bench.elf> --at 0x001000CC --count 7` prints for
`tests/programs/bench.s`; the right side is the same block in
`build\windows-clang\ps2n\generated\bench.cpp` (`fn_001000C0`), without the
comment at the end of each line. If the recompiler comes to write that block
differently, the page should show what it writes then. It is from the
project's own test program: nothing on the site comes from a game or from a
BIOS, and nothing may.

The two moving pictures that stand for something (the squares that cross
from the instructions to the C++, and the two lanes under "The same run")
are illustrations, and the lanes say so.

## The brand

The logo and the icons in `site/brand/` are copies, byte for byte, of files
in `assets/branding` (Pages cannot reach outside `docs`):
`logos/ps2native-horizontal-dark.png`, `icons/favicon.ico`,
`icons/ps2native-icon-blue-64.png`, `-128.png` and `-180.png`, and
`social/ps2native-social-1280x640.png`. If the kit changes, copy them again.
The page follows the kit's README: the dark logo on navy, never narrower
than 280 px, the emblem alone where there is less room, and the kit's
colours. PCSX2's logo is nowhere on it
([ADR 0012](../adr/0012-ps2native-branding-not-pcsx2s.md)).

The squares that fly together into the logo are sampled from the logo's own
picture when the page loads, so they make the logo and not a drawing of it.

## Motion

Everything that moves is behind `html[data-motion="on"]`. The first script
in `index.html` sets it to `off` for a reader whose system asks for reduced
motion, and the switch in the page's footer sets it either way and remembers
the choice. With it off, and with JavaScript off, the page shows all of its
content and nothing moves. Keep it that way: an animation never holds
content back.

## If the site moves

Its address is written in three places: `<link rel="canonical">` and the
`og:` tags in `index.html`, `<base>` in `404.html`, and this file. A custom
domain, or another name for the repository, changes all three, and the
default of `-Path` in `scripts\windows\site.ps1`.
