# PS2Native

PS2Native (`ps2n`) is a static recompiler for PlayStation 2 executables. It
reads an Emotion Engine (R5900) ELF, finds its functions, translates every
instruction into plain C++, and links the result against a runtime that stands
in for the rest of the console: the EE kernel, the hardware registers, the GS,
and the IOP. The output is an ordinary native program you build with CMake.

```
game.elf ──► ps2n recompile ──► C++ (one statement per instruction)
                                    │
                ps2n runtime ───────┤  kernel, memory map, DMAC/GIF/GS,
                                    │  VIF + VU, timers, SIF, the IOP,
                                    ▼  fallback interpreter
                              native executable
```

Generated code reads like the assembly it came from:

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
```

## Status

**Early development: no game is playable yet.** The recompiler, the part that
translates code, is the furthest along: it covers the whole EE instruction set
and has recompiled a retail game. Most of the work left is in the runtime,
which stands in for the rest of the console. There is no sound at all, the
graphics come from a software renderer that cannot keep full speed even in the
first game's menus, the vector units never run alongside the EE, and the IOP
services cover what the first game needed.

### Games

Rated on PCSX2's compatibility scale: Nothing, Intro, Menus, In-game,
Playable, Perfect.

| Game | Stage | Notes |
|---|---|---|
| 007: Agent Under Fire (Europe, SLES-50539) | Menus | All three executables run recompiled. The language selection, the intro movies (without sound), the title screen and the main menu work, and so does the controller. Starting a mission is untested, and the driving missions' executable is not recompiled yet. |

### By area

| Area | State | Notes |
|---|---|---|
| EE instruction set | Done | All 359 instructions: R5900, MMI, COP0, FPU, VU0 macro mode. Recompiled code and the interpreter share one implementation. |
| Analysis and code generation | Working | Stripped ELFs, Ghidra maps, jump tables, games made of several executables. Used on test programs, homebrew and one retail game (8,076 functions in Agent Under Fire's main executable). Code overlays are not supported. |
| EE kernel | Working | 127 of the 145 syscalls ps2sdk names. Missing: event flags, `ExecPS2`, the TLB calls, CPU timers. |
| IOP | Partial | IRX modules run on an R3000A interpreter; the IOP kernel and Sony's system modules are host code, with 428 of the 557 functions known for 26 libraries. Missing: FILEIO's asynchronous protocol (SDK 2.2 and later, which most later games use), the `mcman` library (0 of 40), network, hard disk, USB. |
| Graphics (GS) | Partial | A software rasterizer at the PS2's resolution, drawing on the game's thread: right in the first game's menus, but below full speed there. Missing: texture filtering and mipmaps, dithering, anti-aliasing, reading GS memory back to the EE, a GPU renderer. |
| Vector units | Partial | Every micro instruction, interpreted with the pipeline effects micro code can observe. A program finishes the moment it starts, so it never runs alongside the EE. No VU recompiler. |
| Movies (IPU) | Working | Every command, following the MPEG-2 standard. Commands take no emulated time. |
| Sound (SPU2) | Not started | A stand-in for libsd keeps sound drivers running (transfers, interrupts, voice status), but there is no SPU2: nothing is mixed and nothing is heard. |
| Discs | Working | ISO images or directories, through libcdvd and the IOP's cdvdman. Missing: dual-layer DVDs, CD audio, raw sector reads. |
| Memory cards | Working | A directory per card, behind MCSERV and XMCSERV. |
| Window and controllers | Working | An SDL2 window (4:3, deinterlaced, fullscreen); a DualShock 2 per port from the keyboard or game controllers, with PCSX2's bindings, settings pages and vibration. Missing: multitap, other controller types. |
| Widescreen, higher resolutions | Not started | See the [roadmap](#roadmap). |

**Done**: complete and tested. **Working**: does its job in the tests and in
the one game tried so far; other games will find gaps. **Partial**: large
pieces missing. [docs/runtime.md](docs/runtime.md#what-is-covered) details
every area.

<sub>The counts are made by hand, last on 2026-10-28. Syscalls are counted
against the numbers ps2sdk's `syscallnr.h` names (reserved ones left out), and
IOP functions against the export tables the project knows
([irx_exports.inc](core/src/irx_exports.inc), plus those the runtime
declares), not every function of every Sony module. Calls the runtime accepts
with no effect count as implemented.</sub>

**Speed.** Recompiled code is fast: a tight integer loop runs at about 1.8
billion guest instructions per second on an Apple M3, about 10 times the
interpreter and well above the EE's 294.9 MHz. Whole games are held back by
the rest of the emulation: in Agent Under Fire the intro movies run at 98% of
full speed and the title screen at about 75%, most likely limited by the
software GS.

**Platforms.** Developed and tested on Windows x64, with clang-cl and MSVC.
The project started on macOS (Apple silicon), which has not been rebuilt
since the move to Windows. Linux is untested.

## Roadmap

Roughly in order:

1. **Agent Under Fire in game**: start a mission, recompile the driving
   missions' executable, and fix what the game reaches next.
2. **Sound**: emulate the SPU2 (ADPCM voices, envelopes, reverb, the two cores
   and their streamed input) and play the result through SDL.
3. **Graphics speed**: the rasterizer decodes the drawing state for every
   pixel and draws on the game's thread. A faster software renderer first,
   then a GPU renderer, which higher resolutions also need.
4. **More games**: FILEIO's asynchronous protocol, `mcman`, event flags,
   `ExecPS2`, dual-layer discs, code overlays, and a second game to bring up.
5. **Vector units**: run micro programs alongside the EE, with their timing,
   and recompile them like EE code.
6. **Widescreen and higher resolutions**, the way PCSX2 does them:
   - **16:9.** PS2 games compute their own projection, so there is no general
     switch. PCSX2 applies per-game widescreen patches (`.pnach` files) that
     change each game's own field of view, and shows the picture at 16:9. A
     recompiled game keeps every guest address, so those patches carry over.
     Patches to code become `[[patches]]`, applied at recompile time because
     the code is compiled ahead of time; patches to data are written into RAM
     at start-up or every vsync, as PCSX2 writes them. The window gets
     PCSX2's aspect ratio setting (4:3, 16:9, stretch). The same path brings
     PCSX2's no-interlacing patches, for games that draw fields.
   - **Higher internal resolution.** Drawing at a multiple of the PS2's
     resolution needs the GPU renderer: the software rasterizer is already
     below full speed at the original resolution, and its work grows with the
     square of the scale (PCSX2's software renderer does not upscale either).
     Upscaling also brings the problems PCSX2 handles with per-game fixes:
     half-pixel offsets, seams between sprites, and effects that read the
     picture back at its original size.

## Quick start

Requirements: CMake 3.20+, a C++20 compiler, and Ninja or Make; on Windows,
the Visual Studio 2026 Build Tools. SDL2 is optional: it adds the window and
keyboard and game controller input
([docs/runtime.md](docs/runtime.md#running-a-recompiled-game) names the
Windows package). Without it, frames can be dumped to files, which is what the
tests check.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                 # unit tests + end-to-end programs

# The example: assembled, recompiled and compiled as part of the build.
./build/examples/hello/hello --elf build/examples/hello/hello.elf
```

On Windows, configure from a Visual Studio developer prompt ("x64 Native Tools
Command Prompt" or "Developer PowerShell"). clang-cl is the recommended
compiler; plain MSVC (`cl`) works too.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
```

The example prints:

```
Hello from a recompiled PS2 program!
loop: sum(1..100) = 5050
recursion: fib(20) = 6765
switch: 0 -> zero
...
longjmp: setjmp returned 42
thread: started worker
thread: worker running, arg=7
thread: main woke up
...
vsync: counted 3 vblank interrupts
done
```

[examples/hello](examples/hello) shows the whole path from source to native
binary. Its program ([hello.s](examples/hello/hello.s)) starts like a real PS2
executable (SetupThread, SetupHeap, main, Exit) and exercises loops,
recursion, a switch jump table, function pointers, MMI, the FPU, VU0, a
non-local return, kernel threads and a vblank interrupt handler.

## Porting a game

1. **Look at the ELF.**
   `ps2n analyze SLUS_123.45 --write-config game.toml` prints the
   code ranges and what the analysis finds, and writes a starter config.
2. **(Recommended for stripped games) export Ghidra's function map.** Run
   [tools/ghidra/ExportPs2nFunctions.java](tools/ghidra/ExportPs2nFunctions.java)
   in Ghidra and set `[input] ghidra_map` to the CSV. Ghidra's boundaries take
   priority over the recompiler's heuristics. CSVs from PS2Recomp's exporter
   and Ghidra's own Functions / Symbol Table exports also work.
3. **Recompile.** `ps2n recompile game.toml` writes the C++, a
   `CMakeLists.txt` and `report.txt` (functions, stubs, unresolved indirect
   jumps, warnings). If the game is several executables (the boot ELF starts
   the game proper with `LoadExecPS2`, or loads it and jumps in), list the
   others in `[input] programs`
   ([docs/config.md](docs/config.md#games-with-several-executables)).
4. **Build and run.** `cmake -S out/game -B out/game/build && cmake --build out/game/build`,
   then `out/game/build/game --elf SLUS_123.45 --cdrom <disc image or directory>`
   (add `--region europe` for a PAL game).
5. **Iterate.** The runtime prints a summary of every unimplemented syscall,
   hardware register, IOP call and interpreted address when it exits (Ctrl+C
   works too). From there:
   - addresses it had to interpret are written to `ps2n_missing.toml`; add
     that file to `[analysis] entries_files` and recompile;
   - replace library functions with host stubs (`[stubs] bind`), or with your
     own C++ (`[functions] override`), or stub out functions that block
     progress (`[functions] skip`, or `bind = { "0x..." = "ret0" }`);
   - implement the missing syscalls, registers and IOP services in the runtime
     (see [docs/runtime.md](docs/runtime.md)).

Run the same build with `--interpret` to execute everything through the
interpreter instead (functions the config replaced stay replaced); comparing
the two runs is a quick way to tell a recompiler bug from a runtime gap.

## Command line

```
ps2n recompile <config.toml> [--elf PATH] [--program PATH]... [--out DIR] [--name NAME]
               [--files N | --single] [--no-project] [--no-comments] [--quiet]
ps2n analyze <game.elf | config.toml> [--map CSV]... [--list] [--write-config FILE]
ps2n disasm <file.elf> [--function NAME|ADDR] [--range START:END] [--cpp NAME|ADDR]
ps2n irx <module.irx>          # IOP module name, version, imports, exports
ps2n assemble <in.s> -o <out.elf> [--base ADDR] [--strip]
ps2n stubs                     # runtime stub handlers available for binding
```

`disasm --cpp NAME` prints the C++ the recompiler would generate for one
function, which is the quickest way to check a translation.

The recompiled game takes runtime options; `--help` lists them and
[docs/runtime.md](docs/runtime.md#running-a-recompiled-game) describes them.
The common ones are `--cdrom` (an ISO image or a directory), `--region europe`
for PAL games, `--memcards DIR`, `--interpret`, `--strict`, `--virtual-clock`
(reproducible runs), `--pad-script FILE`, `--dump-frames DIR`,
`--trace-syscalls` and `--log-level`.

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
```

Every key is documented in [docs/config.md](docs/config.md).

## How it works

- **Decoding** is driven by one table ([r5900_ops.inc](core/include/ps2n/r5900_ops.inc))
  shared by the decoder, the disassembler and the assembler; a second table
  ([r5900_sem.inc](core/include/ps2n/r5900_sem.inc)) maps each instruction to
  the runtime function that implements it. The recompiler turns those rows into
  source text, the interpreter into calls, so both execute identical semantics.
- **Instruction semantics** live in the runtime headers
  ([ops_int.h](runtime/include/ps2/ops_int.h), [ops_mmi.h](runtime/include/ps2/ops_mmi.h),
  [ops_fpu.h](runtime/include/ps2/ops_fpu.h), [ops_vu0.h](runtime/include/ps2/ops_vu0.h)),
  not in generated code. Fixing an instruction means rebuilding, not
  re-recompiling.
- **Control flow** becomes C++ control flow: labels and `goto` for branches,
  direct calls for JAL, `switch` for jump tables, `return` for `jr $ra`.
  Every call site checks that the callee returned where it should; when it did
  not (a thread switch, longjmp, exit), the caller returns too and the
  dispatcher resumes the right code later through the function's resume
  switch. Because host stack frames never hold guest state, any number of
  guest threads run on one host thread.
- **The runtime** loads the ELF's data at startup (the generated code embeds no
  game data), runs the program from its entry point, and services interrupts,
  timers and thread switches at loop back-edges and syscalls.

The full model, including why it was chosen, is in
[docs/architecture.md](docs/architecture.md).

## Design decisions, and where this differs from PS2Recomp

[PS2Recomp](https://github.com/ran-j/PS2Recomp) was the reference for scope and
for which PS2 details matter. This is an independent implementation; where it
takes a different route, these are the reasons:

| Area | PS2Recomp | Here | Why |
|---|---|---|---|
| Calls | calls to other functions go through `runtime->dispatchGuestBranch` (a lookup per call); the scheduler unwinds with a C++ exception (`EeDispatcherTransfer`) | direct C++ calls, a return-address check after each call, resume switches | the compiler can inline and optimise calls; one mechanism covers thread switches, longjmp and odd returns; no exceptions on hot paths |
| CPU context | `ctx` pointer passed to every function | one global `ps2::cpu`, guest RAM a separate global array | the host compiler can prove guest stores never alias guest registers, so register values stay in host registers |
| Semantics | emitted as code per instruction | shared inline functions | readable output; one place to fix a bug; shared with the interpreter |
| SIMD | SSE intrinsics, sse2neon on ARM | portable lane loops (auto-vectorised) | builds as-is on ARM64 (Apple silicon) and x86-64 |
| Missed code | a function-not-found error | a built-in interpreter, plus a list of the addresses for the next recompile | a missed entry point costs speed, not a crash |
| IOP | runs IRX modules on an R3000 interpreter | also runs IRX modules on an R3000 interpreter, but the IOP kernel and Sony's system modules (CD/DVD, memory cards, pads, libsd) are host code speaking their RPC protocols | game-specific modules behave as written, while the system side needs no BIOS image and no SIO2, CDVD or SPU2 register emulation |
| Dependencies | ELFIO, toml11, fmt, libdwarf, rabbitizer (fetched at configure time) | toml++ and Dear ImGui (vendored); SDL2 optional, for the window | offline, reproducible builds |
| Output size | `ctx->pc = ...` before every instruction | pc stored only at control transfers | smaller, faster code |

Also worth knowing:

- **Generated code is a build artifact.** `ps2n_add_recompiled_game()`
  ([cmake/Ps2nRecompile.cmake](cmake/Ps2nRecompile.cmake)) recompiles at build
  time; the recompiler only rewrites files whose content changed, so
  incremental builds stay incremental.
- **Fail loudly, keep running.** Unimplemented paths are logged the first few
  times, counted, and summarised at exit; `--strict` stops at the first one.
- **Patches check what they replace** (`expect`), so a config written for one
  release of a game cannot silently corrupt another.

## Project layout

```
core/         ELF and IRX readers, R5900 decoder / disassembler / assembler (ps2n_core)
recompiler/   analysis, code generation, config, CLI (ps2n_recomp, ps2n)
runtime/      what recompiled games link against (ps2n_runtime)
  include/ps2/  generated-code API: cpu.h, memory.h, ops_*.h, recomp.h, runtime.h
  src/          dispatcher, interpreter, VU, kernel/ (EE syscalls), hw/ (DMAC, GS, VIF, IPU),
                iop/ (IOP CPU, kernel, services), stubs/, video/, input/, ui/
tools/ghidra/ function-map exporter for Ghidra
examples/     the end-to-end example
tests/        unit tests and end-to-end programs
docs/         the documentation below
third_party/  toml++ (MIT), Dear ImGui 1.92.9b (MIT)
```

## Documentation

- [docs/architecture.md](docs/architecture.md): the execution model, and why it
  was chosen.
- [docs/runtime.md](docs/runtime.md): running and debugging a game, what the
  runtime covers, and where to add what is missing.
- [docs/config.md](docs/config.md): every key of a game's config.
- [docs/input.md](docs/input.md): controllers, the settings file and hotkeys.

## Testing

```sh
ctest --test-dir build --output-on-failure
```

| Suite | What it covers |
|---|---|
| `core` | Every table row decodes to itself, 10,000+ random instructions round-trip through the disassembler and assembler, known encodings, ELF and IRX parsing. |
| `recompiler` | Config parsing and errors, function map formats, jump tables, stripped-ELF discovery, bindings, patches, code generation. |
| `runtime` | Instruction semantics against independently computed results (sign extension, R5900 division, unaligned access, MMI lanes, FPU clamping, VU0 flags), guest printf, the GS (swizzling, depth, fields, both circuits, deinterlacing), the IPU's commands, FIFOs and DMA (against a reference IDCT), scratchpad DMA, SIF RPC end to end, IOP resets and services, discs, recognising programs in RAM, `--strict`, controller input and settings. |
| `program_*` | Assembly programs recompiled at build time and run twice, recompiled and interpreted, against recorded output or a recorded frame: control-flow edge cases, a stripped ELF that needs the interpreter, the kernel scheduler, `printf` over the DECI2 TTY, config bindings, `LoadExecPS2` across three executables, a loader that jumps into the next program. |
| `sdk_*` | Programs built with ps2sdk, run the same two ways: an IRX with its own RPC server, libsd, libpad and libpadx, VU0 and VU1 micro programs, libcdvd, libmc. The binaries are checked in under `tests/sdk/bin` ([tests/sdk/README.md](tests/sdk/README.md)). |

## Legal

The recompiler and runtime contain no Sony code or data, and generated code
contains no game data: the runtime loads it from your ELF at startup. Use
executables you are entitled to use. The test programs in `tests/sdk/bin`
are linked with ps2sdk (Academic Free License 2.0).

## Acknowledgements

[toml++](https://github.com/marzer/tomlplusplus) (MIT) parses configs, and
[Dear ImGui](https://github.com/ocornut/imgui) (MIT) draws the settings in
the window. Controller input follows [PCSX2](https://github.com/PCSX2/pcsx2)'s
design: its settings format, defaults and settings pages, written anew.
Instruction encodings were checked against
[rabbitizer](https://github.com/Decompollaborate/rabbitizer)'s tables, edge-case
semantics and the GS memory layout against [PCSX2](https://github.com/PCSX2/pcsx2),
and kernel and SIF structures follow [ps2sdk](https://github.com/ps2dev/ps2sdk)'s
headers. [Play!](https://github.com/jpd002/Play-) was a reference for IOP
service protocols and a cross-check for the IPU's code tables.
[AnyPS5](https://github.com/boykopovar/AnyPS5) gave two ideas: replacing
library functions by name with native code, and logging each unimplemented
path a few times with a summary at exit. [PS2Recomp](https://github.com/ran-j/PS2Recomp)
and [N64Recomp](https://github.com/N64Recomp/N64Recomp) showed the way.
