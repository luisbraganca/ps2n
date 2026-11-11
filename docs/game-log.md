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
| G1 The interpreter as reference | **Run for the first 900 frames; its criterion is not met** (2026-11-11): the same console output as under stock PCSX2 and the same picture at frame 900, but not the same picture at every frame. The compiled run is the interpreted run (step G3), so what G3 found open is open here too. |
| G2 The game's code compiled | **Done** (2026-11-11): the boot executable builds into the host, 1,003,452 instructions in 64 files. |
| G3 The compiled game | **Done for the first 4,200 frames, with one thing open** (2026-11-11): the compiled run is the interpreted run at every one of 47.5 million event tests; after one rebuild none of the executable's code is interpreted; no code outside the executable has run. **Open: the pictures do not all come at the same frame numbers as under stock PCSX2** (the intro film starts four frames later), and why is not established. |
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

# The game's executable, compiled into the host (about half a minute):
scripts\windows\build.ps1 -Game $elf

# A run: the configuration, how many frames, the controller script.
scripts\windows\game.ps1 -Image $iso -Config stock-jit -Frames 4200 -FrameEvery 300 -Pad tests\games\killzone\start.pad -Name killzone
scripts\windows\game.ps1 -Image $iso -Config ps2n-static -Frames 4200 -FrameEvery 300 -Pad tests\games\killzone\start.pad -Name killzone -Capture build\game\killzone\capture.toml

# What a run had to interpret of the game goes into the capture file; build it in:
scripts\windows\build.ps1 -GameCapture build\game\killzone\capture.toml

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
several runs' reports together.

## What is on the disc

`SYSTEM.CNF` boots `SCES_520.04` in PAL mode. Beside it: `LOADER.ELF`, 26
IOP modules (`IOP\*.IRX`), four relocatable EE modules (`MODULES\*.REL`, 0.3
to 5.8 MB: graphics effects, network and online play, going by their names)
and five data archives.

What `ps2n-recomp info` makes of the two executables (2026-11-11, each in
about a second):

| | `SCES_520.04` | `LOADER.ELF` |
|---|--:|--:|
| PCSX2's checksum | `6624A78C` | `94604F6F` |
| Code | `00140000`-`00513EF0` | `00100000`-`0016DED8` |
| Functions | 22,668 | 1,810 |
| Instructions | 1,003,452 | 112,566 |
| Block leaders | 196,584 | 22,163 |
| Delegated to PCSX2's interpreter functions | 4,881 (0.5%) | 319 (0.3%) |
| Functions that run on into the next | 614 | 145 |
| Places where a block is left to the interpreter | 538 | 526 |
| Likely branches left to it when taken | 61 | 88 |
| Idle loops, timeout loops | 46, 1 | 9, 1 |

- Most of what is delegated in the game's executable is VU0: `lqc2` 2,081,
  `sqc2` 886, `vmadda` 352, `qmtc2` 301, `vcallms` 230, `qmfc2` 217.
- Of the 538 places left to the interpreter, 470 are words the decoder does
  not know. All 470 lie in one stretch of 36 KB (`00408000`-`00410FFF`) and
  look like VU micro programs kept in the executable as data, which the
  analysis took for code. None was reached in the runs below ("blocks
  finished by the interpreter: 0"). The other 68 have not been looked at.
- The four relocatable modules are code that is not in the executable. The
  proof of concept left capturing such code open, and so far nothing tells
  whether any of it has run (see "Open" at the end).
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
- 4,200 frames take 23.9 s.
- The controller script is `tests/games/killzone/start.pad`: one press so
  far.

## G1: the interpreter as reference (Windows, 2026-11-11)

The first 900 frames, no input:

| | Stock PCSX2, recompilers | PS2Native's provider, everything interpreted |
|---|--:|--:|
| Wall-clock time of the run | 7.2 s | 31.2 s |
| EE cycle count at frame 900 | 5,302,548,050 | 5,302,548,032 |
| Lines printed by the game and the BIOS | 107 | the same 107 |
| Frame on display | 512x512, fingerprint `C502270840CD4B61` | the same |

- The interpreter ran 2,582,668,250 instructions: 2,449,092,650 of the
  game's executable (94.8%), the rest the BIOS. 6.0% went through PCSX2's
  interpreter functions: `sync` 75.2 million, `mfc0` 72.7 million, `mtc0`
  3.7 million, `cache` 2.4 million, `eret` and `syscall` 0.9 million each.
- **The game itself executes about 71 million `sync` and 68 million `mfc0`
  in these 900 frames** (the BIOS's share is 4.3 and 4.8 million). Both are
  still delegated, in compiled code too.
- There were 4,330,253 event tests.

## G2: the game's code compiled (Windows, 2026-11-11)

`scripts\windows\build.ps1 -Game <SCES_520.04>` runs the recompiler on the
executable and builds the result into `ps2n-host` beside the test programs.

| | |
|---|--:|
| Generated C++ | 115.8 MB in 66 files: 64 parts of at most 2.2 MB, a header, the tables |
| Time for the recompiler to write it | 1.3 s |
| Time to rebuild the host after the game's code changed (clang-cl, Release, 20 threads) | 31 s |
| `ps2n-host.exe` with the game and the 63 test programs | 115 MB |

A program of this size does not compile as one file, so the recompiler now
spreads the functions over a number of files (`--parts`), in address order
and of about equal size, with a header that declares them.

## G3: the compiled game (Windows, 2026-11-11)

### The first 900 frames

| | Compiled, first build | Compiled, rebuilt with what the first run reported |
|---|--:|--:|
| Wall-clock time of the run | 8.6 s | 8.7 s |
| EE cycle count at frame 900 | 5,302,548,032 | the same |
| Event tests | 4,330,253 | the same |
| Frame on display | `C502270840CD4B61` | the same |
| Functions compared with memory, found different | 5,033, 0 | 5,033, 0 |
| Blocks finished by the interpreter | 0 | 0 |
| Block starts in the executable that had to be interpreted | 464 | **0** |
| Instructions of the executable interpreted | 5,104 | **0** |
| Instructions interpreted in all (the BIOS) | 133,580,704 | 133,575,600 |

- The 464 block starts are functions the analysis did not find: small ones
  that follow another function without a call or a pointer to them that it
  could see. The run reported their addresses; the recompiler made them
  entries; nothing of the executable was interpreted after that.
- **Lockstep: the compiled run is the interpreted run at every one of the
  4,330,253 event tests** (the same fingerprint of the run, `3D98DFECF5CEE020`).
- The interpreter's share of the EE's cycles is 3.1%: the BIOS.

### 4,200 frames, with the button press

| | Stock PCSX2, recompilers | Compiled |
|---|--:|--:|
| Wall-clock time of the run | 23.9 s | 27.9 s |
| EE cycle count at frame 4,200 | 24,766,740,046 | 24,766,740,042 |
| Block starts in the executable that had to be interpreted | | 0 |
| Instructions interpreted in all | | 213,777,031 (1.1% of the EE's cycles) |
| of them in the kernel's segments, in the BIOS's program loader (below `00100000`), elsewhere | | 197,728,088, 16,048,943, **0** |
| Event tests | | 47,546,273 |
| Pictures at every 300th frame, of 14 | | the first 7 the same, the last 7 not |

- **Lockstep over all 4,200 frames: the compiled run is the interpreted run
  at every one of the 47,546,273 event tests** (fingerprint of the run
  `9BE6E5CE93E7680C` in both). That run interprets 16,845,109,763
  instructions, 16,631,332,732 of them the executable's, and takes 405 s for
  both halves.
- **Everything still interpreted is the BIOS.** Not one instruction was
  interpreted in user memory outside the executable, so none of the four
  `.REL` modules has run yet.
- 1,114,337 system calls. The most frequent, by number: 122, 906,400 times
  (216 a frame; `SifGetReg` by ps2sdk's numbering, from memory and not
  checked here); 64, 65, 67 and 68, about 22,330 times each (creating,
  deleting, signalling and waiting for a semaphore, by the same numbering:
  what a blocking call to the IOP does); 100, `FlushCache`, 12,751 times,
  three a frame.
- 8,394,871 cycles passed in a timeout loop that was not run (one loop of
  1,048,576 counts).

**The two runs do not show the same picture at every frame, and the
difference is in time, not in drawing.** With every one of the first 2,599
frames fingerprinted in both runs:

- Up to frame 737 every frame is the same.
- From 738 to 1,949, 165 frames differ. One was looked at in both runs
  (frame 923, the copyright screen): the same screen, with nothing the eye
  can tell apart. What differs is a shimmer on the text, an effect that
  moves with the game's clock. 118 of the compiled run's pictures in this
  stretch are pictures stock PCSX2 never shows, and 125 the other way: the
  effect is drawn at slightly different moments of its animation.
- The intro film starts at frame 2,121 under stock PCSX2 and at 2,125
  compiled. Its pictures are the same pictures: from there the compiled
  run's frame N+4 is stock PCSX2's frame N for 352 of 391 frames looked at
  (2,200 to 2,590), and never its frame N.
- Two compiled runs show the same pictures at the same frames, as two stock
  runs do. So each is repeatable; they differ from each other.
- The console lines tell the same story earlier still. Both runs print the
  same 107 lines, but between frames 150 and 220, while the game loads its
  IOP modules, the EE cycle count at which a line appears is up to 0.42
  million cycles lower in the compiled run than under stock PCSX2, and then
  up to 0.32 million higher: up to 7% of a frame, which is 5.9 million
  cycles.

So the game's sense of time, within a frame, is not the same in the two
runs. **Why is not established.** The EE's cycle counts agree to within a
few cycles at the end of both runs, but that only shows that both were
waiting for the same vertical blank; it says nothing about when, within a
frame, the game finished its work. What is known to differ from PCSX2's
recompiler in how time is counted is listed in the execution model ("Blocks
and time") and in the proof-of-concept log ("Open questions"). One of those
differences concerns writes to COP0's status register (the kernel executes
`mtc0` about 4,000 times a frame here; to which registers has not been
counted), and one concerns VU0 through COP2, which the game uses heavily.
Neither has been shown to be the cause.

What was found while looking, and bears on it:

- **The game spins on the EE's count register.** One instruction, `mfc0
  $a1, Count` at `0014FE40`, runs 65,850,183 times in the first 900 frames:
  73,000 times a frame. It is in a function that waits, for up to a second
  (294,912,000 cycles), for a flag to clear, and on each turn calls a
  callback and reads the count register to see how long it has waited. How
  far such a loop gets in a frame depends on what each turn costs, to the
  cycle, so this game is sensitive to exactly the kind of difference in
  counting that the execution model lists.
- PCSX2's recompiler treats that instruction specially: it adds the cycles
  of the block so far to the cycle count before the read. PS2Native calls
  PCSX2's interpreter function, which reads the count as of the block's
  start. For this loop's block the two round to the same total (26 cycles),
  so this is not shown to be the cause either; but the value read differs
  by 8 cycles.
- `FlushCache`, which PCSX2's recompiler does not run but charges 706 cycles
  for, is called three times a frame. What the BIOS's own routine costs here
  has not been measured (it executes about 128 `cache` instructions a call),
  but three calls a frame are too few to explain differences of this size
  by themselves.

**How to find it**, without guessing: the EE cycle count at which each
console line appears (`-times`) is a measure both runs give. Bring
PS2Native's counting to PCSX2's recompiler's, one listed difference at a
time, each checked by lockstep, and watch those numbers converge.

## Open

- **The timing difference above.** Step G1's criterion asks for every
  difference from stock PCSX2 to be explained. This one is not.
- **`sync` and `mfc0`.** About 800 million calls of PCSX2's interpreter
  functions from compiled code in 4,200 frames (410 million `sync`, 402
  million `mfc0`), nearly all from the one loop above. `sync` does nothing
  in PCSX2; `mfc0` needs semantics of its own that match the recompiler's,
  which is also the first step of the timing work.
- **Code outside the executable.** None has run yet. When a `.REL` module
  does, its code has to be captured and compiled; the proof of concept left
  that open.
- **Further into the game:** the title screen, the menus, a level. Each
  needs more of the controller script, and then a rebuild for whatever new
  code runs.
- **A window, sound and a controller** for the maintainer to play it.
- **The pictures have only been looked at by the AI session.** The
  maintainer has not seen them. They are in
  `build\windows-clang\ps2n\tests\game\killzone\<configuration>`.
- **Speed.** 4,200 frames of logos, a prompt and a film take 27.9 s compiled
  against 23.9 s under stock PCSX2, with the software renderer in both. That
  is not yet a measurement of the game: nothing of a level has run.
