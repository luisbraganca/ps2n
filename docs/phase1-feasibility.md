# PS2Native: Phase 1 Feasibility Assessment

| | |
|---|---|
| Date | 2026-11-10 |
| Status | **Awaiting approval.** Nothing has been implemented. |
| Brief | [brief.md](brief.md) |
| PCSX2 examined | tag `v2.9.170`, commit `a886f8ef` (2026-11-06), the newest nightly tag on that date |
| Old project examined | `../ps2-uncompiler`, 70 commits, last 2026-11-10 |
| Reference projects | AnyPS5 `3c9276b7`, PS2Recomp `e5636981`, PS2 Web Recomp `76554ba4`, ps2autotests `97469ffb` |
| Revised | 2026-11-10, after the maintainer set platform priorities ([ADR 0001](adr/0001-platform-priorities.md)): sections 1, 4.8, 9.1, 9.2, 12, 13, 14, 15 and 16 |

Every claim about a codebase below was read from its source at the revision
above. Claims that only a running prototype can settle are marked
**unproven**.

## 1. Verdict

1. **Genuine ahead-of-time recompilation of EE code is feasible, and it can
   drive PCSX2's hardware model without PCSX2's JIT.** PCSX2 selects its CPUs
   through three small provider interfaces, and its scheduler, memory layer and
   code-invalidation machinery are written to be shared by any provider. PCSX2
   itself already ships a third EE provider that is neither interpreter nor JIT.
2. **"Minimal hardware emulation" and "maximum compatibility" pull against
   each other on this console, and the brief ranks compatibility first.** PS2
   games are bare-metal programs. There is no operating-system graphics or
   audio API to reimplement natively; a game's interface to the world is the
   hardware registers. PCSX2's compatibility is its hardware model. Three
   independent projects that replaced that model with their own (the old
   PS2Native, PS2Recomp, PS2 Web Recomp) each run between two and five games.
3. **Recommendation:** statically recompiled code as the CPUs, PCSX2's core as
   the machine, a user-supplied BIOS, no JIT and no PCSX2 user interface in the
   generated game. What static recompilation removes is CPU emulation. It does
   not remove the GS, SPU2, DMA, IOP hardware or timing models, and I do not
   see a way to remove them without giving up the first two priorities.
4. **This is closer to "a game bundled with a trimmed PCSX2 whose CPUs were
   replaced by static code" than the brief's wording suggests.** That is the
   decision you need to make. Section 15 says where I think the brief's goals
   conflict, and section 16 lists the questions.
5. **On the required platforms the goal is parity with PCSX2, not an
   improvement on it.** Windows x64 and Linux x64 are required and everything
   else is optional ([ADR 0001](adr/0001-platform-priorities.md)). PCSX2 has
   mature recompilers on both, so a generated game can at best equal it in
   compatibility, picture, sound and speed, which the brief accepts. What it
   adds is form: a standalone, pre-configured game with no emulator to install
   or set up, whose EE code is static. The one target where the design would
   beat PCSX2 is macOS ARM64, where upstream has no EE, VU or IOP recompilers
   and relies on Rosetta 2; that target is now optional.

## 2. Audit of `../ps2-uncompiler`

About 57,000 lines of first-party C++ and shaders (90,232 counted lines, of
which 32,780 are a compiled SPIR-V header), written between 2026-10-28 and
2026-11-10. No license file; all first-party code is yours, so relicensing to
GPL-3.0-or-later is unencumbered. Third-party: toml++ (MIT), Dear ImGui (MIT),
Vulkan headers (Apache-2.0 or MIT).

| Area | Lines | What it is |
|---|--:|---|
| `core/` | ~5,000 | One instruction table (`r5900_ops.inc`) driving decoder, disassembler and assembler; semantics table (`r5900_sem.inc`); ELF, IRX and SNR2 readers; VU pair decoding and pipeline timing (`vu_micro.h`) |
| `recompiler/` | ~3,700 | Function discovery, jump tables, resume points (`analysis.cpp`); C++ emission (`codegen.cpp`); VU micro code to C++ (`vu_codegen.cpp`); pnach parser; config; CLI |
| `runtime/include/ps2/` | ~2,400 | Instruction semantics as inline functions (`ops_int.h`, `ops_mmi.h`, `ops_fpu.h`, `ops_vu0.h`, `ops_cop0.h`), VU micro operations |
| `runtime/src/` dispatch, interpreter, VU | ~1,500 | Dispatcher with per-function checksums and code epochs, fallback interpreter, VU interpreter |
| `runtime/src/hw/` | 5,534 | Software GS rasterizer, DMAC, VIF, IPU, EE registers |
| `runtime/src/gpu/` | 5,109 | Vulkan renderer that reproduces the software rasterizer's pixels |
| `runtime/src/iop/` | 8,932 | R3000A interpreter, IOP kernel HLE, library HLE, SIF, CDVD, memory cards, pad, SPU2, libsd |
| `runtime/src/kernel/` | 1,188 | EE kernel HLE (129 of 145 syscalls) |
| `video/`, `input/`, `ui/`, `audio/` | ~3,500 | SDL window, input manager, ImGui overlay, audio out |
| `tests/` | ~9,600 | Unit and end-to-end tests, assembly programs, prebuilt ps2sdk programs |

Assessment:

- **The recompiler front end is sound and worth keeping.** The execution model
  (`runtime/include/ps2/recomp.h`, `docs/architecture.md`) is the right one for
  this problem: guest state in globals, direct C++ calls, a return-address
  check after every call, resume switches at function entry, safepoints at
  back-edges, and an interpreter that shares the semantic functions with
  generated code. PS2 Web Recomp arrived at the same model independently.
- **The runtime is where the project stalled, and it is the part that
  duplicates PCSX2.** Its own status table (README, 2026-11-07) reads: DMA and
  timers 4 of 8, discs and memory cards 5 of 10, GS 16 of 22, IOP library
  functions 428 of 557, games finished 2 of 5. The Killzone page records a
  film-audio drift of 1 to 2% "whose cause has not been found". Each of those
  is a hardware-behavior problem PCSX2 has already solved.
- **Its tests cannot catch semantic errors.** Recompiled code and the
  interpreter share one semantics library, so they agree even when both are
  wrong. The Killzone `RSQRT` operand-order bug was found by looking at
  rainbows on screen. This matters for the new design (section 9.4).
- **The renderer is exact but solves the wrong problem.** It reproduces
  console pixels at 1x and scales positions, which is why upscaling artifacts
  needed bespoke work per game. PCSX2 carries 3,958 per-game GS hardware-fix
  entries for the same class of problem (section 4.6).

Section 10 lists what to retain.

## 3. Reference projects

| | AnyPS5 | PS2Recomp | PS2 Web Recomp |
|---|---|---|---|
| License | **GPL-2.0-only** | GPL-3.0 (license text only; no "or later" notices) | MIT |
| Compatible with a GPL-3 project | **No.** Ideas only, no code | Yes | Yes, with notices kept |
| Target | PS5 (x86-64) to Linux and Windows | PS2 EE to C++ | PS2 EE to C++, then WebAssembly or native |
| Method | Relinks the executable; system libraries reimplemented natively behind the OS's dynamic-library ABI; shader recompiler | Per-game porting framework: Ghidra export, TOML config, HLE kernel and library stubs, own software GS, raylib | HLE EE kernel, IOP interpreter with HLE libraries, own GS in WebGPU compute shaders, recorded VU programs compiled ahead of time |
| Stated status | Per-title bring-up threads | "Experimental"; "Performance is very bad for VU and GS"; "Hardware emulation is partial and many paths are stubbed" | Two games (Gran Turismo 4, God of War II), a third experimental; "Another game will most likely stop at a system call or an IOP library function nobody implemented yet" |

What each teaches:

- **AnyPS5** (`docs/dev/ARCHITECTURE.md`, `AGENTS.md`). Its "no emulation"
  design works because the PS5's CPU is the host's and its games call the
  system through named library exports. Neither is true of the PS2: the EE is
  MIPS, and games link Sony's libraries statically and write to hardware
  registers. The architecture does not transfer. The process does: an
  unimplemented path throws instead of returning a made-up success; never
  special-case a value seen in one title's trace; known gaps live in one debt
  file per component; a speed claim needs before-and-after measurements from
  the same machine and scene. Those match the brief's AI rules and are worth
  adopting.
- **PS2Recomp** (`README.md`, `ps2xAnalyzer/Readme.md`). Same family as the old
  PS2Native: an HLE runtime grown per game. Its one distinctive asset is an
  11.8 MB database of Sony SDK function signatures
  (`ps2xAnalyzer/include/ps2recomp/sce_symbol_database_data.h`), "built from
  PS2 games with debug information". It could sharpen function discovery in
  stripped executables. Its provenance is a third-party scanner's data, so
  treat it as optional and verify its origin before use. The recommended
  architecture does not need to identify SDK functions.
- **PS2 Web Recomp** (`README.md`, `scripts/port.sh`, `recomp/codegen.cpp`,
  `recomp/vurecomp.cpp`). The most relevant. It confirms three things with a
  working system:
  1. An unattended convergence loop works: run the game headless with scripted
     input, record entry points and VU programs the analysis missed, recompile,
     repeat until a run finds nothing new. God of War II took three and a half
     minutes on an 8-core laptop.
  2. Code a game rewrites in place (Kingdom Hearts II swaps a module) can be
     handled by translating each captured image and selecting by content at
     run time.
  3. PCSX2 patch files can be applied to the executable before translation and
     to memory at run time.
  It also confirms the ceiling: a second game needed an unimplemented IOP
  timer, exposed a VU flag-timing error of one instruction, and a recompiler
  bug the first game never triggered.

**The pattern across all three PS2 projects and the old PS2Native is the same:
an HLE runtime reaches a handful of games and then pays per game.** That is
the strongest evidence for the recommendation.

## 4. PCSX2 at `v2.9.170`

### 4.1 Build shape

- The emulator core is one library target, `PCSX2` (`pcsx2/CMakeLists.txt`).
  The Qt frontend is a separate target and can be switched off
  (`ENABLE_QT_UI`, `cmake/BuildParameters.cmake`).
- `pcsx2-gsrunner` is a second, upstream-maintained frontend that links the
  same core with its own `Main.cpp` and no Qt
  (`pcsx2-gsrunner/CMakeLists.txt`). Its host layer is about 55 `Host::`
  callbacks, most of them empty. **A non-Qt host for the core is a supported
  configuration, not a hack.**
- Core dependencies (`cmake/SearchForStuff.cmake`): SDL3, zlib, zstd, lz4,
  libpng, libjpeg, libwebp, FreeType, plutovg, plutosvg, rapidyaml, FFmpeg
  (loaded dynamically by default), shaderc for Vulkan, plus vendored libraries
  in `3rdparty/`.
- The core is a monolith. `VMManager.cpp` (120 KB) references nearly every
  subsystem, including ones a fixed-configuration game does not need
  (achievements, Discord, debugger, input recording, USB, network adapter).
  Even PCSX2's own GS-only tool does not extract the GS: it runs the whole
  machine with a dummy EE provider. **Subsystem extraction is not a seam
  upstream maintains.**

### 4.2 The CPU provider seams

| Provider | Interface | Selected at |
|---|---|---|
| EE | `struct R5900cpu`: eight function pointers, `Reserve`, `Shutdown`, `Reset`, `Step`, `Execute`, `ExitExecution`, `CancelInstruction`, `Clear` (`pcsx2/R5900.h:309-357`) | `Cpu = CHECK_EEREC ? &recCpu : &intCpu` (`pcsx2/VMManager.cpp:2759`) |
| IOP | `struct R3000Acpu`: `Reserve`, `Reset`, `ExecuteBlock(eeCycles)`, `Clear`, `Shutdown` (`pcsx2/R3000A.h:181-189`) | `psxCpu = ...` (`VMManager.cpp:2760`) |
| VU0, VU1 | `class BaseVUmicroCPU`: `Reset`, `SetStartPC`, `Execute(cycles)`, `Step`, `Clear`, `ResumeXGkick` (`pcsx2/VUmicro.h:29-78`) | `CpuVU0 = ...`, `CpuVU1 = ...` (`VMManager.cpp:2762-2763`) |

`GSDumpReplayer.cpp:130-133` installs `GSDumpReplayerCpu`, an EE provider
that executes no guest code at all. A fourth provider that runs static code is
the same kind of object.

Provider selection is hard-wired in `VMManager::UpdateCPUImplementations()`
(`VMManager.cpp:2747-2771`), a private function. Installing other providers
needs either a patch of about ten lines there, or, on x86-64 only, building
without PCSX2's JIT sources and supplying the `recCpu`, `psxRec` and
`CpuMicroVU0/1` symbols. The ARM64 branch hard-wires the interpreters, so
ARM64 needs the patch either way.

### 4.3 The scheduler contract

Everything time-driven goes through one function that the source describes as
the "shared portion of the branch test, called from both the Interpreter and
the recompiler": `_cpuEventTest_Shared()` (`pcsx2/R5900.cpp:362-459`). It
raises INTC and DMAC exceptions, gives the IOP its time slice
(`psxCpu->ExecuteBlock`), updates counters, completes DMA events and
synchronizes the VUs. A provider's whole obligation is:

1. Add executed cycles to `cpuRegs.cycle` (the JIT does it per block,
   `x86/ix86-32/iR5900.cpp:1384-1415`; the interpreter per branch,
   `Interpreter.cpp:270-275`).
2. When `cpuRegs.cycle` reaches `cpuRegs.nextEventCycle`, write the current
   guest PC to `cpuRegs.pc` and call `_cpuEventTest_Shared()`.
3. If `cpuRegs.pc` changed, an exception was taken (`cpuException`,
   `R5900.cpp:95-166`, sets it to the vector): continue from the new PC.
4. Honor `ExitExecution`, `Clear` and `Reset`.

This is the old project's safepoint protocol (`PS2_BACKEDGE`, the
return-address check) with PCSX2's function in place of the HLE kernel's.

### 4.4 The memory contract

`vtlb` exposes its fast path in a header: one table load, a sign test and an
add for RAM, a handler call for hardware registers
(`pcsx2/vtlb.h:118-221`, implementation `vtlb.cpp:164-286`). TLB remaps by the
kernel update the table, so static code that uses it follows the guest's
mappings. The JIT's faster variant, fastmem with fault backpatching
(`vtlb_BackpatchLoadStore`), rewrites JIT code and does not apply to static
code. Expect each guest memory access to cost one extra load and branch
compared with PCSX2's JIT. **Unproven** how much that matters.

### 4.5 Code invalidation

PCSX2 write-protects RAM pages that hold translated code. A write faults, the
handler unprotects the page and calls `Cpu->Clear(addr, size)`
(`vtlb.cpp:1451-1496`). The API (`mmap_MarkCountedRamPage`) is public and
provider-agnostic. A static provider can use it to learn that RAM no longer
holds what was compiled, which is exactly the signal needed for overlays and
self-rewriting code. Writes from DMA and other host code fault the same way.

### 4.6 Where compatibility lives

`bin/resources/GameIndex.yaml` has 12,894 serial entries. Entries carrying
each kind of fix:

| Key | Entries | Implemented in |
|---|--:|---|
| `gsHWFixes` | 3,958 | PCSX2's GS hardware renderers |
| `gameFixes` | 977 | Mostly shared hardware code (DMAC, VIF, GIF, counters); five are JIT-only (below) |
| `clampModes` | 800 | **x86 JIT only** for the EE FPU (`x86/iFPU.cpp`, `iFPUd.cpp`); VU clamping partly in the interpreter |
| `roundModes` | 434 | Host FP control register on the CPU thread (`VMManager.cpp:1565`), which any provider inherits; the separate division mode is switched inside the JIT (`x86/iFPU.cpp:1097`) |
| `memcardFilters` | 422 | Memory card code |
| `speedHacks` | 261 | Mixed |
| `patches` | 246 | `Patch.cpp`, memory writes at boot or every vsync |
| `dynaPatches` | 6 | Pattern patches applied only when the JIT compiles an instruction (`x86/ix86-32/iR5900.cpp:1686`) |

Two consequences:

- **Graphics quality comparable to PCSX2 means PCSX2's GS.** Those 3,958
  entries only mean something to its renderers.
- **CPU semantics cannot be reused by linking.** Behavior that games were
  tuned against exists only inside the x86 code generators: the EE FPU clamp
  modes (`fpuFloat`, `fpuFloat2`, `fpuFloat3`, `FPU_ADD_SUB`,
  `x86/iFPU.cpp:339-401`), `FpuMulHack`, `SkipMPEGHack`, `VUOverflowHack`,
  `FullVU0SyncHack`, `IbitHack`, the VU extra and sign clamps, the wait-loop
  and timeout-loop detection (`iR5900.cpp:2151`), and `dynaPatches`. The
  interpreter's FPU (`pcsx2/FPU.cpp`) behaves differently. A static
  recompiler must re-express the JIT's semantics in its own library and prove
  equivalence by testing (section 9.4). This is the largest piece of custom
  code the recommendation requires.

### 4.7 BIOS

PCSX2 does not run PS2 software without a BIOS: `VMManager.cpp:1457-1470`
refuses to boot, and only GS dump replay is exempt. It has no HLE kernel. The
EE kernel, the syscall and interrupt handlers, the thread scheduler and every
IOP system module are the BIOS's own code, executed as guest code. Fast boot
(`EnableFastBoot`) skips the BIOS's menu program, not the kernel.

### 4.8 ARM64

Optional since ADR 0001; kept as the record of what was found.

- The root `CMakeLists.txt` warns on ARM64: "Apple Silicon support in PCSX2
  is INCOMPLETE. There are currently no EE/VU/IOP recompilers, and games will
  run VERY slow."
- `pcsx2/arm64/` holds a VIF unpack recompiler, assembler helpers and
  `RecStubs.cpp`, whose JIT entry points are `pxFailRel("Not implemented.")`.
- The GS has native ARM64 code (`GS/GSVector4i_arm64.h`, the software
  renderer's ARM64 code generators) and a Metal renderer.
- Upstream CI compiles an ARM64 macOS build on every change
  (`.github/workflows/macos_build_matrix.yml`), so it does not rot at compile
  level. The release workflow publishes only the x86-64 build
  (`release_cut_new.yml`), which runs on Apple silicon under Rosetta 2.
- Reporting on Apple's announcements says Rosetta 2 is a general-purpose tool
  through macOS 27 and is reduced to a subset for older games in macOS 28,
  expected in late 2027. The exact scope of that subset is not confirmed.
- A community fork, ARMSX2, is reported to have ARM64 JITs. It is a fork, not
  upstream, and I did not examine its source.

### 4.9 Reuse, by the brief's list

| Area | PCSX2 component | Reuse |
|---|---|---|
| Graphics, renderers, shaders, upscaling | `pcsx2/GS/` (4 MB of source): Direct3D 11 and 12, Vulkan, OpenGL, Metal, software; `bin/resources/shaders` | Whole |
| SPU2 and audio | `pcsx2/SPU2/`, `Host/` audio streams (cubeb, SDL) | Whole |
| Input | `pcsx2/Input/` (SDL, XInput, DInput sources), `SIO/Pad/` | Whole |
| Memory cards | `SIO/Memcard/` (file and folder cards), `SIO/Sio2.cpp`, multitap | Whole |
| Timing, DMA, hardware | `Counters.cpp`, `Dmac.cpp`, `Gif*.cpp`, `Vif*.cpp`, `SPR.cpp`, `IPU/`, `Sif*.cpp`, `Iop*.cpp` | Whole |
| Compatibility database | `GameDatabase.cpp` and `GameIndex.yaml` | Whole, read at build time and at run time |
| Widescreen, FPS, cheats | `Patch.cpp` and the `pcsx2_patches` release archive (`patches.zip`) | Run-time application whole; code patches must also be applied before translation |
| Game identification, region | `VMManager::UpdateDiscDetails`, `GameList.cpp`, `Elfheader.cpp`, `RedumpDatabase.yaml` | Whole, in the wizard too |
| Disc images | `pcsx2/CDVD/`: ISO, BIN, CHD, CSO, ZSO, GZ readers and the drive model | Whole |
| Configuration | `Pcsx2Config.cpp`, the settings interfaces | Whole. The build bakes a settings file; the game has no settings UI |
| CPU semantics | `Interpreter.cpp`, `R5900OpcodeImpl.cpp`, `COP0.cpp`, `VUops.cpp`, the x86 JIT | **Reference and test oracle, not linked reuse.** Rare system instructions (syscall, TLB, ERET) can call PCSX2's interpreter functions, as PCSX2's JIT does (`iR5900.cpp:780-796`) |
| Function analysis | `DebugTools/MIPSAnalyst.cpp`, `3rdparty/ccc` (MIT) | Candidate second opinion for function discovery |
| Not wanted in a generated game | `pcsx2-qt/`, the EE, IOP and VU JITs, achievements, Discord, PINE, debugger, input recording, updater | Left out where the build allows; see section 13 |

## 5. Can PS2 code be translated ahead of time?

| Code | Known at build time? | Finding |
|---|---|---|
| EE boot executable and other executables on the disc | Yes | Feasible. The old project and PS2 Web Recomp both do it. |
| EE code reached only through pointers | Partly | Not decidable in general. Every project uses a fallback plus recording. Converges in practice (PS2 Web Recomp's loop; the old project's `ps2n_missing.toml`). |
| EE code loaded later: overlays, relocatable modules (SNR2, ERX), code unpacked from archives | Only if it can be found in the disc's files | Translate what analysis finds; capture the rest at run time, keyed by content; select by content. Needs a fallback the first time. |
| EE kernel and loader (the BIOS) | Yes, once the user supplies the BIOS | Feasible in principle: the kernel and EELOAD load at fixed addresses (`R5900.h:294-297`) from modules stored in the ROM image, so they can be translated per BIOS version at build time. **Unproven**; not part of the first proof of concept. Kernel patches that games copy into kernel memory are data in the game's executable and fall under "loaded later". |
| VU0 macro mode (COP2 instructions) | Yes | Inline in EE code. Feasible. Timing sync with VU0 micro mode is JIT-defined behavior to reproduce. |
| VU micro programs | Partly | Uploaded as data at run time. Static scanning for VIF `MPG` packets is a heuristic; recording is reliable. Program identity is its content, not its address. The old project's generator handles this; so does PCSX2's microVU, at run time. State explosion is a known hazard (the old project once produced 138 MB of C++ for Killzone before pruning). |
| IOP modules (BIOS modules, the game's IRX files) | Mostly | Relocatable, loaded at addresses chosen at run time, often inside archives or embedded in the EE executable. Feasible as position-independent translations keyed by content, as the old project did for SNR2. Lowest value: the IOP is a 37 MHz CPU. |
| Code a game generates or modifies while running | No | Fallback only. Rare on the PS2. |

**No design translates 100% of executed code ahead of time for arbitrary
games.** A general tool needs a defined path for code it has not seen. The
choices are an interpreter, a run-time JIT, or stopping with a diagnostic. The
brief requires that this be documented and approved, so it is question 2 in
section 16.

## 6. Hardware emulation that cannot realistically be eliminated

| Component | Why it stays |
|---|---|
| GS | Games emit GS primitives and register writes through three GIF paths. Any renderer has to implement GS semantics. PCSX2's is the one with the fixes. |
| GIF, VIF, DMAC, scratchpad, IPU | The routes by which game data reaches the GS and VUs. Games depend on their stall, interrupt and FIFO behavior; the old project documents this (Agent Under Fire's VIF1 stalls, `docs/architecture.md`). |
| INTC, timers, vsync and hsync, event scheduling | Game logic is paced by them. Games bound polling loops by counts of tries. |
| SIF, IOP hardware (SIO2, CDVD drive, SPU2, IOP timers and DMA) | Games ship their own IOP drivers, which talk to these registers. Emulating the registers lets every game's drivers run unmodified; replacing drivers with host services is the per-game work that capped the HLE projects. |
| SPU2 | Sound drivers write its registers and expect its interrupts at exact sample positions. |
| EE and IOP kernels | Either Sony's code runs (BIOS required), or it is reimplemented (compatibility ceiling). |

What static recompilation removes: instruction decoding, the JIT compiler and
its code cache, block lookup, and interpretation of every instruction it
covers. On x86-64 that is most of the 945 KB of `pcsx2/x86/` (the VIF unpack
recompiler in that directory serves the hardware model and stays).

## 7. Architecture alternatives

| | A. Evolve the HLE runtime | B. HLE runtime with PCSX2's GS (and SPU2) transplanted | **C. Static CPUs on the PCSX2 core** |
|---|---|---|---|
| Compatibility | Per-game work. Evidence: four projects, two to five games each | Graphics inherit PCSX2's fixes; kernel, IOP, DMA and timing stay per-game | PCSX2's, minus defects in our CPU translation |
| Visual fidelity | Own renderer; PCSX2's fixes do not apply | PCSX2's | PCSX2's |
| Audio | Own SPU2 and drivers | Own drivers, optionally PCSX2's SPU2 | PCSX2's |
| BIOS | Not needed | Not needed | **Required**, at build and at run time |
| Hardware emulation in the game | Smallest | Medium | PCSX2's full hardware model |
| Custom code to maintain | ~57,000 lines today, growing per game | Runtime minus renderer, plus a hand-maintained extraction of the GS from a monolith upstream does not keep separable | Recompiler, semantics library, providers, host shell, wizard. No hardware |
| Upstream divergence | None | High: every upstream change to GS-to-core coupling lands on us | Low: three small interfaces and a short patch |
| Meets priority 1 and 2 | No | 2 only | Yes |
| Meets "minimal emulation" | Yes | Partly | **Only for the CPUs** |

A fourth path, **D**, is C plus staged reduction, each step checked against
C's own behavior: translate the BIOS kernel, the VU programs and the IOP
modules so that nothing is interpreted in steady state; leave unused devices
out of the build; and only then consider native replacements for specific
services where equivalence can be demonstrated. D is the recommended roadmap.
B remains possible later as a BIOS-free mode, with C as its oracle.

## 8. How static code communicates with PCSX2

```
                 build time                                run time
  disc image ─┐
  BIOS ───────┼─► ps2n analyze + recompile ─► C++ ─► compiler ─► game code ──┐
  settings ───┘      (EE, later VU, kernel, IOP)                             │
  GameIndex, patches ─► baked settings + patched image                       ▼
                                                        ┌──────────────────────────────┐
                                                        │ ps2n providers               │
                                                        │  EE dispatcher, resume       │
                                                        │  points, fallback interpreter│
                                                        │  capture of unseen code      │
                                                        ├──────────────────────────────┤
   cpuRegs, fpuRegs, VU0/VU1 state ◄── shared state ──► │ PCSX2 core (pinned)          │
   vtlb fast path and handlers     ◄── memory ────────► │  vtlb, scheduler, DMAC, GIF, │
   cycle count, event test         ◄── time ──────────► │  VIF, IPU, SIF, IOP hardware,│
   cpuException, Clear             ◄── control ───────► │  GS + renderers, SPU2, CDVD, │
                                                        │  pads, memory cards, GameDB  │
                                                        ├──────────────────────────────┤
                                                        │ ps2n host shell (window,     │
                                                        │ Host:: callbacks, no UI)     │
                                                        └──────────────────────────────┘
```

- **State.** Generated code reads and writes PCSX2's own register structures
  (`cpuRegisters`, `fpuRegisters`, `VURegs`). No copying at boundaries.
- **One adapter file.** Generated code does not include PCSX2 headers. It
  uses a small ps2n header with fixed offsets and entry points; a single
  adapter source compiled against PCSX2 asserts those offsets and forwards the
  calls. An upstream layout change then fails to compile in one place.
- **Control flow.** The old project's model unchanged: direct calls between
  translated functions, return-address check, resume switch. An interrupt is
  taken at an event test; the kernel's handler runs; `ERET` returns to a PC
  that is, by construction, a resume point.
- **Exceptions at arbitrary instructions** (a TLB miss, a trap) return to a
  PC with no resume label. The fallback interpreter runs from there to the
  next known entry. PCSX2's JIT does not deliver TLB-miss exceptions at all;
  it logs them, and only the interpreter raises them (`vtlb.cpp:519-550`), so
  games that run on PCSX2's JIT do not depend on this path.

## 9. Recommended architecture

### 9.1 Components and their classification

| Component | Target | Interim, during Phases 2 and 3 |
|---|---|---|
| EE game code on the disc | **Statically compiled** | Same |
| EE code not seen at build time | Statically compiled after capture and rebuild | Fallback interpreter on first sight |
| EE kernel and loader (user's BIOS) | **Statically compiled** per BIOS | Fallback interpreter |
| COP1, MMI, COP2 macro | **Statically compiled**, semantics matched to PCSX2's JIT per clamp mode | Same |
| Rare system instructions (syscall entry, TLB, ERET, CACHE) | Call PCSX2's interpreter functions | Same |
| VU0 and VU1 micro programs | **Statically compiled** from discovered and captured programs | PCSX2's microVU recompiler if approved (9.2), otherwise its VU interpreter |
| IOP modules | **Statically compiled**, position-independent, keyed by content | PCSX2's IOP recompiler if approved (9.2), otherwise its IOP interpreter |
| GS, SPU2, IPU, DMAC, GIF, VIF, SIF, timers, CDVD, SIO2, pads, memory cards | **Emulated: PCSX2** | Same |
| Window, input devices, audio output | Native host code (PCSX2's input and audio layers, ps2n host shell) | Same |
| BIOS-free operation | **Unsupported** | Unsupported |
| Network adapter, HDD, USB peripherals | PCSX2's, off unless a game needs them | Not built or not enabled |

### 9.2 Fallback and stand-ins (needs your approval)

Two different things need a defined path.

**EE code not seen at build time.** Recommended: interpretation only for that
code, always reported, never silent, and converging to zero.

- The fallback is ps2n's own interpreter, built from the same semantics
  library as generated code, so the two cannot disagree. PCSX2's EE JIT is
  not suitable here: it owns its dispatcher and never returns to its caller
  (`x86/ix86-32/iR5900.cpp:459-490`, `752-777`), so running single unseen
  functions inside a static program would need a hybrid of ours.
- Every address interpreted is logged, counted in an exit summary and written
  to a capture file with the code bytes. A rebuild includes it.
- The wizard runs the convergence loop unattended (scripted input, repeat
  until nothing new), then reports coverage.
- A strict mode stops at the first interpreted address, for testing.

**CPUs that have no static translator yet (VU, IOP).** Until those translators
exist, something has to run VU micro programs and IOP modules. With Windows
and Linux as the required platforms, PCSX2's own recompilers exist everywhere
that matters, which was not so while macOS ARM64 was required.

| Stand-in | Effect |
|---|---|
| **PCSX2's recompilers (microVU and the IOP recompiler), labeled** (recommended) | A real game can reach full speed with only the EE static, so Phase 3 tests the architecture instead of waiting on the riskiest translator. Each static translator is then built against the recompiler it replaces, as its oracle. Every build report lists these CPUs as "not static: run-time JIT". |
| PCSX2's interpreters only | No JIT ever ships. VU-heavy games are expected to run far below full speed until the static VU translator is complete. PCSX2's VU interpreter speed is **unmeasured** here; the old project's own VU interpreter held Killzone to about 17 vblanks a second, against 50 once its VU code was compiled. |

Either way the target in section 9.1 is unchanged, and VU programs not seen
at build time run on whichever stand-in is approved. Section 15, item 7 says
what that target is worth on Windows and Linux alone.

### 9.3 Timing

Event tests at every branch, with PCSX2's interpreter's cycle accounting
(`Interpreter.cpp:29-62`, `217`). That keeps interrupt latency in the range
games were tested with. It also makes every branch target a resume point,
which limits optimization to roughly what a JIT does per block. A relaxed mode
(tests at back-edges and calls only) is a later, measured option. **Unproven:**
the cost of the exact mode.

### 9.4 Verification, designed to prevent debugging loops

The old project could only compare itself with itself. With PCSX2 linked in,
three independent oracles exist, each isolating one class of defect:

| Defect class | Oracle | Method |
|---|---|---|
| Instruction semantics | PCSX2's x86 JIT (what games were tuned against), cross-checked with its interpreter | Per-instruction differential fuzzing: random state and operands, one block, compare all registers and flags. Per clamp mode. |
| Translation (control flow, delay slots, resume points) | ps2n's fallback interpreter | Lockstep: run both on the same hardware model and compare full CPU state at every event test. Bit-exact; the first divergence names the block. |
| Hardware interaction and timing | Real hardware, and stock PCSX2 | ps2autotests (ISC license): 55 prebuilt test programs with 78 expected outputs recorded on consoles, covering EE, FPU, MMI, VU0 macro, VU, DMA, kernel. PCSX2 already ships a runner for it (`tests/run_test.pl`). Then frame hashes and audio dumps against stock PCSX2 with the same settings. |

A failing game is then bisected mechanically instead of by reading traces.

### 9.5 Upstream synchronization

- PCSX2 as a git submodule pinned to a nightly tag (`v2.9.170` to start).
  Nightlies are tagged `v2.9.N`.
- A patch queue, expected to be under 50 lines, applied at configure time. If
  it grows, that is a signal to stop and reassess.
- A scheduled job, in the ps2n repository only: check out the newest tag,
  apply the patches, build, run the adapter's offset assertions (API breaks),
  ps2autotests and the lockstep suite (regressions), and report the diff of
  the contract files (`R5900.h`, `R3000A.h`, `VUmicro.h`, `vtlb.h`,
  `VMManager.h`, `Host.h`, `Config.h`) and of `GameIndex.yaml`. A human
  approves each bump. Nothing merges on its own.
- **PCSX2's `AGENTS.md` forbids AI-generated contributions from new
  contributors and any automated posting to its GitHub.** So no patch of ours
  can be planned as "upstream it later", and no tooling may open issues or
  pull requests there. The patch queue is ours to carry.

## 10. What to retain from `../ps2-uncompiler`

| Keep, retargeted to PCSX2's state and memory | Retire (PCSX2 replaces it) |
|---|---|
| Instruction tables, decoder, disassembler, assembler (`core/`) and their round-trip tests | Software GS rasterizer and Vulkan renderer (`runtime/src/hw/gs*`, `gpu/`) |
| ELF, IRX and SNR2 readers | DMAC, VIF, IPU, EE register models (`runtime/src/hw/`) |
| Function discovery, jump tables, resume points (`recompiler/src/analysis.cpp`) | EE kernel HLE (`runtime/src/kernel/`) |
| C++ emission and the execution model (`codegen.cpp`, `recomp.h`) | IOP kernel and library HLE, services, CDVD, memory cards, pad, SPU2, libsd (`runtime/src/iop/`) |
| Semantics headers (`ops_*.h`), **after** equivalence testing against PCSX2 | Clock, GS thread, VU1 thread (PCSX2's limiter, MTGS, MTVU) |
| Fallback interpreter and dispatcher: content checksums, code epochs, recording of interpreted entries | Window, input manager, overlay and settings pages, audio output |
| VU pair decoding, pipeline model and generator (`vu_micro.h`, `vu_codegen.cpp`), **after** equivalence testing against PCSX2's VU interpreter | Libc stubs and per-game configs as a compatibility mechanism |
| Config format, pnach parser for build-time code patches | The website (`docs/_layouts`, `docs/assets`, `docs/index.md`) |
| Assembly test programs, ps2sdk test programs, the test harness, the "plant a bug and see it caught" practice | Tester telemetry (`axiom.cpp`), unless you want it back |
| Ghidra exporter (`tools/ghidra`), CMake helpers | |
| Branding (`assets/branding/`, `ps2n.rc`), the name, the abbreviation | |
| The habit of reporting every gap (`PS2_UNIMPL`, `PS2_FALLBACK`) | |

Roughly 13,000 lines carry over and 26,000 retire, not counting tests. The
retired code stays in the old repository, untouched, as an archive and as a
record of hardware behavior learned the hard way. The new repository starts
clean and imports what it keeps.

## 11. Licensing

- **PCSX2's own code** (SPDX identifiers in `pcsx2/`, `common/`, frontends,
  tests): 1,007 files GPL-3.0+, 13 GPL-2.0+, 12 "GPL-3.0" without the plus
  (the ARM64 files), 2 GPL-3.0-only (a USB wheel driver), 3 MIT, 1
  BSD-3-Clause; 10 source files carry no identifier.
- **Its vendored libraries:** MIT, BSD, zlib, ISC, Apache-2.0, public domain,
  LGPL (SoundTouch), GPL-2.0+ (FreeSurround), GPL or LGPL (the demangler), SIL
  OFL (a font). All compatible with GPL version 3. Not verified: the terms of
  Microsoft's WinPixEventRuntime binaries (Windows, Direct3D 12 debugging) and
  of the libraries PCSX2 takes from its prebuilt dependency package (SDL3,
  FFmpeg, shaderc and others). Audit those when the build is set up.
- **PS2Native as GPL-3.0-or-later is compatible.** A binary that includes the
  GPL-3.0-only files (any ARM64 build) is distributed under GPL-3.0.
- **Obligations for PS2Native's own releases:** ship or offer complete
  corresponding source, including the pinned PCSX2 revision and the patch
  queue; keep copyright notices and license texts; state changes.
- **Generated games cannot be redistributed, by anyone.** Each contains a
  translation of the game's executable (the publisher's copyright) and, once
  the kernel is translated, of the BIOS (Sony's), linked with GPL code. The
  GPL places no conditions on works a user builds and runs privately, so
  personal use is clean. Distribution would infringe the game's copyright and
  could not satisfy the GPL. The wizard should say so at the packaging step.
- **PS2Native distributes no BIOS and no game data.** `patches.zip` is
  PCSX2's patch collection, fetched from its releases as PCSX2's own builds
  do; check that repository's license before bundling it.
- **Reference projects:** AnyPS5 is GPL-2.0-only, so none of its code may be
  used. PS2 Web Recomp is MIT and PS2Recomp GPL-3.0; both may be used with
  attribution. ps2autotests is ISC.

## 12. Platforms

Priorities per [ADR 0001](adr/0001-platform-priorities.md): Windows x64 and
Linux x64 are required. Everything else is optional and may be dropped where
PCSX2's own support is lacking.

| Tier | Target | PCSX2 core | Static CPUs | Notes |
|---|---|---|---|---|
| Required | Windows x64 | Supported upstream | Parity expected, unproven | Development platform; toolchain present |
| Required | Linux x64 | Supported upstream; built with clang in its CI | Same | Upstream keeps a Qt-free dependency script (`.github/workflows/scripts/linux/build-dependencies-runner.sh`) and Linux window code in `pcsx2-gsrunner/Main.cpp`. The old project never built here, and **this machine has no Linux environment** (WSL is not installed) |
| Optional | macOS ARM64 | Compiled by upstream CI, not released, flagged "unsupported configuration"; GS and Metal are native | The one target where static code would beat PCSX2 | FP equivalence with x86 (denormal inputs, min/max with NaN) would need explicit handling |
| Optional | macOS x64 | Supported upstream | Same as the required targets | Reported: Intel Macs do not get macOS 27 |
| Optional | Android, iOS | No upstream host | Possible later | iOS forbids JIT: the GS software renderer's code generator and the VIF unpack recompiler would need their non-JIT paths |

What it changes that both required targets are x86-64:

- **The semantics library can mirror PCSX2's JIT operation for operation.**
  The JIT's FPU and VU behavior is defined by SSE operations and the MXCSR
  register. On x86-64 the library can use the same operations, so equivalence
  is transcription plus testing, not re-deriving x86 behavior on another
  architecture. The hardest part of risk 2 goes away.
- **PCSX2's recompilers exist on every required platform**, which opens the
  stand-in choice in section 9.2.
- **Portability stays a design rule, not a deliverable.** Generated code
  remains portable C++ that calls the semantics library; everything
  x86-specific stays inside that library and the adapter. A later ARM64 port
  would replace one library and re-run the fuzzer against recorded x86-64
  results. Nothing is built or tested for ARM64 until you ask for it.
- **Linux has to be exercised from the proof of concept on**, or it will
  repeat the old project's "never built". That needs a Linux environment
  (section 16, question 6).

## 13. Risks and tradeoffs

| # | Risk | Severity | Mitigation or test |
|--:|---|---|---|
| 1 | The result does not match your idea of "native": the game carries PCSX2's hardware model and needs a BIOS | Project-defining | Decide before Phase 2 (question 1) |
| 2 | Semantic equivalence with PCSX2's JIT, per clamp mode (x86-64 only while ARM64 is optional) | High | Mirror the JIT's SSE operations; differential fuzzing before any game work. This is the largest custom component |
| 3 | VU translation: program identity, pipeline state explosion, speed against microVU | High | Keep it off the critical path: run the VUs on a PCSX2 stand-in first (9.2); port the old generator; measure against microVU before committing |
| 4 | Interrupts and thread switches through the real kernel with resume points | High, untested | First thing the proof of concept proves |
| 5 | Timing differences from PCSX2's JIT change behavior in sensitive games | Medium | Event tests at every branch; compare against stock PCSX2 |
| 6 | Unseen code: overlays, kernel patches, packed code | Medium | Fallback, capture, rebuild; coverage in the build report |
| 7 | Performance below PCSX2 on x86-64 (memory access path, block-level optimization only) | Medium | Measure in the proof of concept; parity is the stated bar |
| 8 | Upstream changes break the build or change behavior | Medium | Pinned tag, adapter assertions, scheduled sync job with the regression suite |
| 9 | The monolithic core brings unused subsystems into every game | Low to medium | Accept at first; trim with link-time removal and build options; do not maintain a source-level extraction |
| 10 | End users need a C++ compiler for the wizard | Medium, Phase 4 | Bundle clang and lld (Apache-2.0 with LLVM exception), or compile game code freestanding into a module loaded by a prebuilt runtime |
| 11 | The project's standing with PCSX2's maintainers, given their AI policy | Low, non-technical | Never file ps2n defects upstream; never automate anything on their GitHub |
| 12 | Scope: four CPUs' worth of translators plus a wizard, for one maintainer | High | Strict staging; each stage ships something that runs |
| 13 | Linux is required but cannot be built or run on the development machine today | Medium | Choose a Linux environment (question 6); build and run the headless tests there from the proof of concept on |

## 14. Proposed proof of concept (Phase 2)

Goal: prove risks 4, 5, 6 and the method for risk 2, on legally usable
programs, with no wizard, no packaging and no commercial game.

| Step | Work | Pass criterion |
|---|---|---|
| P0 Baseline | Build the pinned PCSX2 core with a minimal host derived from `pcsx2-gsrunner`. Boot ps2autotests programs and the old project's ps2sdk test programs with stock PCSX2 (JIT and interpreter). Record console output, frame hashes and which tests stock PCSX2 passes. | Stock results reproducible. Patch queue under 50 lines. |
| P1 Static EE | Port the old recompiler front end; emit C++ against the adapter; EE provider, dispatcher, fallback interpreter. BIOS kernel on the fallback interpreter; IOP and VUs on PCSX2's own providers (9.2). | ps2autotests `cpu/ee`, `cpu/ee_fpu`, `cpu/ee_simd`, `cpu/vu0_macro`: same output as stock PCSX2, and as the console wherever stock PCSX2 matches it. |
| P2 Hard problems | (a) Interrupts, alarms and thread switches through the real kernel: ps2autotests `kernel/ee`, `dma/*`, and the ps2sdk pad, memory card, CDVD and IOP RPC programs. (b) Lockstep comparison against the fallback interpreter over whole runs. (c) A program that overwrites its own code: detected through `Clear`, interpreted, captured, rebuilt, then fully static. (d) Differential fuzzing of the FPU and MMI semantics against PCSX2's JIT in the default clamp mode and one other. | All outputs equal stock PCSX2's. Lockstep bit-exact. Zero interpreted game-code addresses after one rebuild. Fuzzer finds no divergence, or every divergence is explained and fixed. |
| P3 Graphics path | The ps2sdk VU program and ps2autotests `vu/*` and `gs/*`, drawn through PCSX2's GS. | Frame hashes equal stock PCSX2's software renderer; the Vulkan renderer runs at 1x and 3x. |
| P4 Measurement | A CPU-bound EE loop: static code, PCSX2 JIT, PCSX2 interpreter, same machine. | Numbers reported as measured, whatever they are. |
| P5 Linux | Build the same proof of concept on Linux x64 with upstream's Qt-free dependency script and run P1 to P3 headless. | Same outputs as on Windows. |

The pass criterion of P1 in this table was replaced on 2026-11-10 by "never
further from the console than stock PCSX2, and every remaining difference
listed with its cause": [ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md).
What each step came to is in [poc-log.md](poc-log.md).

Stop conditions, any of which sends the design back for review: the patch
queue outgrows a short hook; kernel-driven thread switching cannot be made
correct with resume points; lockstep cannot be made deterministic; static EE
code is slower than PCSX2's interpreter.

Needs for P0: PCSX2's Windows dependency package (a download of several
hundred megabytes from PCSX2's GitHub releases), a local build of the core,
and one of your BIOS dumps. Visual Studio Build Tools 2026 with clang-cl,
CMake, Ninja and the Vulkan SDK are already on this machine.

Needs for P5: a Linux environment, which this machine does not have. The
choices are WSL 2 (an install that needs administrator rights and a restart;
enough for building and headless tests, weak for GPU rendering), a Linux
machine of yours, or CI once the GitHub repository exists.

## 15. Where this assessment challenges the brief

1. **"Minimize hardware emulation" versus "maximum compatibility".** On the
   PS2 these are opposed, and the ranked priorities settle it in favor of
   compatibility. The recommendation minimizes *CPU* emulation only.
2. **"Do not simply bundle a game with a customized PCSX2 emulator."** The
   recommended game is static code plus PCSX2's hardware model. It differs
   from a bundled emulator in what executes the game's code (no JIT, no
   interpreter for covered code), in having no emulator interface, and in
   being built per game. It does not differ in how the hardware is modeled.
3. **"If unnecessary, do not require a BIOS."** Under the recommendation a
   BIOS is always necessary.
4. **"Never substitute interpretation for static recompilation."** No general
   tool can honor this absolutely, because some code does not exist until the
   game runs. The honest form is: interpretation only for unseen code,
   reported, and removed by the next build.
5. **"All configuration is finalized during compilation."** Achievable. Some
   settings change the generated code (clamp modes, cycle rate, patches);
   others (renderer, resolution, bindings) are baked data the game cannot
   change. Both are fixed at build time.
6. **"Native recompilation" as a quality lever.** The old project's gap to
   PCSX2 in speed, looks and compatibility came from its hardware model, not
   from how CPU code was executed. Static recompilation does not close that
   gap by itself; reusing PCSX2's hardware model does.
7. **Static VU and IOP translation on Windows and Linux.** With those as the
   only required platforms, PCSX2's recompilers for the VUs and the IOP are
   always available, and games were tuned against them. Replacing them with
   static translations would add no compatibility and probably no speed
   there. Their value would be the principle (no JIT inside a generated game)
   and the optional platforms. I recommend keeping them on the roadmap behind
   the EE and the kernel, and deciding after Phase 3, with measurements,
   whether to build them.

## 16. Decisions

Made (2026-11-10):

- **Platforms** ([ADR 0001](adr/0001-platform-priorities.md)): Windows x64
  and Linux x64 are required; every other target is optional.
- **Architecture** ([ADR 0002](adr/0002-static-cpus-on-pcsx2-core.md)):
  question 1, approved for the proof of concept.
- **No JIT in generated games** ([ADR 0003](adr/0003-no-jit-in-generated-games.md)):
  questions 2 and 3. PCSX2's VU and IOP recompilers are temporary stand-ins
  only. This overrides item 7 of section 15: static VU and IOP translators
  are required, not optional.
- **Proof-of-concept environment** ([ADR 0004](adr/0004-proof-of-concept-environment.md)):
  questions 4 (the `SCPH-70004` dump), 5 and 6 (the maintainer's own Linux
  machine).

Made at the end of the proof of concept on Windows (2026-11-10, recorded
2026-11-11):

- **What counts as correct** ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)):
  never further from the console than stock PCSX2, and every remaining
  difference listed with its cause. This replaces the pass criterion of step
  P1 in section 14.
- **Scope of the no-JIT rule** ([ADR 0006](adr/0006-no-jit-rule-covers-ps2-code-only.md)):
  PS2 program code only. PCSX2's VIF unpack recompiler and its software
  renderer's code generator stay.
- **Translated BIOS code** ([ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md)):
  question 4, second half. The BIOS's EE code is to be translated ahead of
  time, on the user's machine, from the user's dump, as a file of its own.
- **The first Linux run** ([ADR 0008](adr/0008-linux-run-deferred.md)):
  deferred. Step P5 stays open.

What is open now is at the end of [poc-log.md](poc-log.md).

The questions as they were asked before Phase 2:

1. **Architecture.** Do you accept option C (static CPUs, PCSX2's hardware
   model, mandatory BIOS) as PS2Native's definition of "native", with D as the
   roadmap, knowing that on Windows and Linux the result is parity with PCSX2
   in a standalone form? If hardware emulation must be smaller than that, the
   alternative is B or A and their compatibility ceiling.
2. **Unseen EE code.** Interpreter with capture and rebuild (recommended), or
   stop with a diagnostic?
3. **Stand-ins for the VUs and the IOP** until their static translators
   exist: PCSX2's recompilers, labeled (recommended), or PCSX2's interpreters
   only? The order of work stays EE, then the BIOS kernel, then VU, then IOP.
4. **BIOS.** Required at build and run time. May the proof of concept read a
   dump from `Documents\PCSX2\bios` in place? I suggest `SCPH-70004` (v2.00,
   PAL): the newest kernel of the three there and the region of your games.
   Is it acceptable that a generated game will later contain translated BIOS
   code, for personal use only?
5. **Proof of concept setup on Windows.** May I add PCSX2 `v2.9.170` as a
   pinned submodule, download its Windows dependency package, build its core
   locally, and use your installed PCSX2 as the reference?
6. **Linux environment.** Where should Linux builds and tests run: WSL 2 on
   this machine, a Linux machine of yours, or CI after you recreate the
   GitHub repository?
7. **Upstream and repository.** Carry a minimal patch queue and never
   contribute AI-written code upstream; start `ps2n` clean and import the
   retained components, leaving `../ps2-uncompiler` untouched. I will treat
   both as agreed unless you object.

Later, not blocking: the first real game for Phase 3. Killzone (SCES-52004)
is the natural candidate, since you have a PCSX2 comparison and a history
with it.
