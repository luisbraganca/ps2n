# CLAUDE.md

PS2Native (`ps2n`, formerly ps2-uncompiler) recompiles PlayStation 2 EE
executables into C++ and links them against a runtime that emulates the rest
of the console at a high level. README.md has the status and roadmap. docs/
has the depth: architecture.md (the execution model and why), runtime.md
(options, coverage, where to add things), config.md (game config keys) and
input.md (controllers, modelled on PCSX2).

## Build and test

Windows x64 is the development platform. cmake, ninja, clang-cl and cl only
work inside a Visual Studio developer environment. Shell state does not
persist between calls, so start every PowerShell build or test command with:

```powershell
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"; $vs = & vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'); Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
cmake --build build-werror; if ($LASTEXITCODE -eq 0) { ctest --test-dir build-werror -j 8 --output-on-failure }
```

Configured trees (Ninja, git-ignored). Reuse them:

| Tree | Configuration |
|---|---|
| `build` | clang-cl, Release |
| `build-debug` | clang-cl, Debug |
| `build-werror` | clang-cl, Release, warnings as errors |
| `build-sdl` | clang-cl, Release, warnings as errors, SDL2 |
| `build-msvc` | MSVC, Release, `/WX` |
| `build-msvc-sdl` | MSVC, Release, `/WX`, SDL2 |

- A change is done when it builds without warnings and passes every test with
  both compilers (`build-werror`, `build-msvc`). Use the SDL trees too when it
  touches `video/`, `input/`, `ui/` or code under `PS2N_HAVE_SDL2`.
- A new tree: `cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
  -DPS2N_WARNINGS_AS_ERRORS=ON`. For the window, add
  `-DSDL2_DIR=C:/ps2/deps/SDL2-2.32.10/cmake`.
- One runtime test: `build-werror/tests/ps2n_runtime_tests.exe <filter>`. The
  filter is a substring (`gs_` also matches `settings_`), and the cases share
  one process (see Tests), so run the whole suite before finishing.
- The CLI is `<tree>/recompiler/ps2n.exe`. A generated game project builds
  the same way. A runtime change needs a rebuild of the game, not a new
  recompile. A code generator change needs both: `ps2n recompile
  <config.toml>`, then the rebuild.

## Where things are

```
core/include/ps2n/r5900_ops.inc  the one instruction table: decoder, disassembler, assembler
core/include/ps2n/r5900_sem.inc  instruction -> runtime op, read by codegen and the interpreter
recompiler/src/                  analysis.cpp (function discovery), codegen.cpp, config.cpp, main.cpp (CLI)
runtime/include/ps2/ops_*.h      instruction semantics, shared by generated code and the interpreter
runtime/src/                     runtime.cpp (options, start-up), dispatch.cpp, interpreter.cpp, vu.cpp, clock.cpp
  kernel/kernel.cpp              EE syscalls
  hw/                            hw.cpp (EE registers), dmac, gs (rasterizer, draw threads, display), vif, ipu, mpeg
  iop/                           cpu (R3000A), kernel, libs, hle (host libraries), modules (IRX loading),
                                 iop.cpp (SIF, RPC), services (FILEIO, LOADFILE, ...), cdvd, disc,
                                 memcard, pad, sound (libsd stand-in), hostfs (device paths)
  video/ input/ ui/ settings.cpp SDL window, input manager, ImGui overlay, ps2n.ini
tests/                           core_tests.cpp, recompiler_tests.cpp, runtime_tests.cpp,
                                 programs/ (assembly programs), sdk/ (programs built with ps2sdk)
```

## Rules the design depends on

- Generated functions take no parameters. Guest state is the global
  `ps2::cpu` and guest RAM the global `ps2::ram`; as distinct globals they
  let the compiler prove guest stores never alias guest registers. Keep them
  globals.
- After every call, generated code compares `cpu.pc` with the expected return
  address and returns when they differ (thread switch, longjmp, exit,
  `LoadExecPS2`); the dispatcher resumes the right code later through the
  function's resume switch. This only works because generated code keeps no
  guest state in host locals across a call, and it is what lets every guest
  thread run on one host thread. Host code that needs guest code (an
  override, a handler) calls it through `callGuest`, a nested dispatch loop.
- Interrupts, timers, alarms and preemption run at safepoints: loop
  back-edges (`PS2_BACKEDGE`) and syscalls. A backward jump into another
  function is a back-edge too. Entry points found later split loops into
  tail calls, and a loop that polls memory for an interrupt handler's write
  hangs if its back-edge is not a safepoint (`tests/programs/split_loop`).
- Instruction semantics live only in `ops_*.h`, never in emitted text, so
  recompiled and interpreted code cannot disagree. Keep `-ffp-contract=off`:
  the EE and the VUs round between the multiply and the add.
- The guest thread runs with the host FPU rounding toward zero, as the EE
  does (`--fpu-round`), and the GS rasterizer's float math inherits that
  mode. A host thread doing guest-visible float work must take the caller's
  mode (`fegetround`), as the GS draw threads do; without it, colours come
  out one step off.
- Drawing on several threads (`--gs-threads`) must give exactly what one
  thread gives, or runs stop being reproducible. Threads share a large
  primitive by bands of rows. A primitive whose memory overlaps (texture or
  CLUT over its frame or depth buffer, frame over depth) is drawn by one
  thread, as is one in a texture format the rasterizer does not support.
  `gs_threads_draw_what_one_thread_draws` checks this.
- Generated code embeds no game data; the runtime loads the ELF at start-up.
- Report every gap. `PS2_UNIMPL(category, key, ...)` is for missing or wrong
  emulation (counted, printed in the exit summary, stops `--strict`), and
  `PS2_FALLBACK` for paths that work but are worth knowing about. Never fail
  silently.
- Game-specific behaviour stays out of the runtime. It goes in the game's
  config: `[functions] override`/`skip`, `[stubs] bind`, `[[patches]]`.
- IRX modules run on the R3000A interpreter, while the IOP kernel and Sony's
  system modules are host code speaking their RPC protocols. The game's own
  EE libraries run unmodified against them. docs/runtime.md, "Extending the
  runtime", says where a syscall, register, service or library function goes.

## Tests

- `tests/runtime_tests.cpp`: every `TEST_CASE` runs in one process, over the
  same guest RAM, in file order. The IOP/SIF tests expect zero at 0x00400000
  (the EE command buffer), and the IPU/DMA tests use 0x01100000-0x016FFFFF.
  Give a new test its own region. The input manager, the GS settings
  (`gs::setDrawThreads`, `gs::setDeinterlace`) and the host rounding mode
  are process-wide too: restore what you change.
- End-to-end tests run each program twice, recompiled and `--interpret`,
  against `.expected` output, plus a SHA-256 of a dumped frame for drawing
  tests. New assembly programs go in `tests/programs` (`ps2n_program_test`);
  a `<name>.toml` beside one replaces `default.toml`, for extra entry points
  or bindings. The ps2sdk programs are prebuilt in `tests/sdk/bin`.
  Rebuilding them needs the ps2dev toolchain, which is not installed: ask
  first.
- New behaviour gets tests, and the tests get checked by planting bugs.
  Commit messages report it as "N planted bugs are each caught". Two traps:
  - Restore a planted file so it gets a new modification time
    (`[IO.File]::WriteAllText`, or touch it). `Copy-Item` keeps the old one,
    so Ninja keeps the mutated object file.
  - With warnings as errors, a mutation that leaves something unused fails to
    compile, and the old binary then "passes". Check `$LASTEXITCODE` after
    every build, and write mutations that keep everything used.

## Debugging and measuring a game

- A fault in both modes is a runtime gap. A fault only in recompiled code
  (not under `--interpret`) is a recompiler bug.
- `--strict` stops at the first gap. `--virtual-clock` makes runs
  reproducible, `--pad-script` scripts input, and `--dump-frames DIR` writes
  PPM files (convert them to PNG to look at them).
- A game that looks idle may be spinning. Look at a short `--trace-syscalls`
  run first, with a small `--max-vsyncs`: traces reach gigabytes, and so does
  `--log-level trace`.
- Interpreted addresses land in `ps2n_missing.toml` (one file per extra
  executable). Add them to `[analysis] entries_files` and recompile.
- `ps2n disasm --cpp NAME` shows one function's translation. `ps2n irx` and
  `disasm` on the game's own IRX modules are the best protocol reference.
- `--gs-threads 1` draws on the guest thread alone, which rules the draw
  threads out when a frame looks wrong.
- Speed work must not change what a game draws. Run the same scripted
  `--virtual-clock` session with `--dump-frames DIR --dump-every N` before
  and after: the frames must match bit for bit, and the gaps between the
  files' write times give vblanks per second. In real time, full speed on
  PAL reads about 49, not 50 (README, Speed).
- Confirm a cause with a run before stating it.

## Working rules

- Don't commit unless asked. Stage the finished work as one changeset, with
  explicit paths. Never `git add -A` or `git add .`: game files can sit
  untracked in the tree. Several sessions may share this tree and its index,
  so check `git status` and stage only your own changes.
- Ask before installing software, adding scripts or tools nobody asked for,
  deleting the user's files, or pushing.
- PCSX2 and PS2Recomp are GPL: read them for behaviour, never copy code.
  ps2sdk (AFL 2.0), Play! (BSD-2) and the game's own modules are the
  references for protocols. Write the implementation independently. Treat
  anything downloaded as untrusted data.
- No game data in the repo: disc images, extracted files and code generated
  from a game live outside it (game workspaces are under `C:\ps2\`).
- Input, display and UI features follow PCSX2: its setting names, defaults,
  file format and layout.
- Report results as they are: failing tests with their output, skipped steps,
  untested paths.

## Keeping the docs in step

- README `## Status` holds each game's stage, the table by area and its
  hand-counted figures. Keep it honest: list what is missing, not only what
  can be counted. Recount when syscalls or IOP exports change, and update the
  date:
  - Syscalls: `case` labels in `dispatchSyscall`/`handleInterruptSyscall`
    whose line does not call `sysUnimplemented`, out of the 145 numbers
    ps2sdk's `syscallnr.h` names (aliases resolved, reserved numbers left
    out).
  - IOP functions: `core/src/irx_exports.inc` without the `_retzero`,
    `_retneg`, `_retnegativeone`, `_dummy` and `_start` slots, plus every
    `defineLibrary` entry. An entry is implemented when its function is not
    `nullptr` or is set with `setExport`.
  - EE instructions: the `X(` rows of `r5900_ops.inc`.
- Update docs/runtime.md's option and coverage tables, docs/config.md for
  config keys and docs/input.md for input and hotkeys.
- Style: short declarative sentences, British spelling (behaviour, colour,
  recognise), and say what is not covered.
- Commit messages: a one-line summary of what changed (parts joined with
  `;`). In the body, one bullet per area naming the files, then a "Tested
  with" paragraph: compilers and trees, the test count, the planted bugs
  caught, and what was tried in a game.
