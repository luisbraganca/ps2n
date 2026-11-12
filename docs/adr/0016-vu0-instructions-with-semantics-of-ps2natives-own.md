# ADR 0016: VU0's instructions with semantics of PS2Native's own

| | |
|---|---|
| Status | Accepted; done (2026-11-12) |
| Date | 2026-11-12 |
| Decided by | Maintainer, on the session's recommendation |

## Context

The speed step G6 ([ADR 0015](0015-a-speed-step-before-phase-3-ends.md))
brought the compiled game from 1.35 to 1.21-1.23 times stock PCSX2's time
over the level run, with the game still the stock run. What was left in
the generated code had become smaller than what one change can show on the
development machine. The session recommended ending the step there, and
named the next one: the game's VU0 instructions.

They are the one family of the game's own code that is not PS2Native's.
The COP2 instructions in the game's EE code that compute on VU0's
registers are carried out by PCSX2's VU interpreter functions, through the
runtime, one by one: 158 million calls in the level run's 17,500 frames.
The runtime puts the flags right afterwards as PCSX2's recompiler leaves
them (execution model, rule 16), but the arithmetic is the interpreter's.
That goes against [ADR 0005](0005-never-further-from-the-console-than-pcsx2.md),
by which instruction semantics follow PCSX2's recompiler, and two cases
are known where the two differ: `VRSQRT` of zero by zero (the flags and
the result) and `VSQRT` of -0 (the invalid flag). In the level run neither
has left a trace. The calls are also the largest part of the runtime's
time (2.4 s of the 91 s run).

The session's recommendation, as it was put: "The next real gain is giving
VU0's instructions semantics of PS2Native's own. That is first a
compatibility item (it would close the two known VU0 differences), so I'd
do it as a step of its own rather than as part of G6"; and, after the
commit: "I recommend it does [end here], with giving VU0's instructions
semantics of PS2Native's own as the next step if you want it."

## Decision

The maintainer's words: "Yes it does end here. Let's proceed with next
work."

How they were read:

- **Step G6 ends where it stands**, as committed on 2026-11-12. ADR 0015 is
  done.
- **The next work is the step the session named**, G7: the VU0
  instructions in EE code carried out by semantics of PS2Native's own,
  following PCSX2's recompiler (microVU in macro mode), and the console
  where a recording shows the recompiler wrong, as ADR 0005 has it for
  every other family. Its pass criterion is in the game log.

## What does not change

- **What PCSX2's recompiler itself hands to a function of the core stays
  that function**: `VCALLMS` and `VCALLMSR`, which start a micro program
  on VU0, are calls of PCSX2's own function in its recompiler too. The
  micro programs themselves run on PCSX2's VU0 recompiler, one of the
  stand-ins of [ADR 0003](0003-no-jit-in-generated-games.md), as before.
- **When VU0 is brought up to date before an instruction, and which
  instructions' flags reach the flag registers**, stay as the recompiler
  decides them for the block (execution model, rules 15 and 16): that is
  time, and [ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)
  covers it.
- **The compiled game stays the stock run**, VU0's registers included, at
  every sync point.

## What was done

On 2026-11-12 (the game log, "G7"): the 82 VU0 instructions PS2Native's
decoder knows, all but `VCALLMS` and `VCALLMSR`, have semantics of
PS2Native's own (`runtime/include/ps2n/ops_vu0.h`), which the runtime calls
where it called PCSX2's interpreter functions. The self-test holds every one
to PCSX2's recompiler in each VU clamp mode and with the three game fixes
that change them, Killzone's own configuration among them: no difference in
196,800 cases, nor in 1,968,000 on a longer run. The two known differences
are closed. The compiled game is still the stock run over the whole level
script, VU0's registers the same at every one of 93,221,937 sync points,
and the interpreted run at every one of 47,705,744 event tests. Its time
did not change that the measurement can show: 1.21 times stock PCSX2's
(91.8 s against 76.1 s, the two timed in turn), where G6 ended at 1.21 to
1.23.
