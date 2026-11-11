# ADR 0002: Static CPUs on the PCSX2 core

| | |
|---|---|
| Status | Accepted, for the Phase 2 proof of concept |
| Date | 2026-11-10 |
| Decided by | Maintainer |

## Context

[phase1-feasibility.md](../phase1-feasibility.md), sections 7 to 9, compared
three architectures. Projects that replaced the PS2's hardware model with
their own each run two to five games. PCSX2's compatibility and picture
quality live in its hardware model and its game database.

## Decision

In the maintainer's words: "Approve, but don't compile JIT like PCSX2, we
should fully pre-compile the game (or as much as possible, to improve
performance)."

- The game's code is translated ahead of time and acts as the console's CPUs.
- PCSX2's core, pinned to a nightly tag, is the machine: memory, scheduler,
  DMA, GIF, VIF, IPU, SIF, IOP hardware, GS and its renderers, SPU2, CDVD,
  pads, memory cards, game database and patches.
- A user-supplied PS2 BIOS is required at build time and at run time.
- The generated game has no PCSX2 user interface.
- What "fully pre-compile" means in practice is [ADR 0003](0003-no-jit-in-generated-games.md).

## Consequences

- PS2Native owns the recompiler, the instruction semantics, the CPU
  providers, the host shell and later the wizard. It owns no hardware model.
- On Windows and Linux the result can at best equal PCSX2 in compatibility,
  picture and sound. Whether pre-compiled code is faster than PCSX2's
  recompilers is **unproven** and is measured in the proof of concept (step
  P4) before anyone claims it.
- Approval covers the proof of concept. Phase 3 needs its own go-ahead once
  the proof of concept's results are in.
