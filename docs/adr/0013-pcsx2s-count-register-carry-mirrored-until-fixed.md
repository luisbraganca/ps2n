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

Later on 2026-11-11 the maintainer chose to report each defect to PCSX2 as
an issue of its own, with the correction they suggest, instead of one pull
request for both
([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md), "An
issue each instead of a pull request", which also has what a search of
PCSX2's issues and pull requests found: nothing about this defect). Nothing
else here changes: where this record says "the pull request", read "the
issue". The issue is PCSX2's number 15186, opened on 2026-11-11.

The addition became 64 bits wide with PCSX2's change to a 64-bit cycle
counter (its pull request 14105, merged on 2026-03-07). PCSX2's `master` of
2026-11-10 and its release 2.8.2 both have it.

**Changed on 2026-11-12.** The maintainer's words: "If the issues are
ignored, we'll implement the fixes locally, but they seem to be taking care
of the issues." How they were read: should the issue go 30 days without an
answer from PCSX2, this correction is carried in PS2Native's patch queue as
the status-flag one would be, and PS2Native stops reproducing the defect
then. The second bullet above said the opposite and no longer holds.

As it went, the issue was answered within the hour: the author of PCSX2's
change confirmed the defect and the correction (ADR 0010 has what was
said). So nothing is carried. Until a PCSX2 that has the correction is the
pinned one, PS2Native goes on reproducing the defect, on the condition
above; `tests/programs/countcarry.s` says when that day has come.

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
