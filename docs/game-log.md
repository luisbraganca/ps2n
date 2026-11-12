# Game log: Killzone

What Phase 3 has done and measured with the first real game, step by step
([ADR 0009](adr/0009-phase-3-first-game-killzone.md)). Every number here
came from a run on the date given; nothing is estimated.

The game is the maintainer's own copy of the European release
(`SCES-52004`), used where it is: `C:\ps2\killzone` holds the disc image and
the files extracted from it. Nothing from the game is in this repository,
and nothing generated from it: this log holds numbers, addresses and words.
The run files, pictures included, are under the git-ignored build tree.

| Step | State |
|---|---|
| G0 Baseline | **Done as far as the intro film** (2026-11-11): the game boots from the disc image under stock PCSX2 in `ps2n-host`, takes a scripted button press and plays its intro film. Runs repeat. Not yet: the title screen and beyond; the maintainer looking at the pictures. |
| G1 The interpreter as reference | **Done for the first 4,200 frames** (2026-11-11): with everything interpreted the run is the stock run. Same console output, same picture at every frame, and the same place and cycle count at every one of the 38.5 million moments the two can be compared; the registers as well, by way of the compiled run (G3). |
| G2 The game's code compiled | **Done** (2026-11-11): the boot executable builds into the host, 1,003,452 instructions in 64 files. |
| G3 The compiled game | **Done for the first 4,200 frames** (2026-11-11): the compiled run is the stock run at every one of those 38.5 million moments, registers included, with the same picture at every frame; it is the interpreted run at every event test; none of the executable's code is interpreted; no code outside the executable has run. |
| G4 Further into the game | **Done as far as the first level** (2026-11-12): by script, and played by the maintainer. By script (2026-11-11): with the same input the compiled game is where stock PCSX2 is, at the same cycle count, at every one of 93 million sync points over 17,500 frames, and shows the same picture at each of the 1,750 frames compared: menus, three and a half minutes of play, being killed, Retry. What differs is listed, and it is one thing: the sign of a zero in the FPU's registers, where the console is followed on purpose. The general registers, COP0's and VU0's are the stock run's at every sync point, and the sound hardware is in the stock run's state at each of 350 moments (2026-11-12). **It can be played** (2026-11-11, Windows): a window at the console's speed, with sound, the keyboard and a controller, checked by machine with the sound off. The maintainer played it into the first level on 2026-11-12: "Everything looked good." Not yet: the rest of the game. |
| G5 Measurement and report | **Done as far as the first level** (2026-11-12): times repeat now (a run is kept on the processor's fast cores). Over the level run the compiled game takes 1.35 times as long as stock PCSX2: 100 s against 74 s. The whole difference is in the game's own code, which takes twice as long compiled as under PCSX2's recompiler; the rest of the machine takes the same time. A first look says why: the generated code keeps the registers in memory and checks every address twice. The report on compatibility, issues and limitations is at the end of "G5". |

## The steps and their pass criteria

| Step | Work | Pass criterion |
|---|---|---|
| G0 Baseline | The game on stock PCSX2 through `ps2n-host`, from the disc image: what it prints, the frame on display at fixed frame numbers, how long it takes. Scripted controller input, to get past the title screen. | Two runs give the same frames at the same frame numbers. The maintainer recognizes the pictures as the game. |
| G1 The interpreter as reference | The same run on PS2Native's provider with everything interpreted. | The same console output and the same frames at the same frame numbers as under stock PCSX2, or every difference explained. |
| G2 The game's code compiled | The boot executable through the recompiler, the generated code split over several files and built into the host. | It builds. Functions, instructions, what is delegated, what is left to the interpreter and the build time are reported. |
| G3 The compiled game | The same run with the executable's code compiled. Code the executable does not hold (the modules the game loads) is captured and compiled as well. | The compiled run is the interpreted run at every event test. The same frames. After rebuilding with what was captured, no instruction of the game's own code is interpreted. |
| G4 Further into the game | Title screen, menus, a level, with scripted input. A window, sound and a controller, so that the maintainer can play. | With the same input, the compiled game gets where stock PCSX2 gets. What differs is listed. |
| G5 Measurement and report | Speed against stock PCSX2 on the same scenes with the same renderer. Where the time goes: compiled code, the interpreter, the rest of the machine. Compatibility, visual issues, limitations. | Numbers reported as measured, whatever they are. |

The brief's rules for this phase: reusable solutions, no game-specific
hardcoding unless unavoidable, and no wizard or packaging yet.

## How to run it

```powershell
$iso = "C:\ps2\killzone\Killzone (Europe) (En,Fr,De,Es,It,Nl).iso"
$elf = "C:\ps2\killzone\Killzone (Europe) (En,Fr,De,Es,It,Nl)\SCES_520.04"
$env:PS2N_BIOS = "$HOME\Documents\PCSX2\bios\SCPH-70004_BIOS_V12_PAL_200.BIN"

# The game's executable, compiled into the host:
scripts\windows\build.ps1 -Game $elf

# A run: the configuration, how many frames, the controller script.
scripts\windows\game.ps1 -Image $iso -Config stock-jit -Frames 4200 -FrameEvery 300 -Pad tests\games\killzone\start.pad -Name killzone
scripts\windows\game.ps1 -Image $iso -Config ps2n-static -Frames 4200 -FrameEvery 300 -Pad tests\games\killzone\start.pad -Name killzone -Capture build\game\killzone\capture.toml

# What a run had to interpret of the game goes into the capture file; build it in:
scripts\windows\build.ps1 -GameCapture build\game\killzone\capture.toml

# The compiled run against the stock run: at every sync point, and the picture at every frame
# (17 minutes for both runs, and 3 GB in the build tree while it lasts):
scripts\windows\game.ps1 -Image $iso -Config same-as-stock -Frames 4200 -FrameEvery 1 -Pad tests\games\killzone\start.pad -Name killzone

# The compiled run against the interpreted run, at every event test:
scripts\windows\game.ps1 -Image $iso -Config lockstep -Frames 900 -Name killzone

# Into the first level and through three and a half minutes of play there (step G4;
# 30 minutes for both runs, and 7.5 GB in the build tree while it lasts):
scripts\windows\game.ps1 -Image $iso -Config same-as-stock -Frames 17500 -FrameEvery 10 -Pad tests\games\killzone\level1.pad -Name level1

# The pictures a run saved, sixty to a sheet, to look a whole run over:
scripts\windows\sheets.ps1 -Name level1 -Config same-as-stock

# How long the level run takes and where the time goes (step G5): profile.txt among the run's files.
# Every run of game.ps1 is kept on the processor's fast cores, so that its time repeats.
scripts\windows\game.ps1 -Image $iso -Config ps2n-static -Frames 17500 -Renderer null -Pad tests\games\killzone\level1.pad -Name profile -TimeProfile
scripts\windows\game.ps1 -Image $iso -Config stock-jit -Frames 17500 -Renderer null -Pad tests\games\killzone\level1.pad -Name profile -TimeProfile

# Playing it: a window, the console's speed, sound, the keyboard and the first controller,
# until the window is closed. Keys: arrows, W A S D, T F G H, I J K L (triangle, square,
# cross, circle), Q E, 1 3, 2 4, Return (start), Backspace (select); Alt+Enter covers the screen.
scripts\windows\game.ps1 -Image $iso -Play -Name killzone
# The same under stock PCSX2, to compare by eye and ear (the memory card is shared):
scripts\windows\game.ps1 -Image $iso -Play -Name killzone -Config stock-jit

# The window checked by machine, with the sound off: photographed, a key pressed in it, closed.
scripts\windows\window-check.ps1 -Image $iso -Name killzone -Steps '41 shot film.png', '42 key K', '45 shot title.png', '47 close'
```

The capture file is derived from the game and lives in the build tree, which
can be deleted; running and rebuilding as above makes it again.

What the host and the tools gained for this (all of it general, none of it
for this game): a disc image run for a number of frames (`EXPECT_EXIT` in
the test driver); `-frame-every`, which fingerprints and saves the picture
on display every so many frames; `-pad`, a script of controller input that
changes at frame boundaries, so that every run gets it at the same moments;
`-times`; `ps2n-recomp compile --parts`; `ps2n-recomp capture`, which puts
several runs' reports together; `-sync` and `-sync-against`, the record of
sync points that a stock run can write as well, and the `same-as-stock`
configuration built on it; `ps2n-recomp disasm`; in the host's report, the
places where compiled code handed over to the interpreter;
`scripts\windows\sheets.ps1`, which puts the pictures a run saved side by
side under their frame numbers; a fingerprint of the sound hardware beside
that of the picture; `-window`, the machine in a window to be played
(step G4); `-fast-cores`, which keeps a timed run on the processor's fast
cores; and `-profile`, which says where the CPU thread's time went (step
G5).

## What is on the disc

`SYSTEM.CNF` boots `SCES_520.04` in PAL mode. Beside it: `LOADER.ELF`, 26
IOP modules (`IOP\*.IRX`), four relocatable EE modules (`MODULES\*.REL`, 0.3
to 5.8 MB: graphics effects, network and online play, going by their names)
and five data archives.

What `ps2n-recomp info` makes of the two executables (2026-11-11, each in
about two seconds):

| | `SCES_520.04` | `LOADER.ELF` |
|---|--:|--:|
| PCSX2's checksum | `6624A78C` | `94604F6F` |
| Code | `00140000`-`00513EF0` | `00100000`-`0016DED8` |
| Functions | 22,668 | 1,810 |
| Instructions | 1,003,452 | 112,566 |
| Block leaders | 197,344 | 22,271 |
| COP2 instructions, which the runtime carries out | 4,690 | 76 |
| Other instructions delegated to PCSX2's interpreter functions | 1 (`tlbr`) | 1 (`tlbr`) |
| Functions that run on into the next | 635 | 146 |
| Places where a block is left to the interpreter | 536 | 524 |
| Likely branches left to it when taken | 60 | 87 |
| `DI` compiled with the instruction after it | 3 | 4 |
| Branches that may close an idle loop | 46 | 9 |

- Most of the COP2 instructions are transfers, which are PS2Native's own
  semantics: `lqc2` 2,081, `sqc2` 886, `qmtc2` 301, `qmfc2` 217. The VU0
  instructions among them are carried out by PCSX2's interpreter functions:
  `vmadda` 352 and `vcallms` 230 are the most frequent.
- Of the 536 places left to the interpreter, 472 are words the decoder does
  not know. All 472 lie in one stretch of 36 KB (`00408000`-`00410FFF`) and
  look like VU micro programs kept in the executable as data, which the
  analysis took for code. The other 64 are branches whose delay slot cannot
  be compiled (54) and blocks that run on past the code that was followed
  (10); they have not been looked at. None of the 536 was reached in the
  runs below.
- The four relocatable modules are code that is not in the executable. The
  proof of concept left capturing such code open, and so far none of it has
  run (see "Open" at the end).
- `LOADER.ELF` has not been seen to run.

## G0: the game under stock PCSX2 (Windows, 2026-11-11)

`ps2n-host` boots the disc image through the BIOS with PCSX2's software
renderer, a DualShock 2 in port 1, no memory card, no sound device, as fast
as the host goes.

| Frames | What is on display |
|---|---|
| 900 | The copyright screen |
| 1,200 | The developer's logo |
| from about 1,800 | "No memory card (PS2) found ... Do you want to start the game?", which waits for a button |
| from about 2,120, after Cross at frame 1,900 | The intro film |

- The pictures at frames 900, 1,200, 1,800, 2,700 and 4,200 were looked at
  and are what those screens should be.
- **Runs repeat.** Two stock runs with the same input show the same picture
  at all eight frame numbers both fingerprinted (300 to 2,400).
- 4,200 frames take 22.9 s.
- The controller script is `tests/games/killzone/start.pad`: one press so
  far.

## G1: the interpreter as reference (Windows, 2026-11-11)

The first 900 frames, no input:

| | Stock PCSX2, recompilers | PS2Native's provider, everything interpreted |
|---|--:|--:|
| Wall-clock time of the run | 7.2 s | 15.4 s |
| EE cycle count at frame 900 | 5,302,548,050 | the same |
| Lines printed by the game and the BIOS | 107 | the same 107 |
| Frame on display | 512x512, fingerprint `C502270840CD4B61` | the same |

- The interpreter ran 2,582,495,603 instructions: 2,450,943,199 of the
  game's executable (94.9%), the rest the BIOS. 1,918,685 of them (0.07%)
  went through PCSX2's interpreter functions.
- There were 4,327,274 event tests.

4,200 frames, with the button press: 16,834,048,591 instructions, of which
16,620,421,292 are the executable's, in 99.2 s with nothing drawn.

**The interpreted run is the stock run.** Compared with a stock run at
every sync point (38,478,822), it is at the same place at the same cycle
count each time, prints the same 107 lines and shows the same picture at
each of the 4,199 frames fingerprinted. That comparison was made before the
interpreter reproduced a defect of PCSX2's recompiler in one COP0 register
(below), and that register was the one thing that differed; the compiled
run, made after, has the stock run's registers too, and the interpreted
run is the compiled run at every event test (G3). "The timing difference"
below has how this was reached.

## G2: the game's code compiled (Windows, 2026-11-11)

`scripts\windows\build.ps1 -Game <SCES_520.04>` runs the recompiler on the
executable and builds the result into `ps2n-host` beside the test programs.

| | |
|---|--:|
| Generated C++ | 152.4 MB in 66 files: 64 parts, a header, the tables |
| Time for the recompiler to write it | 1.8 s |
| Time to rebuild the host after the game's code changed (clang-cl, Release, 20 threads) | 119 s |
| `ps2n-host.exe` with the game and the 63 test programs | 194 MB |

A program of this size does not compile as one file, so the recompiler
spreads the functions over a number of files (`--parts`), in address order
and of about equal size, with a header that declares them.

**Counting time as PCSX2's recompiler does made the generated code larger
and much slower to compile.** Before, it was 115.8 MB, the host 115 MB, and
a rebuild after the game's code changed took 31 s. Generated code now asks
at every label where its block ends, and tests at every label reached from
the instruction before it whether the block ends there. Nothing has been
done yet to make that cheaper to compile.

## G3: the compiled game (Windows, 2026-11-11)

### The first 900 frames

| | Compiled |
|---|--:|
| Wall-clock time of the run | 7.7 s |
| EE cycle count at frame 900 | 5,302,548,050 |
| Event tests | 4,327,274 |
| Frame on display | `C502270840CD4B61` |
| Blocks compared with what was compiled, found to be other code | 28,100, 0 |
| Blocks handed to the interpreter, whole or in part | 0 |
| Block starts in the executable that had to be interpreted | 0 |
| Instructions of the executable interpreted | 0 |
| Instructions interpreted in all (the BIOS) | 131,552,404 |

- **One rebuild is enough.** A build from the executable alone, with no
  capture file, has to interpret from 463 block starts in 4,200 frames
  (5,079 instructions of the executable): functions the analysis did not
  find, small ones that follow another function without a call or a pointer
  to them that it could see. The run reports their addresses, the
  recompiler makes them entries, and the next build interprets nothing of
  the executable. (Before today's change of how time is counted there were
  464: the one more is at a 4 KB page boundary, where a block can now begin
  anyway.)
- **Lockstep: the compiled run is the interpreted run at every one of the
  4,327,274 event tests** (the same fingerprint of the run,
  `F1AAA21FDD8A4D30`).
- The interpreter's share of the EE's cycles is 3.05%: the BIOS.

### 4,200 frames, with the button press

| | Stock PCSX2, recompilers | Compiled |
|---|--:|--:|
| Wall-clock time of the run, two runs of each back to back | 22.8 s, 22.9 s | 25.8 s, 25.7 s |
| The same with nothing drawn (the null renderer) | 11.2 s, 11.1 s | 15.1 s, 15.2 s |
| EE cycle count at frame 4,200 | 24,766,740,046 | the same |
| Sync points | 38,478,822 | the same at every one: where the EE is, its cycle count, its registers |
| The picture at every frame (4,199 fingerprinted) | 2,035 different pictures | the same picture at every frame |
| Lines printed by the game and the BIOS | 107 | the same 107 |
| Block starts in the executable that had to be interpreted | | 0 |
| Blocks handed to the interpreter, whole or in part | | 0 |
| Instructions of the executable interpreted | | 0 |
| Instructions interpreted in all | | 213,627,299 (1.11% of the EE's cycles) |
| of them in the kernel's segments, in the BIOS's program loader (below `00100000`), elsewhere | | 197,395,488, 16,231,811, **0** |
| Event tests | | 47,705,744 |

- **The compiled run is the stock run.** The two runs' records of sync
  points, 3 GB each, are the same file byte for byte.
- **Lockstep over all 4,200 frames: the compiled run is the interpreted run
  at every one of the 47,705,744 event tests** (fingerprint of the run
  `F99B5EFD472E87EC` in both; 302 s for both halves). So the interpreted
  run is the stock run as well, registers included.
- **Everything still interpreted is the BIOS.** Not one instruction was
  interpreted in user memory outside the executable, so none of the four
  `.REL` modules has run yet.
- 1,120,141 system calls. The most frequent, by number: 122, 906,382 times
  (216 a frame; `SifGetReg` by ps2sdk's numbering, from memory and not
  checked here); 5, 45,234 times; 119 and 120, 34,205 and 25,764 times; 64,
  65, 67 and 68, about 24,770 times each (creating, deleting, signalling
  and waiting for a semaphore, by the same numbering: what a blocking call
  to the IOP does). Another 13,552 system calls, `FlushCache`, were not
  made, as PCSX2's recompiler does not make them.
- 8,394,871 cycles passed in a timeout loop that was not run (one loop of
  1,048,576 counts, in 3,885 turns).
- Of the VU0 instructions that still go through PCSX2's interpreter
  functions, `vftoi4` ran 168,852 times and `vcallms` 26,240 times.

## The timing difference: found and removed (Windows, 2026-11-11)

**What was seen.** Until today the compiled run and the stock run showed
the same pictures, but not at the same frames: of the first 2,599 frames,
165 between 738 and 1,949 differed in a shimmer that moves with the game's
clock, and the intro film started at frame 2,125 instead of 2,121. The EE's
cycle counts at the end of the two runs were four cycles apart.

**How the cause was found.** The core lets the IOP catch up with the EE
from its event test, whatever provider runs the EE. `ps2n-host -sync` writes
a line at each of those moments (a sync point): where the EE is, its cycle
count, hashes of its registers. Under stock PCSX2's recompiler too, since
the host only wraps the core's IOP provider. Two such records side by side
show the first moment at which two runs differ. For the smallest test
program that was the second sync point of the run, still in the BIOS: stock
PCSX2 had counted 109 cycles for one block where PS2Native counted 67. By
hand, from PCSX2's source, 109 is what its recompiler counts for that block:
every instruction double, because the processor's configuration register
still said so when the block was compiled (the block itself changes it),
and the cycles so far added to the cycle count, rounded down, at each of
its two writes to COP0.

**The cause.** PCSX2's recompiler counts time by the blocks it compiles,
and what a block is depends on what had been compiled before it
([ADR 0011](adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)
lists how). No rule about the program alone reproduces that. This game
reads the EE's count register 73,000 times a frame in a loop that waits for
a flag, so it sees every cycle of difference.

**What was done.** PS2Native now keeps the record of blocks that PCSX2's
recompiler would keep, without the code, and counts time by it: first in
the interpreter, which carries those blocks out; then in generated code,
which asks at every label where the block that begins there ends
([execution-model.md](execution-model.md)). PS2Native's own way of counting
is gone.

**What was measured.**

| | Before | Now |
|---|--:|--:|
| Test programs whose run is the stock run at every sync point, of 63 | not counted: the one tried parted from it at its second sync point | 59, interpreted and compiled |
| The game, 4,200 frames: sync points the same as stock | | all 38,478,822 |
| Frames, of 4,199, that show stock's picture | not all: see above | all |
| EE cycle count at frame 4,200 against stock's | 4 less | the same |
| The game interpreted, first 900 frames | 31.2 s | 15.4 s |
| The game compiled, 4,200 frames, against stock's 22.9 s | 27.9 s | 25.7 s |

- The four test programs whose run is not the stock run are those that
  print something else than stock PCSX2 on purpose, because the console
  does, and one that stock PCSX2 crashes on.
  `tests/same-as-stock-known-differences.cmake` has each with its cause.
- The test is in the suite: `<suite>/same-as-stock/<program>` runs a
  program under stock PCSX2 and then compiled and requires the two records
  to be equal. `scripts\windows\game.ps1 -Config same-as-stock` does the
  same for a game, pictures included.
- Whole suite: 2 unit test programs (42 cases) and 447 tests pass in
  136 s. The 63 new ones are the same-as-stock tests.
- The interpreter is twice as fast as it was because a block is now decoded
  once, when it is formed, and not every time it runs.
- What the compiled game gained is `sync` and `mfc0`: the game's waiting
  loop executes them 800 million times in these frames, and they are
  PS2Native's own semantics now instead of calls of PCSX2's interpreter
  functions (27.9 s to 25.8 s, measured before the change of how time is
  counted). Counting time the new way then cost nothing that shows in the
  run time. It did cost in the size of the generated code and in the time
  to compile it (G2).

**Found on the way.**

- **A defect in PCSX2's recompiler, which PS2Native now reproduces.** When
  the count register is read, the recompiler brings it up to date with a
  64-bit addition, and the register that follows it in memory, `EntryHi`,
  takes the carry: it goes up by one each time the count register
  overflows, about every fourteen seconds (`recMFC0`, `x86/iCOP0.cpp`;
  PCSX2's interpreter does not do it). In this run that happens at sync
  point 3,761,620, around frame 728. It is reproduced so that a run can be
  held to the stock run register for register; nothing here was seen to
  depend on it.
- **`DI` takes effect one instruction late** under PCSX2's recompiler. The
  game's routine that disables interrupts runs 19 times a frame; generated
  code first left it to the interpreter (146,950 instructions of the game
  in 760 frames), and now compiles `DI` together with the instruction that
  follows it.
- The test program `cpu/ee/branchdelay` (a branch in a branch's delay slot)
  now prints what stock PCSX2 prints: PCSX2's recompiler drops the second
  branch, and so does PS2Native.

## G4: further into the game (Windows, 2026-11-11)

`tests/games/killzone/level1.pad` takes the game from power-on through its
menus into the first level of the campaign and plays there: 17,500 frames.
What each press is for was found by looking at the pictures of runs under
stock PCSX2, sixty to a sheet (`scripts\windows\sheets.ps1`).

| Frames | What is on display |
|---|---|
| from 2,350 | The title screen. Cross at 2,300 skips the intro film |
| from 2,750 | The main menu: Options, Game, Extras |
| from 2,950 | Game: Campaign or Battlefields |
| from 3,400 | "Create profile", once the game has looked for a memory card |
| 3,750 to 4,200 | The game's keyboard: a letter is taken when a direction is held and a button pressed |
| from 4,480 | "Do you want to save this profile to the memory card?" No |
| from 4,750 | Select level; from 5,050 the difficulty; from 5,350 the character |
| from 5,800 | The level's opening scene, skipped at 5,900; the level loads until 6,550 |
| from 6,600 | The first level, in first person: a trench under fire. The script walks, looks, fires, crouches, reloads |
| 9,000 to 14,000 | Out of the trench and across the battlefield: enemy soldiers, shell bursts, objectives met and set, the player hit and, at about 14,000, killed |
| from 14,500 | "Failed". Retry at 15,200, "Save progression?" Cancel at 15,500 |
| from 15,600 | The level again, and more of the same play |

### The compiled game against stock PCSX2

The first 9,000 frames, each configuration run by itself with a picture
saved every 50 frames (the script as it then was; it is the same up to frame
8,960):

| | Stock PCSX2, recompilers | Compiled |
|---|--:|--:|
| Wall-clock time of the run | 76.8 s | 83.6 s |
| EE cycle count at frame 9,000 | 53,078,292,032 | the same |
| Lines printed by the game and the BIOS | 108 | the same 108 |
| The 179 pictures | | the same, file for file |
| Block starts in the executable that had to be interpreted | | 1 (11 instructions); none after a rebuild with it |
| Instructions interpreted in all (the BIOS) | | 162,415,664 |
| in user memory outside the executable | | **0** |
| VU0 instructions carried out by PCSX2's interpreter functions | | 43,356,999 |

All 17,500 frames, the compiled run compared with a stock run at every sync
point and in the picture of every tenth frame
(`game.ps1 -Config same-as-stock`), with the last build:

| | |
|---|--:|
| EE cycle count at frame 17,500, both runs | 103,213,332,030 |
| Sync points | 93,221,937 |
| of them with the EE at the same place at the same cycle count | **all** |
| Pictures compared, one every ten frames | 1,750, **all the same** |
| Sync points where the general registers, COP0's or VU0's differ | **0** |
| Sync points where the FPU's registers differ | 964, the first of them 57,130,066 (below) |
| Instructions of the executable interpreted | 0 |
| Instructions interpreted in all (the BIOS) | 173,513,096 (0.21% of the EE's cycles) |
| in user memory outside the executable | **0** |
| Event tests | 112,297,746 |
| System calls made; `FlushCache` calls not made | 1,047,768; 30,437 |
| VU0 instructions carried out by PCSX2's interpreter functions | 158,299,586 |
| `max.s` and `min.s` given +0 and -0, or bits PCSX2 clamps | 1,523 times |
| `abs.s` and `neg.s` given such bits | never |
| Both runs and the comparison | 1,817 s |

- **The compiled game gets where stock PCSX2 gets**, at the same time, all
  the way: place and time are the same at every sync point, and the picture
  is the same at every frame compared.
- **Still no code outside the executable has run**, not even in the level:
  none of the four `.REL` modules has been called on yet.
- The analysis, with the capture file of the first 4,200 frames, had found
  all of the game's code that the menus and the level use but for one entry
  point.
- The run was first made before the rule for VU0's flags below was in, and
  is what showed the need for it. The table is the run repeated with that
  rule in; the numbers the first run had as well (cycle count, sync points,
  what was interpreted, event tests, system calls) were the same then.
- The time is not a measurement of speed: the stock run writes a record of
  7.5 GB that the compiled run reads back, and a picture is fingerprinted
  and saved every ten frames.
- Whole suite with the last build: 2 unit test programs (42 cases) and 453
  tests pass in 153 s. The six new ones are those of
  `tests/programs/countcarry.s`
  ([ADR 0013](adr/0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)).

### What differs, and why

The registers of the two runs are compared at every sync point as well.
Three things came of that.

1. **Not a difference: a variable of PCSX2's, not a register.** The record
   hashed the VU interpreter's pending result of a division (`VU0.q`),
   which PCSX2's interpreter functions write on the way to the Q register
   and its recompilers never touch. The Q register itself was the same. The
   record now holds the machine's registers only.
2. **VU0's status flags, in the level's first frame. Reproduced since.** The
   game compares two boxes with two `vsub` in a row. PCSX2's recompiler
   (microVU) never sets the overflow and underflow flags, and with its
   "flag hack" setting, which is on unless switched off, it brings the
   status and MAC registers up to date only at the last such instruction
   before something can read them, as it finds out from each block
   (`COP2FlagHackPass`). PS2Native carried these instructions out with
   PCSX2's interpreter functions, which set the flags at every instruction
   and call a result whose exponent is all ones an overflow: the sticky
   overflow flag was set here and not there. The block record now holds
   the recompiler's rule and the flags are put right after each such
   instruction (`FollowRecompilerFlags`). At the sync point where the runs
   differed before, they no longer do, and over the whole run VU0's
   registers are the stock run's at every sync point.
3. **The sign of a zero in an FPU register. On purpose.** `min.s` of -0 and
   +0, at EE cycle 38,745,698,429, as the level starts. PS2Native gives -0,
   as the console does (ps2autotests `cpu/ee_fpu/arithmetic`,
   [ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)).
   PCSX2's recompiler gives whichever operand its register allocator put
   second, here +0. The value goes into memory and comes back: the FPU's
   registers differ at 964 of the 36 million sync points from 57,130,066
   on, that one the first. This is the difference that was listed before a
   game met it; nothing else has been seen to follow from it: not a place,
   not a cycle, not a picture, not another kind of register. The host now
   counts how often `max.s`, `min.s`, `abs.s` and `neg.s` are given such
   operands, and says so after a run: here 1,523 times for the first two
   and never for the other two.

Because of the third, a game is held to the stock run in place and time and
in what it shows, and the comparison reports for each kind of register
(general, FPU, COP0, VU0) at how many sync points it differed and where
first, so that one kind that differs for a known reason does not hide the
others. A test program is still held to the registers as well.

What is known of the VU0 instructions that is still PCSX2's interpreter's
and not its recompiler's: `vrsqrt` of zero by zero (the flags and the
result), and `vsqrt` of -0 (the invalid flag). In these 17,500 frames
neither has left a trace: VU0's registers, its flags among them, are the
stock run's at every sync point.

### A window, sound and a controller (Windows, 2026-11-11)

`ps2n-host -window` (`scripts\windows\game.ps1 -Play`) is the same machine
with someone in front of it:

- **A window of the host's own**, made on the main thread, which serves its
  messages; the core's renderer draws into it from its GS thread, as it
  draws into the window of PCSX2's own frontends (`host/window.cpp`, after
  the window of `pcsx2-gsrunner`). 960 by 720 at a display scaling of 100%,
  and it follows the display's scaling. It can be resized, and Alt+Enter or
  F11 puts it over the whole screen and back. Its icon is PS2Native's and
  its title the game's name and "PS2Native"
  ([ADR 0012](adr/0012-ps2native-branding-not-pcsx2s.md)).
- **The console's speed**: the core's frame limiter, which the test bench
  switches off, stays on.
- **Sound** through the core's own output, as PCSX2 sets it up by default.
- **The pad in port 1 on the keyboard**, with the bindings PCSX2 has by
  default, through the core's input manager; **and on the first
  controller**, which gets the mapping PCSX2 would make for it
  automatically, added beside the keyboard when the controller is found.
  None of PCSX2's hotkeys is bound.
- **A memory card**, which the core makes when there is none (two, as PCSX2
  has two slots filled by default), and the console's own settings. They
  are in `build\game\<name>\play`, shared by the configurations, so that a
  game saved under one can be loaded under another (and so one session at a
  time). The BIOS image is linked there, not copied, and the core writes the
  console's NVRAM beside the link.
- **What a session had to interpret of the game is written down** for the
  recompiler (`capture.toml` in that folder; `game.ps1 -Capture <file>`
  adds it to the file the build takes), because someone playing goes where
  no script has been.
- **The renderer PCSX2 chooses for the computer** (here Direct3D 12), at the
  console's resolution; `-Renderer` chooses another.
- **Nothing drawn over the picture** by the core; `-Osd` for whoever wants
  its messages and figures while bringing a game up.
- Stock PCSX2 runs in the same window (`-Config stock-jit`), to compare by
  eye and ear.

What was checked, by machine: a script outside the host finds the window,
photographs it, presses keys in it and closes it
(`scripts\windows\window-check.ps1`). Every session was muted.

| | |
|---|---|
| The window comes up, titled "Killzone - PS2Native" once the core knows the game | seen in the photographs |
| The pace of a session of a minute | 2,958 frames in 59.3 s: 49.79 a second, where the console shows 50 |
| Keys pressed in the window reach the game | K (cross) ended the intro film; Return (start) opened the main menu: seen in the photographs |
| Over the whole screen and back (F11) | the picture's area 1,920 by 1,080, then 960 by 720 again; the picture stays 4:3 |
| Closing the window | the machine stops at the next frame; the host reports as after any run |
| The executable's instructions interpreted in a session | 0 |
| The controller on this computer ("XInput Controller") | found, and given the pad in port 1: 27 inputs. Nobody pressed anything on it |
| The sound device | opened by the core: 48,000 Hz, stereo. Nothing was played aloud |
| The console's NVRAM | written beside the link; no file in the maintainer's BIOS folder changed |
| Stock PCSX2 in the same window | runs |
| The whole suite with this host | 2 unit test programs (42 cases) and 453 tests pass |

**The sound the compiled game makes is the stock run's, as far as the sound
hardware shows it** (2026-11-12). The host fingerprints the SPU2's
registers and its memory, which holds the last 512 samples of everything it
mixed and the memory of its reverberation
([execution model](execution-model.md), "What a program sounds like"), and
the comparison with stock PCSX2 now holds the compiled run to that as it
holds it to the pictures:

| | |
|---|---|
| The 17,500 frames of the level run, every 50 frames, stock and compiled | 350 moments, **the same at each** |
| The test programs, when they end (`*/same-as-stock/*`) | 60, the same in each; the other four are the programs listed as differing |

That is what the game is given to hear with, not what a person hears: the
way from the SPU2 to the loudspeaker is PCSX2's own, unchanged, and nobody
has listened.

**The maintainer played it** (2026-11-12): the compiled game, with sound and
the controller, from power-on through the menus into the first level, where
the window was closed.

| | |
|---|---|
| The session | 6,219 frames in 124.9 s: 49.75 a second, where the console shows 50 |
| The executable's instructions interpreted | 0 |
| Block starts that had to be interpreted, for the recompiler | none: the capture file is as it was, 465 entries |
| The frame the session ended on | the first level, in the trench |

Asked how the picture looked in motion, whether the sound was right and
whether the controller answered as it should, the maintainer said:
"Everything looked good." Read as an answer to all three.

Not done: the window on Linux (`-window` says so there), vibration, a
second controller, what the system's volume controls call the game.

### How long the level run takes (Windows, 2026-11-12)

The 17,500 frames of `level1.pad`, as fast as the computer goes, with
nothing else of the project's running. Four rounds, each configuration once
per round, in this order; seconds for the whole run, the host's start
included:

| Round | Nothing drawn, stock PCSX2 | Nothing drawn, compiled | Direct3D 12, stock PCSX2 | Direct3D 12, compiled |
|---|--:|--:|--:|--:|
| 1 | 141.2 | 202.9 | 107.8 | 141.4 |
| 2 | 75.7 | 99.8 | 77.7 | 107.2 |
| 3 | 86.8 | 115.2 | 76.3 | 101.3 |
| 4 | 115.3 | 147.1 | 131.0 | 103.0 |
| The fastest of the four | 75.7 | 99.8 | 76.3 | 101.3 |

- **The times do not repeat.** The same run took between 76 and 141 s, and
  in the fourth round stock PCSX2 took longer than the compiled game. All
  sixteen runs ended at the same cycle count, and the eight whose report
  was kept whole with the sound hardware in the same state and, where a
  picture was drawn, on the same picture: it is the computer's pace that
  changed, not the work. The cause was found the same day, and with it the
  remedy: "G5", "Times that repeat". The runs of the first 4,200 frames
  ("G3") had repeated to within a hundredth.
- **By the fastest of four, the compiled game takes a third longer than
  stock PCSX2**: 1.32 times as long with nothing drawn, 1.33 times with the
  Direct3D 12 renderer. With times that vary like these, that is an
  estimate, not a measurement.
- **Against the console's speed there is room.** The slowest compiled run
  shows 86 frames a second over the whole script, the fastest 175, where
  the console shows 50. That is an average: the frames of the level cost
  more than those of the menus, and no run has timed the level by itself.
- **With PCSX2's software renderer the level does not reach the console's
  speed, under stock PCSX2 either**: the whole run took 431 s (stock, with
  other work going on beside it) and 435 s (compiled, by itself), where the
  first 9,000 frames take about 80 s. That is 24 frames a second in the
  level. The software renderer is what the pictures are compared with, not
  what a game is played with; the window uses the renderer PCSX2 chooses,
  here Direct3D 12.
- With nothing drawn the runs are not shorter than with Direct3D 12. That
  was not expected, and has not been looked into.

## G5: measurement and report (Windows, 2026-11-12)

### Times that repeat

The same run of the level took between 76 and 141 s (above). The processor
has two kinds of cores, six fast ones with two threads each and eight slow
ones, and Windows moves a program that is not in front between them as it
sees fit. The stock run with nothing drawn, held to one kind or left alone:

| Where the run was kept | Seconds |
|---|--:|
| The first twelve logical processors (the fast cores) | 76.6, 75.3 |
| The last eight (the slow cores) | 150.1 |
| Left to Windows | 98.3, 75.4 |

So `ps2n-host -fast-cores` keeps a run on the processor's fastest cores, as
the system itself ranks them; on a processor whose cores are all alike it
does nothing. `game.ps1` and `bench.ps1` now pass it for every run they
time. The test suite does not: its runs are not timed, and it uses every
core. A window in front is not moved to the slow cores, and a session there
kept the console's pace.

### The level run, timed

The 17,500 frames of `level1.pad` again, every run on the fast cores, three
rounds; seconds for the whole run:

| Round | Nothing drawn, stock PCSX2 | Nothing drawn, compiled | Direct3D 12, stock PCSX2 | Direct3D 12, compiled |
|---|--:|--:|--:|--:|
| 1 | 74.0 | 100.1 | 75.5 | 102.1 |
| 2 | 74.7 | 100.0 | 75.2 | 101.6 |
| 3 | 73.9 | 100.3 | 75.7 | 101.7 |

- **The times repeat**, to within 1%. All twelve runs ended at the same
  cycle count with the sound hardware in the same state.
- **The compiled game takes 1.35 times as long as stock PCSX2** over this
  run, with nothing drawn and with the Direct3D 12 renderer alike: 100 s
  against 74 s, 102 s against 75.5 s.
- That is 175 frames a second compiled and 236 under stock PCSX2, on
  average over the script, where the console shows 50.

### Where the time goes

`ps2n-host -profile` (`game.ps1 -TimeProfile`) looks where the CPU thread is
executing about 660 times a second, from the first frame to the last, and
says what was there. The CPU thread carries the EE, the IOP and the VUs;
the GS has a thread of its own, which is not looked at. The level run with
nothing drawn, in seconds of the run (the share of the samples times the
run's length):

| | Stock PCSX2, 74.3 s | Compiled, 102.6 s |
|---|--:|--:|
| **The game's own code**: PCSX2's EE recompiler's code, or the code compiled ahead of time | 25.8 | 43.6 |
| PS2Native's runtime | | 7.6 |
| PCSX2's core | 26.8 | 29.6 |
| The code PCSX2's VU1 recompiler generated | 10.9 | 10.9 |
| The code PCSX2's IOP recompiler generated | 4.5 | 5.0 |
| The code PCSX2's VU0 recompiler generated | 1.3 | 1.3 |
| Windows, drivers, other libraries | 4.4 | 4.0 |
| Not identified | 0.5 | 0.6 |

- **The whole difference is in the game's own code.** It takes 51.2 s as
  compiled code with its runtime, and 25.8 s as the code PCSX2's recompiler
  generates: twice as long. That is 25.4 of the 28.3 s the compiled run
  takes longer. Another 2.7 s are in PCSX2's core, which has not been
  broken down: the VU0 instructions of the game are carried out there by
  PCSX2's interpreter functions for the compiled game, where stock PCSX2
  has them in its recompiler's own code, and that is part of it.
- Everything else is the same machine doing the same work, and takes the
  same time: the VUs, the IOP, the sound, the rest of the core.
- **In the compiled code, most of the time is in few places.** Of 22,668
  functions, three have 27% of it:

  | Function | Seconds | What it is |
  |---|--:|---|
  | `0014FD68` | 5.5 | reads the count register and adds up the time gone by, in a loop: the game waiting |
  | `001C0190` | 4.3 | reads a counter in memory until it has moved on: the game waiting |
  | `0050C560` | 2.0 | a long function of floating-point arithmetic |

  The two that wait are 9.9 s, a tenth of the run. Stock PCSX2 runs the
  same loops the same number of times: the runs are at the same place at
  the same cycle count throughout.
- **In the runtime, the largest part is the VU0 instructions**: passing one
  on to PCSX2's function and putting the flags right afterwards
  (`cop2Instruction`, `Cop2Operation`, `Cop2Prologue`: 2.8 s of the 7.6),
  then the dispatcher (1.0 s) and finding the block that is running
  (0.6 s).
- PCSX2's VU1 recompiler spends 3.4 s of the stock run and 4.1 s of the
  compiled run compiling (`mVUcompileJIT`), beside the 10.9 s its code
  runs. It is one of the stand-ins of
  [ADR 0003](adr/0003-no-jit-in-generated-games.md).

What this does not say: why the compiled code takes twice as long as the
recompiler's for the same instructions. It is one run of one game, and a
sample is a place, not a cause.

**A first look at why**, in one of the two loops where the game waits
(`001C01B0`, twelve instructions a turn, six of them loads and stores). The
C++ compiler made 107 host instructions of one turn:

- every load and store looks its address up in the core's table twice,
  once to see whether it is plain memory and once to reach it, with a
  branch each time: 13 such checks a turn;
- every register the game writes goes to memory, and is read back from
  there when it is used: 25 instructions a turn touch the registers or the
  table's own address in memory. After a store into the console's memory
  the compiler reads them again, because it cannot know that the store
  left them alone.

PCSX2's recompiler keeps the registers of a block in host registers and
reaches plain memory with one instruction. Nothing in this is the game's:
it is how the generated code is written, the same for every function. The
numbers are counted in the object file, not measured; what each would give
back has to be measured when it is changed.

### The report, as far as the first level

**Compatibility.** Killzone (`SCES-52004`) boots from its disc image through
the BIOS, plays its logos and intro film, goes through its menus and plays
its first level: 17,500 frames by script, and two minutes by the
maintainer's hand. Nothing is known not to work. Nothing beyond the first
level has been run, nor the multiplayer side, and none of the four `.REL`
modules has been called on yet.

**Against stock PCSX2**, with the same input: the same place at the same
cycle count at each of 93 million sync points; the same picture at each of
1,750 frames (software renderer); the sound hardware in the same state at
each of 350 moments; the general registers, COP0's and VU0's the same
throughout. One difference, listed and on purpose: the sign of a zero in
the FPU's registers, where the console is followed.

**Visual and audio issues.** None known. In the window, with the renderer
PCSX2 chooses (Direct3D 12) and sound, the maintainer found nothing wrong.
Not compared: that renderer's pictures with the software renderer's, and
either with a console.

**What is compiled and what is not.** All of the executable's code that has
run is compiled ahead of time: 0 of its instructions were interpreted in
any of these runs, once the game had been rebuilt with what earlier runs
reported (465 places in all). What is not the game's is not compiled yet: the BIOS's code is
interpreted (0.21% of the EE's cycles in the level run,
[ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md)), and the VUs
and the IOP run on PCSX2's recompilers, the stand-ins of
[ADR 0003](adr/0003-no-jit-in-generated-games.md): what is built today
still has a recompiler in it. The game's VU0 instructions are carried out
by PCSX2's interpreter functions.

**Speed.** Above: 1.35 times stock PCSX2's time over the level run, all of
it in the game's own code; 175 frames a second on average where the
console shows 50.

**Limitations.**

- Windows only. Linux has never been built or run, and the window is not
  written for it.
- A BIOS image is needed at run time, and the disc image.
- Time is counted as PCSX2 `v2.9.170`'s recompiler counts it
  ([ADR 0011](adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)),
  two of its defects included (ADR 0010, ADR 0013). Moving to another
  PCSX2 means making the comparison again.
- A rebuild after the game's code changed takes two minutes, the
  executable is 194 MB, and the generated C++ 152 MB.
- One game. Nothing here was written for it, but nothing has shown that on
  a second one.
- No application yet: no wizard, no packaging (Phase 4).

## Open

- **VU0's instructions.** They are the one family of the game's own code
  that is not PS2Native's: 158 million calls of PCSX2's interpreter
  functions in 17,500 frames, each through the runtime. Semantics of
  PS2Native's own, following PCSX2's recompiler, would close the two known
  differences above and take those calls out of the level's time.
- **Speed.** 4,200 frames of logos, a prompt and a film take 25.7 s
  compiled against 22.9 s under stock PCSX2 with the software renderer, and
  15.1 s against 11.2 s with nothing drawn. Where the four seconds go has
  not been measured. The BIOS, which is interpreted, accounts for 1.1% of
  the EE's cycles there; translating it ahead of time is
  [ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md), not started.
  Over the level run the compiled game takes 1.35 times as long as stock
  PCSX2, and all of the difference is in the game's own code, which takes
  twice as long compiled as under PCSX2's recompiler ("G5"). A first look
  at one loop says where to begin: the generated code keeps the machine's
  registers in memory and looks every address up twice. Changing how the
  code is generated (registers held in local variables within a block, one
  look at the table for each load and store, or none where the address is
  known to be plain memory) is what it takes for PS2Native to be as fast as
  PCSX2, and then faster. None of it has been tried, so none of it has a
  number yet.
- **What a longer run could meet.** PCSX2's recompiler throws all its
  blocks away when its code cache is full, and PS2Native cannot know when
  that is (execution model, "What is not reproduced"). It did not happen in
  17,500 frames: stock PCSX2's log has the two resets at start-up and no
  other. PCSX2 logs it when it does ("EE/iR5900 Recompiler Reset" in
  `core.log`), and the same-as-stock comparison would show the runs part
  there.
- **Code outside the executable.** None has run yet. When a `.REL` module
  does, its code has to be captured and compiled; the proof of concept left
  that open.
- **Further into the game:** the rest of the first level and the levels
  after it, the other menus, the multiplayer side. Each needs more of the
  controller script, and then a rebuild for whatever new code runs.
- **Playing on.** The maintainer has played two minutes of it, into the
  first level. Longer sessions go where no script has been: each writes
  down what it had to interpret, for a rebuild. Stock PCSX2 opens in the
  same window (`-Config stock-jit`) for whoever wants the two side by
  side.
- **The window on Linux.** Not written: `ps2n-host -window` says so there.
  `pcsx2-gsrunner` has the X11 window to go by.
- **The pictures the runs saved have only been looked at by the AI
  session.** The maintainer has seen the game itself, in the window. The
  pictures are in
  `build\windows-clang\ps2n\tests\game\killzone\<configuration>`.
