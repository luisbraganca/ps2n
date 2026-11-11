# Proof-of-concept log

What Phase 2 has done and measured, step by step. The steps and their pass
criteria are in [phase1-feasibility.md](phase1-feasibility.md), section 14.
Every number here came from a run on the date given; nothing is estimated.

| Step | State |
|---|---|
| P0 Baseline | **Done on Windows** (2026-11-10). Not run on Linux. |
| P1 Static EE | **Done on Windows** (2026-11-11), by the criterion the maintainer set in its place ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)): on no line of the 55 ps2autotests programs is the compiled run further from the console than stock PCSX2, and every difference from the console is listed with its cause. All 62 test programs run with their own code compiled ahead of time, identical to the interpreted run at every event test; 40 of the 55 match the console (stock PCSX2: 39 with its recompilers, 38 with its interpreters). Still on PCSX2's interpreter functions: COP0 and the VU0 macro instructions other than `CFC2` and `CTC2`. |
| P2 Hard problems | (a) done except CDVD: the kernel, DMA and interrupt programs of ps2autotests and the ps2sdk IOP RPC, pad and memory card programs print what stock PCSX2 prints. Two of the five ps2sdk programs print it at another time than under PCSX2's recompilers (one frame later, two frames earlier); both are explained. (b) done for all 62 test programs. (c) done. (d) done for the FPU in three clamp modes, for the multimedia instructions (2026-11-10) and for `CFC2` and `CTC2` (2026-11-11). |
| P3 Graphics path | **Done on Windows for the one program that draws** (2026-11-10): the frame of the ps2sdk VU program from PCSX2's software renderer is the same under stock PCSX2 and compiled; PCSX2's Vulkan renderer runs at 1x and 3x. |
| P4 Measurement | **Done on Windows** (2026-11-10): compiled code runs as fast as PCSX2's recompiler on six CPU-bound workloads (0.87 to 1.47 times its time), 8 to 23 times as fast as PCSX2's interpreter, and counts the recompiler's EE cycles on those six to within one cycle (the same on four). |
| P5 Linux | **Open, deferred by the maintainer** ([ADR 0008](adr/0008-linux-run-deferred.md)). Scripts written, never run. Nothing is known to work on Linux. |

## How to build and test

Windows (PowerShell; the scripts enter the Visual Studio developer shell
themselves):

```powershell
scripts\windows\build.ps1
scripts\windows\test.ps1 -Bios "$HOME\Documents\PCSX2\bios\SCPH-70004_BIOS_V12_PAL_200.BIN"
```

Linux, on the maintainer's machine (**never run so far**):

```sh
scripts/linux/build.sh
PS2N_BIOS=/path/to/bios.bin scripts/linux/test.sh
```

`test` accepts a filter, for example `-Filter ps2autotests/stock-jit/cpu` or
`-R ps2autotests/stock-jit/cpu`. Without a BIOS every test is reported as
skipped.

## P0 Baseline (Windows, 2026-11-10)

### Set-up

- **PCSX2** `v2.9.170` (`a886f8ef`) is the shallow submodule
  `third_party/pcsx2`, unmodified. **The patch queue is empty.**
- **Build integration without a patch.** PCSX2's CMake project must be the
  top-level one (it uses `CMAKE_SOURCE_DIR` throughout). It is configured as
  such, with `-DCMAKE_PROJECT_Pcsx2_INCLUDE=cmake/ps2n-inject.cmake`, which
  defers an `include()` of `host/targets.cmake` and `tests/tests.cmake` to the
  end of PCSX2's top-level `CMakeLists.txt`. CMake refuses `add_subdirectory`
  in a deferred call, hence `include` and absolute source paths.
- **Windows dependencies:** release `latest-windows-dependencies` of
  `PCSX2/pcsx2-windows-dependencies`, asset `pcsx2-windows-dependencies.7z`,
  165.0 MB, SHA-256
  `e1ec4fb9f201d22a64f3da3390a39e65089df7030c2c5951fe735180823667c5`, matching
  the digest GitHub publishes for it. It is a rolling release; this hash is
  the record of what was used. Extracted to `deps/windows-x64` (git-ignored).
  The package carries one license file for all of its libraries; auditing
  them is still open.
- **Toolchain:** Visual Studio Build Tools 2026, clang-cl 22.1.3, CMake
  4.3.1, Ninja 1.13.2, with the flags of PCSX2's own "CMake Clang" CI job
  minus the Qt interface and link-time optimization. A full build of the core
  is 747 steps and took 0.7 minutes on this machine.
- **Tests:** `third_party/ps2autotests` (`97469ffb`, ISC): 55 prebuilt EE
  programs with outputs recorded on a console. Its 23 IOP tests have recorded
  outputs but no prebuilt modules, so they cannot be run without a PS2
  toolchain.

### The host

`host/` builds `ps2n-host`, a console frontend for the core derived from
PCSX2's `pcsx2-gsrunner`. It boots an ELF through the BIOS with fast boot,
captures the guest's console output through the core's log callback, and stops
when the guest prints a marker, or at a frame or time limit. It has no window
(the GS device is surfaceless, the renderer null) and no sound, pad or memory
card. Its options select PCSX2's recompiler or interpreter for the EE, the IOP
and the VUs. `ps2n-host -help` lists them.

### Results

`ctest` runs each program under two stock configurations and compares the
text between `-- TEST BEGIN` and `-- TEST END` with the console recording.

| Configuration | Programs matching the console | Differ |
|---|--:|--:|
| `stock-jit`: PCSX2's recompilers | 39 of 55 | 16 |
| `stock-interp`: PCSX2's interpreters | 38 of 55 | 17 |

- The programs that differ are listed, with the reason seen, in
  [tests/ps2autotests-known-differences.cmake](../tests/ps2autotests-known-differences.cmake).
  They still run and are expected to fail, so a change either way is noticed.
  With those lists, all 110 tests pass, in 27 seconds with eight jobs.
- **PCSX2's two implementations disagree on five programs.** Only the
  recompiler is wrong about `LWR` and about VU integer flags; only the
  interpreter is wrong about `CFC1`, `MFSA` and one `PMADDW` case. This is the
  Phase 1 finding in measured form: the recompiler's semantics cannot be
  taken from the interpreter.
- **The recompiler crashes on one program.** `cpu/ee_fpu/branchdelay` puts an
  FPU branch in the delay slot of an FPU branch; `recBC1T` then recurses
  without end (`x86/iFPU.cpp:696`, `iR5900.cpp:1851`) until the stack
  overflows. "Same as stock PCSX2" cannot be the criterion there; the console
  recording is.
- **Runs repeat exactly.** Two runs of all 110 tests gave identical output
  for every program, and the same stop point (frame count and EE cycle count)
  for all 109 that reach one. Lockstep comparison in step P2 depends on this.

### What the core demanded of a host

Each of these cost a failed run, and each cause was confirmed in PCSX2's
source before the fix:

1. **The boot ELF's path must be absolute and resolved.** The core lets the
   BIOS loader open the ELF only if the path it asks for equals the stored
   one exactly (`host_path()`, `pcsx2/IopBios.cpp`). A relative path fails
   with "Cannot openfile" and the kernel then jumps to address 0.
2. **Absolute paths must use backslashes on Windows.** The core turns them
   into extended-length paths, which do not accept forward slashes
   (`FileSystem::GetWin32Path`, `common/FileSystem.cpp`). CMake passes forward
   slashes, so the host converts every path argument.
3. **The data directory must be set explicitly.** By default the core uses the
   user's own PCSX2 folder (`EmuFolders::SetDataDirectory`). The host sets a
   private one and refuses to run if the core chose another. **Do not run
   stock `pcsx2-gsrunner` on this machine**: it would write into
   `Documents\PCSX2`.
4. **The core keeps the console's NVRAM beside the BIOS image**
   (`cdvdGetNVRAMPath`, `pcsx2/CDVD/CDVD.cpp`). Each test therefore gives the
   core a link to the image in its own scratch directory (a hard link on
   Windows, a symbolic link elsewhere), removed afterwards. Every run starts
   from the same fresh NVRAM, and nothing is written beside the user's BIOS.
   The image is never copied.
5. **Guest console output is only forwarded while one of the core's own log
   sinks is on** (`VMManager::UpdateLoggingSettings`). The host always writes
   the core's log file for that reason.

### Not covered by P0

- Linux: `scripts/linux/` was written from PCSX2's CI recipe and has never
  been run.
- The IOP tests of ps2autotests (no prebuilt modules).
- The old project's ps2sdk programs (pad, memory card, CDVD, IOP RPC, VU):
  step P2 and P3.
- Frame hashes and any renderer but the null one: step P3.

## P1 Static EE

The plan is to get there in increments that each leave a working, tested
provider:

1. A provider that owns the loop, control flow, timing and event tests, with
   every other instruction delegated to PCSX2's interpreter functions. **Done.**
2. PS2Native's own instruction semantics, one family at a time, each checked
   against PCSX2's function for the same instruction before it replaces it.
3. The recompiler from the old project emitting C++ that calls those
   semantics; a dispatcher with resume points; the provider's loop becomes the
   fallback interpreter for code that was not compiled.

### Increment 1: the provider (Windows, 2026-11-10)

`runtime/ee_provider.cpp` is an `R5900cpu` of our own. The host installs it
with `-ee ps2n` by assigning PCSX2's `Cpu` after `VMManager::Initialize()`.
**No PCSX2 source is changed; the patch queue is still empty.** The core only
re-selects its provider when a recompiler is switched on or off
(`VMManager::CheckForCPUConfigChanges`), which a fixed configuration never
does, and the host checks every frame that `Cpu` is still ours.

What the provider does itself: fetch, every branch and jump with its delay
slot, cycle accounting (the interpreter's arithmetic), event tests at each
branch once the cycle count has reached the next event (the recompiler's
rule), the fast-boot hooks, exit and instruction cancel. What it delegates:
every non-branch instruction, by calling PCSX2's interpreter function through
PCSX2's opcode table. The host prints the split: about 37.2 million EE
instructions per test program, 85.8% of them delegated, about 27,000 event
tests.

Test configuration `ps2n-ee`: this provider for the EE, PCSX2's recompilers
for the IOP and the VUs (the stand-ins of ADR 0003).

| | Result |
|---|---|
| Programs matching the console | 38 of 55, the same 38 as `stock-interp` |
| Output identical to `stock-interp` | 54 of 55 programs, byte for byte |
| The exception | `vu/lower/efu`, identical to `stock-jit`: it depends on the VU provider, which is PCSX2's recompiler here |
| Whole suite | 165 tests (three configurations) pass against the recorded lists in 44 seconds |

What increment 1 established, on a real BIOS kernel: a provider outside PCSX2
can drive the core's scheduler correctly through boot, interrupts, thread
switches, syscalls, SIF, DMA and VIF (`kernel/ee`, `dma`, `gs` and `vu`
programs all behaved as under PCSX2's own interpreter), and it can be
installed without a patch.

What the three configurations showed about semantics at that point, when
`ps2n-ee` still used PCSX2's interpreter function for every instruction:

| Program | Console | `stock-jit` | `stock-interp` and `ps2n-ee` |
|---|---|---|---|
| `cpu/ee/lsu` (`LWR`) | reference | differs | matches |
| `vu/lower/integer` | reference | differs | matches |
| `cpu/ee_fpu/transfer` (`CFC1`) | reference | matches | differs |
| `cpu/ee_simd/funnel` (`MFSA`) | reference | matches | differs |
| `cpu/ee_simd/muldiv` (`PMADDW`) | reference | matches | differs |

`vu/lower/integer` runs VU code on PCSX2's VU recompiler in both `stock-jit`
and `ps2n-ee`, yet differs only in `stock-jit`: the difference comes from the
EE recompiler's side of the test, not from the VU recompiler.

Taking the console-correct behavior wherever either PCSX2 implementation has
it would make 41 of the 55 programs match, more than either stock
configuration. Increment 4 got there. (Increment 5 gave one back, for a
reason that is not in the EE: 40 since 2026-11-11.)

### Increment 2a: the instruction decoder (Windows, 2026-11-10)

`core/` is the old project's decoder library, imported unchanged apart from
license identifiers: one instruction table (`r5900_ops.inc`) driving the
decoder, disassembler and assembler; the semantics table (`r5900_sem.inc`);
the ELF, IRX and SNR2 readers; the VU pair decoder and pipeline model. Its 15
unit tests pass in the new tree, with warnings as errors.

It builds as its own CMake project from the repository root, because it does
not depend on PCSX2 and uses C++ exceptions, which PCSX2's build switches
off. `scripts\windows\build.ps1` builds both projects and `test.ps1` runs
both sets of tests. Nothing in the provider uses the decoder yet.

### Increment 2b: integer semantics of our own (Windows, 2026-11-10)

`runtime/include/ps2n/ops_int.h` carries out 85 instructions itself: integer
arithmetic and logic, shifts, multiply and divide on both pipelines, HI/LO,
the shift-amount register, and the loads and stores of general registers.
Everything else is still *delegated* to PCSX2's interpreter function for the
instruction, and counted.

- **Rule applied** (it was "proposed" under Open design points): match
  PCSX2's recompiler; where a console recording shows the recompiler wrong and
  PCSX2's interpreter right, match the console.
- **`ps2n-host -selftest`** holds the semantics to that. It checks the
  decoder and the cycle table against PCSX2's opcode table for all 359 table
  rows and two million random words (0 disagreements), then runs each of the
  85 instructions on random and boundary operands against two oracles:
  PCSX2's interpreter function (337,764 cases) and PCSX2's recompiler,
  compiling one instruction at a time (34,000 cases). 0 differences outside
  the two documented ones: `MTSA` follows the recompiler and the console, not
  the interpreter; `LWR` follows the interpreter and the console, not the
  recompiler.
- The self-test was itself tested by planting four bugs (a logical shift in
  `SRA`, a cycle count off by one, and removing each of the two documented
  exclusions). It caught all four; the sources were restored.
- Delegation in the interpreted run fell from 85.8% of instructions to 4.2%.
  What is left, by count in `cpu/ee/alu`: `por`, `mfc0`, `swc1`, `lwc1`,
  `sync`, `mtc0`, `eret`, `syscall`, `mtc1`, `cache`, `ei`.
- `ps2n-ee` now matches the console on 39 of 55 programs: `cpu/ee_simd/funnel`
  (`MFSA`) joined.

### Increment 3: static recompilation (Windows, 2026-11-10)

**What exists.** `recompiler/` builds `ps2n-recomp`, which turns an EE
executable into one C++ file against `runtime/include/ps2n/recomp.h`. The
build runs it on the 55 ps2autotests programs and links the result into
`ps2n-host`; the provider picks the image whose CRC is that of the executable
being booted. [execution-model.md](execution-model.md) describes how compiled
code, the interpreter and the core work together.

The old project's analysis was ported (function discovery from `jal` targets,
data pointers, `lui`/`addiu` pairs, prologues, gaps; the test programs have no
symbols). Its code generator was not: the new one is written for the new
contract. Differences that matter:

- every block leader is an entry point, so an interrupt can return anywhere a
  block begins;
- time is counted per block, exactly as the interpreter counts it;
- nothing is high-level emulated: system calls go to the BIOS;
- function boundaries are no longer a correctness matter;
- each function is checked against the code in memory before it runs.

**Numbers** (all 55 programs together):

| | |
|---|--:|
| Functions | 15,681 |
| Instructions | 1,005,094 |
| Block leaders (entries) | 214,885 |
| Instructions delegated to PCSX2's interpreter functions | 25,365 (2.5%) |
| Functions that run on into the next (instructions compiled twice) | 471 (4,647) |
| Places left to the interpreter: unknown encodings, branches in delay slots | 6 |
| Likely branches left to the interpreter when taken (GCC's division check) | 1,887 |
| Generated C++ | 116 MB in 56 files |
| Time to compile and link it (clang-cl, Release, 20 threads) | 43 s |
| `ps2n-host.exe` with all 55 images | 54 MB |

**Results.**

| | |
|---|---|
| `ps2n-static`: programs matching the console | 39 of 55, the same 39 as `ps2n-ee` |
| Output of `ps2n-static` against `ps2n-ee` | identical for all 55 |
| `ps2autotests/lockstep/*`: compiled run against interpreted run, at every event test | identical for all 55 (138,123 event tests in `cpu/ee/alu`, for scale) |
| `ps2autotests/lockstep-no-stack/*`: the same with compiled frames abandoned at every event test | identical for all 6 programs tried; up to 36,849 unwindings in a run |
| Instructions interpreted inside the programs' own code, interpreted run | 129,921,356 |
| The same, compiled run | 15: the branches in delay slots of the two `branchdelay` programs |
| Block starts inside program code that had to be interpreted | 0 in all 55 |
| Functions whose code in memory differed from what was compiled | 0 in all 55 |
| Whole suite | 2 unit test programs (27 cases) and 282 tests pass in 101 s |

The programs' own code is 4.5% of the instructions executed in these runs
(2.87 thousand million in all); the rest is the BIOS: its boot and its idle
loop. The BIOS is interpreted. Translating it ahead of time is the next CPU
stage after the EE (ADR 0002).

**What the lockstep comparison found**, each fixed at its cause:

1. *The interpreter ended a block before the delay slot of a branch not
   taken*, as PCSX2's interpreter does; compiled code ended it after, as
   PCSX2's recompiler does. Runs parted after 23,784 event tests. The
   interpreter now follows the stated rule.
2. *A write to a protected code page forced an event test* at the end of the
   block. The interpreted run has no such event test, and an extra one is not
   neutral in general (the core runs the IOP in slices between event tests).
   Nothing is forced any more; see "Is the code in memory the code that was
   compiled?" in the execution model for what that means for a program that
   changes code it is running.

**Seen and not yet dealt with.**

- In `cpu/ee_fpu/arithmetic` and `cpu/ee_fpu/compare`, functions share pages
  with data the program writes. After 16 re-protections such a function is
  compared with memory on every entry: 119,040 comparisons in one run. It is
  correct and slow. PCSX2 has the same problem and solves it per block.
- The paths for code that changed in memory (state "invalid", capture,
  rebuild) do not run in these programs: none of them changes code. Step
  P2 (c) below has a program that does.
- Step P1's criterion is "same output as stock PCSX2". Against `stock-jit`,
  `ps2n-static` differs on 10 programs. On two it matches the console and
  PCSX2's recompiler does not (`cpu/ee/lsu`, `vu/lower/integer`). On
  `cpu/ee_fpu/transfer` and `cpu/ee_simd/muldiv` the recompiler matches the
  console and we do not. On six neither matches the console and the two
  differ from each other (`cpu/ee/branchdelay`, `cpu/ee_fpu/arithmetic`,
  `fcr`, `muldiv`, `sqrt`, `cpu/vu0_macro/transfer`). All but the first come
  from delegated instructions: they behave as PCSX2's interpreter does. Own
  semantics for the FPU, the multimedia instructions and COP2, checked against
  the recompiler, are what closes this.
- The percentage the host prints for "compiled code" includes idle time the
  core skips by itself: a read of `INTC_STAT` in a waiting loop moves the
  cycle count to the next event (`IntCHackCheck`, `pcsx2/HwRead.cpp`). The
  line now says so.

### Increment 4: the FPU and the multimedia instructions (Windows, 2026-11-10)

**FPU** ([ops_fpu.h](../runtime/include/ps2n/ops_fpu.h)). The 30 COP1
instructions, written to do what PCSX2's EE recompiler emits for them: the
same SSE operations in the same order under the same control register
(rounding toward zero, denormals as zero; to nearest for division and square
root), in its clamp modes None, Normal and Extra + Preserve Sign. Its fourth
mode, Full, is a separate recompiler and is not implemented: with it set, the
provider says so, runs no compiled code and leaves the FPU to PCSX2's
interpreter functions.

Two things had to be solved to compute under a control register the C++
compiler knows nothing about. It folds an operation on constants at compile
time, rounding to nearest; and it may move an operation across a change of the
control register. Every operand goes through an empty inline-assembly
statement that hides its value and fixes the point of the computation.

**Where the recompiler cannot be followed, because it has no single
behavior.** Its results depend on which host register it allocated:

- `MAX.S` and `MIN.S` of +0 and -0 return whichever operand it put second;
- in some paths it clamps an operand inside the host register that holds the
  guest register, so that the *next* instruction of the same block sees a
  changed operand (`cpu/ee_fpu/arithmetic` under `stock-jit` prints such a
  changed operand);
- with clamping off, which of two NaN operands comes out depends on the same.

`ABS.S`, `NEG.S`, `MAX.S` and `MIN.S` therefore operate on the bits, as the
console and PCSX2's interpreter do: on `cpu/ee_fpu/arithmetic` the recompiler
gets 48 of their 114 lines wrong and this gets none wrong.

**Multimedia instructions** ([ops_mmi.h](../runtime/include/ps2n/ops_mmi.h)):
the old project's 91 functions, ported to the core's registers. The self-test
did the review: they agree with both PCSX2 implementations except where those
two disagree with each other.

**Differential fuzzing (step P2 (d)).** `ps2n-host -selftest` now covers 202
instructions. A run with 20,000 cases per instruction, before the multimedia
instructions were added: 2,219,964 cases against the recompiler, 520,000 FPU
cases in each of the two other clamp modes, 18,888,406 against the
interpreter, **0 differences** outside the documented ones, in 46 s. The
routine run (400 cases per instruction, part of the test suite) with the
multimedia instructions: 80,776 cases against the recompiler, 733,835 against
the interpreter, 0 differences.

The fuzzer was tested: five bugs planted in the FPU at once (the guard-bit
masking of `ADD.S`, the rounding of `DIV.S`, the saturation boundary of
`CVT.W.S`, the operand clamp of `C.LE.S`, the result clamp of `MUL.S`) were
all reported, each in the instructions it affects.

**What the fuzzing found in PCSX2 `v2.9.170`** (not reported upstream: its
AI policy forbids it; listed here because PS2Native has to pick a side):

| Instruction | Finding | PS2Native follows |
|---|---|---|
| `DIV`, `DIV1` of two constants | Recompiler bug: the quotient is sign-extended over the remainder (`recDIVconst`, `x86/ix86-32/iR5900MultDiv.cpp`), so a negative quotient leaves -1 in HI | the interpreter |
| `PMADDUW` with `$zero` as an operand | Recompiler bug: it does not load the operands and multiplies whatever two host registers hold (`recPMADDUW`, `x86/iMMI.cpp`) | the interpreter |
| `PMADDW`, `PMSUBW` | The interpreter adds corrections to the high word that the console recording contradicts (`cpu/ee_simd/muldiv`); the recompiler computes the plain result | the recompiler |
| FPU arithmetic, comparisons, conversions, `CFC1` | The two differ by design (the interpreter is closer to the console on arithmetic, further on `CFC1` and `SQRT.S`) | the recompiler |
| `ABS.S`, `NEG.S`, `MAX.S`, `MIN.S` | See above | the console and the interpreter |

**Results for the test programs.**

| | |
|---|---|
| ps2autotests programs matching the console | **41 of 55** (`cpu/ee_fpu/transfer` and `cpu/ee_simd/muldiv` joined) |
| Output against `stock-jit` | identical for 49 of the 54 programs `stock-jit` completes (it crashes on `cpu/ee_fpu/branchdelay`) |
| The five that differ | `cpu/ee/lsu`, `vu/lower/integer`: this matches the console, `stock-jit` does not. `cpu/ee_fpu/arithmetic`: the four bit operations are the console's; every other line is `stock-jit`'s. `cpu/ee/branchdelay`, `cpu/vu0_macro/transfer`: neither matches the console |
| Instructions still delegated in the generated code | 3,562 of 1,005,094 (0.35%): COP0, the VU0 macro instructions, `CACHE`, `SYNC` |
| Whole suite | 2 unit test programs (30 cases) and 289 tests pass in 109 s |

### The criterion of P1

The assessment's criterion for this step was "same output as stock PCSX2, and
as the console wherever stock PCSX2 matches it". The second half held at the
level of whole programs: every program `stock-jit` gets right, this got
right. The first half could not hold as written:

- on three programs this was closer to the console than stock PCSX2, on
  purpose;
- PCSX2's recompiler does not have one output to equal: part of what it
  prints for `cpu/ee_fpu/arithmetic` comes from its register allocation;
- `cpu/ee/branchdelay` (a branch in a branch's delay slot) and
  `cpu/vu0_macro/transfer` (masks of the VU control registers) differ from
  the console in every configuration.

**Decided on 2026-11-10**
([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)): never
further from the console than stock PCSX2, and every remaining difference
listed with its cause.

When the maintainer was asked, they were told that the code already did
this. Checking it line by line the next day showed that it did not, by two
lines. Increment 5 is what that led to.

### Increment 5: `CFC2` and `CTC2`, and the criterion as a test (Windows, 2026-11-11)

**What was wrong.** In `cpu/vu0_macro/transfer`, stock PCSX2 prints the
console's value after `CTC2` to an integer register (`0000ffff`) and to the
status flag register (`00000fc0`). PS2Native printed `ffffffff` for both:
`CTC2` was carried out by PCSX2's interpreter function, which writes all 32
bits. Two lines on which stock PCSX2 was right and PS2Native wrong.

**What was done.** `CFC2` and `CTC2` are PS2Native's own
([ops_cop2.h](../runtime/include/ps2n/ops_cop2.h), `runtime/ops_cop2.cpp`).
PCSX2's two implementations differ in seven places; the console recording
decides three of them:

| Case | PCSX2's recompiler | PCSX2's interpreter | The console | PS2Native follows |
|---|---|---|---|---|
| `CTC2` to an integer register | writes 16 bits | writes 32 | 16 | the recompiler |
| `CTC2` to the status flags | writes the six sticky flags, keeps the six current ones, and rewrites the copies PCSX2's VU recompiler works on | writes 32 bits | sticky flags only | the recompiler |
| `CTC2` to `FBRST` | resets, then stores | stores, then resets | reads back 0 after all ones | the interpreter |
| `CTC2` to the clip flags | writes the register | writes the register and the copy its VU functions work on | no evidence | the interpreter, because the VU0 macro instructions still run on its functions |
| `CTC2` to `CMSAR1` | passes on 32 bits | passes on 16 | no evidence | the recompiler |
| `CFC2` of an integer register | reads 16 bits | reads 32 | no evidence | the recompiler |
| `CFC2` of `R` | 23 bits, zero-extended to 64 | 23 bits, leaving bits 32 to 63 of the general register | no evidence | the recompiler |

Where neither matches the console, both do the same and so does PS2Native
(the clip flags keep 32 bits where the console keeps 24, and so on). How VU0
is brought up to date before the transfer is unchanged: as PCSX2's
interpreter functions do it.

**Self-test.** It covers 204 instructions now. The routine run: 81,545 cases
against the recompiler, 739,544 against the interpreter. A run with 20,000
cases per instruction: 4,077,475 against the recompiler, 36,976,118 against
the interpreter, 520,000 FPU cases in each of the two other clamp modes,
**0 differences**, in 95 s. That is longer than the host's time limit of 60 s,
so the test driver now passes its `SECONDS` on for the self-test as well.

The self-test was tested again: seven bugs planted in the two functions, one
at a time, each rebuilt and run. All seven were reported, each in the
register it affects (an integer register written with 32 bits; the current
status flags dropped; the VU recompiler's copies laid out wrongly; the
interpreter's clip flag copy forgotten; `FBRST` stored after the resets; an
integer register read with its sign extended; `R` read with 32 bits). The
source was restored and compared with its hash.

**What it cost: `vu/lower/integer` no longer matches the console.** Its first
line now reads as under stock PCSX2. The cause was looked for before choosing
between the two behaviors of `CTC2`, and confirmed by an experiment:

- The program clears the status flags with `ctc2 $0, $16`, reads them back,
  runs a VU0 micro program and compares what the micro program saw with what
  it read. On the console the two are the same.
- The zero flag is set when the program starts (presumably left by the
  `vsub` instructions with which the boot clears VU0's registers). It is
  still set after that `CTC2`, in the recompiler and here as on the console:
  only sticky flags can be written.
- **PCSX2's core then starts the micro program without it.**
  `vu0DenormalizeMicroStatus` (`pcsx2/VU0micro.cpp`, PCSX2 `v2.9.170`)
  converts the status flags for PCSX2's VU recompiler when a micro program
  starts. Its comment says it is taken from `mVUallocSFLAGd`
  (`x86/microVU_Alloc.inl`), but it shifts right by 11 and by 14 where that
  function shifts left. The six current flags and four of the six sticky
  ones are lost.
- PCSX2's interpreter `CTC2` overwrites the whole register, the current
  flags included, so both sides read zero. It was hiding the core's bug, and
  PS2Native matched the console on this line for the wrong reason.
- The experiment: with `VCALLMS` starting VU0 micro programs through a copy
  of PCSX2's function that shifts left, and nothing else changed, the
  program matched the console again in the interpreted run, and the other
  nine programs that use the VUs still matched or differed as before. The
  copy was removed afterwards.

Not reported upstream (PCSX2's AI policy forbids it), and PCSX2 is not
patched: whether to carry a patch for it is the maintainer's to decide, see
"Open questions" at the end.

**Results**, the 55 ps2autotests programs:

| | Before | After |
|---|--:|--:|
| Lines stock PCSX2 has right and the compiled run has wrong | 2 | **0** |
| Lines the compiled run has right and stock PCSX2 has wrong | 54 | 53 |
| `cpu/vu0_macro/transfer`: lines that differ from the console (stock PCSX2: 8) | 9 | 7 |
| `vu/lower/integer`: lines that differ from the console (stock PCSX2: 1) | 0 | 1 |
| Programs matching the console (stock PCSX2: 39, with its interpreters 38) | 41 | 40 |
| Instructions delegated in the generated code, of 1,005,094 | 3,562 | 3,032 (0.30%) |

Against PCSX2's *interpreters* the picture is different, on purpose: they
have 67 lines right that the compiled run has wrong (50 of FPU arithmetic on
operands the host takes for infinities or NaNs, 17 that depend on PCSX2's VU
recompiler), and the compiled run has 58 right that they have wrong. The
FPU follows the recompiler because games are tested against it.

**The criterion is now a test.** `ps2autotests/never-further/<program>`
compares the compiled run with the stock run line by line against the
console recording ([tests/never_further.cmake](../tests/never_further.cmake)),
for all 55 programs; CTest runs the two it compares first. It fails when
stock PCSX2 has a line right that the compiled run has wrong, and when the
outputs cannot be compared line by line. What it finds today: 50 programs
print what stock PCSX2 prints; `cpu/ee/lsu` prints what the console prints,
which stock PCSX2 does not; on `cpu/ee_fpu/branchdelay` stock PCSX2 crashes;
three are compared line by line (`cpu/ee_fpu/arithmetic`, closer to the
console on 48 lines; `cpu/vu0_macro/transfer`, on one; `cpu/ee/branchdelay`,
where both have the same one line wrong, differently).

The test was tried by hand before it was relied on: given what PS2Native
printed before this increment (the same as PCSX2's interpreters print for
that program), it reports exactly the two lines above; given an output with
lines missing, it fails.

## P2 Hard problems

### (b) Lockstep, and how tests are named now

Tests are named `<suite>/<configuration>/<program>`. The suites are
`ps2autotests`, `programs` (PS2Native's own, in `tests/programs`) and `sdk`
(built with ps2sdk, in `tests/sdk`). The configurations are the four CPU
configurations, `lockstep` (interpreted run against compiled run at every
event test), for six programs `lockstep-no-stack`, and since 2026-11-11, for
the 55 ps2autotests programs, `never-further` (increment 5). All 62 programs
pass `lockstep`.

Whole suite at the end of this log (2026-11-11): 2 unit test programs (32
cases) and 379 tests pass in 117 s.

### (a) Programs built with ps2sdk (Windows, 2026-11-10)

`tests/sdk` holds the old project's C programs, linked with the real EE
libraries ([tests/sdk/README.md](../tests/sdk/README.md)). Their expected
outputs were recorded from stock PCSX2 with its recompilers: the whole output
of the machine, the BIOS's lines included. Two recordings of each were
identical.

| Program | Under `ps2n-ee` and `ps2n-static` | Lockstep |
|---|---|---|
| `iop_rpc`: a module of our own loaded on the IOP from `host0:`, its RPC server called in wait and no-wait mode, IOP threads, semaphores, event flags, alarms | identical to stock PCSX2 | identical |
| `pad`, `padx`: libpad and libpadx against the BIOS's PADMAN and XPADMAN, over 270 frames of vertical-blank interrupts and semaphore waits | identical to stock PCSX2 | identical |
| `mc`: libmc against the BIOS's XMCSERV, with no card in either slot | identical to stock PCSX2 | identical |
| `vu`: VU0 micro programs through `VCALLMS`, a VU1 program, VIF `UNPACK` (the text it prints; the frame is step P3) | identical to stock PCSX2 | identical |

Not done: `cdvd` (nothing builds the disc image it reads), `iop_sd` (its IOP
module does not load on a real BIOS; the old runtime faked the sound library
it imports), a memory card with a card in it, a pad with buttons pressed (the
host has no input yet).

`stock-interp` prints one line of `vu` differently (the VU0 status flags
after `DIV`): PCSX2's VU interpreter against its VU recompiler, recorded in
`tests/sdk-known-differences.cmake`.

### When the same output comes at another time (Windows, 2026-11-11)

The outputs above are equal. The moments at which they are printed are not
always. `ps2n-host -times <file>` (new) writes each line a program prints
with the number of frames shown and the EE's cycle count at that moment, and
two such files side by side show where two runs part. Where each program
prints its last line:

| Program | PCSX2's recompilers | PS2Native, compiled or interpreted | PCSX2's interpreters |
|---|--:|--:|--:|
| `iop_rpc` | frame 13, cycle 59,041,448 | the same frame and cycle | frame 13 |
| `vu` | frame 11, cycle 49,201,200 | the same frame and cycle | frame 11 |
| `pad` | frame 274, cycle 1,343,192,768 | the same frame and cycle | frame 275 |
| `padx` | frame 281 | frame 282 | frame 282 |
| `mc` | frame 17 | frame 15 | frame 15 |

Both differences come from one loop, ps2sdk's `nopdelay`: five `nop`s, a
decrement and a branch, 1,048,576 times. Its libraries run it after asking
the IOP for a service that was not registered yet, before asking again.

- **`padx`: the loop is charged differently.** PCSX2's recompiler does not
  run such a loop. It adds 8 cycles for each iteration left and goes to the
  next event (its "timeout loop" skip, `recSkipTimeoutLoop`,
  `x86/ix86-32/iR5900.cpp`). The 8 is fixed; by PCSX2's own table this
  loop's eight instructions cost 9. PS2Native runs the loop at 9. That is
  1,048,576 cycles more, which the times show between the same two lines
  (1,038,945 more than the recompiler there), and what follows is tied to
  the vertical blank, so everything after is one frame later. Confirmed by
  an experiment, removed afterwards: with this loop counted at 8 cycles an
  iteration, the interpreted run stops at the recompiler's frame and cycle
  (281, 1,377,633,608). In `pad` the same million cycles do not reach the
  next frame.
- **`mc`: a race in the program goes the other way.** Up to the line before
  `mcInit` the two runs are 24 cycles apart. Under PCSX2's recompiler the
  memory card server is not registered yet when `mcInit` asks for it, so the
  loop runs: `mcInit` takes 8.5 million cycles. Under PS2Native, and under
  PCSX2's interpreters, the server is there at the first attempt and
  `mcInit` takes 0.09 million. Which side of the race a run falls on is
  decided by a few cycles.

What this shows, beyond these two programs: PS2Native's timing is that of
PCSX2's recompiler to within the differences listed in the execution model
("Blocks and time"), and a difference of a few cycles can flip a race
between the EE and the IOP. PCSX2's own interpreter stands in the same
relation to its recompiler. Reproducing the timeout loop skip would remove
the one difference here that is systematic; it is the first entry under
"Open questions".

### Idle loops

The pad programs took 14 s under PS2Native where stock PCSX2 took 3 s. The
program waits on a semaphore for 270 frames; meanwhile the BIOS's idle thread
spins, on our interpreter, at 0.4 times the console's speed. A game that
waits for the vertical blank would have run at 40% speed.

PCSX2's recompiler does not run such loops: a block that jumps back to its
own start and holds nothing but loads, arithmetic on loaded values and
`nop`s moves the cycle count to the next event (its WaitLoop speed hack, on
by default). PS2Native now does the same, with the same test
([waitloop.h](../core/include/ps2n/waitloop.h)), in the interpreter and in
compiled code, where the recompiler finds the loops ahead of time. The pad
programs take 4 s; the lockstep tests still pass, so both skip at the same
places.

### Where blocks end, against PCSX2's recompiler

PS2Native's blocks end at branches and system instructions. PCSX2's
recompiler ends a block in three more places: at a 4 KB page boundary; where
it runs into a block it compiled earlier; and before the target of a
conditional branch that goes back into the block. Each such end rounds the
block's cycles down once more, so the recompiler counts up to one cycle less
per occurrence. The second depends on the order in which code was first
executed, so it cannot be reproduced by a rule about the code. Not
reproduced; on the six benchmark loops the counts are the same on four and
one cycle apart on two (step P4).

A fourth place where it rounds down, found on 2026-11-11 while reading its
`CTC2`: in the middle of a block it adds the cycles so far to the cycle
count before a COP2 instruction that may need VU0 to catch up, and before
the COP0 instructions that read or write the count register or the
performance counters or write the status register
(`scaleblockcycles_clear`, `x86/microVU_Macro.inl`, `x86/iCOP0.cpp`).
PS2Native adds a block's cycles at its end only, so the PCSX2 functions it
calls for those instructions see the cycle count as it was when the block
began. Not reproduced either. None of the test programs' differences from
stock PCSX2 comes from it; code that reads the count register twice in one
block would get a smaller difference than under the recompiler.

### (c) A program that rewrites its own code (Windows, 2026-11-10)

`tests/programs/selfmod.s` calls a two-instruction function, overwrites it
(with `FlushCache`, as a console requires), calls it, puts it back, calls it
again. It is written for PS2Native's own assembler
(`ps2n-recomp assemble`), prints through the EE's serial port and boots
through the BIOS like any executable; there is no console recording for it,
so its expected output is what the program must print by construction, and
stock PCSX2 prints the same under both of its configurations.

| Build | What happened |
|---|---|
| From the executable alone (`programs/first-build/selfmod`) | Output correct. The write hit the write-protected page; the function was compared with memory before its next call and found different; its 2 instructions were interpreted; the run reported 1 block start and the 2 words found in memory. That report equals `tests/programs/selfmod.capture.toml`, which the test checks. |
| With that capture (`programs/rebuilt/selfmod`) | The recompiler made a second version of the function ("variant"). Output correct; **0 instructions of the program interpreted**, 0 block starts reported, 0 functions found different; 1 call passed on to the variant; after the function was put back, the original ran again. |
| Lockstep, rebuilt image | Identical to the interpreted run at every event test. |

The step's criterion, "zero interpreted game-code addresses after one
rebuild", is met for this program. What it does not cover: code loaded where
the executable has none (overlays). See the execution model, "Variants".

Whole suite after this step: 2 unit test programs (29 cases) and 289 tests
pass in 106 s. With the benchmark program of step P4 added: 294 tests, 120 s.

## P3 Graphics path (Windows, 2026-11-10)

`ps2n-host -renderer sw|vulkan|opengl|dx11|dx12` runs the core with a GS
renderer that draws, still without a window. When the run stops, the host
reads back the frame on display at the internal resolution
(`MTGS::SaveMemorySnapshot`), prints its size and a fingerprint of its
colours, and with `-frame <file>` writes it as an image.

The program: `tests/sdk/bin/vu.elf`. A VU1 micro program transforms nine
vertices uploaded through VIF1 and draws three shaded triangles with
`XGKICK`. Four frames after its last line:

| Run | Frame |
|---|---|
| Stock PCSX2, software renderer (twice) | 320x448, fingerprint `A74A19105CFC746D` |
| `ps2n-ee`, software renderer | the same |
| `ps2n-static`, software renderer | the same |
| `ps2n-static`, Vulkan at 1x | 320x448 |
| `ps2n-static`, Vulkan at 3x | 960x1344 |

The step's criterion, "frame hashes equal stock PCSX2's software renderer;
the Vulkan renderer runs at 1x and 3x", is met for this program. The
recording is in `tests/sdk/vu.software.frame`; the `frames/*` tests hold all
three CPU configurations to it and check the size of the two Vulkan frames.
The pictures were looked at: three triangles with interpolated colours, the
3x one sharper.

What this does not cover:

- One program. The assessment also names ps2autotests `vu/*` and `gs/*`;
  those print results and draw nothing, and pass as text in every
  configuration (with the differences recorded for stock PCSX2 too).
- The fingerprint was recorded on one machine. The software renderer computes
  the picture on the CPU, but the read-back goes through the graphics card;
  whether another card, or Linux, gives the same bytes is for step P5 to
  show.
- The frame tests need a graphics device and carry the CTest label `gpu`
  (`ctest -LE gpu` leaves them out).

## P4 Measurement (Windows, 2026-11-10)

`tests/programs/bench.s` has six CPU-bound workloads, each a loop that never
leaves the EE: register arithmetic (`sum`), word loads and stores over 64 KB
(`mem`), a recursive function, 7 million calls (`call`), FPU loads, multiplies
and adds (`fpu`), quadword loads and stores with parallel arithmetic (`mmi`),
and a jump through a table in every iteration (`jump`). It prints a checksum
per workload; all four configurations print the same six, which
`programs/*/bench` checks. `ps2n-host` takes the time between the two lines a
workload prints.

`scripts\windows\bench.ps1 -Repeat 3`, best of three runs, one configuration
at a time, on the development machine (i5-14600KF), Release build, nothing
else running. Milliseconds of wall-clock time, and in brackets millions of EE
cycles per second (a console runs 295):

| Workload | `stock-jit` | `stock-interp` | `ps2n-ee` (our interpreter) | `ps2n-static` | static / jit |
|---|--:|--:|--:|--:|--:|
| sum | 69.7 (3,850) | 1,387.6 (221) | 2,329.4 (115) | 60.4 (4,444) | 0.87 |
| mem | 18.8 (4,007) | 385.2 (226) | 603.7 (125) | 19.2 (3,923) | 1.02 |
| call | 29.8 (3,316) | 621.6 (179) | 957.9 (103) | 34.4 (2,869) | 1.15 |
| fpu | 49.6 (2,035) | 437.3 (250) | 616.2 (164) | 51.7 (1,950) | 1.04 |
| mmi | 8.3 (6,111) | 215.6 (261) | 353.3 (143) | 10.2 (4,962) | 1.23 |
| jump | 48.8 (4,043) | 1,342.7 (164) | 2,036.0 (97) | 71.9 (2,741) | 1.47 |

(Measured with the code as it stands at the end of this log, idle-loop
skipping included. A run before that was added gave the same picture, the
interpreter a few percent faster.)

What the numbers are and are not:

- They are whole-machine times: the IOP, the counters and the event tests
  run for the same emulated time in every column, and are part of every
  number. (The compiled loop of `sum` is 13 host instructions on registers;
  how the 60 ms divide between it and the rest of the machine was not
  measured.)
- **Compiled code is at parity with PCSX2's recompiler**, the bar the brief
  sets: faster on one workload, within 4% on two, 15% to 47% slower on three.
  The slowest, `jump`, goes through the function's entry switch on every
  iteration. None of this is tuned yet.
- **The stop condition "static EE code is slower than PCSX2's interpreter"
  is far from met**: 8 to 23 times as fast.
- PS2Native's own interpreter is 1.4 to 1.7 times slower than PCSX2's. It is
  the fallback, but it is also what runs the BIOS until the BIOS is compiled,
  and in the test programs the BIOS is 95% of the instructions. It decodes
  every instruction every time; it has not been optimized at all.
- Six loops are not a game. Nothing here measures memory-heavy game code,
  system-call-heavy code (each call leaves compiled code), or the cost of
  comparing functions with memory.

**EE cycles counted per workload**, `stock-jit` against `ps2n-static`:
268,435,673 and 268,435,673 (`sum`); 75,501,271 and 75,501,272; 98,688,388
and 98,688,388; 100,852,955 and 100,852,955; 50,436,319 and 50,436,320;
197,132,519 and 197,132,519. PCSX2's interpreter counts 8% to 15% more.

**Two things this step found and fixed.**

1. The first measurement had compiled code 3 to 6 times *slower* than the
   recompiler. The compiled loop was fine; the provider was fingerprinting a
   kilobyte and a half of registers at every event test, 450,000 times in the
   run, for the lockstep comparison, whether or not a comparison was running.
   It is now only done while a run is recorded or compared.
2. Blocks were counted as the sum of their instructions' eighths *rounded to
   nearest*; PCSX2's recompiler rounds *down*. On `call`, whose blocks are
   short, that made the EE 14% slower against the rest of the machine than
   under the recompiler. `blockCycles()` now rounds down. The counts above are
   after the change.

And one about the host: the core's frame limiter was on (it is on unless a
frontend switches it off), so every run so far had been held to the console's
speed. It is off now, as in PCSX2's own `pcsx2-gsrunner`.

### What the interpreted BIOS costs (Windows, 2026-11-11)

Measured for the decision about translating the BIOS
([ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md)). Five test
programs, each run three times under each configuration, one run at a time.
The time is that of the whole process, start-up of the core included, best
of three:

| Program | Stock PCSX2, recompilers | PS2Native, program compiled | More | BIOS instructions interpreted in the compiled run |
|---|--:|--:|--:|--:|
| `sdk` `pad` (274 frames) | 1,702 ms | 2,020 ms | 318 ms | 40,033,201 |
| `sdk` `iop_rpc` (13 frames) | 1,540 ms | 1,847 ms | 307 ms | 34,220,000 |
| `sdk` `mc` (15 frames) | 1,506 ms | 1,846 ms | 340 ms | 34,306,938 |
| `cpu/ee/alu` (26 frames) | 1,449 ms | 1,905 ms | 456 ms | 42,986,448 |
| `kernel/ee/thread/terminate` (11 frames) | 1,490 ms | 1,868 ms | 378 ms | 34,223,519 |

- None of the programs' own instructions was interpreted in the compiled
  runs. Everything in the last column is the BIOS.
- At least 34.2 million instructions are interpreted in every program,
  whatever it does: the two shortest, which do different things, are 3,519
  apart. That points to the boot.
- The time added, 0.31 to 0.46 s, is about what that many instructions cost
  on PS2Native's interpreter (97 to 164 million cycles a second on the six
  loops above).
- `pad` waits for 274 vertical blanks. It interprets 5.8 million BIOS
  instructions more than the shortest programs, about 21,000 a frame for the
  interrupt, the semaphore and the thread switch: about 0.2 ms of host time
  a frame.
- Per instruction the interpreter takes 12 to 39 times as long as compiled
  code (the table above, `ps2n-ee` against `ps2n-static`).
- **Not measured:** how much of a real game's time is spent in the kernel. A
  game makes far more system calls a frame than these programs.

To see it again: the instruction counts are in what every test prints
("interpreter: ... instructions"), in
`build\windows-clang\Testing\Temporary\LastTest.log`; the times are those
CTest prints for each test when the tests run one at a time
(`ctest --test-dir build\windows-clang -j 1 -R "sdk/(stock-jit|ps2n-static)/"`
with `PS2N_BIOS` set).

## Open questions

### For the maintainer

1. **Phase 3**, the first real game, is not approved. Nothing of it has been
   started.
2. **A patch for PCSX2's `vu0DenormalizeMicroStatus`?** (Increment 5.) Two
   characters in `pcsx2/VU0micro.cpp` would start VU0 micro programs with
   the status flags the console starts them with, and `vu/lower/integer`
   would match the console again. It would be the first entry in the patch
   queue: the first place where the machine is not stock PCSX2, to be
   carried over every update of PCSX2 until PCSX2 corrects it. Recommended:
   no patch. PCSX2 accepts reports from people, not from AI tools, so
   reporting it there is the maintainer's to do or not.
3. **The reading of [ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md):**
   that "not use BIOS" means "not interpret it", with a BIOS file still
   required at run time, and that the three conditions of the recommendation
   stand.

### Timing against PCSX2's recompiler

- **The timeout loop skip is not reproduced**, and it shows: one frame in
  `padx` ("When the same output comes at another time"). PCSX2's recompiler
  counts 8 cycles for each iteration of a count-down loop of `nop`s and goes
  straight to the next event; PS2Native runs the loop at what its
  instructions cost. Games have such loops (PCSX2's source names God of War
  2 and the Need for Speed series). Reproducing it means recognizing the
  loops ahead of time and in the interpreter, as was done for idle loops,
  and is the first thing to do before a game's timing is compared with
  stock PCSX2's.
- The other differences are listed in the execution model, "Blocks and
  time". Each is at most a cycle per occurrence. A race between the EE and
  the IOP can turn on that (`mc`).

### Design points

- **Installing the EE provider.** Settled for now: the host sets `Cpu` after
  `VMManager::Initialize` and checks every frame that the core has not
  replaced it. The patch queue is still empty.
- **Code that is not in the executable.** Overlays and modules a game loads
  later are interpreted and their block starts reported, but nothing captures
  their code yet. Needed for the first real game, not for this step.
- **One translation unit per executable** is enough for test programs. A game
  of several megabytes needs its functions split over many files; the old
  project's generator did that and the new one does not yet.
- **More than one image at a time.** The provider picks one image, by the
  checksum of the executable being started. A translated BIOS (ADR 0007),
  and a game's overlays, need several to be in use together.
- **COP0 and the rest of COP2** are still PCSX2's interpreter functions:
  what they do is PCSX2's interpreter's behavior, not its recompiler's. Two
  differences are known and not yet dealt with: the count register read
  twice in one block (execution model, "Blocks and time"), and when VU0 is
  brought up to date before a COP2 instruction, which PCSX2's recompiler
  decides from an analysis of the block.
