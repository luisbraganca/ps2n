# ADR 0023: The IOP's code compiled ahead of time, as step G12 of Phase 3

| | |
|---|---|
| Status | Accepted; done on 2026-11-14 |
| Date | 2026-11-14 |
| Decided by | Maintainer, on the session's recommendation |

## Context

On 2026-11-14 the vector units' micro programs were compiled ahead of time
(step G11, [ADR 0022](0022-vu-micro-programs-compiled-as-step-g11.md)): play
in the first level takes 0.95 times stock PCSX2's time, the whole level run
0.97. Of the stand-ins [ADR 0003](0003-no-jit-in-generated-games.md) allows
for a time, one was left: PCSX2's IOP recompiler, which runs the I/O
processor's code, the BIOS's modules and the game's.

The session's report on G11 put one decision about what comes next. As it
was put:

"**What comes next?** I recommend the I/O processor, the last part still on
PCSX2's run-time translator and next in your own order of work, starting
with a measurement as the BIOS and vector-unit steps did. It will not make
the game faster (it is about 5% of play), but without it Phase 4 can only
produce development builds. If you would rather have speed first, the two
leads above are the place; my guess, not a measurement, is a few percent of
play and most of the first chart's extra second."

(The two leads: finding which micro program memory holds, about 5 s of
play; and a wait loop of the game's EE code that has a fifth of the first
4,200 frames' samples. Both are in the game log, "Where G11 stands".)

## Decision

The maintainer's word: "Let's work on the I/O processor"

How it was read:

- **G12 is a step of Phase 3**: the IOP's code carried out by code of
  PS2Native's own, compiled ahead of time, in the place of PCSX2's IOP
  recompiler. It is the last item of ADR 0003's order of work (EE, the BIOS
  kernel, VU, IOP). Its semantics follow PCSX2's IOP recompiler, as
  [ADR 0005](0005-never-further-from-the-console-than-pcsx2.md) has every
  family follow PCSX2's recompiler, and the game stays the stock run
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)).
- **It begins with a measurement**, as the recommendation said: what the IOP
  runs, from where, how often its code changes, how much time it takes
  (`ps2n-host -iop-report`, which watches the core's IOP provider and
  changes nothing).
- **The speed leads wait.** The answer chose the I/O processor; the two
  leads stay in the game log's "Open".

## What the session decided within that

The maintainer's word names the step, not how it is made. What follows was
decided by the session, by the rules that stand and by what the maintainer
chose for the vector units; the maintainer may decide any of it otherwise.

- **The order of work is G11's**, which the maintainer chose there ("2. Yes.
  3. Yes.", ADR 0022): what is compared with stock PCSX2 first, then an
  interpreter of PS2Native's own, then the translator.
- **The IOP's place and registers are held to the stock run at every sync
  point**, as the EE's, VU0's and VU1's are: its PC goes with the place and
  time a sync point records, and its general registers, HI, LO and its COP0
  registers are a kind of their own, counted apart. This changes what is
  compared with stock PCSX2, which
  [ADR 0019](0019-performance-as-the-research-lays-it-out.md) sends to the
  maintainer first; it was done on the strength of the answer for VU1's
  registers in ADR 0022, the same question for another processor, and it
  only adds to what is compared. It was in place before any IOP code of
  PS2Native's ran.
- **PS2Native's own IOP provider** (`runtime/iop_provider.h`) keeps the
  record PCSX2's IOP recompiler keeps, without the code: which blocks exist,
  formed when the IOP first got to their address from what memory held
  then, thrown away as that recompiler throws them away. That is what keeps
  the time the stock run's. Its interpreter is the reference, and what runs
  a block no build has compiled.
- **IOP code is compiled block by block, each block as PCSX2's IOP
  recompiler formed it in a run that was captured**, and a compiled block is
  found by a fingerprint of its instructions, not by its address: the same
  instructions are carried out by the same function wherever the IOP's
  kernel loaded them, and what depends on the address is reckoned from it
  when the block runs. The Phase 1 assessment had sketched whole modules
  translated position-independently and keyed by their content. Blocks were
  chosen because a block is the unit the stock run's time is counted in, so
  a compiled block has its end and its cycles in it and asks the runtime
  nothing; and because the measurement found what runs to be small and
  still (10,646 blocks over the level run, nearly all formed in the first
  250 frames). A module's code that holds addresses (calls, pointers to its
  data) differs from one load address to another, and such a block compiled
  for one address is not found at another: it is then interpreted, reported
  and captured, as code no build has seen.
- **IOP code no build has compiled is interpreted, reported and captured,
  and the next build compiles it**, as ADR 0003 has it for the EE's code
  (`ps2n-host -iop-capture`, `ps2n-recomp iop`, `build.ps1 -IopCapture`).
  The capture is derived from the BIOS and the game and stays in the build
  tree.
- **What PCSX2's recompiler hands to the core stays the core's**: the
  functions the core has in the place of some imports of the IOP's modules
  (the kernel's `Kprintf`, file access on `host:`, the list of modules for
  its debugger), the GTE's instructions, exceptions, and every read and
  write of a device.
- **What the provider does not carry out, it stops at with an error that
  names it**, rather than guess: the IOP's PlayStation mode (a PS1 game);
  a branch, a SYSCALL or a BREAK in a delay slot; an import that the core
  has a function for, anywhere but in its two-instruction stub; a jump to
  an address that is not a multiple of four or where the IOP has no memory
  (PCSX2 stops the machine there too); and the form at which PCSX2's IOP
  recompiler loses a jump's target (below). None of them is in what
  Killzone or the test programs run.
- **Every run that is not played in a window starts with the console's
  clock at one fixed time** (`ps2n-host -clock fixed`, the default:
  2020-03-04, midnight, which PCSX2 gives its input recordings; `-clock
  host`, the default with `-window`, is the host's time of day, as PCSX2
  sets it). The first comparison with the IOP's registers in it found them
  different at 72 of the level run's 93,221,937 sync points: the IOP reads
  the console's clock, PCSX2 sets that clock from the host's time of day
  when the machine starts, and the stock run and the compiled run of a
  comparison start minutes apart. It had always been so and had not shown.
  Two runs that are compared now start from the same machine.
- **The comparison of the compiled run with the interpreted run** folds the
  IOP's PC, cycle count and registers into the run's fingerprint at every
  event test, beside the EE's. The fingerprint of the game's first 4,200
  frames is therefore another number than the one the game log has had.
- **PS2Native's configurations run the IOP on PS2Native's provider**: the
  tests, the timed runs, the window. PCSX2's IOP recompiler is what the
  stock run of a comparison uses, and what `game.ps1 -IopPs2n off` puts
  back.
- **The self-test holds the provider to PCSX2's IOP recompiler** on random
  IOP programs (`host/selftest_iop.cpp`): registers, memory, cycle counts,
  and the addresses at which each holds a block.

## A defect of PCSX2's IOP recompiler, found on the way

The self-test's random programs found a form on which PCSX2's IOP
recompiler goes wrong by itself (the game log has it, "G12", "The
self-test"): at `JR` or `JALR` whose delay slot is an unaligned load or
store (`LWL`, `LWR`, `SWL`, `SWR`) or an instruction of the GTE's, where
it does not carry the delay slot's instruction out before the jump, it
keeps the jump's target in a host register across the delay slot, gives
every host register up for the call that instruction is, and writes the
target back as the EE's recompiler would (to `cpuRegs.pcWriteback`), then
reads it from the IOP's own field (`psxRegs.pcWriteback`), which nothing
writes. The IOP goes on at address 0. No program anyone runs has that form:
the delay slot's instruction has to load or store the register the jump
goes through, or use the register the jump links into, or the jump has to
link into the register it goes through.

PS2Native's provider stops at it with an error that names it: following it
would be a jump to nowhere, and anything else a guess.
[ADR 0005](0005-never-further-from-the-console-than-pcsx2.md) asks for
PCSX2's recompiler's semantics, or the console's where a recording shows
the recompiler wrong, and there is no recording of this.

**The maintainer tells PCSX2** (2026-11-14). The session's report on the
step put the question, and recommended not to for now: no game is
affected, and PCSX2's team had asked the maintainer for no LLM-generated
issues ([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).
The maintainer's word: "Yes"; and, asked whether that was the telling or
the recommendation: "Write me how you'd write the issue and suggested fix,
then I'll write something myself using your suggestion as a guidance." How
it was read:

- **The defect is reported to PCSX2 by the maintainer**, as an issue of
  their own with the correction they suggest, as the two before it were
  (ADR 0010,
  [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)).
- **The session wrote neither the issue nor a correction, not as a draft
  to go by either.** What a session may do for what goes to PCSX2 is
  PCSX2's to say (its `AGENTS.md`; ADR 0010, "What an AI session may and
  may not do for what goes to PCSX2"), and a text reworded from a
  session's draft would still be a session's. It said so, and gave what
  that rule allows: where the defect is, how it is seen, what was measured
  and on what, that an AI session found it, and whether it is reported
  already.
- **Nothing is carried locally for it, and no time is set**: PS2Native's
  provider stops at the form, which no program has.

Where it is, in PCSX2 `v2.9.170`; PCSX2's `master` of 2026-11-14
(`0ab00c49`) has the four files unchanged:

- `pcsx2/x86/iR3000Atables.cpp`: `rpsxJR` (line 1424) and `rpsxJALR` (line
  1464) keep the target in a host register that they ask for as the kind
  `X86TYPE_PCWRITEBACK`, and read it from `psxRegs.pcWriteback` (lines
  1436 and 1486) when the register was given up meanwhile.
- `pcsx2/x86/ix86-32/iCore.cpp`, `_writebackX86Reg`: a register of that
  kind is written to `cpuRegs.pcWriteback`, the EE's (line 436). The kind
  that is written to `psxRegs.pcWriteback` is `X86TYPE_PSX_PCWRITEBACK`
  (line 446; `pcsx2/x86/iCore.h`, line 83), and nothing asks for a
  register of it.
- The register is given up by `_psxFlushCall(FLUSH_EVERYTHING)`
  (`pcsx2/x86/iR3000A.cpp`, line 409), which the instructions compiled as
  calls of the core make first: `LWL`, `LWR`, `SWL`, `SWR` (`REC_FUNC`)
  and the GTE's (`REC_GTE_FUNC`), at the top of `iR3000Atables.cpp`.
- `psxTrySwapDelaySlot` (`pcsx2/x86/iR3000A.cpp`, line 484) decides
  whether the delay slot's instruction is carried out before the jump;
  where it is, no register is kept and nothing goes wrong.

The lines came in with PCSX2's pull request 7295, merged on 2022-11-19.
Looked up on 2026-11-14 by reading only, without signing in: no issue or
pull request of PCSX2's, open or closed, mentions the defect (searched for
`pcWriteback`, `PCWRITEBACK`, `rpsxJR`, `rpsxJALR`, `psxTrySwapDelaySlot`,
`X86TYPE_PSX`, and for the words a report of what is seen would use).

## What was built and measured

The game log has it ("G12"). In short, on 2026-11-14: by their
instructions the IOP runs 8,023 different blocks over the level run, nearly
all of them formed in the first 250 frames; after one run and one rebuild
every one of the run's 910,923,627 block runs of IOP code is compiled
code's, none interpreted, and none on PCSX2's IOP recompiler, whose table
holds no block after the run; 4,000 random IOP programs give that
recompiler's registers, memory, time and blocks, 1,500 of them on compiled
blocks alone; every test passes (469); the level run is the stock run at
every one of its 93,221,937 sync points, the IOP's place and registers
included, and the interpreted run over its first 4,200 frames; play takes
0.95 times stock PCSX2's time and the whole level run 0.97, what they took
with PCSX2's IOP recompiler under the compiled game. A session in the
window, which has a memory card where the scripted runs have none, ran 349
blocks the capture did not have (of 7,783; 2,356 block runs of 126
million), interpreted them and wrote them down for the next build. The
step's pass criterion is met. What comes after it is the maintainer's to
choose.

## Consequences

- The game log has the step, its work and its pass criterion ("G12"), and
  what the measurement found.
- With the step's pass criterion met, no recompiler for PS2 code runs in a
  run of the game on PS2Native's configurations: ADR 0003's end state for
  what the game runs as far as the first level, and none of its stand-ins
  is in use. Code no build has seen is still interpreted until the next
  build, and listed.
- The website's list of steps has G12, and its board of what is static has
  the IOP's modules as static since 2026-11-14.
- The captures of IOP code are derived from the BIOS and from the game and
  stay in the build tree, as the captures of EE code and of micro code do;
  so do the counts of profile-guided optimization, made again with the
  IOP's code compiled.
- PCSX2's IOP recompiler throws its whole record away when its room for
  host code is full, which PS2Native's provider cannot know. The level run
  fills 1.85 MB of its 32 MB.
