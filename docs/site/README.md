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
| The phases and their states | `CLAUDE.md`, "Status" |
| Steps G0 to G10 and their states | [game-log.md](../game-log.md), the table at the top |
| 1,003,452 instructions, 22,668 functions | game-log.md, "What is on the disc" |
| 1.8 s, 152.4 MB, 119 s | game-log.md, "G2" |
| 463 places, 5,079 instructions, then none | game-log.md, "G3", "One rebuild is enough" |
| 38,478,822 moments, 3 GB, 4,199 frames, 47,705,744 event tests, 1.11% | game-log.md, "G3", "4,200 frames, with the button press" |
| 22.9 s and 25.7 s, 11.2 s and 15.1 s, and the two runs of each | game-log.md, "G3" and "Open" |
| 100 s and 74 s for the run into the first level; "twice as long" for the game's own code | game-log.md, "G5", "The level run, timed" and "Where the time goes" |
| 91 s, 1.21 to 1.23 times and 1.35 for the run into the first level; 41 s and 26 s for the game's own code; "more than a third" reaching memory; "seven changes" | game-log.md, "G6", "What each change gave" and "Where the time is now" |
| G4: "played by the maintainer in a window, with sound and a controller, on Windows" | game-log.md, "G4", "A window, sound and a controller" |
| 59 of 63 test programs | game-log.md, "The timing difference", "What was measured" |
| 453 tests, 42 cases, 153 s | game-log.md, "G4", "The compiled game against stock PCSX2" |
| The longer run: three and a half minutes of the first level, 93,221,937 moments, 1,750 pictures compared | game-log.md, "G4", "The compiled game against stock PCSX2" |
| 51 instructions interpreted in the first 4,200 frames, at boot; the BIOS kernel "Static"; G8's 1.18 times | game-log.md, "G8": "The game's code in kernel memory, compiled with the game", "What is still interpreted, and why", "The time" |
| G10's 1.07 times | game-log.md, "G10", "Loops that have reached a fixed point" |
| 40 of 55, 39, 38, and "on no line" | [ADR 0005](../adr/0005-never-further-from-the-console-than-pcsx2.md) |
| 204 instructions, 4,077,475 and 36,976,118 cases | [poc-log.md](../poc-log.md), "Increment 5" |
| The six loops: the ratios, the milliseconds, "8 to 23 times" | poc-log.md, "P4 Measurement" |
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
