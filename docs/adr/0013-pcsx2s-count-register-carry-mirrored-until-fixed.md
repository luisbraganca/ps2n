# ADR 0013: PCSX2's count-register carry is offered upstream and mirrored meanwhile

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-11 |
| Decided by | Maintainer |

## Context

PCSX2 `v2.9.170`'s EE recompiler brings the count register up to date, when
it is read, with a 64-bit addition (`recMFC0`, `pcsx2/x86/iCOP0.cpp`). The
count register is 32 bits wide, and the 32 bits that follow it in memory are
another COP0 register, `EntryHi`. So each time the count register overflows,
about every fourteen seconds of the console's time, `EntryHi` goes up by
one. PCSX2's interpreter does not do that, and nothing says a console does.

PS2Native found this while holding its runs to stock PCSX2 register for
register, and reproduces it so that they can be
([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)). In the
first 4,200 frames of Killzone it happens once, and nothing was seen to
depend on it. The session asked whether to keep reproducing it.

## Decision

The maintainer's words: "Similar to point 1, let's work on a fix (use the
same branch for me to use both fixes on a single PR), if the PR stalls for
30 days, just keep mirroring the bug (only if it has no impact on
performance and visually)."

How they were read:

- **The correction is offered to PCSX2 by the maintainer**, in the same pull
  request as the correction to VU0's status flags
  ([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)). What
  an AI session may do for that pull request is said there, and is the same
  for this one.
- **Until PCSX2 has it, PS2Native goes on reproducing the defect.** That
  includes the case where the pull request stalls: unlike the status-flag
  correction, this one does not then go into PS2Native's patch queue.
- **On one condition: that it costs nothing in speed and changes nothing on
  screen.** If the defect is ever seen to slow a game or to change a
  picture, that is no longer true, and the maintainer decides again; the
  likely answer then is to carry the correction as the status-flag one is
  carried.
- When PCSX2 has the correction and the pinned PCSX2 is moved to a version
  that has it, PS2Native stops reproducing the defect: it follows the
  recompiler it is pinned to, never an older one.

## Consequences

- `MFC0` in `runtime/include/ps2n/ops_cop0.h` keeps the 64-bit addition,
  with the reason beside it.
- `tests/programs/countcarry.s` shows the defect in a program a few
  instructions long: it prints `EntryHi` before and after the count
  register overflows. What it prints is what PCSX2's recompiler makes of it;
  PCSX2's interpreter prints something else, which is listed. The day the
  pinned PCSX2 no longer has the defect, this test says so.
- The condition has to be watched for. Nothing measures it by itself:
  `EntryHi` matters to a program that reads it back, or that writes the TLB
  without setting it first. A game that behaves differently from a console
  around the fourteenth second of anything is where to look.
