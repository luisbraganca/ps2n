# The decisions

Every decision the maintainer has made, one record each, in the order they
were made. Each entry below is its record's digest, as sessions wrote it into
`CLAUDE.md` until 2026-11-27
([ADR 0061](0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md)),
when the digests were moved here word for word.

- **Read the record itself before changing what it decides.** It has the
  maintainer's words, how they were read, and what the session chose within
  them, "for the maintainer to decide otherwise".
- **A later record may replace part of an earlier one.** The digests say so;
  the earlier record often does not.
- **A new decision** gets a record of its own, the next number, and one entry
  at the end of this list.

## By subject

The numbers are the records' own; each is in the list below.

| Subject | Records |
|---|---|
| What PS2Native is, and on what | 0001 (the platforms), 0002 (the game's code compiled, PCSX2's core the machine), 0003 and 0006 (no recompiler for PS2 code in a game), 0007 (the BIOS's code compiled too), 0028 (Phase 4's design) |
| What counts as correct | 0005 (never further from the console than PCSX2), 0011 (time counted as PCSX2's recompiler counts it), 0013 (a defect of PCSX2's reproduced until it was corrected) |
| PCSX2: following it, changing it, telling it | 0004 (the pinned version), 0010 (a fix goes upstream through the maintainer), 0050 (PS2Native's first change to PCSX2's code), 0053 and 0054 (the move to v2.9.189), 0056 (`nextBlockCycles` kept alone), 0061 (a newer version tried by one command), 0062 (the move to v2.9.198, and a change to its code that only lets it compile) |
| Branding and the website | 0012, 0014 |
| Commits, and sessions at work at once | 0018, 0020, 0021 |
| The phases and their steps | Phase 3: 0009, 0015, 0016, 0017, 0018, 0019, 0022, 0023, 0024, 0026, 0027. Phase 4: 0028, 0033, 0036, 0037, 0038, 0041. Phase 5: 0053, 0061, 0062 |
| How a game is shown and played | 0025 (the window's defaults, the patches), 0032 (the wizard goes through PCSX2's settings), 0035 and 0055 (the notice over the picture, the pace a session kept), 0039 (full screen), 0060 (the menu over the game), 0064 (a second controller) |
| The toolchain, a game's folder and its own compile | 0029, 0030, 0031, 0034, 0040, 0044, 0045, 0048, 0057 |
| Speed | 0015, 0019, 0043, 0046, 0047, 0050, 0051, 0052 |
| Linux | 0004, 0008, 0038, 0041 |
| Releases | 0042, 0047, 0049, 0052, 0058, 0059, 0063 |
| The games | 0009 (Killzone), 0026 (Agent Under Fire), 0054 (three more made ready), 0056 (Jak and Daxter at a 130% EE cycle rate) |
| How the project keeps itself | 0061 (the release's scripts in the repository, `CLAUDE.md` cut to what a session needs) |

## In the order they were made

- [ADR 0001](0001-platform-priorities.md): Windows x64 and Linux x64
  are required; macOS, Android and iOS are optional. Build and test nothing
  for an optional platform unless asked.
- [ADR 0002](0002-static-cpus-on-pcsx2-core.md): the game's code is
  translated ahead of time and runs on PCSX2's core as the machine. A BIOS is
  required.
- [ADR 0003](0003-no-jit-in-generated-games.md): no recompiler for
  PS2 code ships in a generated game. PCSX2's VU and IOP recompilers are
  temporary, labeled stand-ins; its EE recompiler is never one. Unseen code
  is interpreted once, reported, captured and rebuilt.
- [ADR 0004](0004-proof-of-concept-environment.md): PCSX2 `v2.9.170`
  as a submodule (`v2.9.189` since ADR 0054); the `SCPH-70004` BIOS read in place; Linux is built and
  tested by the maintainer on their own machine, so never report Linux as
  working without their run.
- [ADR 0005](0005-never-further-from-the-console-than-pcsx2.md):
  what counts as correct. On no line of a console-recorded test may stock
  PCSX2 be right and PS2Native wrong, and every remaining difference is
  listed with its cause. Instruction semantics follow PCSX2's recompiler,
  and the console where a recording shows the recompiler wrong.
- [ADR 0006](0006-no-jit-rule-covers-ps2-code-only.md): the no-JIT
  rule covers PS2 program code only. PCSX2's VIF unpack recompiler and its
  software renderer's code generator stay.
- [ADR 0007](0007-bios-code-translated-ahead-of-time.md): the
  BIOS's EE code is to be translated ahead of time, on the user's machine
  from their own dump, as a file of its own beside the game. A BIOS image is
  still required at run time. Begun on 2026-11-12 as step G8 (ADR 0017).
- [ADR 0008](0008-linux-run-deferred.md): the first Linux run is
  deferred until the maintainer chooses. Linux is still required.
- [ADR 0009](0009-phase-3-first-game-killzone.md): Phase 3 is
  approved; the first game is Killzone (`SCES-52004`).
- [ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md):
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
- [ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md):
  time is counted exactly as PCSX2's EE recompiler counts it. A run on
  PS2Native is the stock run, sync point for sync point; a difference is a
  defect to find, unless it comes from an instruction that deliberately
  follows the console (ADR 0005) and is listed with its cause.
- [ADR 0012](0012-ps2native-branding-not-pcsx2s.md): what PS2Native
  builds carries PS2Native's branding (`assets/branding`) and never PCSX2's
  logo; PS2Native's logo is not put on PCSX2's interface either. PCSX2's
  name stays wherever it is attribution: logs, license texts, copyright
  headers, documentation. A finished game shows nothing of what the core can
  draw over the picture (messages, performance figures, indicators): that is
  switched off by settings, never restyled by patching PCSX2. The host's
  window does all of this (`ps2n-host -window`).
- [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md):
  a defect of PCSX2's recompiler (reading the count register carries into
  `EntryHi`) is offered upstream the same way, as an issue of its own, and
  reproduced by PS2Native until the pinned PCSX2 has the correction, as
  long as it costs nothing in speed and changes nothing on screen. (Had the
  issue been ignored for 30 days, the correction would have been carried
  locally; it was confirmed instead.) `tests/programs/countcarry.s` shows
  it. PCSX2 corrected it on 2026-11-12, first in `v2.9.173`; PS2Native
  pinned `v2.9.170` until it moved to `v2.9.189` on 2026-11-23 (ADR 0054),
  and reproduces it no more: the program holds both to the 32-bit addition.
- [ADR 0014](0014-the-website-is-served-from-docs.md): the project
  has a website, one page that GitHub Pages serves from `docs/` as it is
  (`docs/.nojekyll`). The documentation stays where it is; the site is
  `docs/index.html`, `docs/404.html` and `docs/site/`. The page says only
  what the logs say, with dates, and shows nothing from a game or a BIOS.
- [ADR 0015](0015-a-speed-step-before-phase-3-ends.md): a speed
  step inside Phase 3 (G6) before the phase ends. The generated code is
  made faster one change at a time, each measured on the level run and
  with the profile; the compiled game stays the stock run. What comes
  after the two changes named is decided by the profile and reported; a
  change of another kind (to how time is counted, to what is compared with
  stock PCSX2) is put to the maintainer first.
- [ADR 0016](0016-vu0-instructions-with-semantics-of-ps2natives-own.md):
  the speed step ended where it stood on 2026-11-12. The next step, G7:
  the VU0 instructions in EE code carried out by semantics of PS2Native's
  own, following PCSX2's recompiler (microVU in macro mode) as ADR 0005
  has it, in place of PCSX2's interpreter functions. What the recompiler
  itself hands to a function of the core (`VCALLMS`, `VCALLMSR`) stays
  that function; when VU0 is brought up to date, and which flags reach the
  registers, stay as the recompiler decides them for the block. Done on
  2026-11-12 (`runtime/include/ps2n/ops_vu0.h`).
- [ADR 0017](0017-bios-ee-code-compiled-as-step-g8.md): the next
  step, G8, inside Phase 3: the BIOS's EE code compiled ahead of time, as
  ADR 0007 has it, beginning with the four pieces that run under fast boot
  (the reset code, the kernel, `EELOAD`, `EENULL`). During development the
  translation is linked into `ps2n-host` from the build tree, made from the
  dump the build is given and never in the repository; a file of its own
  beside a packaged game is Phase 4's. Done on 2026-11-12: four images,
  and the code the game copies into kernel memory compiled with the game
  (`game_outside`).
- [ADR 0018](0018-vu0-instructions-carried-out-by-compiled-code.md):
  step G9: compiled code carries out a VU0 instruction itself, inline, where
  the runtime's record of its block says nothing is done about VU0 first
  (`PS2N_VU0`); elsewhere the runtime as before. Made in a worktree of its
  own (`..\ps2n-vu0`, branch `vu0-direct`) while another session worked on
  G8 in the main tree; the two sessions agree before timing anything, and
  whichever step is committed second is rebased onto the first. Done on
  2026-11-12.
- [ADR 0019](0019-performance-as-the-research-lays-it-out.md):
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
- [ADR 0020](0020-no-claude-references-in-commit-messages.md):
  no commit message references Claude: no `Co-Authored-By` line naming
  Claude, no `Claude-Session` line. The history was rewritten on
  2026-11-12 to remove them (messages only; files, authors and dates
  unchanged) and force-pushed. Mentions of the file `CLAUDE.md` are fine.
- [ADR 0021](0021-two-sessions-on-step-g10.md): two sessions on
  G10 at once, the maintainer's choice: the second in the main tree, the
  first in its worktree, each with its own changes, saying "go" and "done"
  around every timing. The second's: calls through a register made by
  compiled code (`callSites`, `PS2N_CALL_DYNAMIC`), `ps2n-host -code-size`,
  the way a load or store seldom takes made short (one runtime call, no
  registers put back): play 1.18 to 1.14 times stock PCSX2's time without
  profile-guided optimization; the FPU's settings read once per function
  tried and taken out.
- [ADR 0022](0022-vu-micro-programs-compiled-as-step-g11.md):
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
- [ADR 0023](0023-the-iops-code-compiled-as-step-g12.md):
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
- [ADR 0024](0024-further-into-the-game-before-phase-3-ends.md):
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
- [ADR 0025](0025-how-a-game-is-shown-and-its-patches.md): how
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
- [ADR 0026](0026-a-second-game-and-games-of-several-executables.md):
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
- [ADR 0027](0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md):
  Phase 3 ends on 2026-11-16 with its report
  ([docs/phase3-report.md](../phase3-report.md)), and G13 with it.
  Phase 4, the user application, is approved, and begins with a design
  proposal the maintainer decides on before any code is written, first of
  all how a user's computer compiles a game.
- [ADR 0028](0028-phase-4s-design-a-shipped-compiler-and-game-modules.md):
  Phase 4's design, at the maintainer's word on 2026-11-16, every
  recommendation of [docs/phase4-proposal.md](../phase4-proposal.md)
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
- [ADR 0029](0029-a-games-module-reaches-the-runtime-through-its-import-table.md):
  a game's module reaches the runtime through its import table, as an
  ordinary DLL does; no loader of PS2Native's own. The maintainer's rule
  on 2026-11-16 ("If the custom loader makes the game faster, I'd go with
  the custom loader. If not, your recommendation"; "If it doesn't affect
  game speed, only compile speed, I'd also go with your recommendation"),
  applied to a measurement of 2026-11-17: the same generated code reaching
  the core directly, as such a loader would have it, took longer than
  through the import table (Agent Under Fire's second session 1 against
  0.977, its fourth mission 1 against 0.954), why not known yet.
  `build.ps1 -Module imports` measures it.
- [ADR 0030](0030-the-wizards-compiler-is-llvms-own-release.md):
  the `clang` and `lld-link` the wizard ships are LLVM's own release, 22.1.3
  (the version the development builds use), from its Windows x64 archive
  downloaded once into `deps`, of which only what a module needs is kept;
  not Visual Studio's copy. P4.3's criterion is checked on another computer
  of the maintainer's, without Visual Studio, with a folder and one command
  the session prepares. The maintainer's choices on 2026-11-17, both the
  session's recommendations.
- [ADR 0031](0031-microsofts-c-runtime-travels-in-the-folder.md):
  Microsoft's C++ runtime (`msvcp140.dll`, `vcruntime140.dll`,
  `vcruntime140_1.dll`), which the runtime and PCSX2's libraries need,
  travels in the wizard's folder and in every game folder, from Visual
  Studio's redistributable files; nothing is installed for it, and newer
  copies come with PS2Native's updates. The maintainer's choice on
  2026-11-17, the session's recommendation.
- [ADR 0032](0032-the-wizard-goes-through-pcsx2s-settings.md):
  the wizard goes through every setting of PCSX2's that applies to a game,
  at PCSX2's defaults but the window's (ADR 0025: the patches with their
  overclock, Vulkan, 1920 by 1080 at three times the console's
  resolution); those that change how the processors are emulated shown
  but fixed at what PS2Native follows, each changeable once checked
  against PCSX2; the core's on-screen display not offered (ADR 0012);
  PCSX2's own per-game settings pages inside the wizard if they build into
  it unchanged, PS2Native's own otherwise; PCSX2's program settings,
  hotkeys and RetroAchievements left out. The maintainer's words on
  2026-11-17 ("the wizard should go through all the PCSX2 settings"), and
  four recommendations accepted. On 2026-11-18, at the maintainer's word
  ("this setting on the wizard should always be as widescreen (16:9)"), the
  aspect ratio's default is 16:9 for every game, not PCSX2's "Auto
  4:3/3:2" (`ps2n::game::SetDefaults`).
- [ADR 0033](0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md):
  P4.5 is done: the maintainer's run of 2026-11-18 (Gran Turismo 3, made
  without a command line, three settings changed) counts for its
  criterion. What was left of it is a step of its own, P4.8, the folder for
  others, done before the folder `kit.ps1` makes goes to anyone but the
  maintainer: the licences of everything in it, disc images other than
  `.iso`, PCSX2's patches where PCSX2 is not installed, a fixed setting
  edited by hand refused, a choice of the executables compiled. P4.6 is
  next, beginning with the game's code a session finds outside its
  executables compiled in parts. The maintainer's word: "Agree", to the
  session's recommendation.
- [ADR 0034](0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md):
  how a game compiles new code itself (P4.6), the maintainer's two choices
  on 2026-11-18: every game's folder carries its own copy of the toolchain
  (about 200 MB more per game), so it compiles wherever it is, against the
  session's recommendation (PS2Native's folder's); and it says so with an
  icon in Windows' notification area while it compiles and a notification
  when the next start will have it, nothing when a session met nothing new.
  The rest as the session laid it out: the compile starts when the window
  closes, at low priority, only what changed, the new modules taken at the
  next start.
- [ADR 0035](0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md):
  at the maintainer's word on 2026-11-18, having tried it ("The compilation
  notification was really delayed... It should show right after closing
  the game showing that is compiling, than a different one saying it's
  compiled. Also when the game is running on interpreted code that can be
  compiled, there should show an overlayed notification saying so"): a
  game's own recompile shows one notification as soon as it finds the
  session met new code (it merges only the sessions it has not merged,
  `captures\merged.txt`) and a different one when it is done; and while a
  game in its folder runs interpreted code a compile would compile, a
  notice of PS2Native's own shows over its picture, in a window of
  PS2Native's laid over the game's (`host/window.cpp`, `ShowNotice`), never
  PCSX2's on-screen display, which ADR 0012 keeps off. The session's
  choices, for the maintainer to decide otherwise: when over the last half
  second at least 1% of the EE's, the IOP's or the VUs' work was
  interpreted (any of it since ADR 0055, the BIOS's at boot aside; of the
  EE's, since ADR 0057, what a run records for the next compile); five
  seconds; at most once a minute; top left; the brand's colours.
- [ADR 0036](0036-the-folder-for-others-how-p4-8-is-done.md):
  P4.8, the folder for others, is next, before P4.7 (the maintainer's
  "Yep", 2026-11-18), its `.chd` the maintainer's Silent Hill 3 ("I do,
  there's a chd file for silent hill 3"). The session's choices, for the
  maintainer to decide otherwise: a disc read through PCSX2's own readers
  by the runtime (`ps2n-host -disc-executables`, `-disc-extract`), what is
  looked for on it PS2Native's (`ps2n/iso9660.h`, free of exceptions), a
  `.cue` standing for the image it names; PCSX2's archive of patches
  fetched at a button from the latest release of PCSX2's patches project
  (the file PCSX2's own builds take), or a file given, kept in the folder's
  `patches\`; a fixed setting in a game's settings file refused by the
  runtime, naming it; every executable ticked, the boot one always; the
  licences written by `kit.ps1`, Qt's from Qt's SBOMs and source, with
  where each source is; and, found making Silent Hill 3 (one segment, data
  with code, its last function a megabyte), a function cut into pieces of
  about 16,384 instructions, which changes no other game's analysis.
- [ADR 0037](0037-p4-8-done-a-game-does-not-play-while-it-compiles.md):
  P4.8 is done: the maintainer's run of 2026-11-18 on their Windows 10
  computer, which has no PCSX2, counts for its criterion ("Everything went
  flawlessly"). And, in their words, "running the game while compilation
  is running in the background shouldn't be allowed". Started while the
  folder's recompile compiles, the launcher waits for it in a window that
  says so (Cancel leaves the game unstarted, the compile going on), then
  plays with what it compiled: the session's choice, which the maintainer
  made theirs on 2026-11-19 ("2. Yes"; with it "4. Ok", P4.7 next). The
  session's choices, for the maintainer to decide otherwise: compiling is
  while `ps2n-remake` holds
  `work\remake.lock`, which it now lets go of as soon as its modules are
  ready, or for 20 s after one is started, before it takes it; the window
  shows after a second and a half.
- [ADR 0038](0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md):
  P4.7 is built and tested first on a Linux inside the development
  computer, WSL 2 with Ubuntu 22.04 (PCSX2 makes its own Linux releases on
  it), the packages PCSX2's Linux build needs installed in it by the
  session: the maintainer's "1. Sure." on 2026-11-19. ADR 0004 stands:
  Linux is reported as working only after the maintainer's run on their own
  machine. That machine is a laptop with Linux Mint and, as the maintainer
  thinks, NVIDIA's GTX 1660; P4.7's criterion, the session's proposal, is
  accepted ("3. Yes"): there, a game made with the wizard from its disc
  image plays from its folder at the console's pace, and what a session
  meets for the first time is compiled in the background for its next
  start.
- [ADR 0039](0039-the-notice-over-a-full-screen-game-exclusive-full-screen-disallowed.md):
  the notice over the picture (ADR 0035) reached the screen over a game
  covering it only at a session's start: PCSX2's default lets the graphics
  driver take such a game over (Vulkan's full-screen exclusive), which
  NVIDIA's does once it has run a moment, and from then on nothing Windows
  lays over it shows. At the maintainer's "Fix it" (having watched both),
  exclusive full screen is disallowed by default on Windows
  (`ExclusiveFullscreenControl = 0`, `ps2n::game::SetDefaults` and every
  window run), a game's settings file may allow it again; the game folders
  on this computer got the line in their settings files; and the runtime's
  report says how near the notice's line a run came and how often it was
  shown.
- [ADR 0040](0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md):
  at the maintainer's word on 2026-11-19 ("Compiling shouldn't be done on
  the background, but yes on a foreground modal/window ... the user is able
  to click 'Later' or close the modal/window (which is the same thing) ...
  next time the game runs, it will first compile pending compilations before
  actually running"; "It shouldn't be possible to run the game without
  compiling pending compilations first"), what a session met is compiled at
  once, at normal priority, in a window of its own, in front (how far it has
  got, Later); Later stops it, what was compiled kept; the game never starts
  with anything left to compile: before it starts, `ps2n-remake
  --before-play` compiles what is left in the same window, and Later there,
  or a compile that fails, leaves the game unstarted. It replaces ADR 0034's
  background compile, ADR 0035's icon and notifications (its notice over the
  picture stays) and ADR 0037's wait. The session's choices are listed there,
  for the maintainer to decide otherwise.
- [ADR 0041](0041-p4-7s-run-inside-wsl-on-the-development-computer.md):
  P4.7's criterion is met by the maintainer's own run inside the Ubuntu
  22.04 of WSL 2 on the development computer, the Linux laptop not being
  theirs to use (their "Yes", 2026-11-19): a game made with the Linux
  wizard from its disc image plays from its folder at the console's pace,
  and what a session meets is compiled for its next start. There the
  renderer is OpenGL (Mesa's D3D12 driver on the RTX 4070 SUPER; Vulkan
  inside WSL is the CPU's), the keyboard plays, and a desktop shortcut
  starts the Linux `PS2Native` through `wslg.exe`. Until a Linux machine of
  its own runs PS2Native, Linux is described everywhere as tested under WSL
  2 (Ubuntu 22.04) on the development computer. The maintainer's run, Jak
  and Daxter made with the Linux wizard and played for five sessions, about
  90% of the console's pace inside WSL, counted ("I guess it's good").
- [ADR 0042](0042-releases-on-github-beginning-with-0-1.md):
  PS2Native is released on GitHub, beginning with 0.1 (tag `v0.1`), at the
  maintainer's word ("Let's create releases on Github"): both folders as
  downloads (`PS2Native-<version>-windows-x64.zip` from `kit.ps1 -Out`,
  `PS2Native-<version>-linux-x64.tar.xz` from `kit.sh --archive`), made from
  the tagged, committed and pushed tree, with their licences and nothing of
  a game, a BIOS or PCSX2's patches; 0.1 a pre-release (five games, two
  Windows computers, Linux under WSL only); the version the file
  `version.txt`, shown by the wizard's title, the runtime's `-help` and the
  licences. GitHub
  CLI (`gh`, installed with winget at the maintainer's choice) creates a
  release as a draft, published at the maintainer's word. The maintainer
  published 0.1 on 2026-11-19.
- [ADR 0043](0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md):
  a speed step, at the maintainer's word on 2026-11-20 ("How come our
  compiled games are running slower than PCSX2's ...? We need to check why
  and fix it"; and why first sessions are so much slower than on PCSX2):
  first measured in the configuration users get (a game's folder, its
  modules without a profile) against stock PCSX2 on the maintainer's own
  sessions run again, then the costs the profiles show made smaller one
  change at a time, each kept only if it gains, time counted as before.
  First sessions get faster within today's rules ("Faster interpreters
  (Recommended)"): PS2Native's interpreters, and the BIOS's kernel compiled
  for a new game's first session; a profile of the user's own play given to
  the compiler is put to the maintainer first, with its cost. What was
  measured and changed: [docs/app-log.md](../app-log.md), "The speed
  step".
- [ADR 0044](0044-a-games-folder-learns-from-its-users-own-play.md):
  a game's folder learns from its user's own play, at the maintainer's word
  on 2026-11-20 ("1. Yes.", to the session's recommendation): first the
  order of the game's functions, as its sessions ran them, given to the
  linker when the folder compiles after a session; then a profile sampled
  while the game plays, given to the compiler (the counted profile gave
  7.5%), built and measured before it goes into a folder. Each measured on
  the maintainer's sessions first; what a folder learns stays in it. The
  order was built on 2026-11-21, on Windows and Linux (docs/app-log.md, "The
  order of a folder's functions, built"): about 0.4% on Jak and Daxter,
  where a first link by hand had seemed to give 2.0%. The sampled profile,
  tried the same day, gave 0.2% and goes into no folder (ADR 0045).
- [ADR 0045](0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md):
  a game's folder takes its profile by counting, not by sampling, at the
  maintainer's word on 2026-11-21 ("1. Yes", to the session's
  recommendation): the sampled profile gave 0.2%, the counted one 7.5% on
  Jak and Daxter. After a session of at least 3 minutes, once per game, the
  folder's compile compiles the modules to count, runs the session's first
  3 minutes again with them without a window, and compiles them again with
  the counts; Later puts it off to the next session's end and never keeps
  the game from starting (unlike new code, ADR 0040). Built and measured on
  a second game (Gran Turismo 3, the session's choice) before it goes into
  a folder. It replaces ADR 0044's second part. Built on 2026-11-21 at
  clang's IR level (the session's choice, listed in the ADR with the others):
  Jak and Daxter 8.0% less time after its folder's own round, Gran Turismo 3
  3.6%.
- [ADR 0046](0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md):
  at the maintainer's word on 2026-11-21 ("Speed: match PCSX2", asked what
  to work on next), compiled games are to take no more than stock PCSX2's
  time in the configuration users get, a folder with the profile it counts
  itself: first measured so, each game counted on one session of the
  maintainer's and timed on another; then the costs the profiles show, one
  change at a time, each kept only if it gains, time counted as before. The
  session's expected order: the IOP's block entry and exit, the COP2
  information decided when the code is generated (checked against the
  runtime's record before a block runs it), the VU provider's entry and exit
  of a micro program, accesses to devices, the 128-bit instructions, block
  starts and ends, the IOP's jump to the next block. The same day, from 1.06
  to 1.11 times stock PCSX2's time to 1.04 to 1.08, the goal not reached,
  the maintainer's answers ("1. Yes", "2. Yes", "3. Ok"):
  committed and pushed; the new programs into their game folders; next the
  runtime itself built with a profile of play, measured first, against stock
  PCSX2 built without one. What becomes of a folder's counts after an update
  was put to them on 2026-11-22 (ADR 0048) and answered (ADR 0049).
- [ADR 0047](0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md):
  the maintainer's answers of 2026-11-22 to what that measurement found
  ("1. Yes", "2. Yes", "3. Yes", "4. Yes, write the text, I'll change it",
  "5. Yes", "6. Yes", "7. Ok do it"). Stock PCSX2 is timed built as its own
  releases are, with link-time optimization, which the build here had left
  out, and without a profile (built so it takes 1.5 to 4% less time; against
  it compiled games took 1.07 to 1.11 times its time, not 1.04 to 1.08), and
  the website's chart is corrected. A release's runtime is built with
  link-time optimization and a profile of play (0.99 to 1.00 times that stock
  PCSX2's time), on each system from training runs made there, the profile
  kept in the build tree; the maintainer's game folders get it. The maintainer
  tells PCSX2 of the defect found with it themselves; asked for the text, the
  session wrote none (ADR 0010) and gave where it is and how it is seen; the
  maintainer opened it the same day, PCSX2's issue 15223. A
  folder counts its profile after 3 minutes of the game's own time (9,000
  frames at 50 frames a second, 10,800 at 60: it changes ADR 0045's number).
  0.2 is next, with a way to bring the game folders of an earlier version up
  to date. The session's choices, for the maintainer to decide otherwise: the
  tree the folder is made from is built so, the development tree as before;
  stock PCSX2 is timed on a runtime of a tree of its own.
- [ADR 0048](0048-a-games-folder-of-an-earlier-version-is-brought-up-to-date-by-the-wizard.md):
  how a game's folder made with an earlier PS2Native is brought up to date
  (the maintainer's "7. Ok do it", ADR 0047; the rest the session's choices,
  for the maintainer to decide otherwise). The wizard does it, at the user's
  word: "Games made earlier..." in its first step lists the games of its own
  `games` folder and of any folder the user adds, with how each one's programs
  stand to this PS2Native's, and gives those ticked this version's runtime,
  launcher and toolchain, nothing else of the folder touched. The game then
  compiles itself again before it next starts, by its own recompile, whose
  window says why. `programs.txt` in a folder says which PS2Native's its
  programs are (0.1's folders do not say, and are earlier ones; a later
  version's folder is left as it is); nothing is changed while the game runs
  or compiles. The version is 0.2 from this work on. Put to the maintainer
  with a measurement (ADR 0046's point 6), and answered "3. Yes" (ADR 0049):
  a folder brought up to date with another toolchain counts its profile once
  more after a session.
- [ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md):
  the maintainer's answers of 2026-11-22 to what was built for 0.2 ("1. Yes",
  "2. Yes, prepare the release", "3. Yes", "4. Do all seven", "5. After").
  Committed and pushed. 0.2 is released from that commit as ADR 0042 has a
  release made: both downloads from the committed and pushed tree, the runtime
  built with link-time optimization and the profile of play, the release a
  draft on GitHub with its notes, which the maintainer reads and publishes
  (asked again once it was made, "1. Yes, publish", "2. Yes do it": the
  session published it the same day, and the website's downloads moved to
  0.2). A
  folder brought up to date with another toolchain counts its profile once
  more after a session. The session brings the maintainer's own game folders
  up to date, the six on Windows and the one inside WSL. Why a compiled game
  takes more than stock PCSX2's time on Linux (Jak and Daxter 1.05 under WSL
  2) is looked for after 0.2 is out. The session's choices, for the maintainer
  to decide otherwise: 0.2 is a pre-release, as 0.1 is; the profile of play is
  the one made on 2026-11-22, the runtime's source unchanged since; the
  maintainer's folders get the release's own programs, the session has each
  compile its game and runs the start of a session again, and what each counts
  waits for a session of the maintainer's; the website's downloads stay 0.1's
  until 0.2 is published.
- [ADR 0050](0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md):
  the maintainer's answers of 2026-11-22 to what looking for Linux's speed
  found ("1. Yes", "2. Yes, write an issue as a guide I'll write it myself.",
  "3. Yes", "Stop."). PS2Native's builds leave out PCSX2's wakes of its VU
  thread where that thread is not in use (at every STROW and STCOL command of
  VIF1; PS2Native has the thread fixed off): the first change PS2Native
  carries to PCSX2's code, a patch in this repository applied to a copy of
  PCSX2's source when the runtime is built, the submodule never edited, stock
  PCSX2 timed as released, without it; carried at once, since it changes no
  result of a run. The maintainer tells PCSX2 themselves; asked for an issue
  written as a guide, the session wrote none (ADR 0010) and gave where it is,
  how it is seen and what limits it. Committed and pushed. The last 3% of the
  compiled game's distance to stock PCSX2 on Linux is left for now. How the
  change is carried is the session's choice, listed there (it changes ADR
  0002's "the machine is unmodified PCSX2", as ADR 0010 said a first patch
  would): `patches/pcsx2`, applied by git to copies of the files a patch
  changes when a tree is configured, PCSX2's targets compiling the copies;
  compiled sources only, not headers; every tree but the one stock PCSX2 is
  timed on, so the tests' "stock" configurations run PCSX2 with it; a
  runtime's `-help` and a release's `licences` say so; dropped by deleting the
  patch. Built and measured the same day: Jak and Daxter 9.2% less time on
  Windows and 27.5% less under WSL 2, every test passing. Found writing it
  down, measured, and put to the maintainer: PCSX2 switches its VU thread on
  by default on a computer with three cores or more, and every run of
  `ps2n-host` has had it off, stock PCSX2's among them; with it on
  (`ps2n-host -pcsx2-vu-thread`) stock PCSX2 takes 40% less time on Jak and
  Daxter's session, 24%, 12% and 2% less on the other three games', and the
  compiled games are at 1.02 to 1.50 times its time, where 0.90 to 1.00
  against stock PCSX2 with VU1 on the CPU thread.
- [ADR 0051](0051-the-comparison-with-stock-pcsx2-said-as-it-is-vu1-on-a-thread-of-its-own-next.md):
  the maintainer's answers of 2026-11-22 to what ADR 0050 measured ("1. Yes",
  "2. Yes", "3. Yes", "4. Yes", "5. Wait for that step", "6. Leave it as it
  is"). ADR 0050's change is committed and pushed. The public comparison is
  corrected: the website, the README and 0.2's notes on GitHub (edited by the
  session at that word, two passages marked as corrections, nothing else of
  the release touched) say that both sides ran with VU1 on the main processor
  thread, and give what stock PCSX2 takes with its VU thread on; from now on
  a speed claim against stock PCSX2 says which of the two it is against. The
  next speed step is VU1 carried out on a thread of its own, as PCSX2 does by
  default, measured against stock PCSX2 with its VU thread on. The
  maintainer's seven game folders get the runtime with ADR 0050's change. No
  release with that change alone: the next comes with the VU thread step.
  ADR 0050's record of its first answer stays as it is. The step as the
  session proposes to take it, for the maintainer to decide otherwise:
  PS2Native's VU provider on PCSX2's own VU thread, with PCSX2's machinery
  for it, following PCSX2's VU recompiler as it behaves with the thread on;
  on by default as in PCSX2 (three cores or more, off where PCSX2's game
  database says so), the user's to switch on PCSX2's own settings page; every
  comparison that holds VU1's registers to a stock run keeps VU1 on the main
  thread; a run with the thread on is held to the maintainer's sessions being
  their sessions at every fingerprint wherever stock PCSX2's with its VU
  thread on are; its criterion, no more than stock PCSX2's time with its VU
  thread on, every test passing as before; measured on one game first and
  put to the maintainer before it goes into a folder or a release. Built and
  measured the same day (the ADR's "The step, built"; docs/app-log.md, "VU1
  on a thread of its own"), and put to the maintainer, in no folder of theirs
  and no release: the provider on PCSX2's VU thread where that thread is on,
  held to PCSX2's VU recompiler by a sixth mode of the self-test and nine
  tests more. The session's choices within it, for the maintainer to decide
  otherwise: a game played has the thread as PCSX2 would give it, the user's
  to switch on PCSX2's own page in the wizard; every run without a window
  has VU1 on the main thread, whatever a game's settings file says; a session
  written down says when it was played with the thread; the runtime's
  profile of play is trained with the thread on; and the thread's setting is
  decided once for a run, when the game's disc is read, PCSX2's game database
  heard then (ADR 0050's change holds only if the thread is never switched
  on while a game runs; the note at its top says what is true now). Found
  with it: Agent Under Fire's run with the thread on does not repeat (on
  stock PCSX2 as compiled), so a session of such a game played with the
  thread cannot be run again as itself; and two of Linux's tests had never
  run there, skipped for want of a capture and counted as passing, which
  `scripts/linux/vu-selftest.sh` and `iop-selftest.sh` put right.
- [ADR 0052](0052-vu1-on-its-own-thread-committed-the-maintainers-folders-get-it-and-0-3-is-prepared-with-it.md):
  the maintainer's answers of 2026-11-22 to what ADR 0051's step measured
  ("1. Yes", "2. Yes", "3. Yes", "Do whatever needs doing from your side
  (from your "Not done" category)"). The step is committed and pushed. The
  maintainer's seven game folders get the release's programs, made from
  0.3's tree, each compiling its game again. 0.3 is prepared with it as ADR
  0042 and ADR 0049 have a release made (the profile of play made again on
  each system from the tree released, both downloads tried, a draft on
  GitHub the maintainer publishes); the session had recommended doing it
  after the maintainer had played with the step, and their "Yes" with "Do
  whatever needs doing" is read as now, the draft leaving them free to play
  first. What the session had not done is done: the Linux runtime's profile
  of play made again with the thread, and a game in a window on Linux with
  the thread, inside WSL, its window on the maintainer's screen meanwhile.
  The session's choices, for the maintainer to decide otherwise: the Windows
  profile made again too (the runtime's source changed after the one
  measured with); the release's runtime timed again before the notes quote
  a number; 0.3 a pre-release, its notes saying which stock PCSX2 each speed
  claim is against. The version is 0.3 from this commit. All of it done the
  same day (the ADR's "Done the same day"), and 0.3 published at the
  maintainer's word ("1. Publish"), the website's downloads and the README
  moved to it ("2. Yes").
- [ADR 0053](0053-ps2native-tried-on-pcsx2s-newest-version-2-9-189.md):
  Phase 5, Maintenance, begins at the maintainer's "Do 2" (2026-11-22), with
  PS2Native tried on PCSX2's newest version, v2.9.189: built and tested on it,
  and the maintainer's sessions run again on it; whether the submodule moves
  is the maintainer's to decide after the results. The session's choices, for
  the maintainer to decide otherwise: the work in a git worktree beside the
  repository and on a branch of its own (`..\ps2n-pcsx2-189`,
  `pcsx2-2.9.189`), where the submodule is moved, nothing committed or pushed;
  PCSX2's newer Windows dependency package (DirectX-Headers 1.620.2) in that
  worktree only; what PCSX2 changed of what PS2Native follows followed there
  (the count register's 32-bit addition, v2.9.173; `nextBlockCycles` 32 bits
  again, v2.9.174; the IOP recompiler's JR and JALR keeping their target,
  v2.9.183); no timing. Results the same night: every test passes as on
  v2.9.170 on both systems, and every session run again is the session at
  every fingerprint, the compiled games' EE cycles the same to the cycle.
  PS2Native moved to it (ADR 0054).
- [ADR 0054](0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md):
  the maintainer's answers of 2026-11-23 to what ADR 0053 found ("1. Yes",
  "2. Yes", "3. I will, after. Make sure they're all ready"). PS2Native moves
  to PCSX2 v2.9.189: the submodule, ADR 0053's ten changes, PCSX2's Windows
  dependency package of 2026-11-21 in `deps` (the earlier one kept beside it),
  the places that name the version PS2Native follows; on Windows and inside
  WSL; committed and pushed. The maintainer's three games PS2Native has never
  run (Resident Evil – Code: Veronica X, Secret Agent Clank, SSX Tricky) are
  made ready for them to try: each with the wizard, every default, into their
  own PS2Native folder made again from the committed tree, and checked on a
  copy, their folders left as the wizard made them. The session's choices,
  for the maintainer to decide otherwise: no release for the move alone (the
  next makes the profile of play again and is timed); their seven existing
  game folders keep their programs (not asked); ADR 0053's worktree and
  branch removed once the main tree has everything of them.
- [ADR 0055](0055-a-session-writes-down-the-pace-it-kept-and-the-notice-shows-for-any-new-code.md):
  the maintainer's answers of 2026-11-23 ("Yes record. Yes the notice should
  show for any new code."), after a session of Jak and Daxter that lagged
  ("laggy as hell, specially during boss fight") with no notice shown: its run
  again showed the emulation never behind (its slowest 30 seconds 0.29 of the
  console's time), its code met for the first time at most 0.37% of the EE's
  cycles in a half second, under ADR 0035's 1%. A session in the window writes
  down the pace it kept in `pace.txt` beside its `input.txt`, every 300 frames:
  the frames a second, the longest frame on the CPU thread and the longest
  PCSX2 showed, what the game itself drew a second (at the console's pace, so
  a run without a window says it too: `ps2n-host -pace <file>`, `game.ps1
  -Pace`), and how much of each processor's work was interpreted; the report
  says the worst. The notice shows for any code met for the first time, the
  BIOS's instructions at boot aside, still at most once a minute (of the
  EE's, since ADR 0057, what a run records for the next compile). The
  maintainer then wrote that the boss fight lagged on compiled code too ("So
  maybe that was the reason"), read as the game itself drawing fewer pictures
  there, which a session run again with `-pace` can show.
- [ADR 0056](0056-jak-and-daxter-at-a-130-ee-cycle-rate-its-interpreted-code-looked-for-next-nextblockcycles-kept-alone.md):
  the maintainer's answers of 2026-11-23 to what ADR 0055's work found ("1. Ok
  2. Yes 3. Agree 4. Commit and push."):
  - **Jak and Daxter at a 130% EE cycle rate.** Their folder (on Windows) plays
    the game so (`EECycleRate = 1` in its game's settings file), brought up to
    date so that its next session writes its pace down. The boss fight, where
    the game drew about 30 pictures a second, is to be played again.
  - **Why part of the code Jak and Daxter loads from its data stays
    interpreted after its folder compiles it is the next step.** The notice
    for any new code stays meanwhile, about once a minute in that game.
  - **PS2Native keeps `nextBlockCycles` alone.** PCSX2 v2.9.189's 64-bit read
    there (under VUSyncHack or FullVU0SyncHack) depends on where the system put
    its memory. It is listed in the execution model's "What is not reproduced",
    and telling PCSX2 is the maintainer's.
  - **Committed and pushed.**
  - **Not answered:** their other earlier folders keep their programs until
    the next step is done.
- [ADR 0057](0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md):
  the maintainer's "Yes implement the recommendation" (2026-11-23), after the
  next step of ADR 0056 found why part of Jak and Daxter's loaded code stays
  interpreted. A folder's compile kept one version of the code a game ran
  outside its executable at each address, the first captured, and Jak and
  Daxter loads other code into memory it ran code from before (83,085 such
  addresses in the maintainer's folder).
  - **Captured.** A block run outside the executable, the BIOS's files and the
    executables a program loads itself, where no compiled code ran it, is
    captured as the interpreter carried it out (`[[block]]`), every version
    of it.
  - **Compiled.** The first image of code run outside the executable is what
    it was (`<id>_outside`, every `[[code]]` piece of earlier captures, and
    the blocks that agree with it). Blocks that disagree go into further
    images (`<id>_outside_2` and on), each into the first it agrees with,
    which the first lists (`ImageInfo::layers`; module version 6).
  - **Run.** The runtime puts them all in use and runs a block with whichever
    holds what memory holds (`blockStart`, `Block::owner`).
  - **Tests.** `tests/programs/overlay.s` and its capture test it, and every
    rebuilt test program must interpret nothing outside its executable either.
  - **Measured on Jak and Daxter first,** with Gran Turismo 3 checked to come
    out unchanged, before a folder or a release.
    - Jak and Daxter's session, run again with what it met compiled,
      recorded nothing new.
    - Gran Turismo 3's C++ differs only by the empty table of further images.
  - **Found with it:**
    - a block is captured over the span the runtime compares (past a block
      PCSX2 cut short), its end a boundary in its image;
    - where a BIOS image's code is not memory's, the game's images are tried
      (Gran Turismo 3's patched program loader);
    - the notice counts what a run records for the next compile
      (`ee::Stats::new_code`), not every instruction interpreted.
- [ADR 0058](0058-every-version-compiled-committed-the-maintainers-folders-brought-up-to-date-0-4-after-their-play.md):
  the maintainer's answers of 2026-11-23 to what ADR 0057's work measured
  ("1. Yes", "2. Yes", "3. Yes"):
  - **Committed and pushed.**
  - **Their game folders brought up to date:** every folder in their
    PS2Native folder's `games`, and the one inside WSL, compiling its game
    again. Their PS2Native folders are made again from the committed tree
    first, each game's own settings kept.
  - **0.4 prepared after their play:** once they have played a session or two
    with the updated folders, as 0.3 was made.
- [ADR 0059](0059-0-4-is-released.md): the maintainer's words of
  2026-11-25, "I did some play tests, everything looks smooth. Let's release
  0.4."
  - **Made as 0.3 was:** the version 0.4, committed and pushed. The runtime's
    profile of play is made again on each system from that tree, the training
    folders brought up to date first (module version 6).
  - **The downloads:** both made from that tree and tried, a folder of 0.3's
    download brought up to date among the tries.
  - **The draft:** the release's runtime timed again before the notes quote a
    number, then a draft pre-release on GitHub for the maintainer to publish.
  - **The session's choices:** their game folders keep `2cc8125`'s programs
    unless they say otherwise; the website and the README move to 0.4 once it
    is published.
  - **The maintainer's answers to the draft** ("1. Yes, publish yourself", "2.
    Yes do that", "3. Yes please"): published by the session (the tag `v0.4`
    on `a243df4`); the website and the README moved to 0.4; their game folders
    (`build\kit\games` on Windows, `build/PS2Native/games` inside WSL) given
    0.4's programs.
- [ADR 0060](0060-a-menu-over-the-game-opened-with-esc-quit-controls-and-the-graphics-card.md):
  the maintainer's words of 2026-11-25, "I want to add an overlay that opens
  when pressing Esc, allowing the user to Quit (loose progress warning), Edit
  keybinds and controllers and which GPU is in use (if needed and it if makes
  it easier, ask for a game restart, I don't mind). I think those are the only
  acceptable changeable settings post-compilation.", "Use a fancy sound when
  it opens, when it closes and when clicking/hovering the menu items." and "Use
  PS2 style sounds".
  - **The menu** (`host/menu.cpp`): Esc opens it over a game played in its
    window, the game held between two frames meanwhile (a session with the
    menu in it runs again as itself); Resume, Controls, Graphics card, Quit
    (asking first). Drawn in the picture with the ImGui of PCSX2's renderer by
    PS2Native's own code, never PCSX2's own menus.
  - **Controls:** the controller in port 1, each input's key and controller
    input set by pressing it, a connected controller chosen to play, the
    defaults back; kept in the game's data (`inputprofiles\controls.ini`).
  - **Graphics card:** the cards PCSX2 lists for the game's renderer, a choice
    taken at once (PCSX2 makes its renderer again on it) and kept in the
    game's settings file; no restart.
  - **Sounds** (`host/menu_sound.cpp`): in the manner of the PlayStation 2's
    own menus, made by PS2Native's own code (nothing of Sony's), played
    through SDL beside the game's sound at its volume, none where it is muted.
  - The session's choices are listed in the ADR, for the maintainer to decide
    otherwise.
  - **The maintainer's answers** ("1. Yes", "2. Yes", "3. No"): committed and
    pushed; their game folders given these programs; only Esc opens the menu,
    never a controller.
  - **2026-11-26, "To finish this feature":** Reboot on the menu, asking first
    (PCSX2's reset, marked in the session written down so that it runs again
    as itself), and the window's close button and Alt+F4 asking Quit's
    question first (pressed again there, Quit chosen; with the game held up,
    closed at once); neither Quit nor Reboot while PCSX2 counts the memory
    card busy (300 frames after a write). At their answers ("1. Yes", "2.
    Yes"): committed and pushed; their game folders given these programs.
- [ADR 0061](0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md):
  asked "What should we work on next?" on 2026-11-27 and given four options,
  the maintainer chose "Maintenance tools"; asked how far the trial of PCSX2
  v2.9.198 should go, "Follow them in a branch".
  - **A newer PCSX2 is tried by one command** (`scripts\windows\try-pcsx2.ps1`,
    `scripts/linux/try-pcsx2.sh`), in a worktree and branch of its own, with a
    report. It moves nothing: the move stays the maintainer's to decide.
    `scripts/pcsx2-watch.txt` lists PCSX2's files PS2Native follows or
    compiles.
  - **A release, an update of the maintainer's game folders and a timing
    against stock PCSX2** are scripts in the repository (`release.ps1`,
    `update-games.ps1`, `time-games.ps1`, with `common.ps1`, `hidden.ps1` and
    `wsl.ps1`; `release.sh`, `update-games.sh`, `common.sh`, `sync.sh`),
    described in `docs/development.md`. What is this computer's or a game's
    comes from parameters and from lists in the build tree.
  - **`CLAUDE.md` is cut to what a session needs,** by moving, not deleting:
    its history to `docs/history.md`, these digests to this index, its build
    and test instructions to `docs/development.md`. The rules that bind every
    session stay in it, 40 of them, with one new one that is the maintainer's
    to strike: `build\` is never deleted or cleaned whole.
  - **v2.9.198 is tried** in its worktree, PS2Native following PCSX2's changes
    there, nothing changed in `main`; the results go to the maintainer.
  - The session's choices are listed in the ADR, for the maintainer to decide
    otherwise.
  - **The maintainer's answers** ("1. Yes", "2. Yes", "3. Yes", "4. Yes"):
    committed and pushed; the rule on `build\` kept; PS2Native moves to
    v2.9.198, carrying the patch for the missing include; the worktree of
    Phase 3 removed.
- [ADR 0062](0062-ps2native-moves-to-pcsx2-2-9-198-carrying-a-missing-include.md):
  the maintainer's "3. Yes" (2026-11-27) to moving to PCSX2 v2.9.198, which
  ADR 0061's trial had found every test passing on and the sessions running
  again as themselves.
  - **The submodule at v2.9.198,** on Windows and inside WSL, and the
    worktree's changes in `main`: PS2Native's description of the FPU's one
    control register, the module version 7, `RSQRT`'s flags, the self-test
    held to both of PCSX2's implementations where they now agree, the lists
    of known differences.
  - **PS2Native's second change to PCSX2's code,** a missing include, without
    which PCSX2's source does not compile on Linux here. Changing nothing a
    run does, and saying so, it is applied to stock PCSX2's trees too.
  - The session's choices are listed in the ADR, for the maintainer to decide
    otherwise.
  - **The maintainer's answers** ("1. Yes", "2. Yes"): committed and pushed;
    their game folders given these programs.
- [ADR 0063](0063-0-5-is-released.md): the maintainer's words of 2026-11-27,
  "Handle release 0.5".
  - **Made as 0.4 was,** by the scripts of ADR 0061 for the first time: the
    version 0.5, committed and pushed; the runtime's profile of play made
    again on each system from that tree (the last was made at v2.9.189), the
    training folders brought up to date first (module version 7).
  - **The downloads:** both made from that tree and tried, a folder of 0.4's
    download brought up to date among the tries.
  - **The draft:** the release's runtime timed against stock PCSX2 v2.9.198
    before the notes quote a number, then a draft pre-release on GitHub.
  - **The session's choices:** "Handle" read as far as the draft, publishing,
    the website and their game folders put to them then; their game folders
    keep `d50ef76`'s programs unless they say otherwise; the website and the
    README move to 0.5 once it is published.
  - **The maintainer's answers to the draft** ("1. Yes", "2. Yes", "3. Yes",
    "4. No"): published by the session (the tag `v0.5` on `bbe4e56`); the
    website and the README moved to 0.5; their game folders (`build\kit\games`
    on Windows, `build/PS2Native/games` inside WSL) given 0.5's programs; Jak
    and Daxter's compiled run on Windows, about 3% more than 0.4's, not looked
    into further.
- [ADR 0064](0064-a-second-controller-for-a-second-player.md): the
  maintainer's words of 2026-11-28, "Can you add 2 controllers support? I
  think currently we only support 1 controller but I could be wrong".
  - **A second player, in port 2:** a controller put into port 2 while the
    game runs, as PCSX2 puts one in, once a second player has something to
    play with (a second controller, or keys of their own); there until the
    game ends. A game played by one player is as before, port 2 empty.
  - **Controllers given out in the order they connect:** the first to player
    1, beside the keys, as before; the second to player 2.
  - **The menu's Controls page has both players,** each with its keys and
    controller; an input, and a controller, play for one player at a time;
    both kept with the controls in the game's folder.
  - **A session keeps the second controller too** (`connect 2`, `pad2`
    lines): a session played by two runs again as itself; one written down
    before had port 2 empty and runs again so.
  - The session's choices are listed in the ADR, for the maintainer to decide
    otherwise.
  - **The maintainer's answers** ("1. Yes", "2. Yes"): committed and pushed;
    their game folders get these programs after the commit.
