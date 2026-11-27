# How things are done

How PS2Native is built, tested, measured, packaged and released: one section
to a task. Read the section for what you are about to do. The rules that hold
whatever the task are in [`CLAUDE.md`](../CLAUDE.md).

Most of it was `CLAUDE.md`'s "Layout, build and test" until 2026-11-27
([ADR 0061](adr/0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md)),
moved here word for word. The sections after "The website" are new with that
step: they describe the scripts that came into the repository then.

## Contents

| To do this | Read |
|---|---|
| Find something in the tree | [Layout](#layout) |
| Build; try another PCSX2 source tree; carry a change to PCSX2's code | [Build](#build) |
| Run the tests, read what they say, find why one fails | [Test](#test), [The tests: suites and configurations](#the-tests-suites-and-configurations), [When a test fails](#when-a-test-fails), [Known differences](#known-differences), [What the host reports after a run](#what-the-host-reports-after-a-run) |
| Time or profile a run | [Timing and profiling a run](#timing-and-profiling-a-run) |
| Build the runtime as a release has it; make its profile of play | [The runtime itself](#the-runtime-itself-link-time-optimization-and-a-profile-of-play) |
| Change what a module and the runtime share; the generated code's headers | [The compiled code in modules of its own](#the-compiled-code-in-modules-of-its-own), [Looking at the generated code](#looking-at-the-generated-code) |
| Make the PS2Native folder; make a game with it or with the wizard; a game's folder and its own compile | [The PS2Native folder, a game's folder and the wizard](#the-ps2native-folder-a-games-folder-and-the-wizard) |
| Compile a game, the BIOS, micro code or IOP code into the host | [A game compiled into the host](#a-game-compiled-into-the-host), [The BIOS's EE code](#the-bioss-ee-code), [The vector units' micro code](#the-vector-units-micro-code), [The IOP's code](#the-iops-code) |
| Play a game, check its window, run a session again | [Playing a game, and running a session again](#playing-a-game-and-running-a-session-again) |
| Work on Linux | [Linux](#linux), [Linux inside WSL, from Windows](#linux-inside-wsl-from-windows) |
| Change the website or the README | [The website](#the-website) |
| Run anything long on a computer someone may be playing on | [Batches on a computer someone is playing on](#batches-on-a-computer-someone-is-playing-on) |
| Give the maintainer's game folders new programs | [The maintainer's game folders brought up to date](#the-maintainers-game-folders-brought-up-to-date) |
| Measure speed against stock PCSX2 | [Compiled games timed against stock PCSX2](#compiled-games-timed-against-stock-pcsx2) |
| Make a release | [A release](#a-release) |
| Try a newer PCSX2, or move to it | [A newer PCSX2 tried, and the move to it](#a-newer-pcsx2-tried-and-the-move-to-it) |

## Layout

```
third_party/pcsx2         PCSX2 v2.9.189 (ADR 0054), a pinned submodule. Never edit it; what PS2Native changes of PCSX2's code is in patches/pcsx2
patches/pcsx2             PS2Native's changes to PCSX2's code (ADR 0050), one patch to a change, each decided by the
                          maintainer and saying at its top what it changes and why; its README lists them (one so far:
                          PCSX2's VU thread is not woken where it is not in use)
third_party/ps2autotests  hardware-verified PS2 test programs, a pinned submodule
cmake/ps2n-inject.cmake   adds PS2Native's targets to PCSX2's own CMake project without touching it; and
                          ps2n-pcsx2-patches.cmake, which applies patches/pcsx2 to copies of the files they change, in the
                          build tree, and gives PCSX2's targets the copies to compile
CMakeLists.txt, core/     build-time tools, a project of their own with no PCSX2 dependency: the
                          instruction decoder, disassembler, assembler, ELF/IRX/SNR2 readers, the reader of a
                          BIOS image's directory (romdir) and of a disc image's files (iso9660); for the
                          vector units, the first pass of PCSX2's VU
                          recompiler (ps2n/vu_mvu.h) and a block of micro code as a list of steps, its plan
                          (ps2n/vu_plan.h; the steps themselves, which compiled micro code needs, in
                          ps2n/vu_step.h and vu_steps.inc); for the IOP, its instructions as PCSX2's IOP
                          recompiler treats them and what a block of them is (ps2n/r3000a.h); all used by the
                          runtime too, and vu_step.h and r3000a.h by compiled code as well
recompiler/               ps2n-recomp, in that same project: analysis and C++ generation for EE executables
                          and for the BIOS's EE code (bios.cpp), and C++ generation for the blocks of micro
                          code (vu_codegen.cpp) and of IOP code (iop_codegen.cpp) that runs captured
runtime/                  what PS2Native adds to the core at run time: the EE provider (dispatcher,
                          fallback interpreter), the record of the blocks PCSX2's recompiler would have formed
                          (rec_blocks), instruction semantics, the contract for generated code; the VU
                          provider (vu_interp), which follows PCSX2's VU recompiler: its record of programs
                          and blocks, the planner, the interpreter of plans; the steps' semantics
                          (ps2n/vu_ops.h) and the contract for compiled micro code (ps2n/vu_recomp.h); the
                          IOP provider (iop_provider), which follows PCSX2's IOP recompiler: its record of
                          blocks, the interpreter; the IOP's instructions carried out (ps2n/iop_ops.h) and the
                          contract for compiled IOP code (ps2n/iop_recomp.h); what a module of compiled code
                          and the host share (game_module.h, module_entry.cpp, ps2n/module.h); what compiled
                          code reaches of PCSX2's core, PCSX2's own in the runtime and PS2Native's description
                          of it in a module (ps2n/pcsx2.h, ps2n/pcsx2_layout.h), and the description compared
                          with PCSX2's when the runtime is built and when the host starts (pcsx2_check.cpp)
host/                     ps2n-host: console frontend for the PCSX2 core, the test bench (the self-test of the
                          instruction semantics, of the VU provider and of the IOP provider; the watches on the
                          core's providers: sync_trace, vu_report, iop_report); with -window, where a game
                          is played (window.cpp: a Win32 window on Windows, an X11 one on Linux; on Linux
                          shm_names.cpp gives PCSX2's one shared memory name for every process the process's
                          number), with PS2Native's menu over the game, opened with Esc (menu.cpp: the controls,
                          the graphics card, quitting; its sounds in menu_sound.cpp; ADR 0060); a game's
                          launcher on Windows (launcher.cpp:
                          the program a game's folder is started from, which starts the runtime beside it,
                          never with anything left to compile: ADR 0040);
                          and a disc image's files read through PCSX2's own readers of every format it plays
                          (disc.cpp: -disc-executables, -disc-extract; step P4.8, ADR 0036)
wizard/                   ps2n-wizard, the application a game is made with (Phase 4, step P4.5; ADR 0032):
                          its five steps (wizard_window.cpp), the recompile (pipeline.cpp), the game's folder
                          (package.cpp), PCSX2's archive of patches fetched or given where PCSX2 is not installed
                          (patches.cpp; step P4.8), the games made with an earlier PS2Native brought up to date
                          (update_window.cpp; ADR 0048); and ps2n-remake (remake_main.cpp; step P4.6, ADR 0034), the same
                          recompile in a small window of its own, with which a game's folder compiles what its
                          sessions met for the first time itself, after a session and before the game starts (ADR
                          0040); a game's launcher on Linux (launcher_main.cpp, Qt's
                          windows; step P4.7), and what differs between the two systems for the wizard, the
                          recompile and a game's folder (platform.h: the files' names, the toolchain's compiler and
                          linker and what they are given); PCSX2's own settings pages, compiled from the submodule unchanged, in
                          a window of its own (settings_window.cpp: PCSX2's SettingsWindow class with the pages a
                          game has), with what they call of the rest of PCSX2's frontend (stand_ins.cpp,
                          pcsx2-qt/MainWindow.h, found before PCSX2's) and of the core (host_interface.cpp)
tests/unit/               unit tests of the build-time tools
tests/module/             what a module defines for itself, tested (stack_probe.cpp: its __chkstk)
tests/disc/               a small disc of our own in three formats, for the runtime's reading of a disc (disc/formats)
tests/                    CTest registration for programs booted on the core, the driver script, the recorded
                          baselines; recompiles the test programs into the build tree and links them into the host
tests/programs/           our own test programs, in assembly for core's assembler: selfmod (rewrites its code), bench,
                          timeout (a delay loop that PCSX2 does not run), countcarry (the count register's addition,
                          32 bits wide since PCSX2 corrected its recompiler), loader (loads the two programs in loader/
                          itself, over the same addresses, as a game of several executables does), overlay (copies two
                          routines in turns to one address outside its executable, as Jak and Daxter loads code: ADR 0057)
tests/sdk/                C test programs built with ps2sdk (prebuilt in bin/), their sources, what stock PCSX2 prints
tests/games/              controller scripts for runs of a game (button presses we wrote; nothing from a game)
scripts/windows, linux    build and test entry points
assets/                   branding/ (the logo, the icons, the colours; its README says which to use where) and
                          ps2n.rc (the icon of the Windows executables)
docs/                     brief, assessment, decisions (adr/), proof-of-concept log, game log, Phase 3 report, Phase 4
                          proposal and log (app-log.md); and the website,
                          which GitHub Pages serves from this folder: index.html, 404.html, .nojekyll, site/
                          (site/README.md says what belongs to it and where each of its numbers comes from)
version.txt               PS2Native's version (0.4), which the build gives the programs as ps2n_version.h (ADR 0042;
                          not VERSION: the root is on the include path, and Windows would find it for <version>)
build/, deps/             build trees and downloaded dependency packages; git-ignored
```


## Build

Build: `scripts\windows\build.ps1` (add `-Reconfigure` after changing any
CMake file). It enters the Visual Studio developer shell by itself.
`-Pcsx2Source <dir> -BuildDir build\<name>` builds against another PCSX2
source tree in a build tree of its own (and `test.ps1 -BuildDir` tests
it): to try a newer PCSX2, or a change someone made to PCSX2
(`build.sh --pcsx2-source` on Linux). A change of the session's own to
PCSX2 is tried so, on a copy of the submodule in the build tree, never in
the submodule (`build\measure\vif-trial.ps1` is one such trial). A change
the maintainer has decided PS2Native carries (ADR 0050) is a patch in
`patches\pcsx2`: when a tree is configured, each file a patch changes is
copied into it (`ps2n-pcsx2\src`), the patches are applied to the copies,
and PCSX2's targets compile the copies in the place of the files they came
from (`cmake/ps2n-pcsx2-patches.cmake`; a patch that does not apply, or
changes a file no target compiles, a header among them, stops the
configuration). Every tree has them but the one stock PCSX2 is timed on:
`build.ps1 -Pcsx2Patches off` (`build.sh --pcsx2-patches off`) builds PCSX2
as it is, and the build tree remembers it. `ps2n-host -help` says in its
second line which a runtime has, and a release's `licences` folder carries
the patches. So in every other tree the tests' "stock" configurations run
PCSX2 with PS2Native's changes: a change that would alter a result of a
run is not one to carry this way without the maintainer's word on that
too.

## Test

Test: `scripts\windows\test.ps1 -Bios <BIOS image>`, optionally
`-Filter <regex>`. The BIOS is the maintainer's `SCPH-70004` dump in
`Documents\PCSX2\bios`. Tests are skipped, not passed, without one.

## Timing and profiling a run

Speed: `scripts\windows\bench.ps1 -Bios <BIOS image> -Repeat 3` runs
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
install's `VC\Tools\Llvm\x64\bin`). On Linux `ps2n-host -profile` (through
`tests/guest_test.cmake -DPROFILE=1`) says it by kind of code and by
function, without lines, a sample for every millisecond the thread ran,
and names what called each of the system's functions, with where in the
host's file the call returns to (`llvm-symbolizer --inlines -e` the host
says what was compiled into there); to see what the two systems' compilers
make of one generated file, compile it for both on Linux (`clang
--driver-mode=cl --target=x86_64-pc-windows-msvc` with a Windows folder's
options; docs/app-log.md, "Linux's speed"). Over the level run, two builds of
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
"Link-time optimization"). What the vector units run: `game.ps1
-VuReport` (`ps2n-host -vu-report`) writes `vu-report.txt` among the
run's files (what was written into each VU's micro memory, where, with
how many contents, where programs were started, how long each ran), with
the counts frame by frame and the contents written beside it, which are
the game's and stay in the build tree ("G11"). What the IOP runs:
`game.ps1 -IopReport` (`ps2n-host -iop-report`) writes `iop-report.txt`
(how much IOP time passed and how long the host took for it, how often
the IOP's memory was written, which modules its kernel listed where and
when, and, under PCSX2's IOP recompiler, how many blocks it formed in
each), with the counts frame by frame beside it ("G12"). How much host code
each kind of generated line became: `ps2n-host -code-size <file>` (from
the host's own debugging information; no machine, about 20 s). To try a
change to a header on one generated file, compile that file alone with
the flags `build\windows-clang\build.ninja` gives it (without `/Yu` and
`/Fp`) and an include directory searched first, into a scratch folder
outside the build, and read `llvm-size -A` of the object. `game.ps1 -CallSites off` times one
build with calls through a register sent to the runtime's dispatcher.
A game's folder as users get it (the speed step, ADR 0043): copies made
with `make-game.ps1` into `build\measure\<kit>\games` (`-LineTables`
compiles the modules with line tables, for `-TimeProfile` to name their
functions and lines; `-ExtraFlags` and `-ExtraLink` try other compiler and
linker options; all three for measurements only), a session of the
maintainer's run again from a copy with `game.ps1 -Runtime
<copy>\ps2n-host.exe -Replay <session> -Marks 1`, in turns with
`-Config stock-jit`; `build\measure` keeps the scripts (`replay.ps1`,
`variant.ps1`, `flags.ps1`, `relink.ps1`, the batches) and
`results.txt`. Batches run on a desktop of their own
(`build\measure\hidden.ps1`), one at a time. `game.ps1 -FunctionsRan`
(`ps2n-host -functions-ran`) writes the compiled functions a run ran, for
`lld-link /ORDER`; a module made to count what of its code runs (compiled
with `-fprofile-generate`, linked with LLVM's profile runtime and
`runtime/module_counts.cpp`, as a game's folder makes it: ADR 0045) has
its counts written by the host at the run's end (`ps2n-host -module-counts
<dir>`, with `-modules <dir>` to load such modules from elsewhere than
beside the host). `game.ps1 -SampleProfile`
(`ps2n-host -sample-profile`) writes the profiler's samples of the modules
(made with `-LineTables`) as a sampled profile for clang's
`-fprofile-sample-use`: tried on 2026-11-21, it gave nothing that shows
(docs/app-log.md, "A profile sampled while the game plays, tried").
Clang's `-mllvm -sample-profile-check-record-coverage=100` says how much
of a sampled profile a file's functions took.

## The runtime itself: link-time optimization and a profile of play

The runtime itself (ADR 0046; docs/app-log.md, "The runtime itself"):
`build.ps1 -RuntimeLto on` builds `ps2n-host`, PCSX2's core and what
PS2Native adds to it, with link-time optimization, as PCSX2's own releases
are built (`build.ps1` otherwise leaves it out); `-RuntimePgo generate`
builds it to count what it runs, in a tree without the wizard (a run then
writes its counts where `LLVM_PROFILE_FILE` says), `llvm-profdata merge
-sparse` puts the runs' counts together without the functions no run ran,
and `-RuntimePgo use -RuntimePgoData <file>` builds it with them
(`build.sh --runtime-lto`, `--runtime-pgo`, `--runtime-pgo-data`). The
build tree remembers them; `off` takes them out. The modules of compiled
code, the launcher and the wizard's own code are built as before
(`ps2n_not_the_runtime`, `cmake/ps2n-inject.cmake`). The counts come from
playing games and stay in the build tree. At the maintainer's word (ADR
0047) a release's runtime is built with both, so the tree the folder is
made from is: `build.ps1 -BuildDir build\windows-clang-module -RuntimeLto on
-RuntimePgo use -RuntimePgoData build\pgo\runtime-windows.profdata`; the
development tree, `build\windows-clang`, stays without either. The profile
is made again for a release, from the tree released, by
`scripts\windows\runtime-profile.ps1 -Bios <BIOS image>`
(`scripts/linux/runtime-profile.sh`): the runtime built to count in
`build\windows-clang-count`, the training runs of `build\pgo\training.txt`
(one to a line: a name, the disc image, a game's folder whose modules the
run takes, a session to run again or `-`, the frames, and `vu-thread` for
a sixth part where the run is to have VU1 on PCSX2's VU thread, as a game
is played where that thread is on; a session that is timed against stock
PCSX2 never among them) each through `game.ps1
-Modules <game folder>`, and the counts merged sparse; about 10 minutes on
Windows, 18 on Linux under WSL 2, where each game's folder has to be one
made on Linux. The build gives the compiler a copy of the profile named
after what it holds, so a profile made again under the same name is
compiled with (what was compiled with two profiles does not link). `kit.ps1`
and `kit.sh` say how the tree's runtime was built, and warn when it is not a
release's. Stock PCSX2 is timed built as its releases are, on the runtime of
a tree of its own with link-time optimization, no profile and none of
PS2Native's changes to PCSX2's code (`build.ps1 -BuildDir
build\windows-clang-stock -Module on -RuntimeLto on -RuntimePgo off
-Pcsx2Patches off`; built with link-time optimization it takes 1.5 to 4%
less time than built as the trees are by default), never on the runtime
with the profile. Every run of `ps2n-host` has VU1 on the CPU thread, stock
PCSX2's too, so that runs repeat; PCSX2's own default where there are three
cores or more is its VU thread ("MTVU"), which `game.ps1 -Config stock-jit
-Pcsx2VuThread` (`ps2n-host -pcsx2-vu-thread`, `-DPCSX2_VU_THREAD=1`
through `tests/guest_test.cmake`) switches on, for timing stock PCSX2 as
its users mostly have it (ADR 0050). With a runtime built with
link-time optimization the three comparisons of two runs of
`dma/vif/stcycl` are disabled, for a defect of PCSX2's that makes its runs
differ there (`tests/same-as-stock-known-differences.cmake`); they run in
the development tree. `build\measure\rpgo.ps1` is the trial that measured
both, `build\measure\batch26.ps1` the measurement as 0.2 has it.

## The compiled code in modules of its own

The compiled code in modules of its own (Phase 4, steps P4.1 and P4.2):
`scripts\windows\build.ps1 -BuildDir build\windows-clang-module -Module on`
with the main tree's game settings builds everything the host would hold
compiled into four modules beside `ps2n-host.exe`, one for each kind of
code (`ps2n-game.dll`, the programs' EE code; `ps2n-bios.dll`;
`ps2n-vu.dll`, the micro code; `ps2n-iop.dll`), which the host loads when
it starts (`runtime/game_module.h`); what crosses between them is
`runtime/include/ps2n/module.h`. A module is compiled with PS2Native's own
headers and nothing else, no system header, no standard library, nothing
of PCSX2's (`ps2n/pcsx2.h` gives it `ps2n/pcsx2_layout.h` in their place),
and linked with the host's import library and nothing else; what the
compiler asks of a C runtime it defines itself (`_fltused`, and
`__chkstk`, which a function whose locals take more than a page calls:
`runtime/module_entry.cpp`). So a header
the generated code includes takes PS2Native's own headers, `<stdint.h>`,
`<stddef.h>` and the SSE intrinsics' headers, and nothing of PCSX2's but
through `ps2n/pcsx2.h`; a field of PCSX2's that compiled code comes to use
goes into `pcsx2_layout.h` and its check, `runtime/pcsx2_check.cpp`. `test.ps1` and
`game.ps1` take the same `-BuildDir`. Without `-Module on` the code is
built into the host as before. `-Module imports` (in a tree of its own,
`build\windows-clang-imports`) compiles the code as for a module and
links it into the host all the same: what reaching the core through the
import table costs, alone. What it costs is in
[docs/app-log.md](app-log.md), "P4.1" and "P4.2".

## The PS2Native folder, a game's folder and the wizard

What a computer with nothing else installed compiles and plays a game
from (Phase 4, steps P4.3 and P4.4): `scripts\windows\kit.ps1` puts it
together in `build\kit` from the module tree, LLVM's own release (ADR
0030; fetched into `deps\llvm-22.1.3` when not there, its SHA-256
checked) and Visual Studio's redistributable C++ runtime (ADR 0031); its
one command, `make-game.ps1 -Image <disc image> -Bios <BIOS image>
[-Title <name>] [-More <names on the disc>] [-Captures <folder>] [-Play]`
(`scripts\windows\kit\`), makes the game into a folder of its own there,
`games\<name>`, that plays it: `<name>.exe`, the launcher
(`host/launcher.cpp`), starts the runtime beside it with `settings.txt`
(`ps2n-host -settings`, one option to a line, `%GAME%` the folder; the
runtime started with no options reads it by itself), which writes each
session down in the folder's `sessions` (`-sessions`) and names the
window (`-title`); `last-run.log` has what the runtime said, and the
launcher says why in a message box when it could not play. It takes the
game's executables from the disc image, in any format PCSX2 plays, through
the runtime (`ps2n-host -disc-extract`, PCSX2's own readers; a `.cue`
stands for the image it names; step P4.8), and the
next time it runs compiles what the folder's sessions had to interpret
too. `game.ps1 -Runtime <folder>\ps2n-host.exe` runs a packaged game as
it is, a session again among them. Run it from the folder with nothing
but Windows on `PATH` to check that it needs nothing else; what it makes
stays in the build tree, as everything made from a game does. The
wizard (P4.5) does the same in five steps: built with `build.ps1
-BuildDir build\windows-clang-module -Wizard on` (Qt from PCSX2's
dependency package), it goes into the folder's `runtime` with Qt and its
plugins (`runtime\wizard-files.txt` lists what a game's folder leaves
out), and `PS2Native.exe` at the folder's root starts it. It finds a
disc's executables itself (`ps2n-host -disc-executables`), each compiled
unless the user leaves it out, writes the game's
own settings file and patch file into its folder's data, and makes the
folder with `-packaged`: the runtime then takes the game's settings from
that file, over the defaults the wizard showed, which are the runtime's
too (`host/game_settings.h`, `ps2n::game::SetDefaults`), and refuses to
start when the file sets what PS2Native fixes (`kFixedSections`,
`kFixedKeys`, the list the wizard keeps out of it; step P4.8). Where
PCSX2 is not installed, its settings step offers to fetch PCSX2's archive
of patches, or to take one the user gives, kept in the folder's
`patches\` (ADR 0036). `kit.ps1` writes `licences\`, the licence of
everything the folder carries and where its source is
(`licences\README.txt`; Qt's notice made from the SBOMs in PCSX2's
dependency package, with the texts of Qt's source, fetched into
`deps\qt-<version>-licences`); the folder for others is made with
`-Out` into a folder of its own, from a tree committed and pushed. A game's
folder, whichever made it, also has a copy of the toolchain, Qt and
`ps2n-remake.exe`, and `make.txt`, how its modules are made (step P4.6,
ADR 0034): when a session ends, its launcher starts `ps2n-remake`, which
merges what the sessions met into the folder's captures and, when that is
more than the modules were compiled from (`work\compiled-from.txt`),
compiles only what changed (objects newer than their sources stay) into
`work\next-<time>`, in a window of its own in front, with how far it has
got and Later, which closing it is too (ADR 0040; `work\remake.log` says
what each run did); the launcher puts those modules in place at the next
start. A session also writes the compiled functions it ran
(`functions-ran.txt`), which the compile adds to the folder's
(`captures\functions-ran.txt`) and links the game's and the BIOS's
modules in that order (ADR 0044: `/ORDER` on Windows,
`--symbol-ordering-file` on Linux), a session that ran new ones having
the modules linked again. Once for a game, after a session of at least
3 minutes of the game's own time (9,000 frames where the console shows the
game at 50 frames a second, 10,800 at 60: the runtime writes a session's
`frames-a-second` down when it ends, ADR 0047; a session written down before
it did is taken for one at 60), `ps2n-remake` then compiles the game for how its
user plays it, in the same window (ADR 0045): the modules compiled to count
(`work\count`), the session's first 3 minutes run again with them by the
folder's runtime without a window (`-modules`, `-module-counts`,
`-progress`; the session's memory cards, NVRAM and settings copied into
`work\count\run`), the counts merged by the toolchain's `llvm-profdata`,
and the modules compiled again with them (`work\objects-profiled`, then the
folder's objects), the counts kept in `captures\profile.profdata`, which
every compile after takes. Later puts it off to another session's end; a
game started meanwhile stops it (`work\stop-profile`); when it fails,
`work\count\failed.txt` says why and it is not tried again. A game's folder
made with an earlier PS2Native is brought up to date by the wizard (ADR
0048; `wizard/update_window.cpp`, `BringUpToDate` in `wizard/package.cpp`):
"Games made earlier..." in its first step lists the games of the folder's
own `games` and of any folder added, and gives those ticked this
PS2Native's runtime, launcher and toolchain; `programs.txt` in a game's
folder says which PS2Native's its programs are (the version, and a
fingerprint of the runtime and toolchain they were copied from; a folder
`make-game.ps1` made says nothing, and is listed as an earlier one's). The
folder's own recompile then compiles the game again before its next start
(the toolchain being another), saying why (`work\brought-up-to-date.txt`),
and, where the folder had its counts, counts once more after a session
(`work\count-again.txt`). For checks, `ps2n-wizard --update <folder>` does
it without a window, a line written for each game, and `--updates <folder>
--shots <folder> [--auto]` opens the window and saves it as a picture. The game never starts with anything left to compile: before the
runtime, the launcher runs `ps2n-remake --before-play`, which compiles
what is left in the same window (and waits for a compile already
running), and starts the game only when it ends with 0; Later there
leaves the game unstarted. For checks,
`ps2n-wizard --kit build\kit --image <iso> --bios <bin> --shots <folder>
--auto [--until <step>] [--without-pcsx2 [--fetch-patches]]` fills the
first steps in, saves each step (the settings, each category) as a
picture, and presses Next itself (`--without-pcsx2`: as on a computer
without PCSX2; `--fetch-patches`: the archive fetched unasked); run from
the build tree it needs `deps\windows-x64\deps\bin` on `PATH` and
`QT_PLUGIN_PATH` set to `deps\windows-x64\deps\plugins`.

## A game compiled into the host

The game: `scripts\windows\build.ps1 -Game <its boot executable>` compiles
it into the host (the build tree remembers it; `-GameCapture <file>` adds
what runs reported). A game made of several executables, whose boot
executable loads the others itself without the BIOS's loader (Agent Under
Fire's), gets them with `-GameMore '<path>|<path>'`: each is compiled as an
image the runtime puts in use while memory holds its code (ADR 0026; the
execution model's "Executables a program loads itself"). A build holds
one game's EE code: the build tree now holds Agent Under Fire's, with the
micro code and IOP code of both games; Killzone comes back with `-Game
<its executable> -GameMore none -GameCapture build\game\killzone\capture.toml`
and its profile (`-Pgo use -PgoData build\pgo\game.profdata`). `scripts\windows\game.ps1 -Image <disc image> -Config
<configuration> -Frames <n>` runs it, with `-Pad` for a controller script
(`tests/games/<game>/*.pad`), `-FrameEvery` for pictures along the way and
`-Capture` to collect what had to be interpreted. `docs/game-log.md`, "How
to run it", has the commands. A run's files are in
`build\windows-clang\ps2n\tests\game\<name>\<configuration>`.
`scripts\windows\sheets.ps1 -Name <name> -Config <configuration>` puts the
pictures a run saved on contact sheets, sixty to an image: the way to look
a whole run over, and to find out what a controller script has to press
when (look at a sheet with the Read tool).

## The BIOS's EE code

The BIOS's EE code (step G8): `scripts\windows\build.ps1 -Bios <BIOS
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

## The vector units' micro code

The vector units' micro code (step G11): in PS2Native's configurations
both VUs run on PS2Native's VU provider (`ps2n-host -vu-ps2n both`;
`game.ps1 -VuPs2n none` puts PCSX2's VU recompiler back under them; the
stock run of a comparison never has PS2Native's). It carries a block of
micro code out by the function a build compiled for it, found by a
fingerprint of what the block does (its plan's key), and by its
interpreter otherwise.
`game.ps1 -VuCapture build\game\<name>\vu-capture.toml` adds the blocks a
run had to interpret to that file (`ps2n-host -vu-capture`, `ps2n-recomp
vu-capture`), and `scripts\windows\build.ps1 -VuCapture <file>` compiles
them into the host, into `build\windows-clang\ps2n\generated\vu` (the
build tree remembers the file; `-VuCapture none` forgets it). After a run
the host says, for each VU, how many different blocks of micro code ran,
how many of them compiled and how many interpreted, and how often: for
the level run "interpreted" must be zero. `game.ps1 -VuStatic off`
(`ps2n-host -vu-static off`) has every block interpreted, the reference
for the compiled ones. `scripts\windows\vu-selftest.ps1 -Bios <BIOS
image>` captures the self-test's first random programs and builds them
in (`build.ps1 -VuSelftestCapture`), for the test `selftest/vu-compiled`:
run it again whenever that test says blocks had to be interpreted, which
is what any change to how blocks are planned does. On Linux it is
`scripts/linux/vu-selftest.sh --build-dir <tree>` (`PS2N_BIOS` set), once
for each tree: a tree it was never run for skips the test, and ctest counts
a skipped test among those that did not fail, so look for "Skipped" in a
test run's output before saying that every test passed. So does a game's
capture go stale: run the game with `-VuCapture` again and rebuild. The
captures are derived from the game and stay in the build tree. The
execution model's "The vector units' micro programs" says how it works.

## The IOP's code

The IOP's code (step G12): in PS2Native's configurations the IOP runs on
PS2Native's IOP provider (`ps2n-host -iop-ps2n`; `game.ps1 -IopPs2n off`
puts PCSX2's IOP recompiler back under them; the stock run of a
comparison never has PS2Native's). It carries a block of IOP code out by
the function a build compiled for it, found by a fingerprint of the
block's instructions wherever they are loaded, and by its interpreter
otherwise. `game.ps1 -IopCapture build\game\<name>\iop-capture.toml`
adds the blocks a run had to interpret to that file (`ps2n-host
-iop-capture`, `ps2n-recomp iop-capture`), and
`scripts\windows\build.ps1 -IopCapture <file>` compiles them into the
host, into `build\windows-clang\ps2n\generated\iop` (the build tree
remembers the file; `-IopCapture none` forgets it). After a run the host
says how many different blocks the IOP ran, how many of them compiled
and how many interpreted, and how often, and how many blocks PCSX2's IOP
recompiler holds: for the level run "interpreted" and that must be
zero. `game.ps1 -IopStatic off` (`ps2n-host -iop-static off`) has every
block interpreted, the reference for the compiled ones.
`scripts\windows\iop-selftest.ps1 -Bios <BIOS image>` captures the
blocks of the self-test's random IOP programs and builds them in
(`build.ps1 -IopSelftestCapture`), for the test `selftest/iop-compiled`:
run it again whenever that test says blocks had to be interpreted, which
is what a change to the programs the self-test makes does (on Linux,
`scripts/linux/iop-selftest.sh --build-dir <tree>`, as for the VUs). The
captures are derived from the BIOS and the game and stay in the build tree. What
the provider does not carry out it stops at, with an error that names it
(`r3000a::shape`, `Refuse` in `runtime/iop_provider.cpp`). The execution
model's "The I/O processor's code" says how it works.

## Playing a game, and running a session again

Playing the game: `scripts\windows\game.ps1 -Image <disc image> -Play
-Name <name>` opens it on PCSX2's Vulkan renderer at three times the
console's resolution, with a picture of 1920 by 1080: full screen on a
screen that size, in a window on a larger one (Alt+Enter or F11
switches; ADR 0025, step G14; `-Renderer`, `-Upscale` and `-WindowSize`
change them), with the game's widescreen and frame-rate patches where
the installed PCSX2's archive has them, its films kept at 4:3, and the EE
overclock the frame-rate patch asks for (steps G15 to G17; `-Patches
none` leaves both out, `-Patches widescreen` the frame-rate one,
`-EeCycleRate 0` the overclock; the game's patch file is copied into the
run's data folder), at the
console's speed, with sound, the keyboard and the first controller
(`ps2n-host -help` has the keys), until the window is closed; `-Config
stock-jit` puts stock PCSX2 in the same window. In a window VU1 is carried
out on PCSX2's own VU thread where the settings have it, as in PCSX2 (ADR
0051: its default where the computer has three processor cores or more, a
game's own setting over it, PCSX2's game database over both); every run
without a window has VU1 on the CPU thread, so that it repeats, whatever a
game's own settings file says (`OnSettingsLoaded`, `host/main.cpp`), unless
`game.ps1 -Pcsx2VuThread` (`ps2n-host -pcsx2-vu-thread on`) asks for the
thread. The setting is decided once for a run, when the game's disc is
read, PCSX2's game database heard then, and nothing switches the thread on
while a game runs: `patches/pcsx2`'s first change holds only so (a run's
report says how many of VU1's runs were on the thread: all of them or
none). A session written down says when it was played with the thread
on (`vu-thread on`): a run of it again need not be it then (Agent Under
Fire's is not, with the thread or without: with the thread on its run does
not repeat). To check what a played game gets without a window on the
maintainer's screen, a game's window runs on the measurement's hidden
desktop, muted (`build\measure\vut-04.ps1`). `game.ps1 -Patches
widescreen,fps` gives a comparison's two runs the patches and the
overclock their note asks for. A session
in the window takes `-Pad`, `-Frames` and `-Marks` as a scripted run
does: the game log's "How to run it" has the session that measures the
pace in play. What a game keeps between sessions (memory card, the
console's settings) is in `build\game\<name>\play`, and so is what the
last session had to interpret of the game (`capture.toml`; `-Capture
<file>` adds it to the file `build.ps1 -GameCapture` takes), of its
micro code (`vu-capture.toml`; `-VuCapture <file>` likewise) and of the
IOP's code (`iop-capture.toml`; `-IopCapture <file>`). Every session is
written down to be run again, in `build\game\<name>\play\sessions\<time>`
(its controller frame by frame, the clock it started at, its memory
cards and NVRAM as they were, a new card where a slot had none, the
patches it was played with; `ps2n-host -record`), and `game.ps1 -Replay
<that folder>` runs it again without a window, in any configuration,
`same-as-stock` included, with those patches (a session written down
before they were is given them: `-Patches`); each run says whether it is
the session at every fingerprint of its registers (ADR 0024, step G13). In a
window the console's clock starts at this computer's time of day; every
other run starts it at one fixed time (`ps2n-host -clock fixed`), or at
the replayed session's, so that two runs are the same machine. Windows
only so far.
A session that checks the window does it with the sound off, because the
sound comes out of the maintainer's speakers:
`scripts\windows\window-check.ps1 -Image <disc image> -Name <name> -Steps
'5 shot a.png', '8 key K', '14 close'` starts a muted session in a window
of 1280 by 720 (`-WindowSize ''` for the game's own default), has the
host write the picture on display at each `shot` and photographs the
window beside it (`a-window.png`: a picture Vulkan presents comes out
white in a photograph on this computer), presses keys in it and closes
it (look at the pictures with the Read tool).

## The tests: suites and configurations

Tests are named `<suite>/<configuration>/<program>`. Suites: `ps2autotests`
(console recordings), `programs` (our own, `tests/programs/*.s`, built with
`ps2n-recomp assemble`) and `sdk` (C programs built with ps2sdk, binaries in
`tests/sdk/bin`). Configurations: `stock-jit`, `stock-interp`
(PCSX2 as it ships), `ps2n-ee` (our providers, the EE's, the IOP's and
the VUs', interpreting), `ps2n-static` (our providers, the program
compiled ahead of time; a test program's micro code is interpreted, no
build having captured it, and its IOP code runs compiled where the host
has the block, as it has the BIOS's modules' with a game's capture built
in), `lockstep` (the compiled run compared with the interpreted one at
every event test, the IOP's state in the fingerprint), `same-as-stock`
(ADR 0011: the compiled run compared with a stock run at every sync
point: place, cycle count, the IOP's PC and cycle count, and the
registers, VU0's, VU1's and the IOP's among them), for the `programs`
and `sdk` suites `same-as-stock-ee130`, `-ee180`, `-ee300`, `-ee75` and
`-ee50` (the same comparison with PCSX2's EE cycle rate at that setting
on both sides, step G17), `never-further`
(ADR 0005: the compiled run's output against the stock run's, line by
line; CTest runs the two it compares first), and for the console-recorded
VU programs `ps2n-static-microvu` (compiled EE code with PCSX2's VU and
IOP recompilers under it, as before G11: tells a fault of the VU provider
or of the IOP provider from one of the EE's) and `ps2n-static-vu-thread`
(PS2Native's providers with VU1 carried out on PCSX2's own VU thread, ADR
0051: held to the recordings as `ps2n-static` is; the picture a test
program's VU1 draws has it too, `frames/software/ps2n-static-vu-thread/vu`,
beside `stock-jit-vu-thread`, PCSX2's VU recompiler on that thread). Every
other configuration has VU1 on the CPU thread: a run with the thread on is
not compared with another sync point by sync point. Also
`selftest/tables-and-semantics`: our instruction semantics
against PCSX2's interpreter and recompiler, and PS2Native's VU provider
against PCSX2's VU recompiler on random micro programs
(`host/selftest_vu.cpp`: 2,000 per VU in each of five settings, the fifth
with encodings PCSX2's VU recompiler knows nothing of, and for VU1 a sixth,
as on PCSX2's VU thread, where what a program's end tells the main thread
is compared with the rest; a program that
differs is made as small as it still differs in and printed with the
registers it uses); and `selftest/vu-compiled`, the first 150 of those
programs per VU and setting with every block compiled ahead of time
(skipped without the self-test's capture in the build, failed when a
block had to be interpreted). The same self-test holds PS2Native's IOP
provider to PCSX2's IOP recompiler on 4,000 random IOP programs
(`host/selftest_iop.cpp`: registers, memory and cycle counts after every
stretch of time, and the addresses at which each holds a block; a
program that differs is made as small as it still differs in and
printed), and `selftest/iop-compiled` runs the first 1,500 of them with
every block compiled ahead of time (skipped and failed likewise). The
module tree has one test more, `selftest/module-stack-probe`: the stack
probe a module defines for itself (`__chkstk`, `runtime/module_entry.cpp`,
which no test program's code calls), on functions whose locals take up to
fifty pages, on a thread whose stack Windows commits a page at a time.

## When a test fails

A failing test's files are in `build\windows-clang\ps2n\tests\<test name>`:
`core.log` (PCSX2's log), `guest.txt`, `actual.txt`.

A `lockstep` failure names the event test where the runs part, a
`same-as-stock` failure the sync point. The execution model's "Comparing
two runs" says how to go from there (`ps2n-host -sync`, `-sync-against`,
`-sync-detail`; they work under stock PCSX2 too).

## Looking at the generated code

Generated C++ is in `build\windows-clang\ps2n\generated`. To look at what
the recompiler makes of one program without building:
`build\tools-windows\bin\ps2n-recomp.exe info <elf>`. To read code at an
address, of an executable or of a raw image such as a BIOS:
`ps2n-recomp.exe disasm <file> --at <address> [--count n] [--base address]`.

## Known differences

A test that differs from the console recording goes into
`tests/ps2autotests-known-differences.cmake` only after its cause is known;
one whose run differs from the stock run, into
`tests/same-as-stock-known-differences.cmake`, likewise.

## What the host reports after a run

After a run, `ps2n-host` reports what the interpreter did. For a game whose
image is complete, "instructions inside the executable's code" must be
zero; when it is not, the line "compiled code handed over to the
interpreter" names the addresses. "The BIOS interpreted" says, piece by
piece, what of the BIOS was; with the BIOS compiled, that is 48
instructions at boot (blocks counted double) and one `di`/`eret` at
`8000517C`. "VU0's blocks of micro code" and "VU1's" say how many
different ones ran, compiled and interpreted; a line of its own says how
many instructions of encodings PCSX2's VU recompiler knows nothing of
the blocks held (carried out as it carries them out: not at all), and
another names what the VU provider does not carry out, where the run
stopped at it. "PS2Native's IOP provider" and "the IOP's blocks" say the
same for the IOP: its stretches of time, the blocks begun, formed and
thrown away, how many different blocks ran compiled and interpreted, how
many blocks PCSX2's IOP recompiler holds (none, when it carried out
nothing), and what the provider stopped at, if anything.

## Linux

Linux (step P4.7): `scripts/linux/build.sh` (its options are `build.ps1`'s)
and `test.sh` (`PS2N_BIOS=<BIOS image>`, `--build-dir` as `test.ps1
-BuildDir`, half the processors unless `--jobs`) are run in WSL 2 with Ubuntu 22.04 on this computer (ADR
0038), in a clone of the repository inside it, never on `/mnt/c` (Git for
Windows gives the working tree CRLF line endings, which PCSX2's shell
scripts do not run with). It needs CMake 3.25 or later (Qt 6.12), LLVM's
22.1.3 in `deps/llvm-22.1.3-linux`, and builds PCSX2's dependencies with
PCSX2's own script the first time. `scripts/linux/kit.sh` puts the Linux
folder together in `build/kit-linux` from the module tree built with
`--module on --wizard on` (`--archive`: a `.tar.xz` of it as well, which
keeps who may run a program): `kit.ps1`'s counterpart, with the libraries
a desktop Linux may not have taken from Ubuntu 22.04 into it (all but
those AppImages leave to the system, as PCSX2's own Linux release), each
finding the others beside it (`$ORIGIN`), and their licences
(`licences/Ubuntu`); `patchelf` does the run paths. Linux is reported as working only after
the maintainer's run on their own machine (ADR 0004); their run inside
this computer's WSL counts for P4.7, Linux then described as tested under
WSL 2 (ADR 0041). Inside WSL, Vulkan is the CPU's (llvmpipe) and OpenGL
the RTX through Mesa's D3D12 driver: a game played there takes OpenGL. A game the
maintainer may be playing on this computer keeps its pace: Linux builds and
tests on half the processors, Windows ones at below-normal priority.

## PCSX2's own programs

Never run stock `pcsx2-gsrunner` here: it writes into the maintainer's own
`Documents\PCSX2`.

## The website

The website: `scripts\windows\site.ps1 -Open` serves `docs` on this
machine as GitHub Pages serves it. It has no build step. The page repeats
numbers and states from `docs/game-log.md`, `docs/poc-log.md` and
`CLAUDE.md`'s "Where things stand": when one of those changes, change `docs/index.html` and the
date at its foot too (`docs/site/README.md` lists what comes from where).
It goes live when `main` is pushed. The repository's `README.md` repeats
the fewest of them (the version to download and its two files, the games
tried, where each system stands, the number of tests): a release, or a
change of one of those, changes it with the page.

## Batches on a computer someone is playing on

The development computer is the maintainer's, and they play on it. Whatever
runs for more than a moment keeps out of a game's way:

- **Look first.** Before anything heavy, see whether a game is running from
  `build\kit\games`, and read the answer before starting. The long scripts do
  it themselves, before each part (`Wait-NoGame`, `scripts\windows\common.ps1`),
  and wait until the game is closed.
- **Never stop a process a script did not start.** On 2026-11-21 a check
  stopped every `ps2n-host` it found and closed the maintainer's game. A
  script stops only a runtime started from its own folders (`Stop-StrayHost`).
- **On Windows: below-normal priority** (`Set-QuietPriority`), and **on a
  desktop of their own**, so that no window comes up over a game:

      & scripts\windows\hidden.ps1 -Script update-games.ps1 -Arguments '-Bios <BIOS image>' -Output build\measure\update-games.log

  Everything the script says goes to `-Output`; `hidden.ps1` writes "exit
  <code>" beside it (`...-hidden.log`) when it has ended. Call it with `&` from
  a PowerShell that stays, not through `powershell -File`. A session starts it
  in the background and reads the log when told it has ended.
- **Inside WSL: half the processors, at a lower priority**
  (`scripts/linux/common.sh`: `half`, `quiet`). `wsl.ps1` waits for a game
  played on Windows before it starts anything there.
- **One batch at a time** on each system, and a timing alone on the computer.
- **A window that has to be looked at** is opened muted (`window-check.ps1`),
  or on the hidden desktop: the sound comes out of the maintainer's speakers.

What is this computer's or a game's is not in the repository. The long
scripts take disc images, sessions and folders from their parameters and
from lists in the build tree, one line to an item, parts separated by `|`,
lines that begin with `#` left out:

| List | For | A line |
|---|---|---|
| `build\pgo\training.txt` | the runtime's profile of play (`runtime-profile.ps1`) | `<name> \| <disc image> \| <game folder> \| <session, or -> \| <frames, or 0> [\| vu-thread]` |
| `build\measure\timed.txt` | a timing, and the sessions a trial of PCSX2 runs again | `<name> \| <disc image> \| <measurement folder> \| <session> \| <renderer>` |
| `build\measure\update-sessions.txt` | the session a game folder is checked with, where the rule is not wanted | `<folder's name> \| <session's name or path, or ->` |
| `build/pgo/training.txt`, `build/timed.txt`, `build/update-sessions.txt` | the same inside WSL (`timed.txt` without the renderer) | |

A measurement folder is a copy of a game's folder, never the folder someone
plays from (`build\measure\<kit>\games`). `build\` holds all of this, the
maintainer's game folders with their memory cards and sessions, and the
profiles of play: it is never deleted or cleaned whole.

## Linux inside WSL, from Windows

`scripts\windows\wsl.ps1` runs one of `scripts/linux` in the clone inside WSL
(`~/ps2n` in Ubuntu 22.04), everything it says in a log on the Windows side:

    scripts\windows\wsl.ps1 -Script test.sh -Bios <BIOS image> -Log build\wsl\test.log
    scripts\windows\wsl.ps1 -Script build.sh -Sync tree -Arguments '--build-dir build/linux-clang-module' -Log build\wsl\build.log

- **`-Sync commit`** (the default) moves the clone's git to this tree's commit
  first (`scripts/linux/sync.sh`). The clone is left as it was where it has
  changes of its own, and the script says which.
- **`-Sync tree`** carries the working tree as it is, changes not committed
  too: to try a change on Linux before it is committed. Only files whose
  content differs are written, so nothing is built again for nothing. After
  the commit, `-Sync commit` finds the clone's files to be that commit's
  already.
- **Paths in `-Arguments` are Linux paths.** `C:\a\b` is `/mnt/c/a/b` there.
  Quote a path with spaces in single quotes.
- **Never sync while a script runs there.** A shell reads its script as it
  goes: a file changed under it breaks the run.
- **The clone's `origin` is GitHub's.** `kit.sh` names it in a folder's
  licences as where the source is.
- **For one command of your own,** write it into a file in the scratch folder
  and run `wsl.exe -d Ubuntu-22.04 -- bash /mnt/c/<that file>`: a command line
  given to `wsl.exe` loses its quotes on the way.

## The maintainer's game folders brought up to date

After a change the maintainer wants in their games, their folders get the new
programs the way a user's do, by the wizard (ADR 0048), and each is checked:
`scripts\windows\update-games.ps1`, and `scripts/linux/update-games.sh`
inside WSL. Their help has the parts.

- **Which folders.** On Windows, those of `build\kit\games`; `SLES_505.39`
  among them is an early check's and is left out (`-Except SLES_505.39`).
  Inside WSL, those of `build/PS2Native/games`. Asked about "my game folders",
  say both places.
- **Only at the maintainer's word,** and after the commit: the PS2Native
  folder is made from a tree with nothing left to commit, since its licences
  name the commit and say so when the tree had changes.
- **A trial first where the change is new:** the same script on copies
  (`-Games <a folder of copies>`, without the `build` and `kit` parts).
- **What is checked:** each folder compiles its game again (seconds to
  minutes); the start of one of its sessions, run again with the new programs,
  is the session at every fingerprint compared; the wizard asked again finds
  it up to date and changes nothing; its memory cards are the bytes they were.
- **Say so when reporting:** every update today has a game compile again
  whole, and a folder that has counted how its game is played count once more
  after its next session of three minutes (ADR 0049). The wizard tells one
  toolchain from another by its files' sizes and times, and every link of the
  runtime writes the host's import library again.

      & scripts\windows\hidden.ps1 -Script update-games.ps1 -Arguments '-Bios <BIOS image> -Except SLES_505.39' -Output build\measure\update-games.log
      scripts\windows\wsl.ps1 -Script update-games.sh -Bios <BIOS image> -Log build\wsl\update-games.log

## Compiled games timed against stock PCSX2

`scripts\windows\time-games.ps1` times the sessions of `build\measure\timed.txt`,
stock PCSX2 and the compiled game in turns; inside WSL, `release.sh --parts
stock,time` does it for `build/timed.txt`.

- **Stock PCSX2 is built as its releases are,** in a tree of its own
  (`build\windows-clang-stock`: link-time optimization, no profile, none of
  PS2Native's changes to PCSX2's code), never timed on the runtime with the
  profile. After a move to another PCSX2 that tree is built again, from
  nothing where PCSX2's dependencies changed.
- **Both sides run with PCSX2's VU thread on,** as PCSX2 has it by default
  where there are three cores or more. Say which stock PCSX2 a number is
  against whenever one is quoted (ADR 0051). Agent Under Fire's runs with the
  thread on part from their session, stock and compiled alike: its numbers
  are still times of the same work.
- **A measurement folder has its own counts,** taken on another session than
  the one timed, and a session that is timed is never among the profile's
  training runs.
- **Alone on the computer,** two rounds at least, the order turned round every
  other round. A difference smaller than the rounds' spread is not one.
- **Before a release's notes quote a number,** its own runtime is timed.

## A release

A release is made from a tree that is committed and pushed, its version in
`version.txt`, never a file named `VERSION` (ADR 0042). Both systems go
through the same parts (`scripts\windows\release.ps1`,
`scripts/linux/release.sh`; their help has them).

1. **The version and the commit.** `version.txt`, the documents, then commit
   and push, at the maintainer's word. From here until both downloads are
   made, nothing in the tree changes, documents neither: the folders'
   licences name the commit and say so when the tree had changes.
2. **Windows:**

       & scripts\windows\hidden.ps1 -Script release.ps1 -Output build\release\release.log -Arguments '-Bios <BIOS image> -Training <list> -Image "<disc image>" -Session "<a session of it>" -Disc "<a disc in another format>" -Earlier "<that game's folder as the release before made it>"'

3. **Linux,** at the same time or after:

       scripts\windows\wsl.ps1 -Script release.sh -Bios <BIOS image> -Log build\wsl\release.log -Arguments "--training <list> --patch-archive '<PCSX2's patches.zip>' --image '<disc image>' --session '<session>' --disc '<disc>' --earlier '<folder>' --to /mnt/c/<this tree>/build/release/<version>"

4. **Read both logs.** Every test passes in the trees the downloads are made
   from, none skipped; each download, unpacked, makes a game and runs a session
   again as itself; a folder of the release before is brought up to date.
5. **Time it,** alone on the computer: `time-games.ps1`, and `release.sh
   --parts stock,time`.
6. **The notes,** `build\release\notes-<version>.md`: what it is, the two
   downloads with their SHA-256, how it works, what it needs, the games tried,
   known limits, the licences. Each speed claim says which stock PCSX2. The
   maintainer reads them first.
7. **The draft:** `scripts\windows\release.ps1 -Parts draft`. Nothing is
   public yet.
8. **Published at the maintainer's word,** by them or by the session: `gh
   release edit v<version> --draft=false`. The tag is made then.
9. **After it is published:** the website's downloads and the README move to
   the version, in a commit of its own (a draft's files are not public
   before). At the maintainer's word their own PS2Native folders are made
   again (`-Parts theirs`, `--parts theirs`) and their game folders brought up
   to date.

`-Rehearsal` (`--rehearsal`) goes through it on any tree, into
`build\release\rehearsal`: it makes no draft and leaves the maintainer's
PS2Native folder alone.

What has gone wrong before:

- **The training folders' modules have to be this tree's.** Where the module
  version changed, the training runs would not load them: the `training` part
  brings each folder up to date and compiles it first. On Linux each training
  folder is one made on Linux.
- **A profile made again under the same name** is compiled with: the build
  gives the compiler a copy named after what the profile holds. What was
  compiled with two profiles does not link.
- **A session run again whole from a freshly made folder is a first session:**
  most of its code is interpreted, and it is slow. It is a check, not a
  timing.
- **`build\release\PS2Native` is 0.1's own folder,** kept to try updates on. A
  version's files are in `build\release\<version>`.
- **GitHub shows a file's size in binary megabytes.**
- **A correction to a published release's notes** is marked as a correction,
  and nothing else of the release is touched.

## A newer PCSX2 tried, and the move to it

PS2Native follows one version of PCSX2, the submodule's. Trying a newer one
moves nothing (ADR 0053, ADR 0061):

    scripts\windows\try-pcsx2.ps1
    & scripts\windows\hidden.ps1 -Script try-pcsx2.ps1 -Arguments '-Tag <tag> -Bios <BIOS image> -Parts all' -Output build\pcsx2-try\<tag>\all.log

- **The check,** the first line above, takes a minute and builds nothing. It
  says what PCSX2 changed since the version followed, and which of the files
  changed PS2Native follows or compiles: `scripts/pcsx2-watch.txt` lists
  those, each with what follows it. Add a file to that list when PS2Native
  comes to follow it.
- **The trial** is made in a worktree beside the repository, on a branch of
  its own, with build trees of its own. A tree built before PCSX2's
  dependency package changed mixes objects of both and the wizard built so
  crashed: never build a newer PCSX2 in the trees of the one followed.
- **Following what PCSX2 changed** is work done in the worktree, between its
  `build` and its `test`, until every test passes: semantics follow PCSX2's
  recompiler, and the console where a recording shows the recompiler wrong
  (ADR 0005). A list of known differences changes only with the cause known.
  Nothing is committed.
- **Where PS2Native's description of PCSX2's structures changes**
  (`runtime/include/ps2n/pcsx2_layout.h`), the module version is raised
  (`kGameModuleVersion`, `runtime/game_module.h`). The move to v2.9.189 forgot
  it, and modules compiled before crashed on the new runtime. The `build`
  part says so when the one changed and the other did not.
- **Where PCSX2's own code does not build here** (v2.9.198 left out an
  include that only a newer standard library than Ubuntu 22.04's brings in),
  the fix is a patch in the worktree's `patches/pcsx2`, in the form of the
  others, and is reported: whether PS2Native carries it is the maintainer's
  to decide (ADR 0050). The submodule is never edited.
- **A test that expects a difference fails when the difference ends.** A
  correction of PCSX2's turns up there: the run then says which entry of the
  lists of known differences to take off, and why.
- **Where PCSX2's scripts for its dependencies changed,** the worktree needs
  the package made from them: `-FetchDeps` fetches PCSX2's own, about 170 MB,
  at the maintainer's word. That path has not run yet.
- **The report,** `build\pcsx2-try\<tag>\report.md`, goes to the maintainer,
  who decides whether PS2Native moves.

A move, at their word, is the worktree's changes brought into `main` and
committed there (ADR 0054):

- the submodule at the tag, here and inside WSL (`sync.sh` fetches PCSX2's
  commit into the clone's own);
- PCSX2's dependency package in `deps`, where it changed, the one before kept
  beside it, and every build tree built again from nothing;
- the places that say which version PS2Native follows
  (`build\pcsx2-try\<tag>\names.txt` lists where the old one is named):
  comments that say what a file was transcribed from, the README, the website,
  `CLAUDE.md`'s "Where things stand";
- stock PCSX2's trees built again at the new version, before anything is timed;
- the worktree and its branch taken away: `try-pcsx2.ps1 -Tag <tag> -Remove`.

PCSX2's GitHub is read without signing in, and never written to
(`CLAUDE.md`, "PCSX2"): `git ls-remote` and `git fetch` for its source, the
API's `repos/PCSX2/pcsx2/...` for an issue or a pull request, sixty requests
an hour.
