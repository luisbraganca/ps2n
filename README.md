<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/branding/logos/ps2native-horizontal-dark.png">
    <img src="assets/branding/logos/ps2native-horizontal-light.png" alt="PS2Native" width="420">
  </picture>
</h1>

<p align="center">
  <strong>PlayStation 2 executables, statically recompiled into native C++.</strong>
</p>

<p align="center">
  <a href="https://luisbraganca.github.io/ps2n/">Website</a> ·
  <a href="GETTING_STARTED.md">Get started</a> ·
  <a href="#game-compatibility">Compatibility</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="https://github.com/luisbraganca/ps2n/issues">Report an issue</a>
</p>

PS2Native (`ps2n`) reads a PlayStation 2 Emotion Engine (R5900) ELF, discovers
its functions and translates them into readable C++. The generated program
links against a runtime that supplies the rest of the console: the EE kernel,
hardware registers, vector units, GS, IOP and SPU2. You provide your own game
files; no proprietary BIOS or game data is included.

> **Early development:** two games reach gameplay and one reaches its menu.
> None is playable from start to finish yet. See the
> [compatibility table](#game-compatibility) before choosing a game to test.

```
game.elf ──► ps2n recompile ──► C++ (one statement per instruction)
                                    │
                ps2n runtime ───────┤  kernel, memory map, DMAC/GIF/GS,
                                    │  VIF + VU, IPU, timers, SIF, IOP,
                                    ▼  SPU2, fallback interpreter
                              native executable
```

### Why this approach

- **Native, inspectable output.** Generated functions use ordinary C++ calls,
  branches and switches, with the original instruction beside each statement.
- **One instruction implementation.** Recompiled and interpreted code share
  the same semantics, making behavioural differences easier to diagnose.
- **Code missed by analysis still runs.** A built-in interpreter handles
  missed entry points and records them for the next recompile instead of
  stopping the game.
- **No BIOS emulation required for supported paths.** Sony's kernel and system
  services are implemented on the host; game-specific IOP modules still run on
  an R3000A interpreter.
- **Native or higher-resolution graphics.** The software renderer draws on the
  CPU. The Vulkan renderer supports 1x to 8x internal resolution and matches
  the software renderer's pixels at 1x on the tested paths.

Generated code stays close to the assembly:

```cpp
// fib  0x00100178-0x001001c8  (20 instructions, from ELF symbol)
void fib_00100178() {
    ...
L_0010018c:
    ADDIU(SP, SP, -32);                             // 0010018c  addiu $sp, $sp, -32
    SD(RA, SP, 0);                                  // 00100190  sd $ra, 0($sp)
    SD(S0, SP, 8);                                  // 00100194  sd $s0, 8($sp)
    SD(A0, SP, 16);                                 // 00100198  sd $a0, 16($sp)
    // 0010019c  jal 0x00100178  -> fib
    setLink(RA, 0x001001a4u);
    ADDIU(A0, A0, -1);                              // 001001a0  addiu $a0, $a0, -1  [delay slot]
    PS2_CALL(0x00100178u, fib_00100178, 0x001001a4u);
L_001001a4:
    ADDU(S0, V0, ZERO);                             // 001001a4  move $s0, $v0
    ...
}
```

## Status

### Game compatibility

Stages follow [PCSX2's compatibility scale](docs/games.md). **In-game means
that gameplay starts with major gaps, not that the whole game works.**

| Game | Tested release | Stage | What has been tested |
|---|---|---|---|
| [007: Agent Under Fire](docs/games.md#007-agent-under-fire) | Europe, SLES-50539 | **In-game** | Menus, movies, sound, the complete first mission and the second mission's race |
| [Resident Evil Code: Veronica X](docs/games.md#resident-evil-code-veronica-x) | USA, SLUS-20184 | **In-game** | Menus, movies and the first areas with controller input and saving |
| [Secret Agent Clank](docs/games.md#secret-agent-clank) | USA, SCUS-97623 | **Menus** | Start screen and main menu; its front end runs on the interpreter, and no level has been loaded |

Other releases need their own configurations. The
[game notes](docs/games.md) list known rendering, sound, performance and
coverage limitations, and [GETTING_STARTED.md](GETTING_STARTED.md) has the
exact setup for these discs.

### Runtime at a glance

- The recompiler covers all 359 EE instructions and has processed the main
  executables of three retail games. Relocatable modules and overlays remain
  open problems.
- The runtime covers the kernel, DMA, interrupts, timers, vector units, MPEG-2
  decoding, SPU2 audio, discs, directory-backed memory cards and controllers.
- Vulkan requires Vulkan 1.3 and fragment shader interlock. Where those are
  unavailable, the software renderer is the fallback; higher resolutions need
  Vulkan.
- Windows x64 is the only platform currently built and tested. Linux build
  instructions are provided but untested; macOS has not been rebuilt since the
  project moved to Windows.

### By area

| Area | Progress | Count | Biggest gaps |
|---|---|--:|---|
| [Games playable](docs/games.md) | `░░░░░░░░░░` 0% | 0 / 3 | No game has been tested end to end; Secret Agent Clank has not loaded a level |
| EE instructions | `██████████` 100% | 359 / 359 | |
| Recompiler | `████████░░` 80% | 8 / 10 | overlays, relocatable modules |
| EE kernel syscalls | `█████████░` 88% | 127 / 145 | event flags, `ExecPS2`, the TLB, CPU timers |
| DMA, interrupts, timers | `█████░░░░░` 50% | 4 / 8 | MFIFO, stall control, transfer timing |
| Graphics (GS) | `███████░░░` 73% | 16 / 22 | the CLUT buffer, scan masks |
| Vector units | `█████████░` 88% | 7 / 8 | their timing |
| Movies (IPU) | `████████░░` 83% | 5 / 6 | timing |
| Sound (SPU2) | `███████░░░` 71% | 10 / 14 | the IOP's DMA channels for drivers without libsd, an audio settings page |
| IOP library functions | `████████░░` 77% | 428 / 557 | `mcman` (0 / 40), `dmacman`, `intrman`, `sifman` |
| IOP services | `████████░░` 83% | 10 / 12 | multitap connections, network, hard disk, USB and i.LINK |
| Discs and memory cards | `█████░░░░░` 50% | 5 / 10 | dual-layer DVDs, drive timing |
| Controllers and window | `███████░░░` 69% | 9 / 13 | multitap, other devices, speed settings |
| Widescreen, upscaling | `████████░░` 80% | 4 / 5 | no-interlacing patches, PCSX2's per-game upscaling fixes (see the [roadmap](#roadmap)) |
| Platforms | `███░░░░░░░` 33% | 1 / 3 | Linux, never built yet; macOS, not rebuilt since the move to Windows |

Each item in the checklists below has the same weight. **A bar measures listed
feature coverage, not remaining effort or game compatibility.** A single
missing service or timing detail can still prevent a game from working.

<details>
<summary><b>What each bar counts</b></summary>

**Recompiler**: 8 of 10
- [x] Function discovery: ELF symbols, Ghidra maps, call targets, code and data pointers, prologues
- [x] Jump tables, as `switch` statements
- [x] Delay slots (and jumps into them), branch-likely, tail calls, fall-through between functions
- [x] Resume points, so threads, interrupts and longjmp can re-enter a function mid-way
- [x] Games made of several executables
- [x] Programs a loader copies in and jumps into, recognised by their code in RAM
- [x] Missed code runs on the interpreter and is recorded for the next recompile
- [x] Config bindings: stubs, skips, C++ overrides, instruction patches
- [ ] Overlays: code replaced while its program runs
- [ ] Relocatable modules a game loads and links itself (Secret Agent Clank's `SNR2` files)

**DMA, interrupts, timers**: 4 of 8
- [x] The interrupt controller
- [x] Timers 0-3, with compare and overflow interrupts
- [x] Vsync timing, NTSC and PAL
- [x] DMA in normal and chain mode: VIF0, VIF1, GIF, IPU, RAM to scratchpad
- [ ] Scratchpad to RAM in chain mode
- [ ] MFIFO, the ring buffer from scratchpad to VIF1 or the GIF
- [ ] Stall control and interleaved transfers
- [ ] Transfer timing: a transfer ends the moment it starts

**Graphics (GS)**: 16 of 22
- [x] Display: both circuits, their merging, interlaced frames and fields
- [x] GIF: paths 1-3, PACKED, REGLIST and IMAGE, PATH3 masking
- [x] Drawing alongside the EE, on the GS's own thread (real-time runs)
- [x] Points, lines, triangles, strips, fans, sprites
- [x] Flat and Gouraud shading
- [x] Every colour texture format, with 4- and 8-bit CLUTs and TEXA
- [ ] Depth buffers sampled as textures
- [x] The four texture functions and every wrap mode
- [x] Fog
- [x] Alpha test
- [x] Depth test
- [x] Alpha blending, with PABE, FBA and colour clamping
- [x] Scissoring and frame-buffer masks
- [ ] Scan masks: drawing only the even or the odd lines (`SCANMSK`)
- [x] Transfers into GS memory and within it
- [ ] The CLUT buffer: CLD load conditions and the CSM2 layout
- [x] Bilinear filtering
- [x] Mipmaps
- [ ] Destination alpha test
- [ ] Dithering
- [ ] Anti-aliasing
- [x] Transfers from GS memory back to the EE

**Vector units**: 7 of 8
- [x] VU0 macro mode
- [x] Every micro instruction, upper and lower, on VU0 and VU1
- [x] The pipeline as micro code sees it: flag delays, Q and P latencies, stalls, delay slots, the E bit
- [x] VIF: every UNPACK format and mode, MPG, MSCAL and the double buffer, DIRECT, the i bit's interrupt and stall
- [x] XGKICK to the GIF
- [x] VU1's micro programs running alongside the EE, on a thread of their own (real-time runs)
- [ ] The vector units' timing: a program takes no emulated time
- [x] A VU recompiler: micro code the runtime recorded, recompiled with the pipeline's timing worked out ahead, each program once and run wherever the game puts it

**Movies (IPU)**: 5 of 6
- [x] Every command: BCLR, IDEC, BDEC, VDEC, FDEC, SETIQ, SETVQ, CSC, PACK, SETTH
- [x] MPEG-2 decoding to the standard: code tables, scans, quantisation, an IEEE 1180 inverse DCT
- [x] Colour conversion: BT.601, alpha thresholds, dithered RGB16
- [x] FIFOs fed and drained by DMA, as on the console
- [x] The IPU interrupt
- [ ] Timing: a command takes no emulated time

**Sound (SPU2)**: 10 of 14. libsd's calls count under IOP library functions.
- [x] ADPCM voice decoding, with the 4-point interpolation
- [x] Pitch and pitch modulation, looping, key on and off
- [x] ADSR envelopes
- [x] Volumes and volume sweeps
- [x] Noise
- [x] Reverb, at half rate through its resampling filter
- [x] Both cores, mixed: dry and reverb mixes, core 0 into core 1
- [x] Streamed PCM input (AutoDMA)
- [x] Interrupts at a sound memory address
- [x] Audio output, in time with the game: SDL, PCSX2's volume settings and hotkeys, a WAV dump
- [ ] The IOP's DMA channels 4 and 7, for drivers that program the SPU2 without libsd
- [ ] Reading the output back by DMA
- [ ] S/PDIF: bypass and compressed output
- [ ] PCSX2's audio settings page (ps2n.ini holds the settings)

**IOP services**: 10 of 12
- [x] SIF: transfers, command packets, RPC bind and call, IOP reset
- [x] Module loading: IRX relocation and linking, LOADFILE
- [x] Replacement IOP images: each module's release reported
- [x] FILEIO, the BIOS's protocol (release 1000)
- [x] FILEIO release 2100 (SDK 2.1): open, close, read, write, seek
- [x] FILEIO release 2200 and later (SDK 2.2 on), whose replies are asynchronous
- [x] The IOP heap
- [x] CDVDFSV: reads, search, streaming, S-commands
- [x] MCSERV and XMCSERV
- [x] PADMAN
- [ ] MTAPMAN with a multitap connected
- [ ] Network, hard disk, USB and i.LINK

**Discs and memory cards**: 5 of 10
- [x] ISO images, with 2048- or 2352-byte sectors
- [x] A directory presented as a disc
- [ ] Dual-layer DVDs
- [ ] Drive timing: seeks and reads take no time
- [ ] Raw sector reads and CD audio
- [x] A host directory per memory card
- [x] The card's file system: 8 MB, 1 KB clusters, three open files, its error codes
- [x] A newly inserted card, reported once
- [ ] File times and attributes
- [ ] PCSX2's memory card files (`.ps2`)

**Controllers and window**: 9 of 13
- [x] DualShock 2: digital, analog and pressure-sensitive modes
- [x] Vibration
- [x] Keyboard and SDL game controllers, bound the way PCSX2 binds them
- [x] Hotkeys
- [x] Settings kept in PCSX2's file format
- [ ] Multitap
- [ ] Other devices: guitars, light guns, USB keyboards and mice
- [ ] Input profiles and macros
- [x] A window, fullscreen, the picture in its television's shape
- [x] Deinterlacing: adaptive, weave, bob
- [x] Pause menu and settings pages
- [x] Screenshots, frame dumps and on-screen messages
- [ ] Speed settings: turbo and slow-motion speeds

**Widescreen, upscaling**: 4 of 5
- [x] PCSX2's aspect ratio settings: Stretch, Auto 4:3/3:2, 4:3, 16:9, 10:7, and its FMV override
- [x] Widescreen patches: PCSX2's pnach files, applied when a game is recompiled, and their writes to memory a game fills at run time every vsync
- [ ] No-interlacing patches
- [x] A GPU renderer: Vulkan, drawing the software renderer's pixels at the console's resolution
- [x] Higher internal resolution: 1x to 8x, PCSX2's `upscale_multiplier`, changed while a game runs

**Platforms**: 1 of 3
- [x] Windows x64, with clang-cl and MSVC
- [ ] Linux x64: never built yet; [GETTING_STARTED.md](GETTING_STARTED.md) has the steps to try
- [ ] macOS on Apple silicon, where the project started: not rebuilt since it moved to Windows

</details>

<sub>Instruction, syscall and IOP function rows are counted rather than
checklisted; last checked on 2026-11-07. Syscalls are counted against the
numbers ps2sdk's `syscallnr.h` names (aliases resolved, reserved numbers left
out). IOP functions are counted against the export tables the project knows
([irx_exports.inc](core/src/irx_exports.inc), plus those the runtime declares),
not every function of every Sony module. Calls the runtime accepts with no
effect count as implemented. [docs/runtime.md](docs/runtime.md#what-is-covered)
describes the coverage and its limits.</sub>

### Speed

Performance depends on the scene, not just the recompiled CPU code. On an
Intel i5-14600KF, Agent Under Fire's measured first-mission views and scripted
second-mission race reach full speed, while some heavy effects still drop
pictures. Code: Veronica X's measured menus and movies reach full speed; its
gameplay has not been benchmarked. A tight recompiled integer loop measured
about 1.8 billion guest instructions a second on an Apple M3, roughly ten
times the interpreter. [The game notes](docs/games.md#speed) record the test
hardware, renderer, resolution and limitations behind these figures.

## Roadmap

Roughly in order:

1. **Agent Under Fire to Playable:** continue the driving missions past the
   second mission's race, then follow the next concrete blocker.
2. **Linux:** build and test the guide on a real installation.
3. **Sound:** add PCSX2's audio settings page and the IOP DMA channels 4 and 7
   used by drivers that program the SPU2 directly.
4. **Graphics speed:** investigate the remaining frame-time spikes in the
   driving race, and improve the software renderer for machines without a
   suitable Vulkan driver.
5. **More games:** take Secret Agent Clank into its first level, support
   relocatable modules, and fill in `mcman`, event flags, `ExecPS2`, dual-layer
   discs and code overlays as games require them.
6. **Vector-unit timing:** micro programs currently take no emulated time.
7. **Display and upscaling:** add no-interlacing patches and PCSX2's remaining
   patch operations and per-game upscaling fixes. Widescreen patches and 1x to
   8x rendering already work; Round Sprite Full is an optional fix for some
   sprite-edge seams, not a universal default. The
   [renderer reference](docs/runtime.md#rendering) lists the remaining cases.

## Getting started

**To test a game**, start with [GETTING_STARTED.md](GETTING_STARTED.md). It
covers the tools, building PS2Native, imaging a disc, choosing the matching
configuration, running the game and reporting a problem on Linux and Windows.

**To build the project**, you need CMake 3.20 or later, a C++20 compiler and
Ninja. SDL2 is optional but recommended for a window, audio and controllers.
Without it, tests can dump frames to files. Vulkan is loaded from the graphics
driver at run time; a separate Vulkan SDK is only needed to rebuild shaders.

```sh
git clone https://github.com/luisbraganca/ps2n.git
cd ps2n
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/hello/hello --no-window
```

On Windows, configure from an x64 Visual Studio developer prompt. clang-cl is
recommended; MSVC (`cl`) is also tested. SDL2's Visual C++ development package
works directly:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DSDL2_DIR="<SDL2-devel folder>/cmake"
cmake --build build
ctest --test-dir build --output-on-failure
.\build\examples\hello\hello.exe --no-window
```

[examples/hello](examples/hello) demonstrates the complete path from PS2
assembly to a native executable, including loops, recursion, a jump table,
function pointers, MMI, the FPU, VU0, non-local returns, kernel threads and a
vblank interrupt handler. Its output starts with:

```
Hello from a recompiled PS2 program!
loop: sum(1..100) = 5050
recursion: fib(20) = 6765
...
vsync: counted 3 vblank interrupts
done
```

## Porting a game

1. **Inspect the ELF.** Run
   `ps2n analyze SLUS_123.45 --write-config game.toml` to see code ranges and
   write a starter configuration.
2. **Supply function boundaries for a stripped game.** Run
   [tools/ghidra/ExportPs2nFunctions.java](tools/ghidra/ExportPs2nFunctions.java)
   in Ghidra and set `[input] ghidra_map` to the CSV. Ghidra's own Functions /
   Symbol Table exports and PS2Recomp's exporter work too.
3. **Recompile.** `ps2n recompile game.toml` writes C++, a CMake project and
   `report.txt` with discovered functions, bindings, unresolved indirect jumps
   and warnings. For games with several executables, list them in
   [`[input] programs`](docs/config.md#games-with-several-executables).
4. **Build and run.** Use
   `cmake -S out/game -B out/game/build && cmake --build out/game/build`, then
   `out/game/build/game --cdrom <disc image or directory>`. Add
   `--region europe` for PAL and `--memcards DIR` to reuse existing cards.
5. **Iterate from the exit summary.** It counts unimplemented syscalls,
   registers, IOP calls and interpreted addresses. Add the recorded
   `ps2n_missing.toml` to `[analysis] entries_files` and recompile, bind or
   override functions in the config, or implement the missing runtime service.
   [docs/runtime.md](docs/runtime.md#diagnosing-a-run) explains the tools.

Run the same build with `--interpret` to compare against the interpreter
(functions replaced by the config stay replaced). A problem in both modes
points to the runtime; a problem only in recompiled mode points to translation.

Once the game runs, record its exact release, tested scope and known problems
in [docs/games.md](docs/games.md), and put its configuration in
[games](games/README.md) without game data or recorded game code.

## Command line

```
ps2n recompile <config.toml> [--elf PATH] [--program PATH]... [--out DIR] [--name NAME]
               [--files N | --single] [--no-project] [--no-comments] [--quiet]
ps2n analyze <game.elf | config.toml> [--map CSV]... [--list] [--write-config FILE]
ps2n disasm <file.elf> [--function NAME|ADDR] [--range START:END] [--cpp NAME|ADDR]
ps2n irx <module.irx>          # IOP module name, version, imports, exports
ps2n assemble <in.s> -o <out.elf> [--base ADDR] [--strip]
ps2n vu <code.toml>... -o <out.cpp> [--namespace NS]   # recorded VU micro code as C++
ps2n stubs                     # runtime stub handlers available for binding
ps2n version
```

`disasm --cpp NAME` prints the C++ generated for one function, the quickest way
to inspect a translation.

A recompiled game has its own `--help`. Common options include `--cdrom`,
`--region europe`, `--memcards DIR`, `--interpret`, `--strict`,
`--virtual-clock`, `--pad-script FILE`, `--dump-frames DIR`,
`--trace-syscalls` and `--log-level`. The
[runtime reference](docs/runtime.md#running-a-recompiled-game) documents every
option, and [tester logging](docs/runtime.md#a-testers-log-in-axiom) explains
how to share a run's diagnostics.

## Configuration

```toml
[input]
elf = "SLUS_123.45"
ghidra_map = "functions.csv"          # optional
programs = ["MAIN.ELF"]               # optional: the game's other executables

[output]
dir = "out/game"
mode = "multi"                        # or "single"
files = 16                            # fixed file count (handy for build systems)

[analysis]
entries_files = ["ps2n_missing.toml"]

[vu]
code_files = ["vu-code.toml"]         # VU micro code a run recorded, recompiled too

[stubs]
auto = true                           # bind symbols named like runtime stubs (printf, memcpy, ...)
bind = { "0x00123456" = "ret0", "sceCdRead" = "ret1" }

[functions]
skip = ["sceMpegInit"]                # not recompiled; logs and returns 0
override = { "0x00140000" = "my_draw_hud" }   # your C++ function

[[patches]]                           # instruction patches, checked against the original word
address = 0x00100abc
value = 0x00000000
expect = 0x0c040000

[pnach]                               # PCSX2 patch groups applied during recompilation
files = ["patches/SLES-50539_992B46DC.pnach"]
groups = ["Widescreen 16:9"]
```

See [docs/config.md](docs/config.md) for every key, path resolution, multiple
executables, VU recordings and supported patch operations.

## How it works

- **Decoding:** one table,
  [r5900_ops.inc](core/include/ps2n/r5900_ops.inc), drives the decoder,
  disassembler and assembler. A second table,
  [r5900_sem.inc](core/include/ps2n/r5900_sem.inc), maps instructions to shared
  runtime operations used by both generated code and the interpreter.
- **Instruction semantics:** the
  [`ops_*.h` headers](runtime/include/ps2) contain the implementation. Fixing
  an instruction requires rebuilding the game, not regenerating its C++.
- **Control flow:** branches become labels and `goto`, direct calls become C++
  calls, and jump tables become `switch`. Return-address checks and resume
  switches let guest threads, interrupts and longjmp unwind host frames without
  losing guest state.
- **VU micro code:** the runtime records programs it interprets. `ps2n
  recompile` turns those recordings into C++ with pipeline timing worked out
  ahead, and finds a program by its code wherever the game uploads it.
- **The runtime:** loads the original ELF's data at start-up and executes from
  its entry point. The GS can draw in a deterministic software rasterizer or in
  an exact-integer Vulkan fragment pipeline. In real-time runs the GS and VU1
  have host threads of their own while guest EE threads remain on one thread.

[docs/architecture.md](docs/architecture.md) explains the execution model,
threading, memory ownership and renderer in depth.

## Design decisions, and where this differs from PS2Recomp

[PS2Recomp](https://github.com/ran-j/PS2Recomp) informed the project's scope.
PS2Native is an independent implementation with different trade-offs:

| Area | PS2Recomp | PS2Native | Reason |
|---|---|---|---|
| Calls | lookup through `runtime->dispatchGuestBranch`; scheduler unwinds with a C++ exception | direct C++ calls, return-address checks and resume switches | enables inlining and avoids exceptions on hot paths; one mechanism handles thread switches, longjmp and unusual returns |
| CPU context | `ctx` pointer passed to every function | one global `ps2::cpu`, separate global guest RAM | guest stores provably cannot alias guest registers |
| Semantics | emitted per instruction | shared inline functions | readable output and one place to fix both execution modes |
| SIMD | SSE intrinsics, sse2neon on ARM | portable lane loops, auto-vectorised | the same source builds on ARM64 and x86-64 |
| Missed code | function-not-found error | interpreter fallback and a recording for the next recompile | missing analysis costs speed rather than stopping the game |
| IOP | IRX modules on an R3000 interpreter | IRX modules too, plus host implementations of the IOP kernel and Sony's system services; libsd drives an emulated SPU2 | preserves game-specific modules without requiring a BIOS image or low-level SIO2/CDVD emulation on supported paths |
| Dependencies | libraries fetched at configure time | vendored toml++, Dear ImGui and Vulkan headers; optional SDL2 | offline, reproducible builds; no PS2 SDK needed to build or run |
| Output size | program counter written before every instruction | program counter stored at control transfers | smaller, faster generated code |

Two working rules matter throughout:

- **Generated code is a build artifact.**
  [`ps2n_add_recompiled_game()`](cmake/Ps2nRecompile.cmake) can recompile during
  a build and rewrites only files whose contents changed.
- **Missing behaviour is visible.** Unimplemented paths are logged, counted and
  summarised at exit; `--strict` stops at the first one. Patches can check the
  original word so a configuration cannot silently modify another release.

## Project layout

```
core/         ELF and IRX readers, R5900 decoder / disassembler / assembler (ps2n_core)
recompiler/   analysis, code generation, config, CLI (ps2n_recomp, ps2n)
runtime/      what recompiled games link against (ps2n_runtime)
  include/ps2/  generated-code API: cpu.h, memory.h, ops_*.h, recomp.h, runtime.h
  src/          dispatcher, interpreter, VU, kernel/ (EE syscalls), hw/ (DMAC, GS, VIF, IPU),
                gpu/ (the Vulkan renderer and its shaders), iop/ (IOP CPU, kernel, services, SPU2),
                stubs/, audio/, video/, input/, ui/
cmake/        build helpers: recompiling at build time, warnings, the runtime's commit
games/        release-specific configs and entry points; no game data
tools/ghidra/ function-map exporter for Ghidra
examples/     the end-to-end example
tests/        unit tests and end-to-end programs
docs/         project website, user and developer documentation
assets/       branding and Windows executable icons
third_party/  toml++ (MIT), Dear ImGui 1.92.9b (MIT), Vulkan headers 1.4.363 (Apache-2.0 or MIT)
```

## Documentation

| Document | Use it for |
|---|---|
| [GETTING_STARTED.md](GETTING_STARTED.md) | Step-by-step setup, testing and reporting a problem |
| [docs/games.md](docs/games.md) | Per-game compatibility, known issues and measured performance |
| [docs/runtime.md](docs/runtime.md) | Runtime options, rendering, debugging, feature coverage and extension points |
| [docs/config.md](docs/config.md) | The complete game-configuration reference |
| [docs/architecture.md](docs/architecture.md) | Why the recompiler, scheduler, threads and renderer work this way |
| [docs/input.md](docs/input.md) | Controllers, bindings, graphics settings and hotkeys |
| [games/README.md](games/README.md) | Which releases and executable files the bundled configs expect |
| [assets/branding/README.md](assets/branding/README.md) | Logo files and usage |
| [CLAUDE.md](CLAUDE.md) | Repository-specific build, testing and maintenance rules for coding agents |

## Testing

```sh
ctest --test-dir build --output-on-failure
```

| Suite | What it checks |
|---|---|
| `core` | Instruction-table decoding, 10,000+ random disassembly/assembly round trips, known encodings, ELF and IRX parsing. |
| `recompiler` | Config parsing and errors, function maps, jump tables, stripped-ELF discovery, mixed code/data sections, bindings, patches and code generation. |
| `runtime` | Instruction semantics, the GS, Vulkan against the software renderer at 1x and above where supported, VU1 threading, IPU decoding, DMA/VIF/GIF, SIF RPC and IOP services, discs, strict mode, timing, input, SPU2 and Axiom logging. |
| `program_*` | Assembly programs recompiled at build time and run both recompiled and interpreted against recorded output or frames: control flow, missing code, polling loops, kernel scheduling, interrupts, `LoadExecPS2` and loaders. |
| `sdk_*` | Prebuilt ps2sdk programs covering an IRX RPC server, libsd, libpad/libpadx, VU0/VU1, libcdvd and libmc. See [tests/sdk/README.md](tests/sdk/README.md). |
| `vu_compiled` | Random VU0/VU1 programs run recompiled and interpreted from identical states, comparing registers, data memory and cycle counts. |

New behaviour gets regression coverage, checked by planting bugs in the code
under test. Development builds run with warnings as errors under both clang-cl
and MSVC; the exact commands are in [CLAUDE.md](CLAUDE.md#build-and-test).

## Legal

The recompiler and runtime contain no Sony code or data, and generated code
contains no game data: the runtime loads it from your ELF at start-up. Use
executables you are entitled to use. The test programs in `tests/sdk/bin` are
linked with ps2sdk (Academic Free License 2.0).

## Acknowledgements

[toml++](https://github.com/marzer/tomlplusplus) (MIT) parses configs,
[Dear ImGui](https://github.com/ocornut/imgui) (MIT) draws the settings UI, and
the Khronos Group's [Vulkan headers](https://github.com/KhronosGroup/Vulkan-Headers)
(Apache-2.0 or MIT) describe the graphics API. Controller input follows
[PCSX2](https://github.com/PCSX2/pcsx2)'s design: its settings format, defaults
and settings pages, written anew. Instruction encodings were checked against
[rabbitizer](https://github.com/Decompollaborate/rabbitizer)'s tables;
edge-case semantics and the GS memory layout against PCSX2; and kernel and SIF
structures follow [ps2sdk](https://github.com/ps2dev/ps2sdk)'s headers.
[Play!](https://github.com/jpd002/Play-) was a reference for IOP protocols and
the IPU's code tables. [AnyPS5](https://github.com/boykopovar/AnyPS5) inspired
name-based native library replacements and summarised unimplemented-path
logging. [PS2Recomp](https://github.com/ran-j/PS2Recomp) and
[N64Recomp](https://github.com/N64Recomp/N64Recomp) showed the way.
