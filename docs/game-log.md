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
| G6 Speed | **Done** (2026-11-12, ended there by the maintainer; [ADR 0015](adr/0015-a-speed-step-before-phase-3-ends.md)): seven changes to how the code is generated, each measured. The level run takes 1.21 to 1.23 times stock PCSX2's time (91.7 s against 75.6 s, and 91.3 s against 74.3 s, each pair timed together), where it took 1.35, and is still the stock run (the same place and time at each of 93 million sync points, the same picture and sound state at each of 1,750 frames) and the interpreted run (47.7 million event tests). The game's own code takes 41.1 s against 25.8 s under PCSX2's recompiler. Four further attempts were measured and taken out again. Where it stands is in "G6" below. |
| G7 VU0's instructions | **Done** (2026-11-12, [ADR 0016](adr/0016-vu0-instructions-with-semantics-of-ps2natives-own.md)): the game's VU0 instructions have semantics of PS2Native's own, all but `VCALLMS` and `VCALLMSR`, which stay PCSX2's function as in its recompiler. The self-test finds them to be PCSX2's recompiler's in every case it tried: 1,968,000 cases, in each VU clamp mode and with the game fixes that change them, Killzone's configuration among them. The two known differences (`VRSQRT` of zero by zero, `VSQRT` of -0) are closed. The level run is still the stock run, VU0's registers the same at each of 93 million sync points, and the interpreted run; its time did not change that the measurement can show (1.21 times stock PCSX2's). Where it stands is in "G7" below. |
| G8 The BIOS's EE code | **Done** (2026-11-12, [ADR 0017](adr/0017-bios-ee-code-compiled-as-step-g8.md)): the BIOS's code that runs on the EE under fast boot is compiled ahead of time from the dump the build is given, as four images, its code followed from where it is entered; of it, 51 instructions at boot are still interpreted, each with its cause. What the game copies into kernel memory is the game's code, and is compiled with it. Rebased onto G9: every test passes, and the game is still the stock run over the level script and the interpreted run over its first 4,200 frames. The level run takes 1.18 times stock PCSX2's time, 1.1 s less than with the BIOS interpreted; a test program boots in stock PCSX2's time. Where it stands is in "G8" below. |
| G9 VU0's instructions in compiled code | **Done** (2026-11-12, [ADR 0018](adr/0018-vu0-instructions-carried-out-by-compiled-code.md), in a worktree of its own): compiled code carries out the game's VU0 instructions itself, inline, where its block has nothing done about VU0 first, and keeps its registers in locals past them. The level run takes 1.19 times stock PCSX2's time (89.5 s against 74.9 s, timed in turn), from 1.21, a gain at the edge of what the measurement shows; the profile has the runtime's share 1.3 s smaller. Still the stock run and the interpreted run. Where it stands is in "G9" below. |
| G10 Performance | **Done** (2026-11-13, ended there by the maintainer; [ADR 0019](adr/0019-performance-as-the-research-lays-it-out.md), begun 2026-11-12): play measured apart from startup, with nothing drawn and with the hardware renderer (the same time: the CPU thread's), and profiled. Profile-guided optimization of the generated code, trained on another run of the level: play takes 1.13 times stock PCSX2's time, from 1.21, startup 1.07, from 1.11, the whole level run 1.11, from 1.18. Then loops that have reached a fixed point (a time round that leaves the machine as it found it): their times round up to the next event test are not run, their cycles counted at once, the event test made where it would have been. The game waits for each frame in one: play takes 1.08 times stock PCSX2's time, startup 1.05, the whole level run 1.07. The COP2 transfers made by compiled code, and link-time optimization of PS2Native's code: no gain. Then the COP2 instructions that come first in their block, 78% of what compiled code still handed to the runtime for COP2, carried out by compiled code unless VU0 is running a micro program: play takes 1.06 times stock PCSX2's time, the whole level run 1.06. Then the rest of them (`VCALLMS` called the way PCSX2's recompiler calls it, `CFC2` and `CTC2` as moves): nothing the timing shows; the runtime now carries out only those at which VU0 is running. Still the stock run and the interpreted run, and every test passes. In the main tree beside it ([ADR 0021](adr/0021-two-sessions-on-step-g10.md)), each change measured by itself without profile-guided optimization: calls through a register made by compiled code itself, play from 1.18 to 1.16 times stock PCSX2's time; the way a load or store seldom takes made short, the game's code 14% smaller, play 1.14; the FPU's settings read once per function, tried and taken out. `ps2n-host -code-size` says how much host code each kind of generated line becomes. The two together, in one history and with profile-guided optimization: play takes 1.04 times stock PCSX2's time, the whole level run 1.04, still the stock run. On 2026-11-13 a third session measured the website's two charts again (the first 4,200 frames 1.09 times stock PCSX2's time with nothing drawn, from 1.35, and 1.03 with the software renderer, from 1.12; of the six loops, `jump` and `call` now about as fast as PCSX2's recompiler, `mmi` at 1.24 as at P4) and made `lq` and `sq` loads and stores on the locals and a block start's call of the runtime one of one argument (the host 12 MB smaller): play still 1.04 times stock PCSX2's time. Then, at the maintainer's word, the FPU's arithmetic: its registers held in SSE registers, as PCSX2's recompiler holds them, and the bits its additions keep past the guard bit looked up instead of branched to, the self-test holding all 511 cases of that to PCSX2's recompiler. Without profile-guided optimization play went from 1.10 to 1.046 times stock PCSX2's time; with it, play takes as long as stock PCSX2's (0.985 to 1.006 times, six rounds in pairs), the whole level run 1.01. Still the stock run and the interpreted run, and every test passes. Where it stands is in "G10" below. |
| G11 The vector units' micro programs | **Under way** (2026-11-13, [ADR 0022](adr/0022-vu-micro-programs-compiled-as-step-g11.md)): measured first, the host watching the core's two VU providers (`ps2n-host -vu-report`). In play VU1's micro memory is rewritten about 40 times a frame, yet all it is given over the level run is 208 distinct pieces of code, 1,750 distinct instruction pairs, written at 5,405 places; PCSX2's VU recompiler makes 6,088 programs of them, and spends about a tenth of play's CPU-thread time starting programs, finding which one memory holds and making new ones. VU0 runs one set of code, written once at boot, started 51.5 million times. The vector units take about a third of play's CPU-thread time, as much as the game's compiled EE code. The maintainer's choices with the measurement: VU1's registers held to the stock run at every sync point too, and PS2Native's own VU interpreter before the translator. VU1's registers are now compared: the level run is still the stock run at every one of its 93,221,937 sync points, VU1's registers the same at each, and every test passes. PS2Native's VU interpreter (`runtime/vu_interp.h`) is in place: 16,000 random micro programs give microVU's results, cycle counts and TPC on it, and with both VUs on it the level run is the stock run at every sync point, VU0's and VU1's registers included, over 13.0 billion VU1 instruction pairs and 1.05 billion VU0 ones. Next is the translator. Where it stands is in "G11" below. |

## The steps and their pass criteria

| Step | Work | Pass criterion |
|---|---|---|
| G0 Baseline | The game on stock PCSX2 through `ps2n-host`, from the disc image: what it prints, the frame on display at fixed frame numbers, how long it takes. Scripted controller input, to get past the title screen. | Two runs give the same frames at the same frame numbers. The maintainer recognizes the pictures as the game. |
| G1 The interpreter as reference | The same run on PS2Native's provider with everything interpreted. | The same console output and the same frames at the same frame numbers as under stock PCSX2, or every difference explained. |
| G2 The game's code compiled | The boot executable through the recompiler, the generated code split over several files and built into the host. | It builds. Functions, instructions, what is delegated, what is left to the interpreter and the build time are reported. |
| G3 The compiled game | The same run with the executable's code compiled. Code the executable does not hold (the modules the game loads) is captured and compiled as well. | The compiled run is the interpreted run at every event test. The same frames. After rebuilding with what was captured, no instruction of the game's own code is interpreted. |
| G4 Further into the game | Title screen, menus, a level, with scripted input. A window, sound and a controller, so that the maintainer can play. | With the same input, the compiled game gets where stock PCSX2 gets. What differs is listed. |
| G5 Measurement and report | Speed against stock PCSX2 on the same scenes with the same renderer. Where the time goes: compiled code, the interpreter, the rest of the machine. Compatibility, visual issues, limitations. | Numbers reported as measured, whatever they are. |
| G6 Speed ([ADR 0015](adr/0015-a-speed-step-before-phase-3-ends.md)) | The generated code made faster, beginning with what G5 found: one look at the address table for each load and store, and the registers held in local variables within a block. Each change measured by itself, on the level run and with the profile. | The compiled game is still the stock run and the interpreted run, and every test passes, after each change. What each change gave back is reported as measured. The aim: the game's own code no slower than under PCSX2's recompiler. |
| G7 VU0's instructions ([ADR 0016](adr/0016-vu0-instructions-with-semantics-of-ps2natives-own.md)) | The VU0 instructions in the game's EE code (COP2 in macro mode) carried out by semantics of PS2Native's own, following PCSX2's recompiler (microVU) as every other family does, in place of PCSX2's interpreter functions. Each compared with the recompiler on random and boundary operands, in each VU clamp mode and with the game fixes that change them (`ps2n-host -selftest`). Not with the interpreter, which computes the flags, the clamps and the special cases differently. | Every VU0 instruction has semantics of PS2Native's own, which the self-test finds to be the recompiler's on every case tried, or the console's where a recording shows the recompiler wrong; what is left is listed with its cause. The two known differences (`VRSQRT` of zero by zero, `VSQRT` of -0) closed. Every test passes, and the game is still the stock run, VU0's registers included, and the interpreted run. The level run's time measured. |
| G8 The BIOS's EE code ([ADR 0017](adr/0017-bios-ee-code-compiled-as-step-g8.md)) | The BIOS's code that runs on the EE under PCSX2's fast boot (the reset code in the ROM, the kernel, the program loader `EELOAD`, the idle thread `EENULL`) compiled ahead of time from the dump the build is given, into the build tree and never the repository ([ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md)). In order, each part checked before the next: what the BIOS runs, measured; the recompiler reads a BIOS image; the runtime runs the BIOS's images beside the game's; compiled code carries out what the BIOS needs and the game did not (blocks counted double, the loader's hooked blocks); the suite and the game with the BIOS compiled. | Every test passes with the BIOS compiled, the `lockstep` and `same-as-stock` comparisons included. From reset on, no instruction of the BIOS is interpreted in any test program or run of the game, or each one left is listed with its cause. The game is still the stock run and the interpreted run. The level run's time, and the test programs' boot, measured. |
| G9 VU0's instructions in compiled code ([ADR 0018](adr/0018-vu0-instructions-carried-out-by-compiled-code.md)) | Compiled code carries out a VU0 instruction itself, with G7's semantics inline, where the runtime's record of its block says nothing is to be done about VU0 first and no cycles are to be added first; elsewhere the runtime, as before. Measured on the level run and with the profile. | The compiled game is still the stock run and the interpreted run, and every test passes. What it gave back is reported as measured. |
| G10 Performance ([ADR 0019](adr/0019-performance-as-the-research-lays-it-out.md)) | As the maintainer's research lays it out: gameplay measured first, startup apart from play, with nothing drawn and with the hardware renderer, and profiled; then the expensive paths the profile names, one change at a time, each measured, profile-guided optimization among them. What would change how time is counted, or what is compared with stock PCSX2, goes to the maintainer first. | Every change keeps the compiled game the stock run and the interpreted run, and every test passing. What each gives back is reported as measured, against stock PCSX2 timed in turn. |
| G11 The vector units' micro programs ([ADR 0022](adr/0022-vu-micro-programs-compiled-as-step-g11.md)) | The micro programs the game runs on VU0 and VU1 compiled ahead of time by PS2Native's own translator, in the place of PCSX2's VU recompiler (microVU), following it as every family follows PCSX2's recompiler ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)). Measured first: what is written into micro memory, what is started where, how long it runs. VU1's registers then held to the stock run at every sync point, as the EE's and VU0's are. Then PS2Native's own VU interpreter, on semantics of its own, VU0's code first: the reference, and what runs code a build has not compiled. Then the translator, checked against it. | The game's micro programs are carried out by code compiled ahead of time, or by PS2Native's VU interpreter where a build has not compiled them, each such place listed; none by PCSX2's VU recompiler. The self-test finds the semantics to be microVU's on every case tried. Every test passes, and the game is still the stock run, VU1's registers included, and the interpreted run. The level run's time measured against stock PCSX2. |

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
  `vmadda` 352 and `vcallms` 230 are the most frequent. (Since G7 they are
  PS2Native's own, all but `vcallms` and `vcallmsr`.)
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
stock run's at every sync point. (Both closed at G7, where the VU0
instructions became PS2Native's own.)

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

## G6: speed (Windows, 2026-11-12)

The step [ADR 0015](adr/0015-a-speed-step-before-phase-3-ends.md) added:
the generated code made faster, one change at a time, each measured by
itself. The measure is G5's: the 17,500 frames of `level1.pad` with nothing
drawn, on the processor's fast cores, three rounds, and the profile of one
more run. Stock PCSX2 takes 74.2 s for it (G5). Every round below ended at
the cycle count the stock run ends at (103,213,332,030) with the sound
hardware in the same state, and interpreted none of the executable's code.

### What each change gave

| | Change | Seconds, three rounds | Mean | Against stock PCSX2 |
|---|---|---|--:|--:|
| | Before (G5) | 100.1, 100.0, 100.3 | 100.1 | 1.35 |
| 1 | One look at the address table for each load and store | 96.7, 96.1, 95.7 | 96.2 | 1.30 |
| 2 | General registers in local variables within straight-line code | 94.5, 95.0, 95.1 | 94.9 | 1.28 |
| 3 | The cycle count in a local variable; what a block's start learns no longer kept in memory | 94.1, 94.0, 94.3 | 94.1 | 1.27 |
| 4 | The runtime remembers where it found a VU0 instruction's step | 92.5, 92.3, 93.3 | 92.7 | 1.25 |
| 5 | The FPU's registers in local variables | 93.1, 92.0, 92.0 | 92.4 | 1.25 |
| 6 | Calls between compiled functions: no registers saved, the start tested first | 91.9, 92.0, 91.0 | 91.6 | 1.23 |
| 7 | Registers held in locals across labels, where every way in holds them | 91.1, 90.3, 90.7 | 90.7 | 1.22 |
| | The same, rebuilt with line tables only (below), again | 91.6, 91.2, 91.2 | 91.3 | 1.23 |
| | The same code once more, the generator's version raised to 0.4.0 | 92.0, 91.7, 92.9 | 92.2 | |
| | Again, right after stock PCSX2 was timed at 75.9, 75.7, 75.2 s | 91.2, 91.5, 92.4 | 91.7 | 1.21 |

**How far these numbers can be trusted.** Within three rounds of one build
the times differ by up to 1.2 s. The last four rows are the same generated
code, and their means range from 90.7 s to 92.2 s. Part of that is the
build (each relink lays the host out differently); part is the machine:
stock PCSX2 took 74.3, 74.4 and 74.3 s, and an hour later 75.9, 75.7 and
75.2 s. So a change worth less than about a second is not
shown by these numbers to have helped, and changes 3 to 7 are each near
that edge; what holds is the whole, measured against a stock run timed
beside it: from 1.35 times stock PCSX2's time at G5 (100.1 s against
74.2 s) to 1.21 (91.7 s against 75.6 s), with the game the stock run
throughout (below).

What each is:

1. **One look at the address table.** A load or store asked the core's
   table twice whether its address was plain memory: once to tell the
   runtime if it was not, once to reach it. Now once (`PS2N_ACCESS`,
   `PS2N_LOAD`, `PS2N_STORE` in `runtime/include/ps2n/recomp.h`). The loop
   G5 looked at went from 107 host instructions a turn to 71. 3.9 s.
2. **Registers in local variables.** Within straight-line code a generated
   function keeps the general registers it works on in local variables,
   which the C++ compiler keeps in host registers, and puts the changed
   ones back before anything outside the function can look: the end of a
   block, a call of the runtime, an instruction that is carried out through
   register numbers (execution model, "Registers in local variables"). At a
   label no local holds anything. 649,561 of the executable's 1,003,452
   instructions (64.7%) are carried out this way. The loop went to about 50 host
   instructions a turn; the run gained less than that promised. 1.3 s.
3. **The cycle count in a local variable.** Every block ends by adding its
   cycles to the core's count and comparing the sum with the time of the
   next event. The function now keeps the count in a local, writes each
   sum to the core and takes the count again only after a call that may
   have moved it (`PS2N_TIME`), so that a short loop no longer waits for
   the count to come back from memory at every turn. And what a block's
   start learns from the runtime's table went through a variable whose
   address was handed to the runtime, which kept it in memory: every block
   began with a store. 0.8 s.
4. **The runtime's shortcut for VU0 instructions.** The game's VU0
   instructions go through the runtime one by one (158 million in this
   run), and for each the runtime looked up the block that was running and
   the instruction's place in it. It now remembers where it found each of
   the last few thousand, for as long as no block is formed or thrown away
   (`StepAt()` in `runtime/ee_provider.cpp`). 1.4 s.
5. **The FPU's registers in local variables.** The FPU's instructions
   took their operands from the core's registers in memory and wrote their
   results back there, so a chain of them waited on memory at every link.
   They now work on locals, as the general registers do
   (`f0 = val::ADD_S(f1, f2)`, `facc = val::MADDA_S(facc, f1, f2)`; the
   functions the interpreter uses are made of the same). 0.3 s.
6. **Cheaper calls between compiled functions.** Every compiled function
   saved eight host registers when it was entered and restored them when it
   left, because the platform's convention says a function must keep them
   for its caller; nothing a compiled function holds in host registers
   lives across such a call. Under clang the functions now have a
   convention that keeps none (`preserve_none`, `PS2N_CC` in `recomp.h`).
   And a call, which enters at the function's start, no longer goes through
   the search the entry `switch` makes over all the function's blocks: the
   start is tested first. 0.8 s.
7. **Registers held across labels.** At a label the locals held nothing:
   every register was put back before it and taken from the core again
   after it. In a loop that happens at every turn, and on the loop's
   critical path, for a value written to memory a few instructions before
   has to come back from there: the profile's hottest instructions, in the
   loop of `0014FD68`, were waiting for exactly that. Now a label holds
   what the code after it reads, where every way into it holds that; a way
   in that holds nothing (the return from a call) takes it from the core,
   and so does an entry from outside the function, through a way in of its
   own (`E_` and the label's address). The generator works the labels out
   over a few passes over each function. What was changed still goes to
   the core at every block's end, for the event test that may follow; the
   register stays in its local. 79,343 of the executable's 197,808 block
   leaders hold registers. 0.9 s.

The whole suite passed after each of changes 1, 2, 3, 5, 6 and 7 (453 tests);
change 4 was tested with change 5. The game's own comparisons, with the
stock run at every sync point and with the interpreted run at every event
test, were made on the last of them:

- **Against stock PCSX2, over the whole level script** (17,500 frames,
  `level1.pad`, 20 minutes for both runs): the same place at the same cycle
  count at each of 93,221,937 sync points; the same picture at each of the
  1,750 frames compared; the sound hardware in the same state at each of
  the 1,750 moments compared. The registers differ where they did at G4 and
  nowhere else: the FPU's, at 964 sync points, the first at sync point
  57,130,066, where the console is followed on purpose ("What differs, and
  why").
- **Against the interpreted run** (4,200 frames, `start.pad`, 5 minutes):
  the same at each of the 47,705,744 event tests, as at G3.

### Tried and taken out again: main memory without the address table

A third of the compiled code's time is spent reaching memory (below), so
the next thing tried was to leave the address table out of it: the runtime
measured, in the table itself, the range in which the table maps main
memory in its usual place, measured it again whenever the core reported a
mapping changed, and a load or store whose address was in that range read
and wrote main memory directly, with one comparison in the place of the
look at the table. It was built twice and measured against the 92.7 s of
change 4:

| What reached memory directly | Seconds, three rounds | Mean | The executable |
|---|---|--:|--:|
| (Change 4, for comparison: nothing) | 92.5, 92.3, 93.3 | 92.7 | 188 MB |
| Every load and store | 96.0, 95.9, 95.8 | 95.9 | 215 MB |
| Only the loads and stores of general registers the generator writes out | 93.4, 93.6, 94.0 | 93.7 | 210 MB |

Both were slower than what they replaced, so the change is not in the code.
What it showed is worth keeping:

- **The kernel leaves the first 512 KB of the address space unmapped for
  programs.** Once the BIOS has set its TLB up, the table has main memory
  in place from `00080000` to `02000000` and a handler below: a load
  through a null pointer finds no memory there. A first build that
  took main memory to begin at address zero found none in place at all and
  ran every access the long way (109.5 s). The range has to have two ends.
- **The loads themselves did get faster**: in the profile of the first
  build the loads of general registers had 16% fewer samples than after
  change 3 (4,088 against 4,880). But the instructions carried out through
  register numbers, the FPU's loads and stores among them, had 40% more
  (3,198 against 2,284), and nearly everything else a few percent more.
- **The code grew by an eighth**, because each access now carried the look
  at the table as well, beside the direct path. Fewer host instructions on
  the path taken did not make up for that. It says something about where
  the time is: in a program of a million instructions, how much code the
  host has to get through counts for as much as how many instructions each
  access costs.

Not tried: the same with the look at the table as a call of one shared
function, which would make the code smaller than it is now, not larger.

### Tried and taken out again: memory through PCSX2's fast-memory area

After change 7 the next thing tried was the largest part left, reaching
memory. PCSX2 keeps a second mapping of the console's whole address space
in the host's, its fast-memory area, in which plain memory is mapped and
devices' pages are not; its recompiler reaches memory there with one
instruction and lets an access to a device's page fault. Compiled code did
the same: each load and store became one instruction at the area's base
plus the address, written as `asm goto` and listed in a table of fix-ups
(a section of its own) with the label of its long way, the address table
as before; a handler the runtime installed after PCSX2's own sent a
faulting instruction to its long way; and addresses whose top four bits
say "devices" (1, 9, B) went the long way at once, so that a loop reading
a device's register does not fault every time.

It worked: the whole suite passed (453 tests; in 64 of its runs an access
faulted and went the long way, correctly), and the waiting loop of
`001C0190` lost its six looks at the table. It was not faster: 92.2, 91.6
and 91.8 s against 91.3 s. In the profile the loads and stores did take
less time (6,167 samples against 7,142), and nearly everything else took
more by as much, the runtime's code and PCSX2's core included: the
executable grew from 184 MB to 229 MB, every access now carrying the fast
instruction and its long way both, and the table 10 MB of its own. The
waiting loop took no less time at all (1,565 samples against 1,503): its
time is in the loop's dependence on what it read and wrote the turn
before, not in the arithmetic of the address. Taken out. Three changes of
this kind have now shown the same thing: in a program of a million
instructions, how much code the host
has to get through counts as much as how many instructions each access
costs.

### Tried and taken out again: a label holding what any way into it holds

After change 7, `0014FD68`'s loop still took two registers from the core
at every turn: they are read in the loop, but a way into the loop's labels
(the return from a call the loop makes now and then) does not hold them,
and a label held only what every way in holds. The other rule was tried:
a label holds what its code reads if any way in holds it, and a way that
does not takes it from the core before it goes in. 90,911 leaders held
registers (242,352 in all) instead of 79,343 (142,252), and the run took
91.3, 91.2, 91.8 s, 0.7 s longer than with change 7: the ways that now take
registers they would not have needed cost more than the loops gained.

### Tried and taken out again: the start test without its mark

The start test of change 6 is marked `[[likely]]`. The profile after it
showed `val::ADD_S`, `SUB_S`, `MUL_S`, `MADD_S` and `DIV_S` as functions of
their own in the runtime, called from compiled code, where before they had
been inlined: a trial compile of one of the game's files, with and without
the mark, showed the mark to be the cause (the compiler takes the rest of
the function for colder and inlines less there; the calling convention
had nothing to do with it). Without the mark the run took 91.1, 91.3,
91.7 s, 0.7 s longer than with it. The mark stays.

### The profiler and the size of the debugging information

After change 7 the profiler could no longer read the host's debugging
information (DbgHelp: "file system or network error reading pdb"; 578 MB).
Generated code is now compiled with line tables and the functions' names
only (`-gline-tables-only`, `tests/tests.cmake`), which is all the profiler
reads: the PDB is 254 MB, a file of generated code compiles in 8 s instead
of 11, and the machine code is the same (one file compared: 1,276,251 bytes
of code either way).

### Tried and not built: moving the paths almost never taken out of the way

The paths a function almost never takes (a load from a device's registers,
a block the runtime has to be asked about, an event to handle) are written
out inline, beside the path it takes, and make the code larger. Clang can
move such paths out of a function (`-hot-cold-split`), given the runtime's
entry points marked as rarely called. Tried on one of the game's 64 files,
it moved 22 KB of its 1.26 MB of code, 1.8%: after such a call the path
rejoins the one taken, and only the call itself can be moved. Not worth a
build.

### Where the time is now

The profile of the last build (91.3 s, 60,445 samples), beside G5's:

| | G5, stock PCSX2 | G5, compiled | G6, compiled |
|---|--:|--:|--:|
| **The game's own code**: PCSX2's EE recompiler's code, or the code compiled ahead of time | 25.8 | 43.6 | 33.1 |
| PS2Native's runtime | | 7.6 | 8.0 |
| PCSX2's core | 26.8 | 29.6 | 28.9 |
| The code PCSX2's recompilers for the VUs and the IOP generated | 16.7 | 17.2 | 16.9 |
| Windows, drivers, other libraries | 4.4 | 4.0 | 3.9 |

The game's own code with its runtime took 51.2 s at G5 and takes 41.1 s
now, against 25.8 s as the code PCSX2's recompiler generates. (Some of the
FPU's semantics are now called, not inlined, after change 6, and count as
the runtime's.)

Since change 2 the profile also says what kind of generated line a sample
in the compiled code fell on, and which instructions had the most samples
(`host/profile.cpp`; the compiler moves and merges instructions, so this is
a guide, not a count). Of the 33.1 s:

| Kind of generated line | Share | Seconds |
|---|--:|--:|
| Loads into general and FPU registers | 25.3% | 8.4 |
| FPU arithmetic and comparisons | 17.3% | 5.7 |
| The start of a block: what the runtime knows of it | 9.7% | 3.2 |
| The end of a block: its cycles, the test for events | 9.1% | 3.0 |
| Stores of general and FPU registers | 7.4% | 2.4 |
| A function's entry and exit | 5.8% | 1.9 |
| Registers put back from locals | 5.6% | 1.9 |
| Arithmetic on locals | 5.4% | 1.8 |
| Loads and stores through register numbers (the 128-bit ones, and others) | 4.0% | 1.3 |
| Registers taken from the core into locals | 2.7% | 0.9 |
| Everything else | 7.7% | 2.5 |

- **More than a third is reaching memory** (12.1 s). The instructions with
  the most samples, in the loops where the game waits, are waiting for the
  address table's entry before the access, and then for the data. PCSX2's
  recompiler reaches plain memory with one instruction through a second
  mapping of the whole address space, and lets an access that is not
  plain memory fault (then patches its own code). Doing the same in
  compiled code was tried next, and was not faster: the accesses got
  cheaper and everything else slower, with the larger code ("Tried and
  taken out again", below).
- **A sixth is the FPU's arithmetic** (5.7 s). It works on locals now; what
  is left is the arithmetic itself, the same host operations PCSX2's
  recompiler emits to reproduce the EE's rounding and clamping, and the
  clamping settings, which PCSX2's recompiler reads when it compiles and
  the generated code at every instruction.
- **A fifth is where blocks begin and end** (6.2 s): what counting time
  exactly as PCSX2's recompiler does costs
  ([ADR 0011](adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)).
  PCSX2's own code pays for the ends too, not for the starts.
- The two functions in which the game waits are 5.9 s (`0014FD68`,
  `001C0190`), from 9.9 s at G5.
- In the runtime, the largest part is still the VU0 instructions, which go
  to PCSX2's interpreter functions one by one (`cop2Instruction`,
  `Cop2Operation`: 2.4 s), then the dispatcher (1.0 s).

## G7: VU0's instructions (Windows, 2026-11-12)

The step [ADR 0016](adr/0016-vu0-instructions-with-semantics-of-ps2natives-own.md)
added: the VU0 instructions in the game's EE code, which PCSX2's interpreter
functions carried out until now, given semantics of PS2Native's own that
follow PCSX2's recompiler, as every other family's do.

### What was built

- **`runtime/ops_vu0.cpp`** ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)):
  the 82 VU0 instructions PS2Native's decoder knows, all but `VCALLMS` and
  `VCALLMSR`, as microVU compiles them in macro mode: the same host SSE
  instructions on the same operands in the same order, under the EE's
  control register, with microVU's clamps in each of PCSX2's VU clamp modes,
  its special cases, its flags, and the three game fixes that change what
  these instructions compute (the I-bit fix, the VU add/sub fix, the VU
  overflow fix). The floating-point operations are written as inline
  assembly, so that the C++ compiler can neither swap the operands of an
  addition or a multiplication (which decides which of two NaNs comes out)
  nor compute one itself (it would round to nearest and keep denormals).
  Each component is computed in its own lane, all four at once; microVU
  computes a single component in the first lane of a register, with the
  same operations, so the results are the same.
- **The runtime calls them** where it called PCSX2's interpreter functions
  (`Cop2Operation` in `runtime/ee_provider.cpp`), and tells them what the
  block decides: whether the instruction's flags reach the status and MAC
  registers (execution model, rule 16). What put the interpreter's flags
  right after each instruction (`FollowRecompilerFlags`) is gone.
- **`VCALLMS` and `VCALLMSR` stay PCSX2's function**, which its recompiler
  calls too. The micro programs they start run on PCSX2's VU0 recompiler,
  a stand-in of [ADR 0003](adr/0003-no-jit-in-generated-games.md), as
  before.
- **`CTC2` to the clip flag register** writes the register alone, as the
  recompiler does. It also wrote the copy of the register that only
  PCSX2's interpreter functions read, for as long as they carried out
  `VCLIP`.
- **PCSX2's VU overflow game fix** is reproduced for these instructions; the
  provider no longer warns that it is not.

### The self-test

`ps2n-host -selftest` gives every one of the 82 VU0 instructions (`VCALLMS`
and `VCALLMSR` are not among them) random and boundary operands: VU0's
registers and accumulator filled with floats of every kind, its data
memory and VU1's registers with random bits. It holds what PS2Native's
function leaves to what
PCSX2's recompiler leaves, compiling a block of the instruction alone for
each case: VU0's registers, accumulator and data memory, VU1's
floating-point and integer registers (which VU0 reaches from address
0x400), the EE's registers and memory. Not to PCSX2's interpreter, which
computes these differently. In each configuration of the core that changes
what these instructions compute:

| Configuration | Cases | Differing |
|---|--:|--:|
| VU clamp mode Normal (PCSX2's default) | 32,800 | 0 |
| VU clamp mode None | 32,800 | 0 |
| VU clamp mode Extra | 32,800 | 0 |
| VU clamp mode Extra + Preserve Sign | 32,800 | 0 |
| Clamp mode None, with the I-bit, VU add/sub and VU overflow game fixes (Killzone's configuration, below) | 32,800 | 0 |
| Clamp mode Normal, with the three game fixes | 32,800 | 0 |

That is 400 cases an instruction in each, as the suite runs it; a run with
4,000 an instruction (1,968,000 cases; the whole self-test took 49 s) found
no difference either.
The first build differed from the recompiler in one thing: with no
component named in their mask, `VSQI` and `VSQD` store all four (the
default case of microVU's `mVUsaveReg`), and then read VF00 as a cleared
register. That is in now.

What following the recompiler means where it is not the console (all of it
in [ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)): the host's rounding and
denormals under the EE's control register; no overflow or underflow flags;
clamping as the VU clamp mode says, except an operand read from VF00 and
the result computed in its place; a register minus itself is +0; `VMAX`
and `VMINI` compare as signed magnitudes; a zero divisor gives the largest
float, and the invalid and division flags are microVU's; `VCLIP` reads a
denormal as +0; the integer registers are 16 bits wide; `VOPMULA` and
`VOPMSUB` with a single component in the mask compute what microVU
computes then, which is not the console's outer product. The two
differences from the recompiler known before (`VRSQRT` of zero by zero:
the flags and the result; `VSQRT` of -0: the invalid flag) are among the
cases the self-test holds to the recompiler now: they are closed.

### The game

PCSX2's game database sets two things for Killzone that concern these
instructions, and `ps2n-host` applies them as PCSX2 does: the VU clamp mode
None, so that nothing of VU0's is clamped, and the I-bit game fix, with
which microVU takes `VIADDI`'s immediate from the first word of VU0's micro
memory. Both are among the configurations above.

- **Against stock PCSX2, over the whole level script** (17,500 frames,
  `level1.pad`, 20 minutes for both runs): the same place at the same cycle
  count at each of 93,221,937 sync points, the same picture at each of the
  1,750 frames compared, the sound hardware in the same state at each of the
  1,750 moments compared. VU0's registers, its flags among them, are the
  stock run's at every sync point. The one difference is the one of G4 and
  G6: the FPU's registers, at 964 sync points, the first at sync point
  57,130,066, where the console is followed on purpose.
- **Against the interpreted run** (4,200 frames, `start.pad`, 5 minutes):
  the same at each of the 47,705,744 event tests, with the same fingerprint
  of the whole run as at G6 (`F99B5EFD472E87EC`).
- **What is still PCSX2's function**: of the level run's VU0 instructions,
  `vcallms` (49,922,683 times) and `vcallmsr` (1,598,148), as in PCSX2's
  recompiler. Until now all 158,299,586 went to PCSX2's functions (G4);
  the other 106,778,755 are PS2Native's now.

### The time

The level run as at G5 and G6 (17,500 frames, nothing drawn, the
processor's fast cores), timed twice, each time beside stock PCSX2:

| | Seconds, three rounds | Mean |
|---|---|--:|
| Stock PCSX2 | 74.1, 73.6, 73.4 | 73.7 |
| Compiled, PS2Native's VU0 instructions | 88.7, 91.7, 91.0 | 90.5 |
| Again, the two in turn, on a quiet machine: stock PCSX2 | 75.5, 75.7, 77.0 | 76.1 |
| Compiled | 91.4, 90.9, 93.1 | 91.8 |

The second set is the one to go by: 1.21 times stock PCSX2's time, each
pair 1.20 or 1.21, where G6 ended at 1.21 to 1.23, so no change that these
numbers can show. During the second and third compiled rounds of the
first set another Claude session was starting on the same machine, and
those two rounds are the slowest of that set; it gave 1.23. Every round
ended at the stock run's cycle count with the sound hardware in the same
state, and interpreted none of the executable's code. The profile of one
more run says
why nothing changed: the VU0 instructions' time moved, it did not shrink.
PCSX2's core lost its VU interpreter functions (28.2 s against 28.9 s at
G6), PS2Native's runtime gained their replacement (8.8 s against 8.0 s).
What costs is not the arithmetic but the way to it, as before: each of the
158 million instructions is handed by the compiled code to the runtime,
which finds its step in the block the recompiler would have formed, does
what that block does about VU0 first, and dispatches on the instruction
(`cop2Instruction` 1,160 samples, `Cop2Operation` 405, `ExecuteVu0` 118;
of the arithmetic, `FMACa` has 166 and each of the others 61 or fewer).
The way to make that cheaper would be for the generated code to call the
semantics itself where its block says what to do about VU0; that is not
built.

## G8: the BIOS's EE code (Windows, 2026-11-12)

The step [ADR 0017](adr/0017-bios-ee-code-compiled-as-step-g8.md) added,
which [ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md) decided:
the BIOS's code that runs on the EE compiled ahead of time, where the
interpreter carried it out until then.

What was known of its cost before the step: the level run interpreted
173.5 million of the BIOS's instructions (0.21% of the EE's cycles; "G4"),
about a second of its 92 s by the samples of G7's profile; every test
program interprets at least 34.2 million at boot, which cost 0.31 to
0.46 s against stock PCSX2 (proof-of-concept log, "P4").

### What of the BIOS runs on the EE, measured

`ps2n-host` boots with PCSX2's fast boot, which goes from the reset code to
the program loader and on to the game, without the BIOS's own menu. Since
this step the host says what of the BIOS the interpreter ran: by where its
blocks begin, before the executable reaches its entry point (the boot) and
after; the instructions in blocks counted double, which compiled code does
not carry out, and the runs of the loader's hooked blocks; and the files
the EE runs from RAM compared with the ROM's, word for word, at the entry
point and at the end (`ps2n-host -bios-capture <file>` writes the block
starts, and the words that differ; the test driver passes it in every run
with compiled code). Five files of the ROM's directory
(`ps2n/romdir.h`) run on the EE (`SCPH-70004`, read where it is):

| File | Size | Where the EE runs it |
|---|--:|---|
| `RESET` | 10,048 bytes | in the ROM, at `BFC00000`, and part of it through the ROM's cached mirror (`9FC00C00`-`9FC00E94`) |
| `RDRAM` | 12,356 bytes | in the ROM, through its cached mirror (`9FC41000`): the set-up of main memory |
| `KERNEL` | 93,736 bytes | copied to `80000000` |
| `EENULL` | 64 bytes | copied to `00081FC0`: the idle thread |
| `EELOAD` | 61,840 bytes | copied to `00082000`: the program loader |

The addresses in RAM are PCSX2's (`EEKERNEL_START`, `EENULL_START` and
`EELOAD_START` in `pcsx2/R5900.h`); the hooks it puts into `EELOAD` for fast
boot depend on them. `KERNEL` begins with the code of the exception vector
at `80000000`. The reset code holds the IOP's too: both processors start at
`BFC00000`, and its first instructions send each its own way.

The game's first 4,200 frames (`start.pad`, compiled, nothing drawn), and
one test program (`sdk/iop_rpc`, compiled), in instructions:

| Where the blocks begin | Game: at boot | Game: after | Game: block starts | `iop_rpc`: at boot | `iop_rpc`: after |
|---|--:|--:|--:|--:|--:|
| The ROM | 7,077,464 | 0 | 338 | 7,034,267 | 0 |
| `KERNEL` | 14,937,669 | 175,380,067 | 814 | 14,959,682 | 153,581 |
| Kernel memory past `KERNEL` | 0 | 288 | 8 | 0 | 1,036 |
| `EENULL` | 1,213,188 | 3,946,318 | 3 | 1,076 | 39,806 |
| `EELOAD` | 11,072,305 | 0 | 216 | 11,069,979 | 0 |

- **The boot is 34 million instructions**, whatever the program: 7 million
  in the ROM, 15 million in the kernel, 11 million in the loader. Of the
  game's 213.6 million interpreted instructions, the other 179 million are
  the kernel and the idle thread while the game runs.
- **Blocks counted double are not what makes the boot long**: 48
  instructions in all are in such blocks. The loader's hooked blocks run
  twice.
- **The code in memory is the ROM's.** When the executable reaches its
  entry point, 46 words of `KERNEL` differ from the ROM's file, and none of
  them is an instruction: variables at `800010C0`-`800010E4`, saved
  registers, the tables of handlers at `800153C0`-`8001543C` (their
  defaults replaced), the path of the executable that was started. By the
  end, addresses have been put into the kernel's tables at
  `80014F60`-`8001533C` where the ROM's file has the kernel's own (or
  zero): addresses in the game's code, and at `80075000` and `80076000`.
  `EENULL` is the ROM's word for word. 9 words of `EELOAD` differ, all
  data; after the game starts, its memory is used for something else
  (12,671 of its words differ at the end), so the loader only matters at
  boot.
- **Code past `KERNEL` in kernel memory**: the 8 block starts there are at
  `80075000` and `80076000`, where some of those table entries lead. None
  of it ran before the game started. It is the game's own ("What is still
  interpreted, and why", below), so it belongs with the game's image, not
  the BIOS's.
- 1,379 distinct block starts in all, in the BIOS's places, over the game's
  4,200 frames.

With these counts in, the whole suite passes (2 unit test programs and 453
tests).

### The recompiler reads a BIOS image

`ps2n-recomp bios <BIOS image> --out-dir <dir> [--capture <file>]...` makes
four images of the BIOS's EE code, each of files that lie close together
where the EE runs them (`recompiler/src/bios.cpp`): `bios_rom0` and
`bios_rom1`, `RESET` and `RDRAM` in the ROM through its cached mirror and at
`BFC00000`; `bios_kernel`; and `bios_loader`, `EENULL` and `EELOAD`. An
image is marked as the BIOS's, with the CRC of the whole ROM it was made
from (`ImageInfo::kind`, `recomp.h`). `ps2n-recomp bios-info` says what the
analysis finds, without writing anything.

The kernel's file holds data among its code: the variables at
`800010C0`-`800010E4` come right after one of its functions. A sweep of the file, as an
executable's code is swept, would put such data inside functions, where the
fingerprints the runtime compares with memory would cover it, and every
block there would be interpreted once the kernel had written to it. So the
code of a BIOS image is followed instead of swept (`Options::followCode` in
`recompiler/src/analysis.cpp`): from the reset vector, the kernel's five
exception vectors, the entries of `EENULL` and `EELOAD`, and the block
starts runs report (`-bios-capture`), through every branch, jump and call,
and every jump through a register the instructions just before make a
constant. Where one image's code goes into another (the reset code into
the ROM's cached mirror, and into the kernel and the loader once it has
copied them), the other is followed from there too. Nothing is taken for
code on a guess. Unit tests hold the analysis to a program of code with
data among it (`tests/unit/recomp_tests.cpp`).

With the capture of the game's first 4,200 frames (`SCPH-70004`):

| Image | Functions | Instructions | Block leaders | Captured entries in it | Found without the capture |
|---|--:|--:|--:|--:|--:|
| `bios_rom0` | 33 | 2,733 | 428 | 326 | 253 |
| `bios_rom1` | 6 | 437 | 57 | 12 | 12 |
| `bios_kernel` | 226 | 15,141 | 2,812 | 814 | 404 |
| `bios_loader` | 61 | 1,837 | 427 | 219 | 189 |

Half the kernel's entries are only reached through its tables of handlers,
which nothing follows; runs report them, as they report a game's. None of
the words step 1 found changed in memory lies inside a function: the one
before the variables ends at `800010C0`. Of the 20,148 instructions, one in
each image but the loader's is delegated (`tlbwi`), and the kernel has
three places where the rest of a block is left to the interpreter and 20
likely branches left to it when taken.

### The runtime runs the BIOS's images beside the game's

The runtime held one image, the executable's, chosen at its entry point.
It now holds the images in use, whose code ranges do not overlap
(`runtime/ee_provider.cpp`): the BIOS's, from the first instruction on,
when the CRC of the whole ROM is the one they were made from; and the
executable's, from its entry point on. A PC finds its image and function
through the dispatcher's cache as before. The build compiles the BIOS when
it is given a dump (`build.ps1 -Bios <file> -BiosCapture <file>`), into the
build tree; `ps2n-host -bios-static off` leaves the BIOS to the interpreter
in a host that has it compiled.

Compiled code now also carries out the two blocks of the program loader
that PCSX2 hooks for fast boot: the runtime calls the hook when compiled
code reaches the block, at every execution, as PCSX2's recompiler does at
the block's start.

The first build had the capture of the game's first 4,200 frames. Every
test run with compiled code reports what it had to interpret of the BIOS
(the test driver passes `-bios-capture`); the 69 such runs of the suite
added 155 block starts, mostly kernel code the game never called (threads
put to sleep and woken), and one more build had them (1,534 in the BIOS's
capture). With that build:

| | Before (BIOS interpreted) | BIOS compiled |
|---|--:|--:|
| Instructions interpreted in a compiled test run, over the suite's 202 | 34 to 43 million | 51 to 1,087 |
| Instructions interpreted in the game's first 4,200 frames | 213,627,299 | 339 |
| The game's EE cycles that were the interpreter's | 1.11% | 486 cycles of 24.8 thousand million |

- **The whole suite passes** with the BIOS compiled (2 unit test programs
  and 453 tests): every `lockstep` test holds the compiled BIOS to the
  interpreted one at every event test, every `same-as-stock` test to stock
  PCSX2 at every sync point.
- **The game's first 4,200 frames are the interpreted run** at each of
  47,705,744 event tests, with the same fingerprint of the whole run as at
  G6 and G7 (`F99B5EFD472E87EC`; 307 s for both runs).
- **The level run is the stock run** (17,500 frames, `level1.pad`, 21
  minutes for both runs): the same place at the same cycle count at each of
  93,221,937 sync points, the same picture at each of the 1,750 frames
  compared, the sound hardware in the same state at each of the 1,750
  moments compared. The registers differ only where they did at G4, G6 and
  G7: the FPU's, at 964 sync points, the first at 57,130,066, where the
  console is followed on purpose. The compiled run interpreted 339
  instructions, where it interpreted 173.5 million at G4.

### What is still interpreted, and why

Of the BIOS's own code, in every run, 51 instructions at boot:

- **The first three blocks after reset** (`BFC00000`, `BFC00014`,
  `BFC00800`; 48 instructions) are counted double: they are formed before
  the reset code sets `Config`, and compiled code counts every block once
  (execution model, rule 3). Carrying them out compiled would change how
  generated code counts time, for 48 instructions once a boot.
- **`di` followed by `eret`** at `8000517C`, once a boot: compiled code
  leaves a `DI` before a system instruction to the interpreter (execution
  model, rule 8).

And code that is not the BIOS's: what programs copy into kernel memory and
then run there (`80075000`, `80076000`; 288 instructions in the game's
first 4,200 frames, the same in the whole level run, about 1,000 in a test
program built with ps2sdk). The words are in the game's executable (at
file offsets `0x3D9300` and `0x3D8B20`) and in the test program's, and not
in the ROM: the programs' libraries put them there, and install them as
system calls. The old count of "instructions in the kernel's segments"
took them for the BIOS's.

### The game's code in kernel memory, compiled with the game

Code where no image has a function was interpreted every time, and its
words were never captured (execution model, "Not covered yet" until now).
A run now writes the words of each such block it ran, outside the
executable's code and the BIOS's files, into the program's capture file
(`[[code]]`), and `ps2n-recomp compile --outside` makes an image of that
code beside the executable's: `game_outside`, with the game's CRC, its
code followed from where it ran as a BIOS image's is. The runtime uses it
with the executable's. One run of the level with `-Capture` recorded the 8
blocks; the next build has 2 functions there, 24 instructions.

With that build the game's first 4,200 frames interpret 51 instructions:
the 48 of the three blocks counted double at boot, and the `di`/`eret` at
`8000517C`. Of the BIOS's code and the game's, nothing else. The test
programs are built from their executables alone, without a capture, so
they still interpret what ps2sdk copies into kernel memory (about 1,000
instructions each).

With the final build, the suite passes again (2 unit test programs, 37 and
17 cases, and 453 tests).

### Rebased onto G9

G8 was made on G7 in the main working tree while another session made G9
([ADR 0018](adr/0018-vu0-instructions-carried-out-by-compiled-code.md)) in
a worktree of its own. G9 was committed first, and G8 was rebased onto it:
the code took both steps' changes without conflict (both change
`runtime/ee_provider.cpp` and `recompiler/src/codegen.cpp`, in different
places), the logs had both added, and G9's cache of COP2 steps is keyed by
place and block, not by image. On the two together (compiled code carrying
out the VU0 instructions itself, the BIOS compiled):

- **The whole suite passes** (2 unit test programs, 38 and 17 cases, and
  453 tests).
- **The game's first 4,200 frames are the interpreted run** at each of
  47,705,744 event tests (fingerprint `F99B5EFD472E87EC`, as before).
- **The level run is the stock run**: the same place at the same cycle
  count at each of 93,221,937 sync points, the same picture at each of the
  1,750 frames compared, the sound hardware in the same state at each of
  the 1,750 moments compared; the registers differ only in the FPU's, at
  the 964 sync points of G4, where the console is followed on purpose.
- Both compiled runs interpreted 51 instructions: those of the BIOS at
  boot.

### The time

Measured on G8 rebased onto G9 (below), the machine otherwise quiet (the
other session held its runs), every run on the fast cores. The level run
as at G5-G9 (17,500 frames, `level1.pad`, nothing drawn; the whole run),
three rounds, each round the three in turn; `-BiosStatic off`
(`ps2n-host -bios-static off`) leaves the BIOS to the interpreter in the
same build:

| Round | Stock PCSX2 | Compiled, the BIOS compiled | Compiled, the BIOS interpreted |
|---|--:|--:|--:|
| 1 | 75.1 | 87.3 | 88.0 |
| 2 | 73.6 | 86.7 | 87.8 |
| 3 | 72.9 | 86.6 | 88.1 |
| Mean | 73.9 | 86.9 | 88.0 |

All nine runs ended at the same cycle count (103,213,332,030) with the
sound hardware in the same state. The compiled runs interpreted 51
instructions with the BIOS compiled, and 173,512,808 without.

- **Compiling the BIOS gave back 1.1 s of the level run** (0.7, 1.1 and
  1.5 s, round by round): about what G7's profile had the interpreter's own
  functions at.
- **The compiled game takes 1.18 times stock PCSX2's time** over the level
  run (86.9 s against 73.9 s; each pair 1.16 to 1.19), where it took 1.19
  after G9 and 1.21 after G7.

The boot, five test programs (those of P4), each run three times under each
configuration, one run at a time; the whole test driver, start-up of the
core included, best of three, in milliseconds:

| Program | Stock PCSX2 | Compiled, the BIOS compiled | Compiled, the BIOS interpreted |
|---|--:|--:|--:|
| `sdk` `pad` (274 frames) | 1,697 | 1,658 | 1,794 |
| `sdk` `iop_rpc` (13 frames) | 1,497 | 1,507 | 1,650 |
| `sdk` `mc` (15 frames) | 1,500 | 1,490 | 1,609 |
| `cpu/ee/alu` (26 frames) | 1,452 | 1,456 | 1,609 |
| `kernel/ee/thread/terminate` (11 frames) | 1,493 | 1,453 | 1,595 |

- **With the BIOS compiled a compiled program takes the time it takes
  under stock PCSX2**, from 39 ms less to 10 ms more; with it interpreted,
  109 to 157 ms more. (P4 measured 307 to 456 ms more on 2026-11-11, with
  an older build; the two measurements are not compared here.)

## G9: VU0's instructions in compiled code (Windows, 2026-11-12)

The step [ADR 0018](adr/0018-vu0-instructions-carried-out-by-compiled-code.md)
added: what G7 found costly, the way to a VU0 instruction, made short.
Another session was compiling the BIOS's code (G8) in the main working tree
at the same time, so this step was made in a worktree of its own, with a
build tree of its own, and the two sessions agreed before timing anything.

### What was built

- **`PS2N_VU0`** in `ps2n/recomp.h`. What PCSX2's recompiler does about VU0
  before an instruction, and where the instruction's flags go, depends on
  the block the instruction is in, which only the runtime knows. The
  runtime already kept its last few thousand answers by address (G6, change
  4); compiled code now reads them itself (`cop2Sites`, `cop2Info()`): a few
  compares and a load, good for as long as no block is formed or thrown
  away. Where the block has VU0 brought up to date first, or the cycles so
  far added, the runtime carries the instruction out as before
  (`cop2Instruction`). Otherwise compiled code adds the instruction's cycles
  to its count and carries it out itself.
- **The semantics inline** ([ops_vu0.h](../runtime/include/ps2n/ops_vu0.h)):
  G7's functions, with the instruction's register numbers as constants,
  so that a `vmulax.xyzw` becomes the loads, the clamp mode's tests, a
  `mulps` and a store. Out of line: the flags, computed only where the
  block lets them reach the registers; the six instructions that reach
  VU0's data memory or VU1's registers, which may wait for VU1's thread;
  the VU add/sub game fix; and the runtime's table, which the interpreter
  and the self-test call. The interpreter's path is the same code.
- **The generator** (`ps2n-recomp` 0.5.0) writes `PS2N_VU0` for the 82 VU0
  instructions, all but `VCALLMS` and `VCALLMSR`, and no longer puts the
  registers it holds in locals back before them: they touch no register of
  the EE's. In the game's executable that is 913 of its 4,690 COP2
  instructions; the other 3,777 (the transfers, `VCALLMS`, `VCALLMSR`) are
  the runtime's as before.
- The first build called the lookup instead of inlining it: in functions
  this large the compiler did not inline it by itself. It is marked to be
  inlined now: where nothing is to be done first, the way to a VU0
  instruction makes no call.

### Still the stock run

On the build that was timed: the whole suite (453 tests and the unit
tests, a new one for `PS2N_VU0`); over the whole level script against
stock PCSX2 (17,500 frames, 22 minutes for both runs), the same place at
the same cycle count at each of 93,221,937 sync points, the same picture
and the same state of the sound hardware at each of the 1,750 frames
compared, and as the only difference the FPU one of G4 (964 sync points,
the first 57,130,066); against the interpreted run (4,200 frames), the same
at each of the 47,705,744 event tests, with the run's fingerprint of G6 and
G7 (`F99B5EFD472E87EC`). The build before it, which called the lookup, had
given the same.

### The time

The level run as at G7, the two timed in turn, while the other session ran
nothing:

| | Seconds, three rounds | Mean |
|---|---|--:|
| Stock PCSX2 | 75.8, 74.5, 74.4 | 74.9 |
| Compiled | 89.8, 88.1, 90.5 | 89.5 |

1.19 times stock PCSX2's time; the pairs 1.18, 1.18 and 1.22. G7's were
1.21, 1.20 and 1.21. The gain, about 0.02 of stock PCSX2's time, is at the
edge of what these numbers can show (G6, "How far these numbers can be
trusted"). Every round ended at the stock run's cycle count with the sound
hardware in the same state, and interpreted none of the executable's code.

The profile of one more run says where it came from:

| | G7 | G9 |
|---|--:|--:|
| PS2Native's runtime, samples (seconds) | 5,876 (8.8) | 4,951 (7.5) |
| of it `cop2Instruction`, `Cop2Operation`, `ExecuteVu0` and the VU0 functions | 1,849 or more | 1,137 |
| VU0 instructions in compiled code | | 129 (0.2) |

What is left on the runtime's path is `VCALLMS` and `VCALLMSR` (51.5
million in the level run), the VU0 instructions whose block has something
done about VU0 first, and the transfers (`LQC2`, `SQC2`, `QMTC2`, `QMFC2`,
`CFC2`, `CTC2`). The transfers could be given the same way in, with the
registers they read put back first: that is not built.

## G10: performance (Windows, 2026-11-12)

The step [ADR 0019](adr/0019-performance-as-the-research-lays-it-out.md)
added, in the order of the maintainer's research: measure gameplay and find
where the host's time goes; remove the expensive paths found; make the
generated code competitive with PCSX2's; use what is known ahead of time.

### The measurement

The host can now note the wall-clock time at chosen frames (`-mark`, which
`game.ps1 -Marks` passes on) and profile from a chosen frame on
(`-profile-from`, `game.ps1 -ProfileFrom`). The level script is the scene:
its startup is power-on, the logos, the menus and the level loading, to
frame 6,600; its play is frames 6,600 to 17,500, three and a half minutes
of play. It was timed with nothing drawn and with the hardware renderer
PCSX2 picks for this computer, which players would have: Direct3D 12, on a
GeForce RTX 4070 SUPER. No pictures and no comparison during the timing;
stock PCSX2 and the compiled game (as G9 left it) in turn, three rounds, on
the processor's fast cores, while the other session ran nothing.

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2, nothing drawn | 16.6, 16.1, 16.5 s | 57.3, 55.7, 56.7 s |
| Compiled, nothing drawn | 19.4, 19.1, 19.3 s | 68.7, 67.3, 68.6 s |
| Stock PCSX2, Direct3D 12 | 17.8, 17.1, 17.5 s | 56.7, 57.4, 56.5 s |
| Compiled, Direct3D 12 | 20.1, 20.5, 20.1 s | 67.9, 69.2, 67.7 s |

The host takes 1.0 s to its first frame either way. Startup takes 1.17
times stock PCSX2's time with nothing drawn and 1.16 with the hardware
renderer; play 1.21 and 1.20. With the hardware renderer play takes no
longer than with nothing drawn, for either: the thread that draws keeps
up, and the time is the CPU thread's. So the null renderer measures what
the compiled code changes, for players' settings as well.

The profile of play alone (nothing drawn; seconds at the profiler's 660
samples a second):

| | Stock PCSX2 | Compiled |
|---|--:|--:|
| The code PCSX2's recompilers generated, for the EE, the VUs and the IOP | 34.6 | |
| The code PCSX2's recompilers generated, for the VUs and the IOP | | 14.9 |
| The game's code compiled ahead of time, with PS2Native's runtime | | 30.9 |
| PCSX2's core | 19.0 | 20.0 |
| Windows, drivers, other libraries | 3.1 | 3.0 |

The VU and IOP code does the same work in both runs, so stock PCSX2's EE
code took about 19.7 s of play (34.6 less 14.9) where the compiled game's
took 30.9. That is the gap, 11 s of it:

- **Reaching memory**, about 9.4 s: loads 6.0 s, stores 1.8 s, the 128-bit
  ones and others through register numbers 1.1 s, `LQC2`, `SQC2` and `LQ`
  in the runtime 0.45 s. The hottest instructions of the whole run are the
  loads and stores of one loop (`fn_001C0190`, about `001C01B0`), each
  through the address table: a load of the table's entry before the load of
  the data, where PCSX2's code makes one access.
- **The FPU's arithmetic**, about 6.4 s, of which 0.83 s are calls of its
  helpers that the compiler did not inline (`val::MUL_S`, `ADD_S`,
  `SUB_S`, `MADD_S`, `DIV_S`).
- **Where blocks begin and end**, 4.5 s: 2.5 s for what a block's start
  asks of the runtime's table, which PCSX2's code does not do (it links its
  blocks), and 2.0 s for its end, which it does.
- **The way through the runtime**: the COP2 transfers, `VCALLMS` and
  `VCALLMSR`, about 1.9 s; calls through a register (`dispatch`), 0.9 s.
- **The BIOS's code**, interpreted, only about 0.2 s of play: compiling it
  (G8) is a matter of startup.

The same, with nothing drawn, once G8 was in (the BIOS's code compiled from
the maintainer's dump, as the other session built it), the baseline for
what follows:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.5, 16.6, 16.3 s | 57.3, 55.3, 55.4 s |
| Compiled | 18.6, 18.3, 18.2 s | 69.0, 66.7, 66.9 s |

Startup takes 1.11 times stock PCSX2's time, from 1.17: that is G8. Play
takes 1.21 times, as before, each pair 1.205 to 1.207.

### Profile-guided optimization

The research asks for it to be evaluated. The compiler that builds the
generated code (clang 22, from Visual Studio's Build Tools) can count, in
an instrumented build, how often each path of the code is taken, and then
use those counts to arrange the code: the paths taken together, the rare
ones out of the way, inlining where it pays. `scripts\windows\build.ps1
-Pgo generate` builds the generated code (the game's, the BIOS's, the test
programs') instrumented; a run then writes its counts where
`LLVM_PROFILE_FILE` says; `llvm-profdata merge` puts them together; and
`-Pgo use -PgoData <file>` builds with them. The counts come from running
the game, so they stay in the build tree.

The training run is not the run that is timed: `tests/games/killzone/
level1-train.pad` goes through the same menus into the same level and then
plays it otherwise (other directions, other looks, more fire), for 17,500
frames.

A first round was made before G8 was in: built, trained, rebuilt (the host
7.8 MB smaller, 185.0 MB from 192.8 MB), and found the stock run (the level
script against stock PCSX2 at every sync point, the same pictures and
sound) and the interpreted run (47,705,744 event tests, the run's
fingerprint unchanged), with every test passing. Its timing was lost: the
session moved its tree onto G8 while the timing ran, which changed the
scripts under it. It was made again on G8, below.

On G8 the instrumented build, the training run, the merge (56 MB of
counts) and the build that uses them took under ten minutes here. The
build with the counts was checked as the first round was. Every test
passes (453, and the unit tests). Over the level script the compiled game
is the stock run at every one of 93,221,937 sync points: the same place
and time, the registers the same but for the FPU's at 964 sync points, as
before (the sign of a zero, "G4"), the same picture at each of the 1,750
frames compared and the sound hardware in the same state at each of 1,750
moments. Over the first 4,200 frames it is the interpreted run at every
one of 47,705,744 event tests, the run's fingerprint the same
(`F99B5EFD472E87EC`).

It was timed as the baseline above was: stock PCSX2 and the compiled game
in turn, three rounds, nothing drawn, the other session running nothing.

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.2, 16.2, 16.2 s | 55.4, 55.4, 55.4 s |
| Compiled, with profile-guided optimization | 17.2, 17.2, 17.4 s | 62.7, 62.6, 62.9 s |

Startup takes 1.07 times stock PCSX2's time, from 1.11; play 1.13, from
1.21; the whole run (81.3 to 81.7 s against 73.0 to 73.2 s) 1.11, from
1.18. That is about 4 s less of play and 1 s less of startup, the
largest gain of any one change since G6 began.

The profile of play, before and after (seconds):

| | Without | With |
|---|--:|--:|
| The game's code, compiled ahead of time | 25.8 | 23.3 |
| PS2Native's runtime | 5.2 | 4.0 |
| PCSX2's core | 19.5 | 18.5 |
| The code PCSX2's recompilers generated, for the VUs and the IOP | 14.9 | 14.4 |
| Windows, drivers, other libraries | 3.0 | 2.9 |

In the game's code the start of a block went from 2.8 to 1.8 s, a
function's entry and exit from 1.5 to 1.2 s, the end of a block from 2.0
to 1.7 s, registers taken from the core into locals from 0.75 to 0.57 s;
loads, stores and the FPU's arithmetic changed little. In the runtime the
FPU's helpers (`val::MUL_S`, `ADD_S`, `SUB_S`, 0.68 s) and `ops::LQ`
(0.12 s) are no longer called: the compiler put them inline where the
counts said, and the FPU's arithmetic took no longer for it. PCSX2's
core took 1.0 s less, though nothing of it was built otherwise; the
profile does not say why.

What is left, in play (63.6 s, profiled): the game's code 23.3 s, of it
loads 5.7 s, the FPU's arithmetic 5.0 s, stores 1.8 s, the start of a
block 1.8 s, its end 1.7 s; the runtime 4.0 s, of it the COP2 path
(`cop2Instruction`, `Cop2Operation`, `Cop2Prologue`, `LQC2`, `SQC2`)
2.1 s and `dispatch` 1.0 s.

The counts are made from the game and stay in the build tree, as the
captures do. They hold for the code they were made from: when the
generated code changes, the functions that changed lose theirs, so each
change that follows is measured with counts made again for it.

Code the training run does not reach is taken for code that never runs,
and pays for it. The session that took on the website's charts found on
2026-11-13 that `tests/programs/bench.s`, which no training run runs, is
slower with profile-guided optimization than without: its `mmi` loop takes
17.7 to 18.0 ms against 12.0 to 12.1 ms (stock PCSX2 8.2 to 8.5 ms). In
the object file the compiler had not inlined the runtime's `ops::LQ` and
`ops::SQ` into that function, as it does without the counts: it gives a
call made where nothing was counted less inlining. The game's code that
the training does not reach, another level for one, is in the same case.
Not tried yet: making the semantics compiled code calls on its usual way
always inline, or training on more of the game.

### Loops that have reached a fixed point

After profile-guided optimization the function with the most samples in
play is `001C0190`: 1,530 of 41,800, 2.3 s of the 63.6 s profiled. Nearly
all of it is one loop of one block, `001C01B0` to `001C01DC`, in which the
game waits for the next frame. Each time round it loads a counter that an
interrupt handler counts up (`0055B360`), keeps it and the value before it
beside an object, and goes back while the counter has not moved, at
seventeen cycles a time round. It stores, so it is not one of PCSX2's idle
loops: PCSX2's recompiler runs every time round, and so did compiled code.

Once the counter stands still, a time round stores what the one before
stored: it leaves the machine as it found it. Nothing else runs on the
machine before the next event test (the IOP, the DMA, the VUs and the
interrupts all wait for one), and the next time round depends on nothing
but the registers and memory, so it does exactly the same, and so does
every one after it, up to the one at whose end an event is due. Compiled
code now notices that and lets those times round pass at once: their
cycles are added, and the event test is made at the cycle count at which
PCSX2's recompiler, running them all, makes it, on the same registers and
memory (`PS2N_BLOCK_END_FIXED`; execution model, "Compiled code"). This is
a change of a kind of its own: fewer of the game's instructions are
carried out than under stock PCSX2, where every change before this one
carried out the same instructions faster. Time is counted as before, and
what is compared with stock PCSX2 is unchanged.

The generator marks such loops by what they hold: a loop of one block,
nothing in it but arithmetic on general registers and their loads and
stores, no register written that it reads before writing it, and not one
of PCSX2's idle loops. There are 17 in the game's executable, 16 in the
BIOS's images (12 of them the kernel's), and one in each of 20 test
programs. Their stores compare the value with what memory holds. A time
round does not count if a load or store in it was not of plain memory, if
its block is not one the image's table knows (a block in a page that is
not write-protected is checked against memory every time it runs, and is
never skipped), or if its branch does not go back.

`tests/programs/fixedloop.s` tests it: a handler of the vertical blank
counts a word up, and the program waits eight times in such a loop for
the word to move. Compiled, 4,873,424 times round are not run (38,987,392
cycles, at 20,078 event tests), and the run is the stock run at all
46,410 sync points and the interpreted run at all 46,500 event tests. The
program's first version kept its words in its code's page, where nothing
is skipped (above). It also hung under PCSX2's interpreter, which showed
something about PCSX2: its interpreter starts a program with interrupts
off where its recompiler starts it with them on. The kernel hands a
program over with `di; mfc0 $k0, Status; ori $k0, $k0, 0x13; mtc0 $k0,
Status; eret` (`80002FA8` in this BIOS), and the recompiler lets `DI` take
effect an instruction late (execution model, rule 8), so the status
register is read, and written back, with interrupts on. The program now
switches them on itself.

It was checked as before. Over the level script the compiled game is the
stock run at every one of 93,221,937 sync points, the registers the same
but for the FPU's at the same 964, with the same picture at each of the
1,750 frames compared and the sound hardware in the same state at each of
1,750 moments. There 1,032,537,462 times round were not run: 17.5 billion
of the run's 103.2 billion EE cycles, at 8,969,189 event tests. Over the
first 4,200 frames it is the interpreted run at every one of 47,705,744
event tests, the run's fingerprint the same (`F99B5EFD472E87EC`), with
5,928,976 times round not run. Every test passes.

Timed as before, with profile-guided optimization trained again for the
new code, stock PCSX2 and the compiled game in turn, three rounds, nothing
drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.6, 16.5, 16.7 s | 56.0, 56.3, 56.0 s |
| Compiled | 17.3, 17.5, 17.3 s | 60.7, 61.3, 60.1 s |

Play takes 1.08 times stock PCSX2's time (1.07 to 1.09, pair by pair),
from 1.13; startup 1.05, from 1.07; the whole run (78.8 to 80.3 s against
74.0 to 74.3 s) 1.07, from 1.11. In the profile of play `001C0190` is gone
from the functions with the most samples, where it had 1,530; the game's
code takes 21.0 s of play, from 23.3 s.

### The COP2 transfers made by compiled code

The transfers between VU0's registers and the general registers or memory
(`QMFC2`, `QMTC2`, `LQC2`, `SQC2`) went through the runtime every time: the
runtime's COP2 path, with `VCALLMS` and `VCALLMSR`, was 2.1 s of play in
the profile above. They now ask the runtime's record what their block does
about VU0 first, as the VU0 instructions have since G9, and where nothing
is done, compiled code makes the move itself (`PS2N_COP2_MOVE`): a general
register's local holds its bits 0 to 63 and the core bits 64 to 127, and
`LQC2` and `SQC2` go through the address table as the other loads and
stores do. Of the game's 4,690 COP2 instructions, 913 are VU0 instructions
and 3,485 transfers, which compiled code now carries out where it may; the
runtime carries out the other 292 (`CFC2`, `CTC2`, `VCALLMS`, `VCALLMSR`)
whole.

Built with profile-guided optimization trained again for it, every test
passes, the level run is the stock run at every one of 93,221,937 sync
points (VU0's registers the same at every one, the FPU's different at the
same 964), and the first 4,200 frames are the interpreted run at every one
of 47,705,744 event tests.

Timed as before, stock PCSX2 and the compiled game in turn, three rounds,
nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.7, 16.3, 16.2 s | 56.5, 55.6, 55.9 s |
| Compiled | 17.5, 17.4, 17.3 s | 61.1, 60.6, 60.5 s |

Play takes 1.08 times stock PCSX2's time (1.08 to 1.09, pair by pair), as
before; startup 1.06; the whole run (79.3 to 80.0 s against 73.5 to
74.7 s) 1.08. The change gave nothing that shows. The profile of play
says why: the runtime's COP2 path has about 180 samples fewer than before
it, and the lines of compiled code that make the transfers 181 more. Most
of the path's time is still the runtime's, about 1,230 samples (1.8 s),
`cop2Instruction` alone 690: what still goes there is the first COP2
instruction of a block, before which PCSX2's recompiler adds the block's
cycles so far and looks whether VU0 is running a micro program, and
`CFC2`, `CTC2`, `VCALLMS` and `VCALLMSR`. Which of these takes the time,
the profile does not say.

### Link-time optimization

The research asks for this to be evaluated too. PCSX2's own Windows builds
link the whole program with it; this build never has
(`scripts\windows\build.ps1` leaves it out), and stock PCSX2 here is built
from the same tree as the compiled game, the same way. `build.ps1 -Lto thin` now applies it to
PS2Native's own code only, the generated code and the runtime, and to
nothing of PCSX2's: that code is compiled to LLVM's bitcode and the linker
optimizes it as a whole (ThinLTO), so that a function of one of the game's
64 parts can take in one of another part, or one of the runtime's. What the
linker made of each part is kept in the build tree (`ps2n\lto-cache`), so a
link after a change to a few parts redoes little. With the COP2 step's
profile-guided optimization the build took 4.6 minutes, and the host came
out 2.3 MB larger (189.7 MB).

Timed as before, stock PCSX2 and the compiled game in turn, three rounds,
nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.6, 16.4, 16.4 s | 56.1, 55.8, 55.9 s |
| Compiled, with link-time optimization | 17.5, 17.3, 17.4 s | 61.6, 61.0, 60.8 s |

Play takes 1.09 times stock PCSX2's time (1.09 to 1.10, pair by pair),
against 1.08 without it; startup 1.06, as without; the whole run (79.7 to
80.5 s against 73.7 to 74.2 s) 1.08. It gave nothing. The profile of play
shows what it did: the runtime's functions went into the game's code
(`dispatch` and `cop2Instruction` in part, `LQC2` and `SQC2` whole), the
runtime's samples down to 1,727 from 2,420 and the game's code's up to
15,239 from 14,318, and the two together took no less: 16,966 samples
against 16,738. The calls between the game's functions, and from them into
the runtime, are not where the time goes. The option stays, off, and what
follows is built without it.

Linking the whole host with it, as PCSX2's builds do, would change the
stock PCSX2 the compiled game is measured against as well: that goes to the
maintainer first (ADR 0019), and has not been tried.

### The COP2 instructions that come first

To know what the runtime's COP2 path still does, the runtime now counts
what compiled code hands it, by instruction, and how often VU0 is running a
micro program then; the host reports it. Over the level script it was
417,787,562 times:

| | Handed to the runtime | VU0 running then |
|---|--:|--:|
| `LQC2` | 164,309,686 | 0 |
| `SQC2` | 94,875,156 | 0 |
| `VCALLMS`, `VCALLMSR` | 51,520,831 | 0 |
| `QMTC2` | 39,456,008 | 0 |
| `QMFC2` | 31,453,924 | 13,221,123 |
| `CFC2` | 17,063,293 | 4,214,519 |
| `CTC2` | 11,235,378 | 0 |
| VU0 instructions | 7,873,286 | 0 |

The transfers and the VU0 instructions among them are the ones before
which PCSX2's recompiler brings VU0 up to date: the first COP2 instruction
of a block, and the first after a store or a `VCALLMS`, either of which may
have started VU0 (`COP2MicroFinishPass` in its analysis). Before them it
adds the block's cycles so far and looks whether VU0 is running a micro
program; only when it is has it anything more to do (`Cop2Prologue` in the
runtime does the same). VU0 was running at 13,221,123 of those `QMFC2` and
at none of the rest: 324,746,937 of them, 78% of all, needed nothing from
the runtime but the cycles.

Compiled code now adds those cycles itself where the record says, and
hands such an instruction to the runtime only where its record has VU0
brought up to date first and VU0 is running (`PS2N_COP2_ASKS`); otherwise
it carries it out, as it has done the others since the step before.

Built with profile-guided optimization trained again for it, every test
passes (459, and the unit tests). Over the level script the compiled game
is the stock run at every one of 93,221,937 sync points, the registers the
same but for the FPU's at the same 964, with the same picture at each of
the 1,750 frames compared and the sound hardware in the same state at each
of 1,750 moments; over the first 4,200 frames it is the interpreted run at
every one of 47,705,744 event tests, the run's fingerprint the same
(`F99B5EFD472E87EC`). Over the level script the runtime is now handed
93,040,625 COP2 instructions, from 417,787,562: the 13,221,123 `QMFC2` at
which VU0 was running, and the `CFC2`, `CTC2`, `VCALLMS` and `VCALLMSR`,
as before.

It was timed as before, stock PCSX2 and the compiled game in turn, three
rounds, nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.7, 16.5, 16.4 s | 56.4, 56.3, 56.4 s |
| Compiled | 17.5, 17.3, 17.5 s | 59.7, 59.5, 59.5 s |

Play takes 1.06 times stock PCSX2's time (1.06 pair by pair), from 1.08:
59.6 s on average against 60.7 s before it, 1.1 s less, where stock
PCSX2's play took 56.3 s against 56.0 s. Startup takes 1.06 times, as
before. The whole run takes 78.2 to 78.7 s against 74.1 to 74.6 s, 1.06,
from 1.08. (The third stock run took 84.5 s in all: ten seconds more,
spent outside its frames, which took as long as the other two runs'.)

In the profile of play the runtime took 1,564 samples, from 2,420:
`cop2Instruction` 255 from 690, `Cop2Operation` 73 from 252, and `LQC2`,
`SQC2` and `Cop2Prologue` are no longer among the functions with the most
samples. The lines of compiled code that carry out the transfers took 430,
from 208. The game's code and the runtime together took 16,029 samples
against 16,738: about 1.1 s less, as the timing says.

### The rest of the COP2 instructions

That left 93,040,625 COP2 instructions handed to the runtime over the
level script. `VCALLMS` and `VCALLMSR`, 51,520,831 of them, need nothing
from the runtime's record: before either, in every block, PCSX2's
recompiler adds the cycles of the block so far and calls its
interpreter's function (`INTERPRETATE_COP2_FUNC`), and the record says
nothing else of them. Compiled code now does the same itself
(`PS2N_VCALLMS`), and puts no register back for it, since the function
touches none of the EE's. `CFC2` (17,063,293) and `CTC2` (11,235,378)
move a VU0 integer or control register from or to a general one: compiled
code now makes them as it makes the other transfers, unless the runtime
has something to do about VU0 first, as the runtime's functions for them
do (`ops_cop2.cpp`). A `CTC2` to the status flag register (which also
rewrites the copies PCSX2's VU recompiler keeps), to `FBRST` (resets) or to
`CMSAR1` (starts VU1) stays the runtime's.

Built with profile-guided optimization trained again for it, every test
passes (459, and the unit tests). Over the level script the compiled game
is the stock run at every one of 93,221,937 sync points, the registers the
same but for the FPU's at the same 964, with the same picture at each of
the 1,750 frames compared and the sound hardware in the same state at each
of 1,750 moments; over the first 4,200 frames it is the interpreted run at
every one of 47,705,744 event tests, the run's fingerprint the same
(`F99B5EFD472E87EC`). Over the level script the runtime now carries out
17,435,656 COP2 instructions whole, where VU0 was running for all but 14
(`QMFC2` 13,221,123 and `CFC2` 4,214,519; and 14 `CTC2` that do more than
store), and the 51,520,831 `VCALLMS` and `VCALLMSR` reach PCSX2's function
the new way.

It was timed as before:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.5, 16.3, 16.3 s | 57.0, 56.1, 56.1 s |
| Compiled | 17.9, 17.5, 17.4 s | 61.5, 59.1, 59.2 s |

The first round was slower on both sides, stock PCSX2's play by 0.9 s and
the compiled game's by 2.3 s; in the other two play takes 1.054 and 1.056
times stock PCSX2's time, against 1.056 to 1.059 before. Timed again, with
the other session running nothing:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.3, 16.2, 16.5 s | 55.9, 55.5, 56.2 s |
| Compiled | 17.2, 17.5, 17.6 s | 58.7, 60.1, 58.7 s |

Play takes 1.06 times stock PCSX2's time (1.04 to 1.08 pair by pair, the
second round slower this time), as before the change; the whole run
(77.3 to 79.0 s against 73.1 to 74.1 s) 1.06. Over the six rounds the
change gave nothing the timing shows. In the profile of play the runtime
took 1,324 samples, from 1,564: `cop2Instruction` 39 from 255, and
`vcallms`, the new way to PCSX2's function, 31. The game's code and the
runtime together took 15,831 samples against 16,029, about 0.3 s less:
under what three rounds can show, which is what they show.

### In the main tree: calls through a register

On the evening of 2026-11-12 a second session worked on G10 in the main
working tree, beside the first in its worktree
([ADR 0021](adr/0021-two-sessions-on-step-g10.md)). Its changes are measured
without profile-guided optimization, so that between two timings the change
is the only difference; profile-guided optimization's own gain is above.

A call whose target is only known at run time (`jalr`, and `jal` to a place
without compiled code) went through the runtime's `dispatch()`, which looked
the target up and called the compiled function: a call more, through the
runtime's lookup, its checks and loads one after the other, and from there
an indirect call that every such call in the game shared. `dispatch()` now
remembers, by target, the function it found (`callSites`, 16,384 places),
and compiled code looks there first and makes the call itself
(`PS2N_CALL_DYNAMIC`); where the place does not hold the target, it calls
`dispatch()` as before. The runtime empties the places whenever the images
in use, whether compiled code runs, or the core's settings may change
(execution model, "Entering and leaving compiled code"). `ps2n-host
-call-sites off` (`game.ps1 -CallSites off`) leaves them empty, so that one
build times the two.

Every test passes (459, and the two unit test programs), and over the first
4,200 frames the compiled game is the interpreted run at every one of
47,705,744 event tests (fingerprint `F99B5EFD472E87EC`, as before).

Timed in one build, the places used and left empty, stock PCSX2 timed in
turn, three rounds, nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.48, 16.47, 16.42 s | 56.69, 55.91, 56.09 s |
| Compiled, the places left empty | 18.37, 18.39, 18.26 s | 66.47, 66.44, 66.41 s |
| Compiled | 18.26, 18.22, 18.38 s | 65.09, 65.23, 65.31 s |

Play takes 1.2 s less (65.21 s against 66.44 s on average, 1.1 to 1.4 s
round by round): 1.16 times stock PCSX2's time, from 1.18, without
profile-guided optimization. Startup does not change. In the profile of play
`dispatch` has 102 samples where it had 686, and the runtime 2,821 where it
had 3,370; the game's code did not get slower for the look it now takes
(16,397 samples against 16,586). The look makes the game's code 0.5% larger.

### How much host code each kind of generated line becomes

The profile says where the time goes; how much code the host has to get
through has counted as much (G6, three changes tried and taken out). So the
host now says that too: `ps2n-host -code-size <file>` reads its own
debugging information, each compiled function's size and, from the line
tables, the generated line each piece of it came from, and adds the bytes
up by image and by the kind of line, as the profile names the kinds. It
needs no machine; it takes 22 s.

The game's code is 84.2 MB for 1,003,452 instructions: 84 bytes of host code
for each of the game's instructions. (The 66 test programs: 48 to 77; the
BIOS's four images, 1.3 MB for 20,692 instructions, 62.) By the kind of
line:

| Kind of generated line | Share of the code | Bytes a game instruction | Share of play's samples in the game's code |
|---|--:|--:|--:|
| Loads into general registers | 21.0% | 17.6 | 22.1% |
| The start of a block | 17.9% | 15.0 | 9.8% |
| Stores of general registers | 17.0% | 14.2 | 5.4% |
| The end of a block | 13.6% | 11.4 | 7.1% |
| Calls, jumps, returns, the entry switch | 6.0% | 5.1 | 3.6% |
| Registers put back from locals | 5.6% | 4.7 | 4.6% |
| Loads and stores through register numbers | 5.1% | 4.3 | 4.5% |
| Registers taken from the core into locals | 3.3% | 2.8 | 2.9% |
| FPU arithmetic | 3.0% | 2.6 | 20.5% |
| Everything else | 7.4% | 6.2 | 19.5% |

What a macro does is all on the line that names it, the way seldom taken
included, and that way is most of the code: a block's start that asks the
runtime (a call of four arguments, the answer through memory, a return),
a block's end that makes the event test, and a load or store that is not of
plain memory. In one function looked at (`001C0190`), a store's way taken
is 20 bytes and its other way 49. The other way round, the FPU's arithmetic
is 3% of the code and a fifth of the time.

(The shares of play's samples are from the profile of the build with calls
through a register made by compiled code, above.)

### In the main tree: the way a load or store seldom takes, made short

A load or store of a general or an FPU register has two ways: the one taken
when its address is plain memory, and the one taken when it is a device's
register. On the second the function put back every register it held
changed in a local (a store of 7 or 8 bytes each), told the runtime (a
call), had the core's function make the access (another call), and took the
cycle count again; in one function looked at, 49 bytes against the 20 of
the way taken. That way is now one call of the runtime (`slowRead`,
`slowReadEnds`, `slowWrite` in `ee_provider.cpp`), and it puts no register
back: nothing on it reads the EE's registers. The runtime looks at its
record of blocks, and the core's function for a device's register at that
device; neither raises an exception, as PS2Native's provider is not PCSX2's
interpreter (`vtlb_Miss`). PCSX2's recompiler does the same: before such a
call it writes back only the registers it holds in host registers the call
may overwrite (`iFlushCall(FLUSH_FULLVTLB)`, and `FLUSH_FULLVTLB` is 0), so
the core's functions never see the EE's registers whole there either. The
argument that named the registers to put back is now what that way does
first (`slow_`): nothing, or in a loop that may reach a fixed point the mark
that the time round changed something. Generator 0.7.0.

Measured first on three of the game's 64 files, each compiled the three
ways (bytes of host code):

| | `game_part001` | `game_part030` | `game_part054` |
|---|--:|--:|--:|
| As it was | 1,203,766 | 1,461,894 | 1,573,784 |
| One call of the runtime on that way | 1,160,244 | 1,409,160 | 1,524,966 |
| And no register put back on it | 1,042,032 | 1,255,408 | 1,358,162 |

One call alone is 3% less code; putting nothing back makes it 14%. Built
whole, the game's code is 72.0 MB, from 84.2 MB: its loads 12.2 MB, from
17.7, its stores 8.1 MB, from 14.3 (`-code-size`); 72 bytes for each of its
instructions, from 84.

Every test passes (459, and the two unit test programs); the suite's
compiled runs take that way in every program that touches a device's
registers. Over the first 4,200 frames the compiled game is the interpreted
run at every one of 47,705,744 event tests, the registers compared at each
(fingerprint `F99B5EFD472E87EC`, as before).

Timed as above, stock PCSX2 timed in turn, three rounds, nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.82, 16.47, 16.44 s | 57.02, 56.46, 56.28 s |
| Compiled | 18.40, 18.39, 18.34 s | 64.69, 64.64, 64.52 s |

Play takes 1.14 times stock PCSX2's time, from 1.16 with the calls through a
register alone (64.62 s against 56.59 s; before, 65.21 s against 56.23 s,
an hour earlier): 0.6 s less, or 1.0 s by the ratio, which is at the edge
of what a timing of two builds can show (G6, "How far these numbers can be
trusted"). The profile of play goes the same way: the game's code had
16,079 samples where it had 16,397, 2% fewer (its stores 784 where 890, a
function's entry and exit 972 where 1,119, the FPU's arithmetic 3,161 where
3,366). Startup does not change.

The two changes together, built again after the third was taken out (the
same code as the build timed above: 72,008,167 bytes of the game's code),
over the whole level script against stock PCSX2: the same place at the same
cycle count at every one of 93,221,937 sync points; the registers the same
but for the FPU's at the same 964 sync points, the first 57,130,066, as
since G4 (the sign of a zero, followed on purpose); the same picture at each
of the 1,750 frames compared and the sound hardware in the same state at
each of the 1,750 moments; 51 instructions interpreted, those of the BIOS
at boot. Every test passes.

### In the main tree, tried and taken out again: the FPU's settings read once per function

The FPU's arithmetic is 3% of the game's code and a fifth of the time its
code takes in play. Each of its instructions read the core's clamp settings
(PCSX2's `CHECK_FPU_OVERFLOW` and `CHECK_FPU_EXTRA_OVERFLOW`), and each
multiplication a game fix's setting, from the core's configuration: a load
the compiler has to make again after every store into the console's memory,
which might be the configuration as far as it can tell. A build read them
once, as one word, where a function begins (`const uint32_t ps2n_fpu =
fpu::currentMode();`), and handed the word to each instruction whose result
they change (`f0 = val::ADD_S(ps2n_fpu, f1, f2)`); the interpreter's
functions read them at each instruction as before. (The settings cannot
change under a compiled function that goes on running: the core applies new
ones between two calls of the provider's `Execute()`, or while the block at
an executable's entry point is formed, after which the runtime leaves
`Execute()` at the next event test.)

The FPU's lines became 2.43 MB of the game's code, from 2.50; the code as a
whole 72.2 MB, from 72.0. Every test passed, and the first 4,200 frames were
the interpreted run at every event test (`F99B5EFD472E87EC`). Timed as
above:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.39, 16.30, 16.40 s | 56.20, 56.05, 56.58 s |
| Compiled | 18.32, 18.36, 18.30 s | 64.03, 64.46, 64.21 s |

Play took 1.141 times stock PCSX2's time, against 1.142 without it, and in
the profile of play the FPU's arithmetic had 3,249 samples, against 3,161
without it: nothing that shows. A likely reason, not measured: the
processor lets a load of the configuration run ahead of the stores before
it, and a branch on a setting that never changes is always predicted, so
those loads cost little to begin with. Taken out: the code is as before.

### Both sessions' work together

Each session measured its own changes: the worktree's with profile-guided
optimization, the main tree's without. Put into one history on 2026-11-13
(the worktree's work rebased onto `33d0e77`, the generator stamped
`ps2n-recomp 0.8.0`), the code was built with profile-guided optimization
trained again for it. The host is 167.6 MB, from 187.8 MB: the shorter way
loads and stores seldom take. Every test passes (459, and the unit tests).
Over the level script the compiled game is the stock run at every one of
93,221,937 sync points, the registers the same but for the FPU's at the
same 964, with the same picture at each of the 1,750 frames compared and
the sound hardware in the same state at each of 1,750 moments; over the
first 4,200 frames it is the interpreted run at every one of 47,705,744
event tests, the run's fingerprint the same (`F99B5EFD472E87EC`).

Timed as before, stock PCSX2 and the compiled game in turn, three rounds,
nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.5, 16.5, 16.7 s | 57.0, 56.9, 56.5 s |
| Compiled | 17.5, 17.4, 17.4 s | 59.5, 58.7, 58.6 s |

Play takes 1.04 times stock PCSX2's time (1.03 to 1.04 pair by pair),
from 1.06; startup 1.05; the whole run (77.4 to 78.4 s against 74.7 to
74.9 s) 1.04, from 1.06. Stock PCSX2's play took 56.8 s on average here,
against 55.9 to 56.4 s in the timings of the day before; the compiled
game's 58.9 s, against 59.2 to 59.9 s since the COP2 instructions that
come first.

In the profile of play the runtime took 731 samples, from 1,324:
`dispatch` 111 from 700, now that calls through a register are looked up
by compiled code itself, whose lines for calls, jumps and returns took 496,
from 224. The game's code and the runtime together took 15,299 samples
against 15,831, about 0.8 s less.

### The website's two charts, measured again (2026-11-13)

On 2026-11-13 the maintainer gave a third session the research of
[ADR 0019](adr/0019-performance-as-the-research-lays-it-out.md) again,
beginning with the website's two speed charts: "We need to improve these
numbers:", with a picture of the two charts. They were the oldest numbers on
the page: the first 4,200 frames from G3 (2026-11-11), and `bench.s`'s six
loops from the proof of concept's step P4 (2026-11-10), neither measured
again since. Read as: G10 goes on, the charts measured first, then their
slowest parts. The session worked in the main tree while the first session
finished its part in its worktree, the two saying "go" and "done" around
every timing, as [ADR 0021](adr/0021-two-sessions-on-step-g10.md) has it.

The first 4,200 frames, on `cba2be7`'s code built with profile-guided
optimization (the first session's build), stock PCSX2 and the compiled game
in turn, three rounds, the whole run timed as at G3:

| | Stock PCSX2 | Compiled |
|---|---|---|
| Nothing drawn | 11.4, 11.3, 11.3 s | 12.3, 12.2, 12.2 s |
| PCSX2's software renderer | 22.7, 22.6, 22.6 s | 23.6, 23.5, 23.6 s |

With nothing drawn the compiled game takes 1.08 times stock PCSX2's time,
where at G3 it took 1.35 (15.1 s against 11.2 s); with the software renderer
1.04, where it took 1.12 (25.7 s against 22.9 s). From the first frame to the
4,200th alone, 1.09 and 1.04. Both runs end at EE cycle 24,766,740,046, as
at G3. (G3's runs were not kept on the processor's fast cores; these were.)

`bench.s`, the best of three runs of each, the compiled program's time over
stock PCSX2's:

| | sum | mem | call | fpu | mmi | jump |
|---|--:|--:|--:|--:|--:|--:|
| P4 (2026-11-10) | 0.87 | 1.02 | 1.15 | 1.04 | 1.23 | 1.47 |
| The same code, without profile-guided optimization | 0.92 | 1.06 | 1.00 | 0.90 | 1.43 | 1.08 |
| With it | 0.90 | 1.06 | 0.99 | 1.00 | 2.12 | 1.08 |

`jump` and `call`, the slowest at P4, are now about as fast as under PCSX2's
recompiler. `mmi` is slower than at P4. With profile-guided optimization it
takes twice stock PCSX2's time: the training run is the game's, `bench.s`
never runs in it, and its functions, with no counts, are compiled as code
that never runs, `ops::LQ` and `ops::SQ` called out of line where they were
inlined ("Profile-guided optimization", above: the same holds for any of the
game's code the training does not reach). Without it `mmi` still takes 1.43
times: its `paddw`, `psrlw` and `pxor` work on the core's copy of the
registers, in memory, where PCSX2's recompiler keeps them in host registers
for the length of the block. The loops also move by a few percent from one
build to the next with nothing in them changed (`fpu` from 0.90 to 1.03 over
the five builds timed that night): where the compiler happened to put them.

### In the main tree again: LQ and SQ on the locals

`lq` and `sq` were carried out through register numbers (`PS2N_ACCESS` with
`LQ` or `SQ`): their registers put back first, the address read from the
core, and the address table looked at twice, once to tell the runtime and
once for the access. They are now made as the other loads and stores are,
and as the COP2 transfers have been since G10: the address from a local,
one look at the table, a general register's bits 0 to 63 in its local and
bits 64 to 127 in the core (`PS2N_GPR_HIGH`), and on the way taken when the
address is not plain memory one call of the runtime (`slowRead128`,
`slowWrite128`). `PS2N_LQ` writes the sixteen bytes it read to the core
whole, and its local takes bits 0 to 63 as a copy, with nothing to put back;
`PS2N_SQ` stores a local that holds bits 0 to 63 changed with bits 64 to 127
from the core, and `PS2N_SQ_CORE` the sixteen bytes the core has, where the
local holds nothing changed. The game has 3,380 `lq` and 3,135 `sq`.
Generator 0.9.0.

That is the second form. In the first, `lq` wrote bits 64 to 127 to the
core and kept bits 0 to 63 in its local as changed, to be put back later.
Without profile-guided optimization nothing showed (play 1.12 times stock
PCSX2's time, startup 1.11; the same code without the change was not timed
beside it):

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.64, 16.44, 16.50 s | 56.91, 56.10, 55.87 s |
| Compiled, the first form | 18.53, 18.27, 18.18 s | 62.91, 63.04, 62.58 s |

With profile-guided optimization the parts of the game that are not play
were slower: the first 4,200 frames with nothing drawn took 1.12 times stock
PCSX2's time from the first frame on (11.06, 11.09, 11.10 s against 9.90,
9.88, 9.91 s), against 1.09 on the code before the change, and the level
run's startup 1.07 (17.53, 17.61, 17.54 s against 16.28, 16.33, 16.68 s),
against 1.05; play 1.05 (58.68, 58.88, 58.39 s against 55.59, 55.91,
56.33 s). The cause, by the code: a parallel instruction after an `lq` reads
the register whole from the core, sixteen bytes, which the first form had
written there as two stores of eight at two different times, and a load
over two stores is not handed on from the processor's store buffer but
waits for both to leave it. The old way wrote the sixteen bytes together.
The second form does too, and puts nothing back afterwards.

With profile-guided optimization trained again for the second form (and
the change below), and timed in turn with stock PCSX2: the first 4,200
frames with nothing drawn 1.10 times stock PCSX2's time from the first frame
on (10.94, 10.94, 10.98 s against 9.99, 9.95, 9.97 s), with the software
renderer 1.03 (22.03, 22.03, 21.89 s against 21.31, 21.17, 21.38 s); the
level run below ("Where it stands"). `bench.s`'s `mmi` takes 10.6 to 10.9
ms in that build, against 17.7 to 18.0 ms in the first session's: the two
accesses are no longer calls out of line in code the training does not
reach.

### In the main tree, tried and taken out again: registers kept changed across the end of a block

At the end of every block the generated code put back every register it
held changed, because an event test may follow: an event is due at few of
them, and only there do the comparisons with stock PCSX2 and with the
interpreter read the registers, or an interrupt look at them. A build put
them back only on that way: in the event test's `[[unlikely]]` branch, before
the runtime is asked about a block (which may interpret it), before a call,
a return or a jump out of the function, as before; a label could then hold a
register as changed when a way into it did, so that a loop kept what it
carried from one time round to the next in host registers, where PCSX2's
recompiler writes them back at the end of every block. Its first form made
the code 13% to 17% larger, on three of the game's files compiled alone: the
registers were put back twice on most ways, once in the event test's branch
and once on the way to the next label. In the second, what every way after
the block's end puts back is put back once before it, as before, and only
what a label after it holds as changed goes into that branch: 0.4% larger.

Every test passed, and the first 4,200 frames were the interpreted run at
every event test (`F99B5EFD472E87EC`). Timed as above:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.37, 16.49, 16.45 s | 56.01, 56.52, 56.13 s |
| Compiled | 18.17, 18.09, 18.07 s | 62.86, 62.87, 63.73 s |

Play took 1.12 times stock PCSX2's time, as without it; of the loops `mem`
went from 1.06 to 0.98 and `jump` from 1.12 to 1.18. Nothing that shows over
the level run. A likely reason, not measured: few of the game's labels hold
anything changed, since a label holds only what every way into it holds and
its code reads, and a loop that writes a register before it reads it still
puts it back on its way round. Taken out: the code is as before.

### In the main tree again: a block start asks the runtime in one argument

A block start's way seldom taken, where the image's table says nothing of
the block, called the runtime with four arguments and its answer through
memory: the image, the leader's index, the body, and the address of a word
for the answer, then the answer read back and the cycle count taken again
(47 bytes in one function looked at), at each of the game's 197,808
leaders. It is now a call of one function for each image with the leader's
index alone (`askAbout<image>(index)`, `[[gnu::noinline, gnu::cold]]`),
whose answer comes back in a register; when that is zero, the value to
return is in `blockExit`. On the three files compiled alone the code is
5.6% to 7.7% smaller; the game's whole code is 67.2 MB, its block starts
9.8 MB of it (`-code-size`). Built with profile-guided optimization, the
host is 156.1 MB where it is 167.5 MB without the change.

Every test passes (459, and the unit tests), and the first 4,200 frames are
the interpreted run at every one of 47,705,744 event tests
(`F99B5EFD472E87EC`). Timed with the first form of `lq` and `sq` on the
locals, without profile-guided optimization:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.36, 16.42, 16.49 s | 55.86, 56.22, 56.07 s |
| Compiled | 18.23, 18.34, 18.20 s | 62.33, 63.03, 62.65 s |

Play takes 1.12 times stock PCSX2's time (62.67 s against 56.05 s; without
the change, 62.84 s against 56.29 s): nothing that shows. With
profile-guided optimization, trained for each, the same: play 58.32, 58.62,
58.46 s against 56.00, 56.03, 56.27 s with the change, 58.68, 58.88, 58.39 s
against 55.59, 55.91, 56.33 s without; the first 4,200 frames with nothing
drawn 11.07, 11.21, 11.16 s from the first frame on, against 11.06, 11.09,
11.10 s. The way it shortens is one the game seldom takes, and
profile-guided optimization already moves such ways out of the way. It
stays for the smaller code.

### Where it stands (main tree, 2026-11-13)

The code as it stands, `lq` and `sq` on the locals in their second form and
a block start asking the runtime in one argument, built with profile-guided
optimization trained for it (the host 155.6 MB), timed as before, stock
PCSX2 and the compiled game in turn, three rounds, nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.49, 16.56, 16.44 s | 57.00, 56.01, 56.09 s |
| Compiled | 17.36, 17.31, 17.39 s | 58.68, 58.12, 58.47 s |

Play takes 1.04 times stock PCSX2's time (1.03 to 1.04 pair by pair),
startup 1.05, the whole run (76.9 to 77.5 s against 74.0 to 74.9 s) 1.04:
where the two sessions' work together had it, the night's changes giving
nothing over the level run that the timing shows. Every test passes (459,
and the unit tests); over the first 4,200 frames the compiled game is the
interpreted run at every one of 47,705,744 event tests
(`F99B5EFD472E87EC`); over the whole level script it is the stock run at
every one of 93,221,937 sync points, the same place at the same cycle
count, the registers the same but for the FPU's at the same 964 sync
points, the first 57,130,066 (the sign of a zero, followed on purpose since
G4), with the same picture at each of the 1,750 frames compared and the
sound hardware in the same state at each of 1,750 moments; 51 instructions
interpreted, those of the BIOS at boot.

The website's two charts, on this build: the first 4,200 frames take 12.3,
12.2, 12.3 s compiled against 11.3, 11.3, 11.2 s under stock PCSX2 with
nothing drawn (1.09), and 23.4, 23.4, 23.3 s against 22.7, 22.6, 22.8 s with
the software renderer (1.03). `bench.s`'s six loops, as at P4 without
profile-guided optimization, the best of three runs, stock PCSX2 in turn:
`sum` 0.94 (64.7 ms against 68.9 ms), `call` 1.01 (29.4 against 29.1),
`fpu` 1.01 (50.5 against 49.8), `mem` 1.06 (19.7 against 18.6), `jump`
1.07 (52.0 against 48.8), `mmi` 1.24 (10.2 against 8.2), where it was 1.43
before `lq` and `sq` were made on the locals.

What the night found about where the rest of the time is:

- **The FPU.** In the function with the most samples in play
  (`0050C560`), a multiplication is about fourteen host instructions: the
  clamp settings read and tested twice, the operand's clamp on integer
  registers, a move into an SSE register and one back out, the
  multiplication, and the clamp of the result against constants in memory.
  PCSX2's recompiler reads the settings when it compiles and keeps the
  FPU's registers in host SSE registers for the length of a block: about
  three. The FPU's arithmetic was a fifth of the samples in the game's code
  in play ("The COP2 instructions that come first", the profile). Its
  registers would have to live in SSE locals, and its semantics be written
  on them, held to PCSX2's recompiler by the self-test as now: not begun.
- **The 128-bit arithmetic.** The parallel instructions work on the core's
  copy of the registers, in memory: `mmi` takes 1.24 times stock PCSX2's
  time. In the game they are rare (about 100 samples of play), in `bench.s`
  they are the loop.
- **Code the training does not reach.** Profile-guided optimization
  compiles it as code that never runs ("Profile-guided optimization").
  `lq` and `sq` no longer suffer from that; any other semantics not inlined
  there would, until they are marked to be inlined everywhere or the
  training covers more of the game.

### The FPU's arithmetic on SSE registers (main tree, 2026-11-13)

On 2026-11-13 the maintainer answered the third session's recommendation
(above: reworking the floating-point arithmetic, its gain an estimate)
with "Let's rework floating point arithmetic". Read as: G10 goes on with
the FPU, as recommended, each change measured as the main tree's are,
without profile-guided optimization and against stock PCSX2 timed in turn,
and then with it. Generator 0.10.0.

**What was measured first.** The profile now also files the FPU's lines by
instruction ("The FPU's lines of the game's compiled code, by instruction":
its arithmetic and comparisons by the function of `ops_fpu.h` they call,
and its loads and stores, which the kinds count with the general
registers'). The code as it stood, built without profile-guided
optimization, timed in turn with stock PCSX2, three rounds, nothing drawn:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.76, 16.57, 16.29 s | 58.05, 56.76, 55.53 s |
| Compiled | 18.38, 17.82, 17.94 s | 63.65, 61.60, 61.68 s |

Play took 1.10 times stock PCSX2's time (62.31 s against 56.78 s; 1.085 to
1.111 pair by pair). (Stock PCSX2's first run took 86.5 s in all, eleven
seconds more outside its frames, as seen before.) The FPU's lines had 4,340
of play's samples: its arithmetic 3,140, comparisons 385, loads 513, stores
302; and 452 more fell in the runtime, where the compiler had left
`val::ADD_S`, `SUB_S`, `MUL_S` and `MADD_S` out of line in places.

**The registers in SSE registers.** A local held an FPU register's 32 bits
in a general register, and every operation moved its operands into SSE
registers and its result back out. The locals are now `val::Fpr`, a
register's 32 bits in the lowest lane of an SSE register, as PCSX2's
recompiler holds them, and the functions of `ops_fpu.h` take and give such
values: the same SSE instructions in the same order as before. A load
(`PS2N_LOAD_F`), a store (`PS2N_STORE_F`, new) and a register put back to
the core move the 32 bits between memory and the SSE register directly. A
multiplication is now the multiplication, the clamp of its result (`minss`,
`maxss`) and the tests of PCSX2's settings, which the processor predicts:
five or six host instructions where it was about fourteen. Two things found
on the way in the code the compiler made: written as an intrinsic, the move
from an SSE register to a general one became `extractps`, two
micro-operations where `movd` is one (it is now `movd`, as assembly); and a
store whose seldom-taken way (a device's register) wants the value in a
general register had the compiler move it there before every store (that
way does it for itself now). The functions that are most of the FPU's time
are always inlined (`PS2N_FPU_INLINE`), also in code that profile-guided
optimization takes for cold.

The other three lanes of an SSE register hold whatever the host left
there. A new check of the self-test calls each of the FPU's 19 functions
on values twice on the same operands, once with those lanes clear (as from
the core's registers, through which the comparisons with PCSX2 go) and once
with them random, in each clamp mode and with the FPU's game fix: 304,000
cases, none different. A planted function that read them (`movmskps` of a
whole comparison) was reported in every mode.

Timed as above:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.68, 16.62, 17.18 s | 57.08, 56.61, 58.61 s |
| Compiled | 18.10, 17.77, 18.17 s | 61.86, 60.67, 62.71 s |

Play took 1.075 times stock PCSX2's time (61.75 s against 57.43 s; 1.070
to 1.084 pair by pair), from 1.10. In the profile of play the FPU's
arithmetic, its lines and the functions out of line together, went from
3,592 samples to 3,076: multiplications 37% fewer, additions 9%,
subtractions 12%, the multiply-adds 8% to 24%; its comparisons from 385
to 343.

**The guard bit without a branch.** The EE's adder keeps one guard bit:
of the operand with the smaller exponent, the bits past it are cleared
before the host adds (`FPU_ADD_SUB` in PCSX2's recompiler, `addSub` in
`ops_fpu.h`), and all but its sign when the exponents are 25 or more apart.
PCSX2's code and PS2Native's chose the mask by branching on the difference
of the exponents, which goes one way or the other as the operands'
magnitudes come, and the additions, subtractions and the multiply-adds'
additions were now most of the FPU's time. The masks now come from a table
of the 511 differences (`kGuardMasks`, 4 KB, built when the code is
compiled), loaded into an SSE register: two `movd` for the exponents, a
subtraction, one load, two ANDs and the addition, and no branch on the
data. The masks are the same, so every result is. To hold every entry to
PCSX2's recompiler, the self-test now gives each of the 511 differences,
four times, to each instruction that adds (`add.s`, `sub.s`, `adda.s`,
`suba.s` and the four multiply-adds), in each clamp mode: 16,352 cases a
mode, none different. A wrong result planted at one difference (200) went
unseen by the random cases of `add.s` against the recompiler, 400 in each
clamp mode, and was reported by this check in every mode, at the four cases
of that difference.

Timed twice, the second time because in the first the third compiled round
took four seconds longer in play while its startup was as usual:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.63, 16.52, 16.63 s | 55.95, 58.41, 56.62 s |
| Compiled | 17.79, 18.59, 18.05 s | 58.76, 60.57, 64.70 s |
| Stock PCSX2, again | 16.79, 16.54, 16.50 s | 56.94, 56.89, 55.80 s |
| Compiled, again | 18.01, 17.98, 17.76 s | 59.42, 59.69, 58.33 s |

Play takes 1.046 times stock PCSX2's time in the second timing (59.15 s
against 56.54 s; 1.044 to 1.049 pair by pair), and 1.050 and 1.037 in the
first two rounds of the first: from 1.075 with the first change alone, and
from 1.10 before either, about 2.9 s of play. Startup takes 1.08 times,
from 1.09. In the profile of play the FPU's arithmetic had 2,197 samples
where it had 3,076: additions 36% fewer, subtractions 29%, multiplications
15%, the multiply-adds 29% to 33%.

Every test passes (459, and the unit tests) after each of the two changes.

**With profile-guided optimization**, trained again for the new code (the
host 157.3 MB), every test passes, and timed in turn with stock PCSX2,
three rounds, nothing drawn, and once more half an hour later:

| | Startup, three rounds | Play, three rounds |
|---|---|---|
| Stock PCSX2 | 16.42, 16.35, 16.41 s | 55.80, 55.11, 55.40 s |
| Compiled | 17.32, 17.32, 17.21 s | 55.21, 55.42, 55.63 s |
| Stock PCSX2, again | 16.56, 16.44, 16.29 s | 55.87, 55.89, 55.39 s |
| Compiled, again | 17.21, 17.13, 17.14 s | 56.09, 55.07, 55.47 s |

Play takes as long as stock PCSX2's: 55.42 s against 55.44 s, and 55.54 s
against 55.72 s; 0.985 to 1.006 pair by pair, where it took 1.04 times
("Where it stands", above). Startup takes 1.04 to 1.05 times, as before;
the whole run (73.7 to 74.7 s against 72.9 to 73.9 s) 1.01, from 1.04. In
the profile of play the FPU's
arithmetic has 2,160 samples, where the last profile with profile-guided
optimization had 3,294 (`cba2be7`'s build, whose FPU code was the code
before these changes).

This build was checked as the others were. Over the level script the
compiled game is the stock run at every one of 93,221,937 sync points, the
same place at the same cycle count, the registers the same but for the
FPU's at the same 964 sync points, the first 57,130,066 (the sign of a
zero, followed on purpose since G4), with the same picture at each of the
1,750 frames compared and the sound hardware in the same state at each of
1,750 moments; 51 instructions interpreted, those of the BIOS at boot. Over
the first 4,200 frames it is the interpreted run at every one of
47,705,744 event tests, the run's fingerprint the same
(`F99B5EFD472E87EC`). The game's compiled code is 68.0 MB, its FPU
arithmetic 4.5 MB of it, about twice what it was, now that its functions
are always inlined.

The website's two charts on this build. The first 4,200 frames, logos, a
prompt and a film with little of the FPU's arithmetic in them, take what
they took this morning, within the noise: with nothing drawn 12.2, 12.1,
12.2 s against 11.3, 11.2, 11.2 s under stock PCSX2 (1.08; from the first
frame on 10.93, 10.82, 10.90 s against 9.96, 9.90, 9.90 s, 1.10), and with
the software renderer 23.5, 23.5, 23.4 s against 22.5, 22.6, 22.6 s
(1.04); both runs end at EE cycle 24,766,740,046. `bench.s`'s six loops,
as at P4 without profile-guided optimization, the best of three runs,
stock PCSX2 in turn: `sum` 0.93 (64.0 ms against 69.1 ms), `mem` 1.07
(19.9 against 18.6), `call` 1.00 (29.6 against 29.5), `fpu` 1.00 (50.0
against 50.1), `mmi` 1.24 (10.3 against 8.3), `jump` 1.06 (52.2 against
49.4): the others as this morning, and `fpu` at 1.00 where it was 1.01.
That loop is one chain of additions (`add.s $f2, $f2, $f3`), each waiting
for the one before, whose exponents stand in the same order every time. A
likely reason it gains little, not measured: there PCSX2's branches are
always predicted and cost nothing, and the table's load lies on the chain.
The website keeps the two charts as measured this morning ("Where it
stands", above), which these figures repeat within the noise; what it now
says of the level run is from this section.

## G11: the vector units' micro programs (Windows, 2026-11-13)

The step [ADR 0022](adr/0022-vu-micro-programs-compiled-as-step-g11.md)
added: the micro programs the game runs on its two vector units compiled
ahead of time, in the place of PCSX2's VU recompiler (microVU), which
[ADR 0003](adr/0003-no-jit-in-generated-games.md) allows only as a
temporary stand-in. First, as in G8, measured: what the VUs run.

### What the vector units run, measured

`ps2n-host -vu-report <file>` (`game.ps1 -VuReport`) watches the core's two
VU providers, PCSX2 untouched. The core reaches each through a pointer
(`CpuVU0`, `CpuVU1`): it announces to it every write into the VU's micro
memory before making it (a VIF `MPG` transfer, a store of the EE into that
memory, a scratchpad transfer), starts every micro program through it, and
runs the VU only through it. Those two pointers are wrapped, as the record
of sync points wraps the IOP's (`host/vu_report.h`). The report says what
was written where and with how many different contents, at which addresses
programs were started, and how long each ran on the host (the time-stamp
counter around each run; one VU's provider running inside the other's is
counted for the inner one). Beside it go the same counts frame by frame,
and each distinct content written, which is the game's code and stays in
the build tree with the run's other files. Every hundredth frame a line in
the core's log says which frame it is, so that the lines microVU writes
when it caches a program can be placed in time. Watched, the level run
took 80.1 s where it takes about 74, and ended where every level run has
ended, at EE cycle 103,213,332,030, with the same sound.

The level run (`level1.pad`, the game compiled, nothing drawn), startup
being its first 6,600 frames and play the 10,900 after:

| | VU1, startup | VU1, play | VU0, startup | VU0, play |
|---|--:|--:|--:|--:|
| Writes into micro memory | 15,582 | 435,841 | 2 | 0 |
| Bytes written | 16.8 MB | 589.2 MB | 3,504 | 0 |
| Programs started | 676,466 | 3,754,037 | 742,527 | 50,778,304 |
| Host time in the provider | 0.82 s | 18.56 s | 0.05 s | 3.10 s |
| VU cycles run | 615 million | 12,837 million | 14 million | 1,182 million |

- **VU0 runs one set of code**: 3,504 bytes, written at boot and never
  again, entered at 17 addresses. Its programs are started 51.5 million
  times, by the game's `VCALLMS` and `VCALLMSR`, 4,659 times a frame in
  play, and run about 23 VU cycles each.
- **VU1's micro memory is rewritten about 40 times a frame in play**, 54 KB
  a frame, and nearly every write leaves something else there (432,510 of
  the run's 451,423). What is written is a little code in many places:
  5,776 distinct contents at 5,405 places (an address and a length), but
  208 distinct contents whatever their place, 254 KB in all, and in all of
  them 1,750 distinct instruction pairs, 14 KB. 55 of the 208 are 2,048
  bytes long, the most one `MPG` command carries; 29 lie inside a longer
  one. The same piece is written at several addresses: 45 of them at one,
  91 at 2 to 9, 48 at 10 to 49, 24 at 50 or more. 71 of the 208 were first
  written in play.
- **VU1's programs are started at 450 addresses**, 4.43 million times; from
  the one with most of the time (`1FD8`), 31% of VU1's. At a start, the
  whole micro memory held one of 50,010 different contents.
- **PCSX2's VU recompiler knows a program by its start address and the
  content of the ranges it compiled from there**, and makes the target of
  an indirect jump (`JR`, `JALR`) the start of a program of its own
  (`doJumpAsSameProgram` is off, `pcsx2/x86/microVU_Misc.h`). After a write
  into micro memory, the next start, and the next indirect jump to each
  address, look through the programs kept for that address and compare
  each with what memory holds until one matches (`mVUsearchProg`,
  `mVUcmpProg`); when none does, a new one is compiled. Over the level run
  it made 6,088 programs for VU1, 1,669 by frame 6,600 and 4,419 after (by
  the frame lines in its log), in bursts that do not stop, and filled
  43.5 MB of its 64 MB cache; for VU0, 29. Stock PCSX2's run made the same.

Where play's time goes, by the profile taken with the FPU's arithmetic on
SSE registers and profile-guided optimization ("G10", 57.0 s, 37,568
samples of the CPU thread):

| | Share of the samples |
|---|--:|
| The code PCSX2's VU1 recompiler generated: VU1's programs running | 18.2% |
| PCSX2's VU recompiler starting programs, finding them, making new ones: `mVUcompileJIT<1>` (the indirect jumps) 6.2%, `memcmp` (its comparisons) 1.6%, `mVUexecute` 1.6%, `mVUcleanUp` 0.5% | 9.8% |
| The GIF transfers VU1's `XGKICK` starts (`Gif_Unit::Execute`, `Gif_Path::ExecuteGSPacket`) | 3.2% |
| The code PCSX2's VU0 recompiler generated | 2.1% |
| The game's code, compiled ahead of time, for comparison | 34.1% |

The profile names the `memcmp` samples `memchr`: `vcruntime140.dll`'s
exported `memcmp` jumps into the code after its export `memchr`, the
nearest name the profiler has. The vector units take about a third of
play's CPU-thread time, as much as the game's compiled EE code, and of it
about 5.6 s of play's 57 is PCSX2's recompiler starting programs and
finding out which one memory holds.

What it means for the translator, to be decided, built and measured:

- **The code to compile is small**: 208 pieces, 1,750 distinct instruction
  pairs, over the level run. What costs PCSX2's recompiler is that the same
  pieces come at different addresses and in different company. Compiled by
  content, each piece (or each block of it) once, wherever it is written,
  its code would have to work at any address, taking the addresses it hands
  on (the link of a `BAL`, the target of a jump) from where it was written;
  each piece would be known when it is written, one look at each write, 40
  a frame, not compared at every start and indirect jump.
- **VU0's code first**: one content, 17 entry points, started from the
  game's compiled code.
- **The semantics and the count of cycles follow microVU's**, as the EE's
  compiled code follows PCSX2's EE recompiler: the self-test holds them to
  it, and the comparisons hold the game to the stock run.

With the measurement the maintainer chose
([ADR 0022](adr/0022-vu-micro-programs-compiled-as-step-g11.md)): G10 ends
where it stood; VU1's registers are held to the stock run at every sync
point too; and PS2Native's own VU interpreter comes before the translator,
as the EE's did in G1, to be the reference the translator is checked
against and what runs code a build has not compiled.

### VU1's registers held to the stock run

The record of sync points (`ps2n-host -sync`, `host/sync_trace.cpp`) folds
a fifth kind of register at each sync point: VU1's, as VU0's are folded
(the VF registers, the VI registers with the control registers among them,
ACC; not the VU interpreter's working Q and P). VU1 runs on the CPU thread
between the EE's blocks, so it is never in the middle of an instruction
there. The kind is counted apart from the others and reported with them.

Both runs still run VU1 on PCSX2's recompiler, so they are expected to be
the same, and are: over the level script, the compiled game is the stock
run at every one of 93,221,937 sync points, VU1's registers included, the
registers the same but for the FPU's at the same 964 sync points as before
(the sign of a zero, followed on purpose since G4), with the same picture
at each of the 1,750 frames compared and the sound hardware in the same
state at each of 1,750 moments. Every test passes (459, and the unit
tests), their same-as-stock comparisons now holding VU1's registers too.

### What following PCSX2's VU recompiler takes

Read from microVU's sources (`pcsx2/x86/microVU*.inl`, `microVU.h`), for
an interpreter that is to give its results and its time:

- **It works in blocks**, each ending at a branch's delay slot, at an E
  bit's (the program's end) or, on VU0, at an M bit. A first pass over a
  block, from the pipeline state it is entered with, decides everything
  that is not a value: each instruction pair's stall (a VF or VI register,
  Q or P not yet written), which of four instances of the status, MAC and
  clip flags it writes and reads, which Q and P instance, whether the lower
  instruction runs first or not at all, how a branch reads a VI register
  written just before it, and the block's cycles. The second pass carries
  the instructions out with that.
- **The time is counted per block**: when a program runs, the cycles it
  may use are tested at each block's start; with none left it stops there,
  keeping the pipeline state for when it goes on, and the VU's cycle count
  grows by the cycles used, never by more than it was given.
- **A compiled block is reused for an entry state that is not its own.**
  Unless the state asks for an exact match, a block is looked for by eight
  of the state's 96 bytes (Q, P, the flags' instances, the kind of block),
  not by the stalls still pending on VF and VI registers: a block entered
  first with some stalls pending runs with them whenever it is entered
  again, from wherever. So the time depends on what microVU compiled first,
  and an interpreter that is to give the same time has to keep the same
  record: programs by content and start, blocks by state, filled in the
  order microVU fills them (both targets of a branch when the branch is
  compiled, an indirect jump when it is taken).
- **What cannot be followed**: microVU throws everything away when 64 MB
  of host code fill its cache (`mVUreset`), and what it compiles after that
  may be reused differently. Over the level run it filled 43.5 MB. Like
  the EE recompiler's own reset (execution model, "What is not
  reproduced"), a run long enough to fill it could part from the stock run
  there.

### PS2Native's VU interpreter

`runtime/vu_interp.h` is a provider of the core's, as microVU is, that the
host puts in microVU's place for VU0, VU1 or both (`ps2n-host -vu-ps2n
0|1|both`, `game.ps1 -VuPs2n`). It has three parts:

- **The first pass**, `core/include/ps2n/vu_mvu.h`: microVU's analysis of a
  block, instruction by instruction, with nothing of PCSX2 in it, so that the
  translator can use it too. Like microVU it keeps the analysis of every
  pair of micro memory in one array per VU, which a few of its checks read
  past the start of a block.
- **The record of programs and blocks**, kept as microVU keeps it: programs
  by where they start and what they were compiled from (the ranges of micro
  memory, compared after each write into it), blocks by state, filled in
  microVU's order, both sides of a conditional branch when it is compiled,
  an indirect jump when it is taken, through the same cache of jumps. With
  it the interpreter runs each block with the stalls microVU runs it with.
- **The second pass**: the instructions carried out with the host
  instructions microVU generates, on the same operands, under the VU's
  floating-point control register (G7's helpers in `ops_vu0.h`, made for
  either VU); the flags in microVU's four instances and its layout of the
  status flag; P and Q as its register holds them; at each block's end what
  its code does there (the flags' instances and P and Q moved for the next
  block, the program stopped when its cycles are used, ended at an E bit).
  With the I-bit game fix, the immediates microVU reads from micro memory
  as its code runs are read so; everything else is taken as the block was
  compiled.

What it does not carry out yet it reports, and the run stops there: the T
and M bits on a branch, the T bit with an XGKICK still to come at its
block's end, the XGKICK game fix, and the forms whose result microVU takes
from what its XMM allocator happens to hold (one component in the field of
OPMULA or of ESADD, ERSADD, ELENG, ERLENG and ESUM, ESUM of VF00 without w;
assemblers write xyz there). Killzone's VU code has none of them: no T or M
bit at all, xyz in the field of its EFU sums. (The D bit microVU leaves alone, and so
does the interpreter.) A branch in a branch's delay slot it carries out as
microVU does.

### The interpreter against microVU, on random programs

The self-test (`selftest/tables-and-semantics`, `host/selftest_vu.cpp`)
runs 2,000 random micro programs on each VU in each of four settings
(clamp mode None; the same with the I-bit game fix, Killzone's settings;
clamp mode Normal; clamp mode None with a third of the random floats
NaN), half of them in slices of a few cycles as the core runs
VU0 beside the EE, with random registers, flags, Q, P and memory, on
microVU and then on the interpreter, and compares everything the VU holds
afterwards, its cycle count and TPC included. A program that differs is
made as small as it still differs in and printed with the registers it
uses. On 2026-11-13 it found, beside two errors of the port (the I register
is clamped as an operand like any other; a pair's flag instance after a T
bit), what microVU does that the reading of its instructions does not
show, all of it now followed:

- **VI registers in host registers.** Within a block microVU keeps VI
  registers in eight 32-bit host registers and writes back only their low
  half, so their high half shows where an instruction uses all 32 bits:
  FMEQ compares the whole register with the MAC flag, ISW and ISWR store
  the whole of one that MFIR sign-extended in place, MTIR leaves a whole
  float in one, and the integer instructions carry the high half from one
  to the next. Which register is in a host register at each point is
  microVU's allocation: the loads it makes at a block's start, the least
  recently used register given up first, the registers a call may change
  emptied before an XGKICK. The interpreter keeps what each host register
  holds, allocated the same way. Killzone's VU1 code has 1,064 FMEQ, 7,379
  MFIR, 4,947 MTIR and 4,875 ISW.
- **A T bit marks the pair after it** as its block compiles: the division
  flag's write is moved to the instance the program's end uses, and the
  rest of the block runs with that. An E bit's end leaves the same marks on
  the pair after it, which later compiles read.
- **TPC after an E bit reached through a branch in a branch's delay slot**
  is the clip flag: microVU stores a register there that its end of the
  program has just used for the clip flag (on Windows; on Linux, whatever a
  call left in another).
- **Which of two NaNs.** With AVX, PCSX2's emitter swaps the two sources
  of a commutative instruction (ADDPS, MULPS) when that makes its encoding
  shorter, which happens when the first is in xmm0 to xmm7 and the second
  in xmm8 to xmm15; where both are NaN, x86 gives the first source's NaN.
  So the NaN a packed addition or multiplication of two NaNs gives under
  microVU depends on the XMM registers its allocator chose. The interpreter
  keeps that allocation too, as it keeps the VI registers' (the same count
  of allocations, the same order, no values), and swaps the sources where
  the emitter does, on a processor with AVX as PCSX2 decides from the
  processor.

With all of it, every one of 16,000 programs gives microVU's results: 2,000
on each VU in each of four settings, the fourth with a third of the random
floats NaN, of any sign and payload, so that two NaNs meet often.

**The first 4,200 frames** (`start.pad`, nothing drawn), compared with stock
PCSX2 at every sync point: with VU0 on the interpreter, and then with both
VUs on it, the compiled game is the stock run at every one of 38,478,822
sync points, VU0's and VU1's registers included, with the sound hardware in
the same state at the end. VU1 ran 31,122,496 instruction pairs in 265,773
programs, 816,062 indirect jumps among them; VU0 652,207 pairs in 26,240.
The interpreter recorded the same programs as microVU, in the same order,
at the same addresses, with the same number kept at each (317 for VU1, 3
for VU0), its lines the same as the ones microVU writes to the core's log.

**The level run** (`level1.pad`, 17,500 frames, the hardware renderer's
pictures every tenth frame), both VUs on the interpreter, compared with
stock PCSX2 at every sync point (2026-11-13, before the XMM allocation was
followed, and again after, with the same result): the compiled game is the
stock run at every one of 93,221,937
sync points in place and time, VU0's and VU1's registers the same at each;
the FPU's registers differ at the 964 sync points where it follows the
console on purpose, as before; the pictures are the same at all 1,750
frames and the sound hardware in the same state at all 1,750 moments. VU1
ran 13,008,036,570 instruction pairs in 4,430,503 runs, with 81,995,176
indirect jumps; VU0 1,054,397,962 pairs in 68,965,572 runs, of which
17,444,741 stopped for their cycles and went on in the next. The interpreter recorded 6,088
programs for VU1 and 29 for VU0, as microVU does. The build before this
day's work had parted from the stock run near frame 5,300 (sync point
47,712,416).

**Its speed is not yet looked at.** The interpreter goes over microVU's
allocation of registers again each time it runs a block, though microVU
decides it once, when it compiles the block: the comparison run took 2,197
s with the XMM allocation followed, 1,554 s without (both runs, stock
PCSX2's and PS2Native's, in each). The allocation could be worked out once
per block and kept with it.

## Open

- **What is left of VU0 in the game's EE code.** Since G7 its instructions
  are PS2Native's, but for `VCALLMS` and `VCALLMSR`, which start a micro
  program and stay PCSX2's function, as in its recompiler. The micro
  programs run on PCSX2's VU0 recompiler, a stand-in of
  [ADR 0003](adr/0003-no-jit-in-generated-games.md). Since G9 compiled
  code carries a VU0 instruction out itself unless its block has something
  done about VU0 first, and since G10 the transfers as well, and it calls
  PCSX2's function for `VCALLMS` and `VCALLMSR` itself; the runtime carries
  out only what comes where VU0 is running a micro program and has to be
  brought up to date first ("G10", "The rest of the COP2 instructions").
  The micro programs themselves, VU0's and VU1's, are step G11's: measured
  so far ("G11").
- **Speed.** 4,200 frames of logos, a prompt and a film took 25.7 s
  compiled against 22.9 s under stock PCSX2 with the software renderer, and
  15.1 s against 11.2 s with nothing drawn, at G3; on 2026-11-13 they take
  23.4 s against 22.7 s, and 12.3 s against 11.3 s ("G10", "Where it
  stands (main tree, 2026-11-13)"). Where the second that is left with
  nothing drawn goes has not been profiled.
  Over the level run the compiled game took 1.35 times as long as stock
  PCSX2 at G5, all of the difference in the game's own code; after the
  changes of G6 it takes 1.21 to 1.23 times as long (91.7 s against 75.6 s, 91.3 s against 74.3 s), and
  the game's own code 41.1 s against 25.8 s; after G7, 1.21 (91.8 s
  against 76.1 s); after G9, 1.19 (89.5 s against 74.9 s); after G8, 1.18
  (86.9 s against 73.9 s); with profile-guided optimization (G10), 1.11
  (81.3 to 81.7 s against 73.0 to 73.2 s), play alone 1.13; with loops
  that have reached a fixed point skipped (G10), 1.07 (78.8 to 80.3 s
  against 74.0 to 74.3 s), play alone 1.08; with the COP2 transfers made
  by compiled code (G10), the same within the noise, 1.08 (79.3 to 80.0 s
  against 73.5 to 74.7 s), play alone 1.08; link-time optimization of
  PS2Native's code (G10) gave nothing (play 1.09), and is not used; with
  the COP2 instructions that come first in their block carried out by
  compiled code (G10), 1.06 (78.2 to 78.7 s against 74.1 to 74.6 s), play
  alone 1.06; with the rest of them (G10), the same, 1.06 (77.3 to 79.0 s
  against 73.1 to 74.1 s), play alone 1.06. In the main tree, without
  profile-guided optimization, calls through a register made by compiled
  code and the seldom-taken way of loads and stores made short took play
  from 1.18 to 1.14 times stock PCSX2's time (G10, "In the main tree");
  both together, with profile-guided optimization (G10), 1.04 (77.4 to
  78.4 s against 74.7 to 74.9 s), play alone 1.04; with `lq` and `sq` on
  the locals and a block start asking in one argument (G10, the main tree,
  2026-11-13), the same, 1.04 (76.9 to 77.5 s against 74.0 to 74.9 s),
  play alone 1.04; with the FPU's registers in SSE registers and the bits
  its additions keep looked up (G10, the main tree, 2026-11-13), 1.01
  (73.7 to 74.7 s against 72.9 to 73.9 s), play alone as long as stock
  PCSX2's (0.985 to 1.006 times). More than a
  third of the
  compiled code's time is now reaching memory through the core's address
  table, where PCSX2's recompiler uses one instruction and a fault ("G6",
  "Where the time is now"). The 4,200-frame times above are from G3 and
  have not been taken again since.
- **What a longer run could meet.** PCSX2's recompiler throws all its
  blocks away when its code cache is full, and PS2Native cannot know when
  that is (execution model, "What is not reproduced"). It did not happen in
  17,500 frames: stock PCSX2's log has the two resets at start-up and no
  other. PCSX2 logs it when it does ("EE/iR5900 Recompiler Reset" in
  `core.log`), and the same-as-stock comparison would show the runs part
  there.
- **Code outside the executable.** What the game copies into kernel memory
  is captured and compiled with it since G8 (`game_outside`); none of the
  four `.REL` modules has run yet. A module's code would be captured the
  same way, as the code it ran outside its executable, but a module loaded
  at another address on another run would need its code taken by its
  content rather than its place: not built. The test programs, built
  without a capture, still interpret what ps2sdk copies into kernel memory.
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
