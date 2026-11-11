# ADR 0001: Platform priorities

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 |
| Decided by | Maintainer |
| Supersedes | The list of initial targets in [brief.md](../brief.md), section 5 |

## Context

The brief named four initial targets (Windows x64, Linux x64, macOS ARM64,
macOS x64) and asked that Android and iOS be considered later.

Phase 1 ([phase1-feasibility.md](../phase1-feasibility.md), section 4.8)
found that PCSX2 `v2.9.170` has EE, VU and IOP recompilers on x86-64 only.
Its ARM64 build compiles but is flagged unsupported and has none, and the
released macOS build is x86-64 under Rosetta 2. The recommended architecture
depends on PCSX2's core, so PS2Native's platform support is bounded by
PCSX2's.

## Decision

In the maintainer's words: "If PCSX2 lacks compatibility for some platforms I
don't mind to ditch some compatibilities. Windows and Linux is top priority,
everything else is optional."

- **Required:** Windows x64, Linux x64.
- **Optional:** macOS ARM64, macOS x64, Android, iOS. Any of them may be
  dropped where PCSX2's own support is lacking.

## Consequences

- On the required platforms the goal is parity with PCSX2 in a standalone
  form. The one target where static code would have beaten PCSX2 (macOS
  ARM64) is no longer required.
- Both required platforms are x86-64, so the instruction semantics library
  may use the same SSE operations as PCSX2's JIT. Equivalence across host
  architectures is no longer part of the first deliverables.
- PCSX2's recompilers exist on every required platform, which makes them
  available as stand-ins for CPUs without a static translator. Whether to use
  them is a separate decision, not made here.
- Portability remains a design rule: generated code stays portable C++, and
  architecture-specific code is confined to the semantics library and the
  PCSX2 adapter. Nothing is built or tested for an optional platform until
  the maintainer asks for it.
- Linux must be built and tested from the proof of concept on. The
  development machine has no Linux environment yet; choosing one is open.
