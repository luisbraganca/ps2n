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
  a contract between the two. Its last two sections are the same for the
  vector units' micro programs and for the I/O processor's code.
- [docs/phase3-report.md](docs/phase3-report.md): what Phase 3 did, how it
  compares with stock PCSX2, what it leaves, and what Phase 4 has to decide
  first.
- [docs/phase4-proposal.md](docs/phase4-proposal.md): Phase 4's design as
  the maintainer decided it (ADR 0028), and its steps, P4.1 to P4.7, with
  their pass criteria.

## Status

**Phase 3 (the first real game: Killzone, then a second) ran from
2026-11-11 to 2026-11-16; its report is
[docs/phase3-report.md](docs/phase3-report.md). Phase 4 (the user
application) was approved on 2026-11-16 (ADR 0027), and the maintainer
decided its design the same day ([docs/phase4-proposal.md](docs/phase4-proposal.md),
ADR 0028): the wizard brings its own `clang` and `lld`, a game's code is
compiled on the user's computer into a module that a prebuilt runtime
loads, a packaged game is a folder, the wizard is Qt 6, Windows first. Its
steps are P4.1 to P4.7, each with its pass criterion; the first, P4.1,
measures what game code loaded as a module costs.**
What follows is Phase 3's history. [docs/game-log.md](docs/game-log.md) has its steps, their pass
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
time, its play as long as stock PCSX2's, still the stock run; there the
maintainer ended G10. Then, at the maintainer's word, step G11 (ADR 0022):
the vector units' micro programs compiled ahead of time, begun with a
measurement (`ps2n-host -vu-report`). In play VU1's micro memory is
rewritten about 40 times a frame, with 208 distinct pieces of code written
at 5,405 places, which PCSX2's VU recompiler makes 6,088 programs of; the
vector units took about a third of play's CPU-thread time. Then, by the
maintainer's choice, VU1's registers held to the stock run at every sync
point, and PS2Native's own VU interpreter (`runtime/vu_interp.h`) before
the translator. On 2026-11-13 the interpreter gave microVU's results on
16,000 random micro programs (`selftest/tables-and-semantics`), which took
following microVU's register allocation, VI and XMM, as well as its
instructions; and with both VUs on it the level run is the stock run at
every one of its 93,221,937 sync points, VU0's and VU1's registers
included. On 2026-11-14 the translator: what microVU's second pass decides
for a block is made once, as a list of steps (the block's plan,
`core/include/ps2n/vu_plan.h`), the same for the same code wherever the
game writes it, and each plan is compiled ahead of time as one C++
function (`ps2n-recomp vu`), which the host finds by a fingerprint of the
plan; blocks go on to each other as microVU's do, and the program at a
start or a jump is not looked for where it cannot have changed. Over the
level run VU1's 42,173 blocks in 6,088 programs have 589 plans, and after
one run and one rebuild every block run is compiled code's: nothing of the
game's micro code is interpreted, and nothing of it runs on PCSX2's VU
recompiler. 20,000 random micro programs give microVU's results, 1,500 of
them on compiled blocks alone; every test passes (468), PS2Native's
configurations on its own VU provider; the level run is the stock run at
every sync point, VU0's and VU1's registers included, and takes 0.97 times
stock PCSX2's time, its play 0.95. G11's pass criterion is met. Then, at
the maintainer's word on 2026-11-14 ("Let's work on the I/O processor"),
step G12 (ADR 0023): the IOP's code, the BIOS's modules and the game's,
compiled ahead of time, begun with a measurement (`ps2n-host -iop-report`).
Over the level run the IOP runs about 10,600 small blocks of code, nearly
all of them formed in the first 250 frames; 85% of its cycles pass in
stretches that end where its kernel's idle loop is, and the host spends
16% of the run in its provider, most of it around the IOP's code and not
in it. The IOP's place and registers are held to the stock run at every
sync point, as the EE's and the VUs' are. PS2Native's IOP provider
(`runtime/iop_provider.h`) keeps the record of blocks PCSX2's IOP
recompiler keeps, and carries a block out by its interpreter or by the
function a build compiled for it (`ps2n-recomp iop`), which it finds by a
fingerprint of the block's instructions wherever the IOP's kernel loaded
them. After one run and one rebuild every one of the level run's 911
million block runs of IOP code is compiled code's (8,023 blocks): nothing
of the IOP's code is interpreted, and nothing of it runs on PCSX2's IOP
recompiler. 4,000 random IOP programs give that recompiler's results,
1,500 of them on compiled blocks alone; every test passes (469),
PS2Native's configurations on its own IOP provider; the level run is the
stock run at every sync point, the IOP's place and registers included,
once both runs start with the console's clock at the same time
(`ps2n-host -clock`), and takes what it took with PCSX2's IOP recompiler
under it: 0.97 times stock PCSX2's time, its play 0.95. G12's pass
criterion is met: in the run into the first level no recompiler of
PCSX2's for PS2 code carries anything out, and none of ADR 0003's
stand-ins is in use. The next step, at the maintainer's word the same day
(ADR 0024), is G13: further into the game before Phase 3 ends, the
maintainer playing on in the window and what each session had to
interpret compiled into the next build. The first session, the same
evening (2 minutes 48 seconds, into the first level, on PCSX2's hardware
renderer), interpreted none of the game's EE code, 26 blocks of VU1's
micro code and 791 blocks of the IOP's, 772 of them in the memory card's
modules (the scripted runs have no memory card). They are compiled into
the build since; every test passes, and the scripted runs are still the
stock run and the interpreted run. At the maintainer's word the same
night: sessions are recorded, to be run again and compared as the
scripted runs are (ADR 0024), and the steps after G13 are G14 to G16
(ADR 0025): the window's defaults (Vulkan, a picture of 1920 by 1080, the
game drawn at three times the console's resolution), then PCSX2's
patches, with a game's widescreen and frame-rate patches applied by
default when there are any. G14 was done that night: the window's
defaults are PCSX2's Vulkan renderer, three times the console's
resolution and a picture of 1920 by 1080, full screen where a window that
size does not fit, the maintainer's choice. G15 the next day: the game's
widescreen patch applied by default, taken from the installed PCSX2's
archive; Killzone in 16:9, nothing of its code interpreted, and a run with
the patch is the stock run with it, the FPU's sign of a zero aside, and
the interpreted run. G16 the same day: the game's frame-rate patch applied
by default with it (the group of its patch file whose name says FPS, none
where several do), at first without the EE overclock its note asks for.
In the first level Killzone shows 41 new pictures a second where it showed
25; the code the patch changes is compiled as a second version of its
function, nothing interpreted after one rebuild; a run with both patches
is the stock run with them, the FPU's sign of a zero aside, and the
interpreted run, and takes 1.00 times stock PCSX2's time with the same
patches, its play 0.98 (without them 0.975 and 0.95, as before); in the
window the console's pace, as stock PCSX2 keeps it. G17 the same day, at
the maintainer's word: the EE overclock a patch asks for in its note
applied with it (130% for Killzone, with which stock PCSX2 shows 46 new
pictures a second in play where 41), and time counted as PCSX2's
recompiler counts it at that setting, at every rate it has (every test
program held to stock PCSX2 at five of them; 519 tests, all passing). The
level run with both patches and the overclock is the stock run with them,
the FPU's sign of a zero aside, and the interpreted run, nothing of it
interpreted after one rebuild, and takes 1.00 times stock PCSX2's time
with the same setting, its play 0.98, a block's cost read from a table
made when the setting is read (calling PCSX2's formula at every block's
end took 1.22); in the window the console's pace. A session run again now
gets a new memory card where it began without one, as the window did. G18
the same evening, at the maintainer's word ("let me test using agent under
fire"; then "Build multi-program support", ADR 0026): a second game,
*Agent Under Fire*, prepared for the maintainer to play, and games made of
several executables. Its boot executable loads three others itself, over
the same addresses, which PCSX2 never sees: each is now compiled ahead of
time, and the runtime tells from memory which is loaded (a test program
does the same; 533 tests, all passing). Over a script into its first
mission nothing of it is interpreted; the run is the stock run at every
sync point, every register included, and the interpreted run, and takes
1.19 times stock PCSX2's time without profile-guided optimization; in the
window the console's pace. The build tree holds its EE code in place of
Killzone's. On 2026-11-16 the maintainer played its first two missions:
HUD artifacts that stock PCSX2 shows too are left to PCSX2 (ADR 0026);
what the session had to interpret is compiled in since, and the session
run again is the stock run in place and time at every sync point (VU0's
FBRST aside, where PS2Native follows the console) and takes 1.13 times its
time; the explosion that slowed the session down, its micro code
interpreted then, takes 0.46 of the console's time now. In the second
session, the fourth mission slowed down: it waits in a small loop whose
page is not write-protected, and compiled code asked the runtime about its
blocks at every execution, 7.6 times stock PCSX2's time. At the
maintainer's word ("Fix it"; "we really need to fix this") the image's
table names such a block by a record of the runtime's, and compiled code
has its words compared at every execution as PCSX2's code does
(`checkedAnswer`): the mission takes 0.27 of the console's time at the
median, from 1.13, 1.8 times stock PCSX2's, and the session run again is
the stock run in place and time at every sync point. The third session
(the fifth mission) kept the console's pace in the window. G13 goes on as
the maintainer plays further. Asked what next, the session recommended
closing Phase 3 and going on to Phase 4; at the maintainer's "Yes" it wrote
the Phase 3 report ([docs/phase3-report.md](docs/phase3-report.md)):
compatibility, the comparisons with stock PCSX2, visual issues,
performance, regressions, limitations, and what Phase 4 has to decide
first. At the maintainer's word ("1. Yes"), Phase 3 ended there and
Phase 4 began (ADR 0027).

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
  confirmed there, and PCSX2 corrected it that night (in `v2.9.173`).
  15185 had no reply on 2026-11-14: from 2026-12-11, look by reading only
  whether it has one, and tell the maintainer. PCSX2's team asked there
  that no LLM-generated issues be sent.
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
  it. PCSX2 corrected it on 2026-11-12, first in `v2.9.173`; PS2Native
  still pins `v2.9.170`, and moving the pin is a decision of its own.
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
  stock PCSX2's, with profile-guided optimization. Ended there by the
  maintainer on 2026-11-13 (ADR 0022).
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
- [ADR 0022](docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md):
  step G11, inside Phase 3: the micro programs the game runs on VU0 and VU1
  compiled ahead of time by PS2Native's own translator, in the place of
  PCSX2's VU recompiler (microVU), the stand-in of ADR 0003, following it
  as ADR 0005 has it; next in ADR 0003's order of work. Begun with a
  measurement, the host watching the core's two VU providers
  (`ps2n-host -vu-report`). With it the maintainer chose: G10 ended; VU1's
  registers held to the stock run at every sync point, as the EE's and
  VU0's are; PS2Native's own VU interpreter before the translator, as the
  reference and what runs code a build has not compiled. Both done on
  2026-11-13: VU1's registers in the sync trace, and the interpreter
  (`runtime/vu_interp.h`, `ps2n-host -vu-ps2n`), which follows microVU's
  analysis (`core/include/ps2n/vu_mvu.h`), its record of programs and
  blocks, its instructions, and its allocation of host registers, where
  that shows in results (the high half of VI registers; which of two NaNs
  an AVX addition or multiplication gives). It refuses what it does not
  follow (`QuirkOf`, and the T and M bits on a branch), none of it in
  Killzone's code. The translator, at the maintainer's word on 2026-11-14
  ("maybe the translator?"), done that day: a block's plan
  (`ps2n/vu_plan.h`, the steps' semantics in `ps2n/vu_ops.h`), compiled
  micro code found by its plan's key (`ps2n-recomp vu`,
  `ps2n/vu_recomp.h`), blocks no build has compiled interpreted and
  captured (`ps2n-host -vu-capture`, `build.ps1 -VuCapture`), PS2Native's
  configurations on PS2Native's VU provider. What the session decided
  within the step is listed there, for the maintainer to decide otherwise.
- [ADR 0023](docs/adr/0023-the-iops-code-compiled-as-step-g12.md):
  step G12, inside Phase 3, at the maintainer's word on 2026-11-14 ("Let's
  work on the I/O processor"): the IOP's code, the BIOS's modules and the
  game's, compiled ahead of time by PS2Native's own translator, in the
  place of PCSX2's IOP recompiler, the last stand-in of ADR 0003,
  following it as ADR 0005 has it. Begun with a measurement
  (`ps2n-host -iop-report`). What the session decided within the step is
  listed there, for the maintainer to decide otherwise: the IOP's place and
  registers held to the stock run at every sync point (on the strength of
  the answer for VU1's in ADR 0022); PS2Native's own IOP provider
  (`runtime/iop_provider.h`), which keeps the record of blocks PCSX2's IOP
  recompiler keeps and interprets them; IOP code compiled block by block,
  each as that recompiler formed it, found by a fingerprint of its
  instructions wherever it is loaded (`ps2n-recomp iop`,
  `ps2n/iop_recomp.h`); blocks no build has compiled interpreted and
  captured (`ps2n-host -iop-capture`, `build.ps1 -IopCapture`); what it
  does not carry out refused by name; PS2Native's configurations on
  PS2Native's IOP provider; and every run that is not played in a window
  started with the console's clock at one fixed time (`ps2n-host -clock`),
  since the first comparison with the IOP in it showed two runs reading
  different times of day. The self-test found a defect of PCSX2's IOP
  recompiler on the way (a jump through a register that goes to address
  0): the provider stops at that form. On 2026-11-14 the maintainer chose
  to tell PCSX2 themselves, in an issue of their own, and asked for a
  draft to go by; the session wrote none, as ADR 0010 has it, and gave
  where the defect is and how it is seen (the ADR's section on it). The
  maintainer opened it the same day: PCSX2's issue 15192.
- [ADR 0024](docs/adr/0024-further-into-the-game-before-phase-3-ends.md):
  the next step, G13, inside Phase 3, at the maintainer's word on
  2026-11-14 ("Ok", to the session's recommendation): further into the
  game before the phase ends. The maintainer plays on in the window,
  beyond what the scripts reach; each session writes down what it had to
  interpret (the game's EE code, the micro code, the IOP's code); a
  session puts that into the captures, makes the build again, checks it
  again and reports what new code each part of the game brought, and what
  went wrong. The step's criterion is the session's proposal, and how far
  is far enough is the maintainer's to say. Begun on 2026-11-14 with a
  first session of the maintainer's (`game.ps1 -Play` with `-Capture`,
  `-VuCapture` and `-IopCapture`; the game log's "How to run it", and
  "G13" for what it brought). The maintainer's answer after it ("1.
  Yes"): a session records what it takes to run it again (its
  controller's input frame by frame, its memory card and its clock at the
  start), and a session is run again without a window and compared. On
  2026-11-15 ("Yes, as a small next step"): it records the patches it was
  played with too, which a run of it again applies.
- [ADR 0025](docs/adr/0025-how-a-game-is-shown-and-its-patches.md): how
  a game is shown by default, at the maintainer's word on 2026-11-14
  ("I want the defaults to be: 1920x1080, Vulkan, Automatically apply
  Widescreen/FPS patches if available"; "let's split into several
  steps"): G14, the window's defaults (PCSX2's Vulkan renderer, a picture
  of 1920 by 1080, the game drawn at three times the console's
  resolution, each changeable); G15, PCSX2's patches available to the
  host and a game's widescreen patch applied by default, where the
  patches come from put to the maintainer first (a new dependency); G16,
  a game's frame-rate patch likewise, its speed measured. The comparisons
  keep the software renderer. Phase 4's wizard starts from these
  defaults. The patches come from the installed PCSX2's archive, read
  where it is (`C:\Program Files\PCSX2\resources\patches.zip`, 2.8.2; the
  maintainer's answer on 2026-11-15, "The installed PCSX2"): the
  repository it is made from declares no license, and where a packaged
  game gets its patches is Phase 4's. A frame-rate patch is the group of
  the game's patch file whose name says FPS (none where several do). At
  the maintainer's word on 2026-11-15 ("Sure apply recommended PCSX2
  patches"), step G17: where a patch applied asks in its note for PCSX2's
  EE overclock (`EECycleRate`), the host applies it (the percentage the
  note names, 130% where it names none; `-ee-cycle-rate` given keeps its
  own), and PS2Native counts time as PCSX2's recompiler counts it with that
  setting; a comparison gives stock PCSX2 the same setting.
- [ADR 0026](docs/adr/0026-a-second-game-and-games-of-several-executables.md):
  step G18, at the maintainer's word on 2026-11-15 ("let me test using
  agent under fire ... Prepare it and do some initial testing"; then
  "Build multi-program support"): a second game, *007 - Agent Under Fire*
  (`SLES-50539`), the maintainer's copy in `C:\ps2\agent-under-fire`, used
  as Killzone's is (read where it is, never copied, everything generated
  in the build tree), prepared for the maintainer to play; and games made
  of several executables. Its boot executable loads three others itself
  (`SifLoadElf`, not the BIOS's loader), over the same addresses, which
  PCSX2 never sees: each is compiled as an image of its own (`ps2n-recomp
  compile --loaded-by`, `build.ps1 -GameMore`), which the runtime puts in
  use when memory holds its code, and what a run interprets in it is
  captured under its CRC. `ACTION.UK` is the English missions'; another
  language's is interpreted. After the maintainer's first session
  (2026-11-16, "Ok let's leave them"): artifacts of PCSX2's renderer that
  stock PCSX2 shows too are left to PCSX2; a setting of PCSX2's own that
  removes them is put to the maintainer when one is found.
- [ADR 0027](docs/adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md):
  Phase 3 ends on 2026-11-16 with its report
  ([docs/phase3-report.md](docs/phase3-report.md)), and G13 with it.
  Phase 4, the user application, is approved, and begins with a design
  proposal the maintainer decides on before any code is written, first of
  all how a user's computer compiles a game.
- [ADR 0028](docs/adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md):
  Phase 4's design, at the maintainer's word on 2026-11-16, every
  recommendation of [docs/phase4-proposal.md](docs/phase4-proposal.md)
  accepted. A game is compiled on the user's own computer by a `clang` and
  `lld` the wizard brings; its generated code is freestanding (no PCSX2
  header, no standard library, the machine state in PS2Native's own
  structures checked against PCSX2's) and compiled into a module that a
  prebuilt runtime (PS2Native's runtime and PCSX2's core) loads, the BIOS's
  code a module of its own. A packaged game is a folder, the disc and BIOS
  images read in place. New code a player meets is compiled by the game in
  the background, outside its process, for its next start, the wizard's
  recompile first. The wizard is Qt 6. Patches come from an installed
  PCSX2's archive or its release fetched at the user's word, never shipped.
  No profile is trained for the first version (1.1 to 1.2 times stock
  PCSX2's time), speed later. Windows first, the design portable. The
  steps P4.1 to P4.7 in the proposal's order, beginning with what the
  module costs.

## Layout, build and test

```
third_party/pcsx2         PCSX2 v2.9.170, a pinned submodule. Never edit it; changes go in a patch queue (none so far)
third_party/ps2autotests  hardware-verified PS2 test programs, a pinned submodule
cmake/ps2n-inject.cmake   adds PS2Native's targets to PCSX2's own CMake project without touching it
CMakeLists.txt, core/     build-time tools, a project of their own with no PCSX2 dependency: the
                          instruction decoder, disassembler, assembler, ELF/IRX/SNR2 readers, the reader of a
                          BIOS image's directory (romdir); for the vector units, the first pass of PCSX2's VU
                          recompiler (ps2n/vu_mvu.h) and a block of micro code as a list of steps, its plan
                          (ps2n/vu_plan.h, vu_steps.inc); for the IOP, its instructions as PCSX2's IOP
                          recompiler treats them and what a block of them is (ps2n/r3000a.h); all used by the
                          runtime too
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
                          contract for compiled IOP code (ps2n/iop_recomp.h)
host/                     ps2n-host: console frontend for the PCSX2 core, the test bench (the self-test of the
                          instruction semantics, of the VU provider and of the IOP provider; the watches on the
                          core's providers: sync_trace, vu_report, iop_report); with -window, where a game
                          is played (window.cpp: Windows only so far)
tests/unit/               unit tests of the build-time tools
tests/                    CTest registration for programs booted on the core, the driver script, the recorded
                          baselines; recompiles the test programs into the build tree and links them into the host
tests/programs/           our own test programs, in assembly for core's assembler: selfmod (rewrites its code), bench,
                          timeout (a delay loop that PCSX2 does not run), countcarry (a defect of PCSX2's
                          recompiler that PS2Native reproduces), loader (loads the two programs in loader/ itself,
                          over the same addresses, as a game of several executables does)
tests/sdk/                C test programs built with ps2sdk (prebuilt in bin/), their sources, what stock PCSX2 prints
tests/games/              controller scripts for runs of a game (button presses we wrote; nothing from a game)
scripts/windows, linux    build and test entry points
assets/                   branding/ (the logo, the icons, the colours; its README says which to use where) and
                          ps2n.rc (the icon of the Windows executables)
docs/                     brief, assessment, decisions (adr/), proof-of-concept log, game log, Phase 3 report, Phase 4
                          proposal; and the website,
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
- The game: `scripts\windows\build.ps1 -Game <its boot executable>` compiles
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
- The vector units' micro code (step G11): in PS2Native's configurations
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
  is what any change to how blocks are planned does. So does a game's
  capture go stale: run the game with `-VuCapture` again and rebuild. The
  captures are derived from the game and stay in the build tree. The
  execution model's "The vector units' micro programs" says how it works.
- The IOP's code (step G12): in PS2Native's configurations the IOP runs on
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
  is what a change to the programs the self-test makes does. The captures
  are derived from the BIOS and the game and stay in the build tree. What
  the provider does not carry out it stops at, with an error that names it
  (`r3000a::shape`, `Refuse` in `runtime/iop_provider.cpp`). The execution
  model's "The I/O processor's code" says how it works.
- Playing the game: `scripts\windows\game.ps1 -Image <disc image> -Play
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
  stock-jit` puts stock PCSX2 in the same window. `game.ps1 -Patches
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
- Tests are named `<suite>/<configuration>/<program>`. Suites: `ps2autotests`
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
  or of the IOP provider from one of the EE's). Also
  `selftest/tables-and-semantics`: our instruction semantics
  against PCSX2's interpreter and recompiler, and PS2Native's VU provider
  against PCSX2's VU recompiler on random micro programs
  (`host/selftest_vu.cpp`: 2,000 per VU in each of five settings, the fifth
  with encodings PCSX2's VU recompiler knows nothing of; a program that
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
  every block compiled ahead of time (skipped and failed likewise).
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
