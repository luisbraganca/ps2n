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
| G4 Further into the game | Not begun. |
| G5 Measurement and report | Not begun. First times are below; they are not yet a measurement of the game. |

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
configuration built on it; `ps2n-recomp disasm`; and in the host's report,
the places where compiled code handed over to the interpreter.

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

## Open

- **Speed.** 4,200 frames of logos, a prompt and a film take 25.7 s
  compiled against 22.9 s under stock PCSX2 with the software renderer, and
  15.1 s against 11.2 s with nothing drawn. That is not yet a measurement
  of the game: nothing of a level has run. Where the four seconds go has
  not been measured either. The BIOS, which is interpreted, accounts for
  1.1% of the EE's cycles here; translating it ahead of time is
  [ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md), not started.
- **What a longer run could meet.** PCSX2's recompiler throws all its
  blocks away when its code cache is full, and PS2Native cannot know when
  that is (execution model, "What is not reproduced"). It did not happen in
  these 4,200 frames. PCSX2 logs it when it does ("EE/iR5900 Recompiler
  Reset" in `core.log` after the two at start-up), and the same-as-stock
  comparison would show the runs part there.
- **Code outside the executable.** None has run yet. When a `.REL` module
  does, its code has to be captured and compiled; the proof of concept left
  that open.
- **Further into the game:** the title screen, the menus, a level. Each
  needs more of the controller script, and then a rebuild for whatever new
  code runs.
- **A window, sound and a controller** for the maintainer to play it. The
  window takes PS2Native's icon and a title of its own. What the core can
  draw over the picture (messages, performance figures, indicators) gets a
  switch: off for a finished game, on if wanted while bringing one up
  ([ADR 0012](adr/0012-ps2native-branding-not-pcsx2s.md)).
- **The pictures have only been looked at by the AI session.** The
  maintainer has not seen them. They are in
  `build\windows-clang\ps2n\tests\game\killzone\<configuration>`.
