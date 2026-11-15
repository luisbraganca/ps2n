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
