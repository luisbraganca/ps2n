# CLAUDE.md

PS2Native (`ps2n`) converts PlayStation 2 games into native applications by
ahead-of-time static recompilation. This repository is a reboot of
`../ps2-uncompiler`, which stays untouched as an archive.

## Read first

- [docs/brief.md](docs/brief.md): the maintainer's brief, verbatim. It is
  authoritative. Its section 11 (AI development rules) applies to every
  session. If an implementation idea conflicts with it, say so; do not bend
  the goals.
- [docs/phase1-feasibility.md](docs/phase1-feasibility.md): the Phase 1
  assessment, with source references into PCSX2, the old project and the
  reference projects.
- [docs/execution-model.md](docs/execution-model.md): how compiled code, the
  fallback interpreter and the PCSX2 core run a program together. Read it
  before touching `runtime/` or `recompiler/`: how time is counted (exactly
  as PCSX2's EE recompiler counts it, by the blocks it would have formed) is
  a contract between the two.

## Status

**Phase 3 (the first real game: Killzone) was approved on 2026-11-11 and is
in progress.** [docs/game-log.md](docs/game-log.md) has its steps, their pass
criteria and what has been measured; its status table at the top says where
things stand. On 2026-11-12 every step was done as far as the game's first
level, the maintainer had played it, and the compiled game took 1.35 times
stock PCSX2's time. The maintainer then chose a speed step inside the phase
(G6, ADR 0015), which ended on 2026-11-12 with the compiled game at 1.21 to
1.23 times stock PCSX2's time. The next step, G7 (ADR 0016), was done on
2026-11-12: the game's VU0 instructions have semantics of PS2Native's own,
which the self-test holds to PCSX2's recompiler, and the level run is still
the stock run, at 1.21 times its time. G9 (ADR 0018), done on 2026-11-12
in a worktree of its own, has compiled code carry out the VU0 instructions
itself where their block lets it: 1.19 times stock PCSX2's time. G8 (ADR
0017), done on 2026-11-12 and rebased onto G9, compiles the BIOS's EE code
ahead of time, from the dump the build is given: 51 of its instructions are
still interpreted, at boot, and the level run takes 1.18 times stock
PCSX2's time. G10 (ADR 0019), performance as the maintainer's research lays
it out, is under way, in the worktree `..\ps2n-vu0` (branch `perf`) and in
the main tree beside it (ADR 0021): profile-guided optimization of the
generated code, loops that have reached a fixed point skipped, the COP2
instructions carried out by compiled code, calls through a register made
by compiled code itself, and the way a load or store seldom takes made
short (the game's code 14% smaller). All of it together, with
profile-guided optimization, takes the level run to 1.04 times stock
PCSX2's time (play alone 1.04), still the stock run. On 2026-11-13 a third
session, in the main tree, measured the website's two speed charts again
(the first 4,200 frames 1.09 times stock PCSX2's time with nothing drawn,
from 1.35) and made `lq` and `sq` loads and stores on the locals and a block
start's call of the runtime one of one argument: the level run the same,
1.04, the host 12 MB smaller. The same day, at the maintainer's word, the
FPU's arithmetic: its registers held in SSE registers, and the bits its
additions keep past the guard bit looked up instead of branched to. With
profile-guided optimization the level run takes 1.01 times stock PCSX2's
time, its play as long as stock PCSX2's, still the stock run. Phase 4 needs
its own approval.

Phase 2 (proof of concept) is done on Windows; its Linux step (P5) is
deferred by the maintainer. Its steps and criteria are in section 14 of the
assessment, and [docs/poc-log.md](docs/poc-log.md) records what was done and
measured. Its last section lists the questions that are open, those waiting
for the maintainer first. Record every decision the maintainer makes as an
architecture decision record under `docs/adr/`.

The game is the maintainer's own copy, used where it is: the disc image and
the files extracted from it are in `C:\ps2\killzone`. Read them, never copy
them, and keep everything generated from them (C++, captures, pictures) in
the git-ignored build tree.

Decisions:

- [ADR 0001](docs/adr/0001-platform-priorities.md): Windows x64 and Linux x64
  are required; macOS, Android and iOS are optional. Build and test nothing
  for an optional platform unless asked.
- [ADR 0002](docs/adr/0002-static-cpus-on-pcsx2-core.md): the game's code is
  translated ahead of time and runs on PCSX2's core as the machine. A BIOS is
  required.
- [ADR 0003](docs/adr/0003-no-jit-in-generated-games.md): no recompiler for
  PS2 code ships in a generated game. PCSX2's VU and IOP recompilers are
  temporary, labeled stand-ins; its EE recompiler is never one. Unseen code
  is interpreted once, reported, captured and rebuilt.
- [ADR 0004](docs/adr/0004-proof-of-concept-environment.md): PCSX2 `v2.9.170`
  as a submodule; the `SCPH-70004` BIOS read in place; Linux is built and
  tested by the maintainer on their own machine, so never report Linux as
  working without their run.
- [ADR 0005](docs/adr/0005-never-further-from-the-console-than-pcsx2.md):
  what counts as correct. On no line of a console-recorded test may stock
  PCSX2 be right and PS2Native wrong, and every remaining difference is
  listed with its cause. Instruction semantics follow PCSX2's recompiler,
  and the console where a recording shows the recompiler wrong.
- [ADR 0006](docs/adr/0006-no-jit-rule-covers-ps2-code-only.md): the no-JIT
  rule covers PS2 program code only. PCSX2's VIF unpack recompiler and its
  software renderer's code generator stay.
- [ADR 0007](docs/adr/0007-bios-code-translated-ahead-of-time.md): the
  BIOS's EE code is to be translated ahead of time, on the user's machine
  from their own dump, as a file of its own beside the game. A BIOS image is
  still required at run time. Begun on 2026-11-12 as step G8 (ADR 0017).
- [ADR 0008](docs/adr/0008-linux-run-deferred.md): the first Linux run is
  deferred until the maintainer chooses. Linux is still required.
- [ADR 0009](docs/adr/0009-phase-3-first-game-killzone.md): Phase 3 is
  approved; the first game is Killzone (`SCES-52004`).
- [ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md):
  the fix for PCSX2's VU0 status-flag bug is offered to PCSX2 by the
  maintainer, as an issue with the correction they suggest; PS2Native
  carries it in its patch queue only if that stalls (30 days after the
  issue is opened with no reply from a PCSX2 maintainer). The issue's text
  and the correction are the maintainer's to write, not a session's. The
  two issues, PCSX2's numbers 15185 and 15186, were opened on 2026-11-11.
  15186 was answered within the hour: the defect and its correction were
  confirmed there. 15185 had no reply on 2026-11-12: from 2026-12-11, look
  by reading only whether it has one, and tell the maintainer. PCSX2's team
  asked there that no LLM-generated issues be sent.
- [ADR 0011](docs/adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md):
  time is counted exactly as PCSX2's EE recompiler counts it. A run on
  PS2Native is the stock run, sync point for sync point; a difference is a
  defect to find, unless it comes from an instruction that deliberately
  follows the console (ADR 0005) and is listed with its cause.
- [ADR 0012](docs/adr/0012-ps2native-branding-not-pcsx2s.md): what PS2Native
  builds carries PS2Native's branding (`assets/branding`) and never PCSX2's
  logo; PS2Native's logo is not put on PCSX2's interface either. PCSX2's
  name stays wherever it is attribution: logs, license texts, copyright
  headers, documentation. A finished game shows nothing of what the core can
  draw over the picture (messages, performance figures, indicators): that is
  switched off by settings, never restyled by patching PCSX2. The host's
  window does all of this (`ps2n-host -window`).
- [ADR 0013](docs/adr/0013-pcsx2s-count-register-carry-mirrored-until-fixed.md):
  a defect of PCSX2's recompiler (reading the count register carries into
  `EntryHi`) is offered upstream the same way, as an issue of its own, and
  reproduced by PS2Native until the pinned PCSX2 has the correction, as
  long as it costs nothing in speed and changes nothing on screen. (Had the
  issue been ignored for 30 days, the correction would have been carried
  locally; it was confirmed instead.) `tests/programs/countcarry.s` shows
  it.
- [ADR 0014](docs/adr/0014-the-website-is-served-from-docs.md): the project
  has a website, one page that GitHub Pages serves from `docs/` as it is
  (`docs/.nojekyll`). The documentation stays where it is; the site is
  `docs/index.html`, `docs/404.html` and `docs/site/`. The page says only
  what the logs say, with dates, and shows nothing from a game or a BIOS.
- [ADR 0015](docs/adr/0015-a-speed-step-before-phase-3-ends.md): a speed
  step inside Phase 3 (G6) before the phase ends. The generated code is
  made faster one change at a time, each measured on the level run and
  with the profile; the compiled game stays the stock run. What comes
  after the two changes named is decided by the profile and reported; a
  change of another kind (to how time is counted, to what is compared with
  stock PCSX2) is put to the maintainer first.
- [ADR 0016](docs/adr/0016-vu0-instructions-with-semantics-of-ps2natives-own.md):
  the speed step ended where it stood on 2026-11-12. The next step, G7:
  the VU0 instructions in EE code carried out by semantics of PS2Native's
  own, following PCSX2's recompiler (microVU in macro mode) as ADR 0005
  has it, in place of PCSX2's interpreter functions. What the recompiler
  itself hands to a function of the core (`VCALLMS`, `VCALLMSR`) stays
  that function; when VU0 is brought up to date, and which flags reach the
  registers, stay as the recompiler decides them for the block. Done on
  2026-11-12 (`runtime/include/ps2n/ops_vu0.h`).
- [ADR 0017](docs/adr/0017-bios-ee-code-compiled-as-step-g8.md): the next
  step, G8, inside Phase 3: the BIOS's EE code compiled ahead of time, as
  ADR 0007 has it, beginning with the four pieces that run under fast boot
  (the reset code, the kernel, `EELOAD`, `EENULL`). During development the
  translation is linked into `ps2n-host` from the build tree, made from the
  dump the build is given and never in the repository; a file of its own
  beside a packaged game is Phase 4's. Done on 2026-11-12: four images,
  and the code the game copies into kernel memory compiled with the game
  (`game_outside`).
- [ADR 0018](docs/adr/0018-vu0-instructions-carried-out-by-compiled-code.md):
  step G9: compiled code carries out a VU0 instruction itself, inline, where
  the runtime's record of its block says nothing is done about VU0 first
  (`PS2N_VU0`); elsewhere the runtime as before. Made in a worktree of its
  own (`..\ps2n-vu0`, branch `vu0-direct`) while another session worked on
  G8 in the main tree; the two sessions agree before timing anything, and
  whichever step is committed second is rebased onto the first. Done on
  2026-11-12.
- [ADR 0019](docs/adr/0019-performance-as-the-research-lays-it-out.md):
  step G10, inside Phase 3: performance as the maintainer's research lays
  it out. Gameplay measured first (startup apart from play, nothing drawn
  and the hardware renderer, profiled; `ps2n-host -mark`, `-profile-from`),
  then the expensive paths one change at a time, each measured against
  stock PCSX2 timed in turn, profile-guided and link-time optimization
  evaluated. What would change how time is counted, or what is compared
  with stock PCSX2, goes to the maintainer first. Under way: profile-guided
  optimization (`build.ps1 -Pgo`), loops that have reached a fixed point
  (`PS2N_BLOCK_END_FIXED`), the COP2 transfers made by compiled code
  (`PS2N_COP2_MOVE`, no gain that shows), the COP2 instructions that come
  first in their block (`PS2N_COP2_ASKS`, `PS2N_COP2_CYCLES`), the rest of
  them (`PS2N_VCALLMS`, `CFC2` and `CTC2` as moves; no gain that shows).
  Link-time
  optimization of PS2Native's own code (`build.ps1 -Lto thin`) was
  evaluated: no gain, not used. On 2026-11-13 a third session, given the
  website's two charts ("We need to improve these numbers"), measured them
  again and took on their slowest parts: `lq` and `sq` on the locals
  (`PS2N_LQ`, `PS2N_SQ`) and a block start asking the runtime in one
  argument (`askAbout`, the game's code 6% smaller), both kept and both
  within the noise over the level run; registers kept changed across the
  end of a block, tried and taken out. Then, at the maintainer's word, the
  FPU's registers in SSE registers (`val::Fpr`) and the masks of its
  additions' guard bit from a table (`kGuardMasks`), the self-test holding
  all 511 exponent differences to PCSX2's recompiler: play as long as
  stock PCSX2's, with profile-guided optimization.
- [ADR 0020](docs/adr/0020-no-claude-references-in-commit-messages.md):
  no commit message references Claude: no `Co-Authored-By` line naming
  Claude, no `Claude-Session` line. The history was rewritten on
  2026-11-12 to remove them (messages only; files, authors and dates
  unchanged) and force-pushed. Mentions of the file `CLAUDE.md` are fine.
- [ADR 0021](docs/adr/0021-two-sessions-on-step-g10.md): two sessions on
  G10 at once, the maintainer's choice: the second in the main tree, the
  first in its worktree, each with its own changes, saying "go" and "done"
  around every timing. The second's: calls through a register made by
  compiled code (`callSites`, `PS2N_CALL_DYNAMIC`), `ps2n-host -code-size`,
  the way a load or store seldom takes made short (one runtime call, no
  registers put back): play 1.18 to 1.14 times stock PCSX2's time without
  profile-guided optimization; the FPU's settings read once per function
  tried and taken out.

## Layout, build and test

```
third_party/pcsx2         PCSX2 v2.9.170, a pinned submodule. Never edit it; changes go in a patch queue (none so far)
third_party/ps2autotests  hardware-verified PS2 test programs, a pinned submodule
cmake/ps2n-inject.cmake   adds PS2Native's targets to PCSX2's own CMake project without touching it
CMakeLists.txt, core/     build-time tools, a project of their own with no PCSX2 dependency: the
                          instruction decoder, disassembler, assembler, ELF/IRX/SNR2 readers, the reader of a
                          BIOS image's directory (romdir)
recompiler/               ps2n-recomp, in that same project: analysis and C++ generation for EE executables
                          and for the BIOS's EE code (bios.cpp)
runtime/                  what PS2Native adds to the core at run time: the EE provider (dispatcher,
                          fallback interpreter), the record of the blocks PCSX2's recompiler would have formed
                          (rec_blocks), instruction semantics, the contract for generated code
host/                     ps2n-host: console frontend for the PCSX2 core, the test bench; with -window, where a game
                          is played (window.cpp: Windows only so far)
tests/unit/               unit tests of the build-time tools
tests/                    CTest registration for programs booted on the core, the driver script, the recorded
                          baselines; recompiles the test programs into the build tree and links them into the host
tests/programs/           our own test programs, in assembly for core's assembler: selfmod (rewrites its code), bench,
                          timeout (a delay loop that PCSX2 does not run), countcarry (a defect of PCSX2's
                          recompiler that PS2Native reproduces)
tests/sdk/                C test programs built with ps2sdk (prebuilt in bin/), their sources, what stock PCSX2 prints
tests/games/              controller scripts for runs of a game (button presses we wrote; nothing from a game)
scripts/windows, linux    build and test entry points
assets/                   branding/ (the logo, the icons, the colours; its README says which to use where) and
                          ps2n.rc (the icon of the Windows executables)
docs/                     brief, assessment, decisions (adr/), proof-of-concept log, game log; and the website,
                          which GitHub Pages serves from this folder: index.html, 404.html, .nojekyll, site/
                          (site/README.md says what belongs to it and where each of its numbers comes from)
build/, deps/             build trees and downloaded dependency packages; git-ignored
```

- Build: `scripts\windows\build.ps1` (add `-Reconfigure` after changing any
  CMake file). It enters the Visual Studio developer shell by itself.
  `-Pcsx2Source <dir> -BuildDir build\<name>` builds against another PCSX2
  source tree in a build tree of its own (and `test.ps1 -BuildDir` tests
  it): to try a newer PCSX2, or a change someone made to PCSX2.
- Test: `scripts\windows\test.ps1 -Bios <BIOS image>`, optionally
  `-Filter <regex>`. The BIOS is the maintainer's `SCPH-70004` dump in
  `Documents\PCSX2\bios`. Tests are skipped, not passed, without one.
- Speed: `scripts\windows\bench.ps1 -Bios <BIOS image> -Repeat 3` runs
  `tests/programs/bench.s` under each configuration and prints the times.
  A run that is timed is kept on the processor's fast cores (`ps2n-host
  -fast-cores`; `bench.ps1` and `game.ps1` pass it): left to Windows, the
  same run takes up to twice as long the next time. `game.ps1 -TimeProfile`
  (`ps2n-host -profile`) writes where the CPU thread's time went, by kind
  of code and by function, into `profile.txt` among the run's files; for
  the compiled code also by kind of generated line, its FPU's lines by
  instruction, and its hottest
  instructions by function and offset, to read beside the function's
  disassembly (`llvm-objdump --disassemble-symbols`, in the Visual Studio
  install's `VC\Tools\Llvm\x64\bin`). Over the level run, two builds of
  the same code, or the same build an hour apart, can differ by a second:
  a change worth less than that does not show, and a ratio needs a stock
  run timed beside it. `docs/game-log.md`, "G5" and "G6", has what was
  measured. `game.ps1 -Marks 6600 -ProfileFrom 6600` notes the time at
  chosen frames and profiles from one on: the level run's startup apart
  from its play ("G10"). Profile-guided optimization of the generated code:
  `build.ps1 -Pgo generate`, a run with `LLVM_PROFILE_FILE` set (the level
  is trained on `tests/games/killzone/level1-train.pad`, not the timed
  script), `llvm-profdata merge` (in the Visual Studio install's
  `VC\Tools\Llvm\x64\bin`), then `build.ps1 -Pgo use -PgoData <file>`; the
  counts stay in the build tree, and are made again after the generated
  code changes ("G10", "Profile-guided optimization"). `build.ps1 -Lto
  thin` links PS2Native's own code (the generated code, the runtime) with
  link-time optimization, PCSX2's code compiled as before; it gave nothing
  over the level run, and `-Lto off` is how the build is used ("G10",
  "Link-time optimization"). How much host code
  each kind of generated line became: `ps2n-host -code-size <file>` (from
  the host's own debugging information; no machine, about 20 s). To try a
  change to a header on one generated file, compile that file alone with
  the flags `build\windows-clang\build.ninja` gives it (without `/Yu` and
  `/Fp`) and an include directory searched first, into a scratch folder
  outside the build, and read `llvm-size -A` of the object. `game.ps1 -CallSites off` times one
  build with calls through a register sent to the runtime's dispatcher.
- The game: `scripts\windows\build.ps1 -Game <its boot executable>` compiles
  it into the host (the build tree remembers it; `-GameCapture <file>` adds
  what runs reported). `scripts\windows\game.ps1 -Image <disc image> -Config
  <configuration> -Frames <n>` runs it, with `-Pad` for a controller script
  (`tests/games/<game>/*.pad`), `-FrameEvery` for pictures along the way and
  `-Capture` to collect what had to be interpreted. `docs/game-log.md`, "How
  to run it", has the commands. A run's files are in
  `build\windows-clang\ps2n\tests\game\<name>\<configuration>`.
  `scripts\windows\sheets.ps1 -Name <name> -Config <configuration>` puts the
  pictures a run saved on contact sheets, sixty to an image: the way to look
  a whole run over, and to find out what a controller script has to press
  when (look at a sheet with the Read tool).
- The BIOS's EE code (step G8): `scripts\windows\build.ps1 -Bios <BIOS
  image> -BiosCapture build\bios\capture.toml` compiles it into the host as
  four images, from the maintainer's dump read in place, into
  `build\windows-clang\ps2n\generated\bios`; the build tree remembers both.
  Every test run with compiled code writes what it had to interpret of the
  BIOS to `bios-capture.toml` among its files (`ps2n-host -bios-capture`);
  `ps2n-recomp capture --out build\bios\capture.toml build\bios\capture.toml
  <those files>` puts them into the BIOS's capture for the next build.
  Without -Bios the BIOS is interpreted. Nothing generated from the BIOS
  may leave the build tree (ADR 0007). `ps2n-recomp bios-info <BIOS image>`
  says what the analysis finds in it.
- Playing the game: `scripts\windows\game.ps1 -Image <disc image> -Play
  -Name <name>` opens it in a window, at the console's speed, with sound,
  the keyboard and the first controller (`ps2n-host -help` has the keys),
  until the window is closed; `-Config stock-jit` puts stock PCSX2 in the
  same window. What a game keeps between sessions (memory card, the
  console's settings) is in `build\game\<name>\play`, and so is what the
  last session had to interpret of the game (`capture.toml`; `-Capture
  <file>` adds it to the file `build.ps1 -GameCapture` takes). Windows
  only so far.
  A session that checks the window does it with the sound off, because the
  sound comes out of the maintainer's speakers:
  `scripts\windows\window-check.ps1 -Image <disc image> -Name <name> -Steps
  '5 shot a.png', '8 key K', '14 close'` starts a muted session, photographs
  the window, presses keys in it and closes it (look at the photographs
  with the Read tool).
- Tests are named `<suite>/<configuration>/<program>`. Suites: `ps2autotests`
  (console recordings), `programs` (our own, `tests/programs/*.s`, built with
  `ps2n-recomp assemble`) and `sdk` (C programs built with ps2sdk, binaries in
  `tests/sdk/bin`). Configurations: `stock-jit`, `stock-interp`
  (PCSX2 as it ships), `ps2n-ee` (our provider, interpreting), `ps2n-static`
  (our provider, the program compiled ahead of time), `lockstep` (the compiled
  run compared with the interpreted one at every event test), `same-as-stock`
  (ADR 0011: the compiled run compared with a stock run at every sync point:
  place, cycle count, registers), `never-further` (ADR 0005: the compiled
  run's output against the stock run's, line by line; CTest runs the two it
  compares first). Also `selftest/tables-and-semantics`: our instruction
  semantics against PCSX2's interpreter and recompiler.
- A failing test's files are in `build\windows-clang\ps2n\tests\<test name>`:
  `core.log` (PCSX2's log), `guest.txt`, `actual.txt`.
- A `lockstep` failure names the event test where the runs part, a
  `same-as-stock` failure the sync point. The execution model's "Comparing
  two runs" says how to go from there (`ps2n-host -sync`, `-sync-against`,
  `-sync-detail`; they work under stock PCSX2 too).
- Generated C++ is in `build\windows-clang\ps2n\generated`. To look at what
  the recompiler makes of one program without building:
  `build\tools-windows\bin\ps2n-recomp.exe info <elf>`. To read code at an
  address, of an executable or of a raw image such as a BIOS:
  `ps2n-recomp.exe disasm <file> --at <address> [--count n] [--base address]`.
- A test that differs from the console recording goes into
  `tests/ps2autotests-known-differences.cmake` only after its cause is known;
  one whose run differs from the stock run, into
  `tests/same-as-stock-known-differences.cmake`, likewise.
- After a run, `ps2n-host` reports what the interpreter did. For a game whose
  image is complete, "instructions inside the executable's code" must be
  zero; when it is not, the line "compiled code handed over to the
  interpreter" names the addresses. "The BIOS interpreted" says, piece by
  piece, what of the BIOS was; with the BIOS compiled, that is 48
  instructions at boot (blocks counted double) and one `di`/`eret` at
  `8000517C`.
- The Linux scripts have never been run. Only the maintainer can run them.
- Never run stock `pcsx2-gsrunner` here: it writes into the maintainer's own
  `Documents\PCSX2`.
- The website: `scripts\windows\site.ps1 -Open` serves `docs` on this
  machine as GitHub Pages serves it. It has no build step. The page repeats
  numbers and states from `docs/game-log.md`, `docs/poc-log.md` and the
  status above: when one of those changes, change `docs/index.html` and the
  date at its foot too (`docs/site/README.md` lists what comes from where).
  It goes live when `main` is pushed.

## Standing rules

- Phases are gated: stop at the end of each and ask for approval.
- Never substitute emulation, interpretation or a run-time JIT for static
  recompilation without a documented, approved fallback.
- Investigate root causes before changing code. After repeated failed
  attempts, stop and reassess instead of trying variations.
- Never invent test or benchmark results, and never skip a failing test
  silently. A performance or compatibility claim needs a reproducible
  measurement.
- Do not commit unless asked. Do not push to GitHub or delete repositories
  without explicit authorization. Ask before installing software.
- Commit messages never reference Claude: leave out the `Co-Authored-By`
  and `Claude-Session` lines a session adds by default
  ([ADR 0020](docs/adr/0020-no-claude-references-in-commit-messages.md)).
  The maintainer dates each commit a few minutes after the one before it
  (author and committer date alike): ask how many if they have not said.
- No BIOS images, game data, or code generated from a game or from a BIOS in
  the repository.
- PCSX2 (GPL-3.0+) may be reused with attribution. AnyPS5 is GPL-2.0-only:
  read it for ideas, never copy its code.
- PCSX2's `AGENTS.md` forbids AI-generated contributions and any automated
  posting to its GitHub. Never open issues or pull requests there, never
  comment there, and never write what is posted there (code, commit
  messages, descriptions, issue texts, replies). The maintainer may
  contribute a fix themselves; a session may explain and point, no more
  ([ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).
  A commit message here never names one of PCSX2's issues or pull requests
  by its address or as `PCSX2/pcsx2#` and a number: GitHub would post a
  reference there.

## Development machine

Windows 11 x64 (i5-14600KF, 64 GB). Visual Studio Build Tools 2026 provides
clang-cl, CMake and Ninja, but only inside a developer shell; the command that
enters one is in `../ps2-uncompiler/CLAUDE.md` under "Build and test". The
Vulkan SDK is in `C:\VulkanSDK`. Stock PCSX2 is installed in
`C:\Program Files\PCSX2`, with its data (including the maintainer's BIOS
dumps) in `Documents\PCSX2`. Game workspaces from the old project are under
`C:\ps2\`. Use the BIOS dumps only after the maintainer says which.

## References examined in Phase 1

| Project | Revision |
|---|---|
| PCSX2 | tag `v2.9.170`, commit `a886f8ef` |
| AnyPS5 | `3c9276b7` |
| PS2Recomp | `e5636981` |
| PS2 Web Recomp | `76554ba4` |
| ps2autotests | `97469ffb` |

They were cloned into a temporary directory for that session and are not part
of this repository.
