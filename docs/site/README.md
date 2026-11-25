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
| The phases and their states | `CLAUDE.md`, "Status"; Phase 3's end and Phase 4's start, [ADR 0027](../adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md); Phase 4's design, [ADR 0028](../adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md); Phase 5's start and its first check, PCSX2 2.9.189, [ADR 0053](../adr/0053-ps2native-tried-on-pcsx2s-newest-version-2-9-189.md) and app-log.md, "PCSX2 2.9.189 tried"; then the pace a session keeps and the notice for any new code, [ADR 0055](../adr/0055-a-session-writes-down-the-pace-it-kept-and-the-notice-shows-for-any-new-code.md); every version of the code a game loads compiled, Jak and Daxter's 0.07% and under a millionth, [ADR 0057](../adr/0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md) and app-log.md, "Measured on Jak and Daxter (2026-11-23)" (580,691,693 and 48,071 EE cycles); 0.4, published on 2026-11-25, its four games' ratios and Jak and Daxter's under WSL 2 (0.93, 1.00, 0.97, 0.94; 0.96), [ADR 0059](../adr/0059-0-4-is-released.md) and app-log.md, "0.4 (ADR 0059)", "Made and tried (2026-11-25)" |
| Phase 4's first step: a game's code as a module costs no time that shows, Agent Under Fire as long as built in, every test passing | [app-log.md](../app-log.md), "P4.1" |
| Phase 4's second step: the compiled code needs only a compiler and PS2Native's own headers, four modules, every test passing, Agent Under Fire the stock run at every point compared | [app-log.md](../app-log.md), "P4.2" |
| Phase 4's third step: LLVM's own compiler, a folder of 254 MB, Agent Under Fire compiled from its disc image on a Windows 10 laptop with nothing else installed in about 8 minutes, the same code as the development build's, played at close to the console's pace (within 3% over the session) | [app-log.md](../app-log.md), "P4.3"; [ADR 0030](../adr/0030-the-wizards-compiler-is-llvms-own-release.md) |
| Phase 4's fourth step: a game is a folder that plays it, started from a program named after the game, no console window; Killzone and Agent Under Fire played from their folders at the console's pace; the development build's sessions, the maintainer's among them, played back from the folders the same at every point compared | [app-log.md](../app-log.md), "P4.4"; [ADR 0031](../adr/0031-microsofts-c-runtime-travels-in-the-folder.md) |
| Phase 4's fifth step, done on 2026-11-18: the wizard goes through all of PCSX2's settings for a game, PCSX2's own pages; a game made from its disc image into a folder that plays it, every default accepted, in about five and a half minutes for Agent Under Fire | [app-log.md](../app-log.md), "P4.5" and its row in the table at the top; [ADR 0032](../adr/0032-the-wizard-goes-through-pcsx2s-settings.md); [ADR 0033](../adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md) |
| Phase 4's fifth step: Gran Turismo 3 made by the maintainer on 2026-11-18, its code loaded from its own data; its first session slower than the console, the code interpreted; made again from what that session wrote down, the console's pace | [app-log.md](../app-log.md), "P4.5", "The maintainer's run (2026-11-18)" |
| Phase 4's sixth step, its criterion met on 2026-11-18: a game's folder carries its own compiler; when a session ends the game compiles what it met for the first time by itself, in the background, with a notification when it begins and another when it is done; its next start has it; a notice over the picture while a game runs code met for the first time | [app-log.md](../app-log.md), the table at the top and "P4.6"; [ADR 0028](../adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md); [ADR 0034](../adr/0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md); [ADR 0035](../adr/0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md) |
| Phase 4's eighth step, begun on 2026-11-18: a game made from its disc image in any format PCSX2 plays, PCSX2's patches fetched at the user's word where PCSX2 is not installed, fixed settings edited by hand refused, the licence of everything in the folder; Silent Hill 3 made from a `.chd`, as on a computer without PCSX2, in two and a half minutes (154.7 s) | [app-log.md](../app-log.md), "P4.8" and its row in the table at the top; [ADR 0036](../adr/0036-the-folder-for-others-how-p4-8-is-done.md) |
| Phase 4's eighth step, done on 2026-11-18 on the maintainer's Windows 10 laptop, which has no PCSX2: Silent Hill 3 made there from its `.chd`, PCSX2's patches fetched at the maintainer's word, played from its folder; a game no longer starts while it compiles what a session met, it waits for the compile, saying so, and then plays with it | [app-log.md](../app-log.md), "P4.8", "The maintainer's run (2026-11-18)" and "A game does not play while it compiles (2026-11-18)"; [ADR 0037](../adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md) |
| Phase 4's last step, Linux, begun on 2026-11-19 on a Linux inside the development computer (WSL 2 with Ubuntu 22.04): PS2Native builds there, every one of its 534 tests passes, the compiled code in shared objects of its own; the folder for Linux put together there, with which Silent Hill 3 was made from its disc image and played from its folder, compiling what its first session met by itself; working on Linux is the maintainer's run's to say | [app-log.md](../app-log.md), "P4.7", "Built and tested on Linux (2026-11-19)" and "The wizard, the recompile and the folder on Linux (2026-11-19)"; [ADR 0038](../adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md) |
| The notice over the picture of a game covering the screen, which on Windows showed only at a session's start, now shown whenever it is due | [app-log.md](../app-log.md), "The notice over a full-screen game (2026-11-19)"; [ADR 0039](../adr/0039-the-notice-over-a-full-screen-game-exclusive-full-screen-disallowed.md) |
| Phase 4's last step done on 2026-11-19 with the maintainer's run inside the Linux of the development computer (their Linux laptop not theirs to use): Jak and Daxter made with the Linux wizard and played from its folder, what each session met compiled for the next, the later sessions at about 90% of the console's pace there; every step of Phase 4 done. The Linux platform's line: 0.1 for 64-bit Linux with glibc 2.35 or later (the release's notes), built and tested under WSL 2 (Ubuntu 22.04) only, by the maintainer too | [app-log.md](../app-log.md), "P4.7", "The run inside WSL (2026-11-19)" and "The maintainer's run (2026-11-19)"; [ADR 0041](../adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md) |
| A game no longer compiles in the background: what a session met for the first time compiled at once, in a window of its own that can leave it for later; a game never starts with anything left to compile, compiling it first, in the same window | [app-log.md](../app-log.md), "Compiled in a window of its own (2026-11-19)"; [ADR 0040](../adr/0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md) |
| Phase 4's sixth step: Jak and Daxter played by the maintainer on 2026-11-18 from a folder the wizard made, its code loaded from its data; 39 minutes and then 23 at the console's pace, 0.17% and then 0.04% of the main processor's work interpreted, each session's new code compiled about a minute after it | [app-log.md](../app-log.md), "P4.6", "The maintainer's run with Jak and Daxter (2026-11-18)" |
| Steps G0 to G18 and their states | [game-log.md](../game-log.md), the table at the top |
| 1,003,452 instructions, 22,668 functions | game-log.md, "What is on the disc" |
| 1.8 s, 152.4 MB, 119 s | game-log.md, "G2" |
| 463 places, 5,079 instructions, then none | game-log.md, "G3", "One rebuild is enough" |
| 38,478,822 moments, 3 GB, 4,199 frames, 47,705,744 event tests, 1.11% | game-log.md, "G3", "4,200 frames, with the button press" |
| Download: 0.4, a pre-release published on 2026-11-25; the two files, their sizes as GitHub gives them (110 MB, 138 MB) and their SHA-256; what each runs on, what PS2Native needs, the five steps, the eight games tried, the limits, and how a game made with an earlier version is brought up to date; the hero's button and line, the page's descriptions, the platforms' lines (Jak and Daxter under WSL 2, 0.96 as 0.4 has it, 0.99 as 0.3 had it) and its closing words name the version too | The release's own page on GitHub (tag `v0.4`) and its notes; [ADR 0042](../adr/0042-releases-on-github-beginning-with-0-1.md), [ADR 0059](../adr/0059-0-4-is-released.md); app-log.md, "0.4 (ADR 0059)". The buttons point at the release's files themselves: **a new release changes them**, and the section's title, date and text with them |
| "Compiled, as users get it": the four games' ratios to the time of stock PCSX2 built as its releases are, in a game's folder with the counts it takes itself, each game twice: with the runtime as built before ("Before": 1.07, 1.10, 1.11, 1.07, Killzone's folder without counts: in the first set of turns of 2026-11-21, "Compiled, the runtime as before" over "Stock PCSX2, link-time optimized") and as 0.2 has it ("0.2": 0.98, 1.00, 0.99, 0.92), the seconds in the tips those means; Killzone's 1.00 without counts and why its counts fit its timed session; in the note, stock PCSX2 1.5 to 4% faster built so, the ratios the chart showed on 2026-11-21 (1.04, 1.06, 1.08, 1.05), the seven training runs, a game left out of them from 1.1% less to 1.2% more time (the third set, "a sparse profile taken without this game" against "the sparse profile"), 10,800 and 9,300 frames; the development build's 0.97; the third bar of each game, stock PCSX2 with its VU thread on (0.60, 0.88, 0.98, 0.76: "Stock PCSX2, its VU thread on" over "Stock PCSX2, VU1 on the CPU thread"), the seconds in its tip, and the compiled game as 0.2 has it against it (1.65, 1.13, 1.02, 1.22: "The compiled game as 0.2 has it" over the same); the title, the lead of "Measured" and the note saying which stock PCSX2 every comparison is with, and that PS2Native carries VU1 out on a thread of its own since 2026-11-22, for its next release; the roadmap's "VU1 on a thread of its own", built and measured on 2026-11-22: 0.96, 1.01, 0.98 and 0.95 times the time of stock PCSX2 with its VU thread on, 1.50, 1.13, 1.02 and 1.24 without the thread, a first session 19% to 32% less, 0.99 under WSL 2, the pictures and sounds, Agent Under Fire's run (app-log.md, "VU1 on a thread of its own (ADR 0051)": "Timed on Windows, as a release would have it", "A game's first session, with the thread", "Timed on Linux, under WSL 2", "A game whose run does not repeat with the thread on"); the Linux platform's "about 2.2" (79.52 s as 0.2 has it over 36.13 s) and its 0.99 | [app-log.md](../app-log.md), "PS2Native's first change to PCSX2's code (ADR 0050)": "Timed on Windows (2026-11-22)", "Stock PCSX2 with its VU thread on (2026-11-22)" and "Timed on Linux, under WSL 2 (2026-11-22)" for the third bars and what the note says of them, [ADR 0050](../adr/0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md), [ADR 0051](../adr/0051-the-comparison-with-stock-pcsx2-said-as-it-is-vu1-on-a-thread-of-its-own-next.md); and "Compiled games against PCSX2, as users get them (ADR 0046)": "As the release has it (2026-11-22)" for 0.2's ratios and seconds, "The runtime itself: link-time optimization, and a profile of play (2026-11-21)" for "Before" and the note, "Three minutes of the game's own time (2026-11-22)", and "Where things stand with the two changes kept (2026-11-21)" for the ratios shown before; [ADR 0047](../adr/0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md); game-log.md, "G12", for the 0.97 |
| "A game's first session": Gran Turismo 3's and Jak and Daxter's first sessions, stock PCSX2 built as its releases are (20.8 s, 13.1 s, its three runs each), PS2Native before the speed work (108.8 s, 59.8 s, 31.84% and 31.83% interpreted) and 0.2 (69.6 s, 44.8 s, its three runs each; 36% and 25% less; 77.5 s and 52.5 s on 2026-11-20), 2.2 times as fast as PCSX2's interpreters, 6% more with the BIOS's code other games' folders met | app-log.md, "As the release has it (2026-11-22)", the table of first sessions; "Code met for the first time (2026-11-20)", "The changes, each timed against the runtime before it", "The BIOS's kernel compiled for a game's first session (2026-11-20)", "Where the step stands (2026-11-20)" |
| The six loops, 2026-11-20: their ratios and milliseconds, the best of three, in the build that makes a game's modules, before the speed work's changes; PS2Native's interpreter 1,020.6 ms against PCSX2's 1,363.1 ms for the sum | app-log.md, "Compiled code against PCSX2's recompilers (2026-11-20)", the table of the six loops |
| The tiles: eight games made and played (2026-11-25: the five of 2026-11-19; SSX Tricky, made on 2026-11-23 and played by the maintainer, its session of 2026-11-25 at the console's pace; Resident Evil – Code: Veronica X and Secret Agent Clank, made the same day and played as far as their menus, app-log.md, "The maintainer's three new games made ready (2026-11-23)", and [ADR 0054](../adr/0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md)); 556 tests in the development build on Windows and on Linux, and in the builds that make modules, their runtime built as a release's, 554 on Windows and 553 on Linux with three comparisons of one program not run (2026-11-23, with the thirteen of `tests/programs/overlay.s`, ADR 0057; 543, 541 and 540 from 2026-11-22), and that two of Linux's tests were skipped there until that day (the roadmap's P4.7 says it too); the interpreters 2.2 times as fast as PCSX2's, 7.56 s and 16.66 s | ADR 0042 and the release's notes; app-log.md, "VU1 on a thread of its own (ADR 0051)": "Held to PCSX2's VU recompiler, and the tests (2026-11-22)" and "The tests on Linux, and the self-tests' compiled blocks there (2026-11-22)" (the tests of the four trees, the two skipped), and "The runtime itself: link-time optimization, and a profile of play (2026-11-21)" (the defect of PCSX2's); app-log.md, "Code met for the first time (2026-11-20)" |
| Phase 4's steps P4.1 to P4.8, their dates and what each did; 0.1; the speed work; learning from play, built 2026-11-21: the order of a folder's functions (0.4%), its own counts (8.0%, 3.6%); the step towards PCSX2's time, measured on 2026-11-22 and in 0.2: 1.07 to 1.11 times the time of stock PCSX2 built as its releases are (the 1.04 to 1.08 of the day before were against one built without link-time optimization), 0.98 to 1.00 as 0.2 has it, PS2Native's runtime built the same way and with a profile of play (Killzone 0.92 with its counts), a first session 10 to 15% less (10.2% and 14.6%, against 2026-11-20); on Linux under WSL 2, Jak and Daxter 1.05 and 1.23 before ("Times on Linux, under WSL 2 (2026-11-22)"); 0.2 published on 2026-11-22: a game's folder made with an earlier version brought up to date by the wizard, tried on a folder made by 0.1 as released on Windows and on Linux; a folder counting after 3 minutes of the game's own time; Linux's speed, looked into on 2026-11-22: 1.05 on Jak and Daxter under WSL 2, 1.03 with two causes put right, the rest not found and left for now ("Linux's speed (ADR 0049, ADR 0050)", "Where Linux stands", "The maintainer's answers (2026-11-22, ADR 0050)"); PCSX2's wake of a thread PS2Native does not use left out of PS2Native's builds, its first change to PCSX2's code: Jak and Daxter 27.5% less time under WSL 2 and 9.2% less on Windows ("PS2Native's first change to PCSX2's code (ADR 0050)", "Timed on Windows", "Timed on Linux, under WSL 2"); that PCSX2 is used as released but for PS2Native's changes to its code, one so far and none in 0.2 (the sections "Compile the game. Keep the machine." and "From a disc to native code", and the page's foot): `patches/pcsx2/README.md`, [ADR 0050](../adr/0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md) | `CLAUDE.md`, "Status"; app-log.md, each step's section, "The speed step (ADR 0043)", "The order of a folder's functions, built (2026-11-21)", "The maintainer's answer (2026-11-21)", "A game's folder compiles its game for how it is played, built (2026-11-21)" and "Compiled games against PCSX2, as users get them (ADR 0046)", its "The runtime itself: link-time optimization, and a profile of play (2026-11-21)", "The maintainer's answers (2026-11-22)", "Three minutes of the game's own time (2026-11-22)" and "As the release has it (2026-11-22)", and "0.2: a game's folder brought up to date (ADR 0048)"; [ADR 0043](../adr/0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md), [ADR 0044](../adr/0044-a-games-folder-learns-from-its-users-own-play.md), [ADR 0045](../adr/0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md), [ADR 0046](../adr/0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md), [ADR 0047](../adr/0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md), [ADR 0048](../adr/0048-a-games-folder-of-an-earlier-version-is-brought-up-to-date-by-the-wizard.md), [ADR 0049](../adr/0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md) |
| "One loop, both sides": the generated code of `ps2n-recomp` 0.10.0, its registers in locals (`g2`, `g8`, `g9`, `g10`, `g11`) | `build\windows-clang\ps2n\generated\bench.cpp`, below |
| 1.35 and 0.97 for the run into the first level, its play 0.95 times stock PCSX2's (Phase 3's steps in the roadmap) | game-log.md, the table at the top: G5, G6 ("seven changes"), G7, G8, G9, G10, G11, G12 |
| 1.21 to 1.23 times for the run into the first level | game-log.md, "G6", "What each change gave" |
| G4: "played by the maintainer in a window, with sound and a controller, on Windows" | game-log.md, "G4", "A window, sound and a controller" |
| 59 of 63 test programs | game-log.md, "The timing difference", "What was measured" |
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
| "A few percent from one build to the next" (the six loops) | game-log.md, "G10", "Where it stands (main tree, 2026-11-13)" and "The website's two charts, measured again" |
| The six loops in the proof of concept: 1.47 and 1.15 | poc-log.md, "P4 Measurement" |
| 63 test programs, compiled and interpreted the same | poc-log.md, the table at the top, P1 |
| PCSX2 `v2.9.189` | [ADR 0004](../adr/0004-proof-of-concept-environment.md) (`v2.9.170`), [ADR 0054](../adr/0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md) (`v2.9.189`) |

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
