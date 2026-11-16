# ADR 0026: A second game, Agent Under Fire, and games made of several executables, as step G18

| | |
|---|---|
| Status | Accepted; G18 done on 2026-11-15 |
| Date | 2026-11-15 |
| Decided by | Maintainer, on the session's recommendation |

## Context

With G17 done ([ADR 0025](0025-how-a-game-is-shown-and-its-patches.md)),
the maintainer: "Ok but let me test using agent under fire, it's on the
same directory. Prepare it and do some initial testing to make sure it
works before it's my turn to properly test it."

How that was read: a second game, *007 - Agent Under Fire* (`SLES-50539`),
the maintainer's own copy beside Killzone's (`C:\ps2\agent-under-fire`: the
disc image and the files extracted from it), used as Killzone's is: read
where it is, never copied, everything generated from it in the build tree.
Prepared for a session of the maintainer's: a controller script from power
on into the game, its runs held to stock PCSX2, and a build with what the
runs had to interpret.

What the first runs found (the game log, "G18"):

- **It runs on PS2Native as it is, correctly.** With the script into the
  first mission (9,000 frames), PS2Native, which had nothing of this game
  compiled, is the stock run at every one of 49,981,658 sync points, every
  register included, with the same picture and the same sound state at each
  of the 180 frames compared. PCSX2's database of games sets its VU clamp
  mode to 2, which PS2Native's VU provider follows.
- **It runs at half the console's speed**: everything of its EE code
  interpreted, 25 frames a second in play where the console shows 50.
- **The game is four executables.** The disc's boot executable,
  `SLES_505.39`, is a loader of 18,641 instructions at `01F00000` that loads
  the others itself, through the IOP (`SifLoadElf`) and not the BIOS's
  `LoadExecPS2`: `MOVIE.ELF` (the films, `00104000`), `ACTION.UK` (the
  missions, English; one for each of the six languages, `00120000`) and
  `DRIVING.ELF` (the driving missions, `00104000`), each a program of its
  own (69,000, 579,000 and 444,000 instructions), all three over the same
  addresses. PCSX2 sees the boot executable only: its CRC stays the boot
  executable's, and so the runtime, which chooses an executable's compiled
  code by that CRC, compiles none of the others. What PS2Native had for code
  an executable runs outside its own (an image made from the words runs
  captured) keeps one word for each address, and so cannot hold three
  programs over the same addresses.

## Decision

As it was put: "Agent Under Fire boots and plays on PS2Native (my script
reaches the first mission), but only at half speed (about 25 frames a
second in play), because none of its code is compiled. The reason: the game
is four programs. Its small boot program loads the others itself: the films
(MOVIE.ELF), the missions (ACTION.UK for English) and the driving levels
(DRIVING.ELF), all at overlapping addresses. PS2Native compiles one program
per game today, and PCSX2 never notices this game's switches. What should I
do?", with "Build multi-program support" recommended ("Compile all four
programs ahead of time, and have the runtime tell from memory which one is
loaded. General, not for this game alone; then compile its micro code and
IOP code as for Killzone, check it against stock PCSX2, and hand it over at
full speed. A few hours; Killzone's build comes back with one rebuild."),
against "Test it now as it is" and "Stop here". The maintainer's answer:
"Build multi-program support".

How it was read:

- **A game may be several executables**, the one the console boots and
  those it loads itself. Each is compiled ahead of time as an image of the
  game's (`build.ps1 -GameMore`), tied to the boot executable by its CRC.
- **The runtime tells from memory which of them is loaded.** When the
  machine forms a block where none of the images in use has code that
  matches memory, and one of the game's other executables has a block there
  whose words are in memory (the fingerprints compiled code is checked by
  anyway), that executable's image is put in use, and any other of them
  over the same addresses is taken out of use. Every block is still checked
  against memory before compiled code runs it, as before.
- **What a run interprets of such an executable, and code it finds in
  place of one of its functions, is captured for that executable**, the
  capture file naming it by its CRC, and the next build compiles it into
  that executable's image.
- **General, not for this game alone**: a test program of PS2Native's own
  loads two programs of its own over the same addresses, as the game does,
  and is compiled and checked the same way.
- **For Agent Under Fire**: its four executables, `ACTION.UK` the English
  one, which the session's script chooses (another language's runs
  interpreted); its micro code and IOP code compiled as Killzone's are. A
  build holds one game's executables, so this build holds Agent Under
  Fire's EE code in place of Killzone's, with the micro code and IOP code of
  both; Killzone comes back with one rebuild.
- **It is step G18, inside Phase 3.**

## What the session decided within G18

For the maintainer to decide otherwise:

- **Which is loaded is told at a block's forming.** Whenever a block is
  formed where none of the executables in use has its code in memory, the
  one whose code is there (the words of the segment the address is in,
  compared by the fingerprints compiled code is checked by) is put in use,
  and any other over the same addresses taken out. Nothing is read from the
  game's loader, which is the game's own; and the core never sees the
  change.
- **Tied to the boot executable by its CRC, named after it**: a loaded
  executable's image carries the boot executable's CRC, by which the
  runtime takes note of it at the entry point, and its own, by which the
  captures name it. `ps2n-host -image <id>` takes the images
  `<id>_loads_<name>` with the one it names.
- **A capture keeps them apart**: `[[program]]` blocks with an executable's
  CRC and its entries, `program =` on code found in place of one of its
  functions. A capture file without them reads as before.
- **One game's executables in a build.** The build tree holds Agent Under
  Fire's EE code now, Killzone's micro code and IOP code with its own, and
  the BIOS's code this game needs as well; the profile-guided optimization
  trained on Killzone's code is off for it.
- **English only.** Of the six languages' mission executables, only
  `ACTION.UK` is compiled.

Done on 2026-11-15 (the game log, "G18"): over the script's 9,000 frames
into the first mission, nothing of the game's EE code, micro code or IOP
code interpreted; the stock run at every one of 49,981,658 sync points,
every register included, and with the widescreen patch on both sides at
every one of 50,091,671; the interpreted run at every one of 56,856,341
event tests; 1.19 times stock PCSX2's time, its play 1.12, without
profile-guided optimization; in the window at the console's pace. Every
test passes (533).

## After the maintainer's first session

On 2026-11-16 the maintainer played the game in the window (`game.ps1
-Play -Name aut`, with the three captures): 42,322 frames, about 14
minutes, the first two missions, the second a driving mission
(`DRIVING.ELF`'s), with the progress saved on the memory card. The window
kept 49.58 frames a second on average where the console shows 50. Beyond
where the script stops, the session had to interpret 587 blocks of VU1's
micro code (which ran 1.75 billion times, the 226 compiled ones 1.12
billion), 21 of VU0's, 893 blocks of the IOP's code, 28 places in
`DRIVING.ELF`'s code and the three of its functions that the widescreen
patch's lines for the driving missions rewrite; nothing of the boot
executable, `MOVIE.ELF` or `ACTION.UK` (the game log, "G18").

The maintainer: "Overall the game looks good. There's a part where the game
really slows down (I played the second level as well), this was a problem
on ../ps2-uncompiler as well. There's HUD artifacts specially on the second
mission, I will see through PCSX2 if it also appears there or if there's a
patch." Then: "I confirm the artifacts are also present on PCSX2 and there's
no patch. I remember we fixed most of the artifacts on ps2-uncompiler but
since they're present on PCSX2 I'm wondering if we should waste time on
fixing them for this project."

The session's answer: PS2Native does not draw the picture, PCSX2's renderer
does, and an artifact PCSX2 shows too is the renderer's; the old project
drew with a renderer of its own, so its fixes do not carry over; fixing them
here would mean keeping changes of PS2Native's own to PCSX2's renderer. Put
as "HUD artifacts: leave them to PCSX2, and fix them through a setting only
if one of its upscaling fixes removes them" (recommended), against keeping
"our own changes to PCSX2's renderer"; and, for the session, "The rebuild
and replay ...: should I start now?", yes recommended (the build and the
check against stock PCSX2 first, the timing of the slow part after, once
the computer is otherwise idle). The maintainer's answer: "1. Ok let's
leave them. 2. Yes". And: "Also fyi, I saved progress to memory card, so
you can start easily on second and third missions."

How it was read:

- **What PCSX2's renderer gets wrong too is left to PCSX2.** PS2Native
  neither changes PCSX2's renderer for it nor keeps changes of its own to
  PCSX2's code. Nothing is done about this game's HUD now; should one of
  PCSX2's own settings (its upscaling fixes) be found to remove the
  artifacts, applying that setting for the game is a small step of its own,
  put to the maintainer then.
- **The session is compiled in and checked now**: the build made again
  with what it had to interpret, the session run again without a window
  against stock PCSX2 at every sync point, then timed in stretches on both
  sides with the window's renderer, to find where it slows down and whether
  stock PCSX2 slows down there too.
- **Scripted runs may start from the maintainer's save**, on a copy of the
  memory card, never on the maintainer's own.

With the session compiled in and checked (the game log, "G18", "The
maintainer's first session"), the session asked whether to prepare the
third mission first, a scripted run from a copy of the save into it,
compiled and checked before the maintainer plays it (recommended), or to
let the maintainer play on at once, what is new interpreted the first time.
The maintainer: "run the compiled game so I can test it (load the same
memory card please)". Read as: the maintainer plays on now, in the window,
on the same memory card, the save on it; what the session meets is compiled
after it, as in G13.

## After the maintainer's second session

On 2026-11-16 the maintainer played on from the save: 105,784 frames, 37
minutes, the second mission to the fourth. "The explosion is now acceptable
yes"; and "The 4th mission had a general slowdown". The session had almost
nothing to interpret, and the window kept 48.05 frames a second on average.
Run again on the processor's fast cores, the fourth mission takes PS2Native
1.06 to 1.21 times the console's time and stock PCSX2 0.11 to 0.18: it waits
in a small loop of `ACTION.UK`'s (at `0012A030`, a word of the scratchpad
polled with a count to give up at) whose page is not write-protected, where
PCSX2's recompiler, and PS2Native after it, compares a block's words with
memory at every execution. PCSX2's code for the block does that with a few
comparisons of its own; PS2Native's compiled code asked the runtime, which
looked the block up and decided about it again every time: three quarters of
the mission's time (the game log, "G18").

Put as: "Fix it now (recommended). I'd make the check as cheap as stock's
while keeping exactly the same rules", the time counted as before so that
runs stay the stock run, measured on the fourth mission and held to stock
PCSX2, against playing on as it is. The maintainer: "Fix it".

How it was read: what the runtime tells compiled code about a block that
compiled code cannot keep in its table, and so asks about at every
execution, is kept for as long as it holds, so that what is left at each
execution is what PCSX2's code for the block does: for a block of a page
that is not write-protected, its words compared and the execution counted.
The same goes for the blocks of a function's variant, which ask the same
way. Which block is formed, where, and what it counts stay as they were;
the change is measured on the session, and held to stock PCSX2 and to the
test suite, in a build with what the session had to interpret.

With that done (the fourth mission 0.48 of the console's time at the
median, still three times stock PCSX2's), the session recommended trying the
mission in the window first, and having compiled code compare such a
block's words itself, as PCSX2's code does, only if it still slowed down.
The maintainer: "Yes we really need to fix this, how come our compiled
code is running slower than interpreted code? That doesn't make sense in
any world." How it was read: the comparison is to be made where PCSX2 makes
it, in the block's own code, without a call into the runtime at every
execution, so that the mission comes near stock PCSX2's time; the rules,
and so the time counted, stay as they are. (Stock PCSX2 runs the game on
its recompiler, which compiles a block to machine code when the game first
runs it, with that comparison inside: compiled code too, made at run time.)

Done the same day (the game log, "G18", "Compiled code checks such a block
itself"): the image's table names such a block by a record of the
runtime's, and the label's one call compares the words, counts the
execution and answers, with no lookup and no frame (`checkedAnswer`). The
fourth mission takes 0.27 of the console's time at the median, 1.8 times
stock PCSX2's (from 7.6 at first and 3.2 with the answers kept), still the
stock run in place and time at every sync point. The comparison inside
every label's code, which would take the call away, was not made: it
would cost megabytes of code.
