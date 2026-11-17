# ADR 0029: A game's module reaches the runtime through its import table

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-17 |
| Decided by | Maintainer, by a rule of theirs applied to a measurement the session made |

## Context

Step P4.1 ([app-log.md](../app-log.md), "P4.1") built a game's compiled code
into a module of its own that the host loads, as
[ADR 0028](0028-phase-4s-design-a-shipped-compiler-and-game-modules.md) has
it, and measured what that costs. In the module, compiled code reaches the
core's variables and calls the runtime's functions through the module's
import table, loading each one's address first, where code built into the
host reaches them directly. Every test passed, and the game took no longer
than with its code built in.

The session put to the maintainer: "Go on to P4.2 with this design? ... I
recommend yes: the boundary measured at no cost. The alternative is first
building a custom loader that places the module next to the runtime to
avoid the address lookups, which isn't worth it for a cost nobody can
measure."

The maintainer: "1. If the custom loader makes the game faster, I'd go with
the custom loader. If not, your recommendation." And then: "1. If it
doesn't affect game speed, only compile speed, I'd also go with your
recommendation."

## The measurement

A loader of PS2Native's own would change one thing: compiled code would
reach the core's variables directly, as code built into the host does. So
the session measured that alone. The same generated code, compiled as for a
module, was linked into the host all the same (`build.ps1 -Module imports`),
whose linker fills the import table's entries itself, and timed beside the
code built into the host as usual, on 2026-11-17, every run fully compiled:

| Agent Under Fire | Built into the host (direct) | The same, through the import table |
|---|---:|---:|
| Its script, from frame 1, three rounds | 31.21, 31.08, 31.26 s (1.18 times stock PCSX2's) | 30.75, 30.67, 30.77 s (1.16) |
| Its play, the last 2,250 frames | 9.01, 8.98, 8.99 s (1.10) | 8.70, 8.72, 8.75 s (1.07) |
| The maintainer's second session replayed, whole | 512.3 s | 500.6 s (0.977) |
| Of it, the fourth mission | 146.1 s | 139.4 s (0.954) |

The code that reaches the core directly was the slower one, in every part.
The module, which reaches it through the import table too, took what this
build takes (twice the session replayed: 0.977 and 0.979 of the built-in
code's time, the fourth mission 0.954 and 0.959).

Why is not known. In the fourth mission's wait loop the two differ only in
how the core's register block is addressed (`[rip+offset]` against
`[rbp+offset]`, its address loaded once), the same instructions otherwise;
the difference may come from where the code falls in memory rather than
from the import table itself. That is for the speed step the maintainer
left for later (ADR 0028, decision 6).

## Decision

As read: a loader of PS2Native's own would give compiled code that reaches
the core directly, which measured no faster (slower), so the session's
recommendation applies. **A game's module reaches the runtime through its
import table, as an ordinary DLL does**, and P4.2 goes on on this design.
No loader of PS2Native's own is built. On Linux (P4.7) a shared object's
counterpart is its global offset table; that is not measured yet.

`build.ps1 -Module imports` stays, to measure the same question again when
the generated code changes.
