# ADR 0006: The no-JIT rule covers PS2 program code only

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 (recorded 2026-11-11) |
| Decided by | Maintainer |
| Resolves | The open point of [ADR 0003](0003-no-jit-in-generated-games.md), "PCSX2's hardware-side code generators" |

## Context

ADR 0003 forbids a recompiler for PS2 code in a generated game. PCSX2 also
generates host code at run time in two places that are not CPUs:

- the VIF unpack recompiler (`pcsx2/x86/Vif_Dynarec.cpp`), which builds the
  routines that unpack vertex data;
- the software renderer's scanline generator (`pcsx2/GS/Renderers/SW/`),
  which builds the pixel loops and runs only when the software renderer is
  in use.

Neither translates PS2 instructions. Whether the rule was meant to cover
them had not been decided.

## Decision

The suggestion put to the maintainer: "the rule covers PS2 program code only,
and these two stay." The maintainer's answer: "Accept your suggestion".

- The rule of ADR 0003 covers the code of PS2 programs: EE code, the BIOS,
  VU micro programs and IOP modules.
- PCSX2's two hardware-side code generators stay in a generated game, as
  PCSX2 ships them. They are not stand-ins and have no removal milestone.

## Consequences

- A generated game still creates executable memory at run time for these
  two. "No JIT in a generated game" therefore means no recompiler for PS2
  code, and should be worded that way wherever it is claimed.
- The question comes back only if a platform that forbids run-time code
  generation is taken up. [ADR 0001](0001-platform-priorities.md) makes those
  platforms optional. Each generator has a compile-time switch in PCSX2
  (`newVifDynaRec` in `pcsx2/Vif_Dynarec.h`, `ENABLE_JIT_RASTERIZER` in
  `GS/Renderers/SW/GSDrawScanline.cpp`). What the core does with them off,
  and what that costs, has still not been examined or measured.
