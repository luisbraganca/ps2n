# ps2-uncompiler

A static recompiler for PlayStation 2 executables. It reads an Emotion Engine
(R5900) ELF, finds its functions, translates every instruction into plain C++,
and links the result against a runtime that stands in for the rest of the
console: the EE kernel, the hardware registers, the GS, and the IOP. The output
is an ordinary native program you build with CMake.

```
game.elf ──► ps2-uncompiler recompile ──► C++ (one statement per instruction)
                                              │
                         ps2uc runtime ───────┤  kernel, memory map, DMAC/GIF/GS,
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

The recompiler covers the whole Emotion Engine instruction set, and the
runtime runs this repository's test programs and ps2sdk homebrew. No retail
game is playable yet. In the first one being brought up, 007: Agent Under
Fire, the boot loader loads the game's IOP modules and starts the movie
player, which runs recompiled.

| Area | Progress | Count |
|---|---|--:|
| EE instructions: R5900, MMI, COP0, FPU, VU0 macro mode | `██████████` 100% | 359 / 359 |
| VU micro instructions, VU0 and VU1 | `██████████` 100% | all |
| EE kernel syscalls* | `█████████░` 88% | 127 / 145 |
| IOP system library functions* | `████████░░` 77% | 428 / 557 |

<sub>* Of the syscalls ps2sdk's `syscallnr.h` names (reserved numbers left
out), and of the functions the project knows for 26 IOP system libraries
(ps2sdk's export tables in [irx_exports.inc](core/src/irx_exports.inc), plus
those the runtime declares), not of every function in every Sony module.
Calls the runtime accepts with no effect, where no emulated hardware stands
behind them, count as implemented. Counted on 2026-10-28;
[docs/runtime.md](docs/runtime.md#what-is-covered) describes what each area
covers.</sub>

What works, and is covered by the test suite (`ctest`):

- **The whole R5900 instruction set**: the MIPS III base the EE implements, the
  R5900 extensions (three-operand MULT/MADD, pipeline-1 HI1/LO1, SA register),
  all 128-bit MMI instructions, COP0, the single-precision FPU with the EE's
  non-IEEE behaviour (no infinities or NaNs, clamping, flush-to-zero), and VU0
  macro mode with MAC/status/clip flags. Encodings were cross-checked against
  rabbitizer's R5900 tables; corner cases (divide by zero, LWL/LWR merging,
  MMI lane layouts, VU flag updates) against PCSX2's interpreter.
- **Analysis**: function starts from ELF symbols, Ghidra maps, the entry point,
  JAL targets, data pointers, lui/addiu code pointers and prologues; switch
  jump tables; delay slots (including jumps into them), branch-likely, tail
  calls, fall-through between functions.
- **Execution model** that keeps guest threads, interrupts, setjmp/longjmp and
  tail calls correct while the generated code stays plain direct calls (see
  [docs/architecture.md](docs/architecture.md)).
- **Runtime**: an HLE EE kernel (threads, semaphores, interrupt and DMA
  handlers, alarms; over 100 syscall numbers including the interrupt-context
  `i*` variants), INTC, timers, DMAC (normal and chain mode), GIF and a
  software GS rasterizer with frame output, VIF0/VIF1 (every UNPACK format and
  mode, MPG, MSCAL with the TOP/TOPS double buffer, DIRECT), VU0 and VU1 micro
  programs on an interpreter that models flag and Q/P timing, XGKICK, and SIO
  debug output.
- **IOP**: IRX modules run on an R3000A interpreter, over a host-code IOP
  kernel (threads, semaphores, event flags, message boxes, memory pools,
  alarms, interrupts, vblank) and system libraries. The SIF wire protocol
  connects the game's EE libraries to RPC servers in modules or on the host:
  files on host directories, the IOP heap, module loading, the CD/DVD drive
  over an ISO image or a directory (libcdvd: search, reads, streaming), memory
  cards as host directories (MCSERV and XMCSERV), controllers (PADMAN, a
  DualShock 2 driven by the keyboard, game controllers or a script), and
  libsd for sound drivers (silent).
- **Games with several executables**: the boot ELF and the executables it
  starts, with `LoadExecPS2` or by loading them itself and jumping in, are
  recompiled into one native program, which recognises each one as it
  starts; an executable that was not recompiled runs on the interpreter.
- **Fallbacks** at every level: an interpreter for code the analysis missed
  (it records the addresses so the next recompile picks them up), logged and
  counted wrappers for unimplemented syscalls, registers and IOP calls, and a
  summary of everything unimplemented at exit.

Speed: recompiled code runs a tight integer loop at about 1.8 billion guest
instructions per second on an Apple M3 (Release build), roughly 10× the
interpreter and well above the EE's 294.9 MHz.

What is not there yet, roughly in order of importance for real games:

- **Retail games**: everything so far was validated with programs written for
  this repository and with ps2sdk homebrew (`tests/sdk`). Commercial games use
  Sony's libraries, whose protocols the services follow, but expect gaps in
  the less common RPC calls and IOP library functions.
- **Sound**: libsd runs without an SPU2, so drivers work and nothing is heard.
  SPU2 emulation (ADPCM voices, envelopes, reverb, mixing) and an audio output
  are the next big step.
- IPU (MPEG video), GS texture filtering and mipmaps, local-to-host GS
  transfers, TLB remapping, dual-layer DVDs, the multitap, network and hard
  disk.
- **VU speed**: micro programs are interpreted and finish the moment they
  start. A VU recompiler on the same model would make VU-heavy games faster.

## Quick start

Requirements: CMake 3.20+, a C++20 compiler, Ninja or Make. Tested with Apple
clang 21 on arm64, and on Windows x64 with clang-cl 22 and MSVC 19.51 (Visual
Studio 2026 Build Tools). SDL2 is optional: it adds a window, which so far has
only been compile-checked against the SDL2 2.30 headers, not run. Without it,
frames can be dumped to files, which is what the tests check.

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
   `ps2-uncompiler analyze SLUS_123.45 --write-config game.toml` prints the
   code ranges and what the analysis finds, and writes a starter config.
2. **(Recommended for stripped games) export Ghidra's function map.** Run
   [tools/ghidra/ExportPs2ucFunctions.java](tools/ghidra/ExportPs2ucFunctions.java)
   in Ghidra and set `[input] ghidra_map` to the CSV. Ghidra's boundaries take
   priority over the recompiler's heuristics. CSVs from PS2Recomp's exporter
   and Ghidra's own Functions / Symbol Table exports also work.
3. **Recompile.** `ps2-uncompiler recompile game.toml` writes the C++, a
   `CMakeLists.txt` and `report.txt` (functions, stubs, unresolved indirect
   jumps, warnings). If the game is several executables (the boot ELF starts
   the game proper with `LoadExecPS2`), list the others in `[input] programs`
   ([docs/config.md](docs/config.md#games-with-several-executables)).
4. **Build and run.** `cmake -S out/game -B out/game/build && cmake --build out/game/build`,
   then `out/game/build/game --elf SLUS_123.45 --cdrom <disc image or directory>`.
5. **Iterate.** The runtime prints a summary of every unimplemented syscall,
   hardware register, IOP call and interpreted address when it exits (Ctrl+C
   works too). From there:
   - addresses it had to interpret are written to `ps2uc_missing.toml`; add
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
ps2-uncompiler recompile <config.toml> [--elf PATH] [--program PATH]... [--out DIR] [--name NAME]
                         [--files N | --single] [--no-project] [--no-comments] [--quiet]
ps2-uncompiler analyze <game.elf | config.toml> [--map CSV]... [--list] [--write-config FILE]
ps2-uncompiler disasm <file.elf> [--function NAME|ADDR] [--range START:END] [--cpp NAME|ADDR]
ps2-uncompiler irx <module.irx>          # IOP module name, version, imports, exports
ps2-uncompiler assemble <in.s> -o <out.elf> [--base ADDR] [--strip]
ps2-uncompiler stubs                     # runtime stub handlers available for binding
```

`disasm --cpp NAME` prints the C++ the recompiler would generate for one
function, which is the quickest way to check a translation.

The recompiled executable takes runtime options (`--help` lists them): `--elf`,
`--host0`, `--cdrom`, `--memcards`, `--interpret`, `--virtual-clock`
(deterministic time), `--no-throttle`, `--max-vsyncs N`,
`--dump-frames DIR`, `--no-window`, `--strict`, `--trace-syscalls`,
`--trace-functions`, `--log-level`, `--missing-out`, `--pad-script`,
`--region`. [docs/runtime.md](docs/runtime.md) describes them.

## Configuration

```toml
[input]
elf = "SLUS_123.45"
ghidra_map = "functions.csv"          # optional
programs = ["MAIN.ELF"]               # optional: executables it starts with LoadExecPS2

[output]
dir = "out/game"
mode = "multi"                        # or "single"
files = 16                            # fixed file count (handy for build systems)

[analysis]
entries_files = ["ps2uc_missing.toml"]

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

- **Decoding** is driven by one table ([r5900_ops.inc](core/include/ps2uc/r5900_ops.inc))
  shared by the decoder, the disassembler and the assembler; a second table
  ([r5900_sem.inc](core/include/ps2uc/r5900_sem.inc)) maps each instruction to
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
| Dependencies | ELFIO, toml11, fmt, libdwarf, rabbitizer (fetched at configure time) | toml++ (vendored, single header) | offline, reproducible builds |
| Output size | `ctx->pc = ...` before every instruction | pc stored only at control transfers | smaller, faster code |

Also worth knowing:

- **Generated code is a build artifact.** `ps2uc_add_recompiled_game()`
  ([cmake/Ps2ucRecompile.cmake](cmake/Ps2ucRecompile.cmake)) recompiles at build
  time; the recompiler only rewrites files whose content changed, so
  incremental builds stay incremental.
- **Fail loudly, keep running.** Unimplemented paths are logged the first few
  times, counted, and summarised at exit; `--strict` stops at the first one.
- **Patches check what they replace** (`expect`), so a config written for one
  release of a game cannot silently corrupt another.

[AnyPS5](https://github.com/boykopovar/AnyPS5) was also suggested as a
reference. The PS5 runs x86-64 natively, so AnyPS5 is a relinker (it converts
the executable format and swaps in native system libraries) and does no
instruction translation; its core technique does not carry over to MIPS. Two
ideas did: replacing library functions by symbol name with native
implementations, and logging each unimplemented path a few times plus an exit
summary.

## Project layout

```
core/         ELF and IRX readers, R5900 decoder / disassembler / assembler (ps2uc_core)
recompiler/   analysis, code generation, config, CLI (ps2uc_recomp, ps2-uncompiler)
runtime/      what recompiled games link against (ps2uc_runtime)
  include/ps2/  generated-code API: cpu.h, memory.h, ops_*.h, recomp.h, runtime.h
  src/          dispatcher, interpreter, VU, kernel/, hw/ (DMAC, GS, VIF), iop/ (IOP CPU, kernel, services), stubs/, video/
tools/ghidra/ function-map exporter for Ghidra
examples/     the end-to-end example
tests/        unit tests and end-to-end programs (assembled or built with ps2sdk, recompiled, run twice)
docs/         architecture, config reference, runtime coverage
third_party/  toml++ (MIT)
```

## Testing

```sh
ctest --test-dir build --output-on-failure
```

- `core`: every table row decodes to itself, 10,000+ random instructions
  round-trip through disassembler and assembler, known encodings, ELF and IRX
  parsing.
- `recompiler`: config parsing and errors, function map formats, jump tables,
  stripped-ELF discovery, bindings, patches, code generation.
- `runtime`: instruction semantics with independently computed expectations
  (sign extension, R5900 division, unaligned access at every alignment, MMI
  lane layouts, FPU clamping, VU0 flags), guest printf, GS swizzling, the
  SIF RPC protocol end to end (bind, FILEIO open/read of a host file,
  LOADFILE), the disc layer (a directory and the ISO image built from it, the
  ROMVER file), and `--strict`.
- `program_*` / `example_*`: assembled programs recompiled at build time and
  run in both recompiled and interpreted mode, compared against recorded
  output: control-flow edge cases, a stripped ELF that needs the interpreter
  fallback, the kernel scheduler, config bindings (skip, stub, C++ override,
  instruction patch), `LoadExecPS2` across three executables at the same
  addresses (one of them not recompiled), a loader that copies the next
  program into memory and jumps into it, and a GS drawing compared against a
  recorded frame.
- `sdk_*`: programs built with the ps2dev toolchain and ps2sdk, run the same
  two ways: an IRX module with its own RPC server and kernel objects, libsd
  driven from an IRX, libpad and libpadx with scripted input, VU0 and VU1
  micro programs (flags, timing, branches, VIF UNPACK modes, XGKICK, checked
  against a recorded frame), libcdvd on a disc built from a directory, and
  libmc on an empty card. The binaries are checked in under `tests/sdk/bin`;
  `make -C tests/sdk` rebuilds them (see [tests/sdk/README.md](tests/sdk/README.md)).

## Legal

The recompiler and runtime contain no Sony code or data, and generated code
contains no game data: the runtime loads it from your ELF at startup. Use
executables you are entitled to use. The test programs in `tests/sdk/bin`
are linked with ps2sdk (Academic Free License 2.0).

## Acknowledgements

[toml++](https://github.com/marzer/tomlplusplus) (MIT) parses configs.
Instruction encodings were checked against
[rabbitizer](https://github.com/Decompollaborate/rabbitizer)'s tables, edge-case
semantics and the GS memory layout against [PCSX2](https://github.com/PCSX2/pcsx2),
and kernel and SIF structures follow [ps2sdk](https://github.com/ps2dev/ps2sdk)'s
headers. [PS2Recomp](https://github.com/ran-j/PS2Recomp) and
[N64Recomp](https://github.com/N64Recomp/N64Recomp) showed the way.
