# ADR 0003: No run-time translation of PS2 code in generated games

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 |
| Decided by | Maintainer |
| Overrides | The recommendation in [phase1-feasibility.md](../phase1-feasibility.md), section 15 item 7, to decide after Phase 3 whether static VU and IOP translators are worth building |

## Decision

In the maintainer's words:

- "don't compile JIT like PCSX2, we should fully pre-compile the game (or as
  much as possible, to improve performance)."
- "PCSX2 recompilers but make sure that it's only temporary, the goal is that
  JIT shouldn't ship inside a generated game."

**End state.** EE code, the BIOS kernel, VU micro programs and IOP modules are
all translated ahead of time. A generated game that is handed to a player
contains no recompiler for PS2 code.

**Temporary stand-ins.** While the static VU and IOP translators do not exist,
PCSX2's microVU and IOP recompilers may run that code, under these rules:

1. Development builds only. Every build report and every start-up log names
   each CPU that is on a stand-in as "not static: run-time JIT".
2. The packaging step (Phase 4) must not produce a release build while any
   stand-in is in use. It refuses, or marks the result a development build.
3. Each stand-in has a removal milestone in the plan, and the static
   translator that replaces it is validated against it before it goes.
4. PCSX2's EE recompiler is never a stand-in. EE code is static from the
   first proof-of-concept step.

**Code not seen at build time.** "As much as possible": code that does not
exist until the game runs (overlays unpacked from archives, code a game
writes) is interpreted by PS2Native's own interpreter the first time, always
reported and counted, captured with its bytes, and included by the next
build. A strict mode stops at the first interpreted address.

**Order of work.** EE, then the BIOS kernel, then VU, then IOP.

## PCSX2's hardware-side code generators (settled)

PCSX2 contains two generators of host code that do not translate PS2 code:
the VIF unpack recompiler (`pcsx2/x86/Vif_Dynarec.cpp`) and the software
renderer's scanline generator (`pcsx2/GS/Renderers/SW/`), which runs only
when the software renderer is in use. This record read the rule as covering
translation of PS2 code, so that those two are outside it. The maintainer
confirmed that reading on 2026-11-10:
[ADR 0006](0006-no-jit-rule-covers-ps2-code-only.md).

## The BIOS (settled)

The end state above has the BIOS kernel translated ahead of time. Whether
translated BIOS code may exist at all, being derived from Sony's BIOS, was
still open when this record was written
([phase1-feasibility.md](../phase1-feasibility.md), section 16). It was
decided on 2026-11-10:
[ADR 0007](0007-bios-code-translated-ahead-of-time.md).

## Consequences

- Static VU and IOP translation are required deliverables, not optional ones.
- The VU translator, the riskiest component, is still off the critical path
  of the first real game: the stand-in lets the EE design be proven first.
- Each static translator has an exact oracle while it is being built: the
  PCSX2 recompiler it replaces.
