# ADR 0022: The vector units' micro programs compiled ahead of time, as step G11 of Phase 3

| | |
|---|---|
| Status | Accepted; under way |
| Date | 2026-11-13 |
| Decided by | Maintainer, on the session's recommendation |

## Context

On 2026-11-13 the FPU's arithmetic was done on SSE registers (step G10,
[ADR 0019](0019-performance-as-the-research-lays-it-out.md)): with
profile-guided optimization, play in the first level takes as long as under
stock PCSX2, the whole level run 1.01 times its time. The maintainer then
asked: "What should we work on now?"

The session answered with where the numbers stood (play as long as stock
PCSX2's, the whole level run 1.01 times its time, startup 1.04 to 1.05, the
website's first chart 1.08 with nothing drawn and 1.04 with the software
renderer, the `mmi` loop of its second 1.24) and four options, recommending
the first. Before the options it wrote: "Before that, I'd suggest declaring
the current speed step (G10) done. Its aim was to make the compiled game's
code no slower than PCSX2's own recompiler, and with profile-guided
optimization, playing the first level now takes as long as in stock PCSX2.
Closing it only means updating the docs; no code changes."

The options, as they were put:

1. "**Compile the vector units' small programs ahead of time
   (recommended).** The game uploads small programs to the PS2's two vector
   units, and these still run on PCSX2's run-time translator. ADR 0003
   allows that only as a temporary stand-in. It sets your order of work as
   the game's main code, then the BIOS, then the vector units, then the I/O
   processor. It also says Phase 4's packaging may not make a release build
   while any stand-in is in use. The main code and the BIOS are done and
   play runs at stock speed, so this is the next item in your own order.
   I'd start the way the BIOS step did, with a measurement and no code
   changes. It would show which programs Killzone runs, how many distinct
   ones there are, and how much time they take. It is the biggest piece of
   work left, and the original assessment rated it the riskiest. The
   programs have to be recognised by their content rather than their
   address, and the result has to be as fast as PCSX2's translator. If you
   choose otherwise, a generated game keeps a run-time translator inside it
   for now."
2. "**Improve the website's numbers.** Startup and the first 4,200 frames
   each take about a second longer than stock PCSX2, and neither has been
   profiled yet. The 128-bit arithmetic could move to SSE registers the way
   the FPU just did. This is small work and shows on the website, but it
   changes almost nothing in play."
3. "**Go further into the game.** That means the rest of the first level
   and later levels, by script or by you playing longer sessions. It finds
   what is still missing. For example, the game's four extra code modules
   (`.REL`) have never run, and a module loaded at a different address isn't
   handled yet."
4. "**End Phase 3 now and ask to start Phase 4** (the wizard, game
   recognition, packaging). That is possible, but under ADR 0003 anything it
   packages could only be marked a development build while the vector units
   and the I/O processor still run on stand-ins."

## Decision

The maintainer's word: "1."

How it was read:

- **G11 is a step of Phase 3**: the micro programs the game runs on VU0
  and VU1 compiled ahead of time by PS2Native's own translator, in the place
  of PCSX2's VU recompiler (microVU), which
  [ADR 0003](0003-no-jit-in-generated-games.md) allows only as a temporary
  stand-in. It is next in that record's order of work, the game's EE code
  and the BIOS's being compiled (G2 to G8). Its semantics follow microVU's,
  as [ADR 0005](0005-never-further-from-the-console-than-pcsx2.md) has every
  family follow PCSX2's recompiler, and the game stays the stock run
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)).
- **It begins with a measurement**, as G8 did, before any translator: which
  programs the game runs, how many distinct ones there are, how much time
  they take. The measurement needed an option of the host that watches the
  core's two VU providers (`ps2n-host -vu-report`); nothing in how the game
  runs changed, and the watched run ends where every level run has ended.
  The decisions the translator needs are put to the maintainer with what it
  found.
- **G10.** The suggestion to declare it done came before the options, and
  the answer does not speak to it, so it is left as it stands until the
  maintainer says. (Asked again with the measurement, below: it ended.)

## The measurement, and three more answers

The measurement is in the game log ("G11", "What the vector units run,
measured"). In play the vector units take about a third of the CPU
thread's time; VU1's micro memory is rewritten about 40 times a frame, with
208 distinct pieces of code written at 5,405 places, which PCSX2's VU
recompiler makes 6,088 programs of, and starting programs and finding which
one memory holds takes it about 10% of play's time; VU0 runs one fixed set
of code. The session put three decisions with it. As they were put:

1. "**End the speed step G10 where it stands?** I recommend yes: its aim,
   the game's code no slower than PCSX2's, is met in play. Your "1." didn't
   cover it, so I left it open. If you say no, it simply stays open beside
   G11."
2. "**Compare VU1's registers with stock PCSX2 at every sync point**, as the
   main processor's and VU0's already are? I recommend yes: a mistake in the
   compiled VU code would then show where it starts, not later in the
   pictures. I'm asking because it changes what is compared with stock
   PCSX2. Without it, VU1 errors surface later and are harder to trace."
3. "**Build our own VU interpreter before the translator?** I recommend
   yes, the way the main processor's interpreter came first in G1: It
   becomes the reference the translator is checked against. It runs code
   the build hasn't compiled, which ADR 0003 requires anyway. It takes
   PCSX2's translator out of the game early. If you choose translator-first,
   a speed result comes sooner. But code not yet compiled would stay on
   PCSX2's translator, and handing a program from one to the other partway
   through isn't practical without patching PCSX2."

The maintainer's words: "1. Yep. 2. Yes. 3. Yes."

How they were read:

- **G10 ended where it stood**
  ([ADR 0019](0019-performance-as-the-research-lays-it-out.md), "End").
- **VU1's registers are held to the stock run at every sync point**, as the
  EE's, the FPU's, COP0's and VU0's are
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)): a
  kind of its own in the record of sync points, counted apart, so that a
  difference in it shows where it starts and does not hide the others. It
  is made before any VU code of PS2Native's runs; until then both runs use
  PCSX2's VU recompiler, and the comparison is expected to find them the
  same.
- **PS2Native's own VU interpreter comes first**, as the EE's did in G1:
  semantics of PS2Native's own for the micro mode, following microVU as
  ADR 0005 has it, held to it by the self-test; an interpreter on them in
  the place of microVU, VU0's code first, the game held to the stock run.
  It is the reference the translator is checked against, and what runs the
  code a build has not compiled (ADR 0003's rule for code not seen at
  build time). The translator comes after it.

## Consequences

- The game log has the step, its work and its pass criterion ("G11"), and
  what the measurement found.
- What the measurement writes from the game, the contents of micro memory,
  stays in the build tree with the run's other files.
- The website's list of steps has G11, under way.
