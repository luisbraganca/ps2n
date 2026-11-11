# ADR 0005: Never further from the console than stock PCSX2

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 (recorded 2026-11-11) |
| Decided by | Maintainer |
| Replaces | The pass criterion of step P1 in [phase1-feasibility.md](../phase1-feasibility.md), section 14: "same output as stock PCSX2, and as the console wherever stock PCSX2 matches it" |

## Context

The old criterion could not be met as written. PCSX2's recompiler and a real
console disagree on some instructions, and PS2Native was already closer to
the console on three test programs, on purpose. Matching stock PCSX2 exactly
would have meant copying its known mistakes. The maintainer was asked what
counts as correct in that case.

## Decision

The recommendation put to the maintainer: replace the criterion with "never
further from the console than stock PCSX2, and every remaining difference
listed with its cause". The maintainer's answer: "Accept your recommendation".

How this record reads it, so that it can be checked:

- **Stock PCSX2** is PCSX2 as it ships: the pinned version with its
  recompilers, the same BIOS and the same settings. In the tests that is the
  `stock-jit` configuration.
- **Never further** is meant line by line: on no line of a test program's
  output does stock PCSX2 print what the console printed while PS2Native
  prints something else. Where stock PCSX2 crashes, nothing PS2Native prints
  is further.
- **Listed with its cause**: every program whose output differs from the
  console recording is named in
  [tests/ps2autotests-known-differences.cmake](../../tests/ps2autotests-known-differences.cmake)
  with the reason, and goes there only after the reason is known.

The rule for writing instruction semantics that follows from it is the one
already in use: do what PCSX2's recompiler does, since games are tested
against it; do what the console does where a recording shows the recompiler
wrong and PCSX2's interpreter right, or where the recompiler has no single
behavior; never invent a behavior that neither has.

## Where it stood when it was accepted, and now

The recommendation said that the code already met the criterion. That was
wrong by two lines, found on 2026-11-11 by comparing the outputs line by
line: in `cpu/vu0_macro/transfer`, stock PCSX2 printed the console's value
after `CTC2` to an integer register and to the status flag register, and
PS2Native did not. `CTC2` was still carried out by PCSX2's interpreter
function, which writes 32 bits to both.

`CFC2` and `CTC2` have been PS2Native's own since that day
([ops_cop2.h](../../runtime/include/ps2n/ops_cop2.h)). Measured afterwards,
on the 55 ps2autotests programs:

| | |
|---|--:|
| Lines stock PCSX2 has right and PS2Native has wrong | 0 |
| Lines PS2Native has right and stock PCSX2 has wrong | 53 |
| Programs matching the console: PS2Native | 40 |
| The same: stock PCSX2 with its recompilers, with its interpreters | 39, 38 |

The change cost one program. `vu/lower/integer` matched the console before
and now prints what stock PCSX2 prints, one line off. The cause is in
PCSX2's core, not in `CTC2`; [poc-log.md](../poc-log.md), "Increment 5", has
the details and the open question it leaves.

## What the criterion does not promise

PCSX2's interpreters are closer to the console than its recompilers on some
lines. PS2Native prints the recompiler's result there on purpose, because
that is what games are tested against: 50 lines of FPU arithmetic on operands
the host takes for infinities or NaNs (`cpu/ee_fpu/arithmetic`, `muldiv`,
`sqrt`), and 17 lines of `vu/lower/efu` and `vu/lower/integer` that depend on
PCSX2's VU recompiler, which runs the VUs for now.

## Consequences

- The criterion is a test: `ps2autotests/never-further/<program>` compares
  the compiled run with the stock run line by line
  ([tests/never_further.cmake](../../tests/never_further.cmake)). A change
  that moves a line away from the console fails it.
- The number of programs that match the console can go down when a behavior
  is corrected, as it did here. The criterion is about lines stock PCSX2 gets
  right, not about that number.
- A console recording is the only evidence that overrides the recompiler.
  Where there is none, the recompiler's behavior stands, also when PCSX2's
  interpreter looks more plausible.
