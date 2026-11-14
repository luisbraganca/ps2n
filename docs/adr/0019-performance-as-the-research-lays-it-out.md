# ADR 0019: Performance, as the maintainer's research lays it out

| | |
|---|---|
| Status | Accepted; done (ended by the maintainer, 2026-11-13, [ADR 0022](0022-vu-micro-programs-compiled-as-step-g11.md)) |
| Date | 2026-11-12 |
| Decided by | Maintainer |

## Context

After step G9 ([ADR 0018](0018-vu0-instructions-carried-out-by-compiled-code.md))
the compiled game took 1.19 times stock PCSX2's time over the level run.
Asked what to work on next, the session recommended taking the game further
by script, compatibility coming first, or giving the COP2 transfers the same
short path as the VU0 instructions.

## Decision

The maintainer's words: "2. We need to improve performance. Here's what I
go with some research:", with two pictures of text, which read:

> PCSX2 already does sophisticated things: keeps PS2 registers in host CPU
> registers, links compiled blocks directly, and uses fast memory accesses.
> Your generated C++ must compete with those optimizations while maintaining
> PS2 state, memory mappings and timing. Translating instructions into C++
> does not automatically recover the efficient structure of the game's
> original source.

> 1. **Measure actual gameplay and identify where host time goes.** Use
>    repeatable scenes, identical settings, no screenshots or correctness
>    traces during timing, and both the null renderer and the hardware
>    renderer players will use. Separate startup from sustained play.
>    Profile compiled EE code, BIOS interpretation, vector helpers, memory
>    access, dispatch and the shared PCSX2 components.
> 2. **Remove the expensive interpreted paths the profile identifies.** BIOS
>    compilation and native VU0 instruction implementations are strong
>    candidates. Preserve the existing correctness comparisons while doing
>    this. Neither should be assigned a promised speedup before measurement.
> 3. **Make the generated code competitive with PCSX2's machine code.** Keep
>    values in host registers longer, reduce helper calls and indirect
>    dispatch, improve common RAM accesses, and use appropriate SIMD
>    operations. Evaluate link-time optimization and profile-guided
>    optimization. The project already builds optimized code; this is more
>    than switching on a Release setting.
> 4. **Exploit information available ahead of time.** Specialize known
>    addresses and call targets, optimize larger regions where timing
>    permits, and potentially replace identified expensive routines with
>    equivalent native implementations. This is where AOT can earn an
>    advantage beyond simply doing the same translation earlier.

How it was read:

- **Step G10, inside Phase 3**, in the order the research gives, and in the
  worktree of G9 (branch `perf`), because the session doing G8 works in the
  main working tree; the two sessions agree before timing anything.
- **Measurement first** (item 1). The level script is the repeatable scene;
  it is timed with nothing drawn and with the hardware renderer PCSX2 picks
  for this computer (Direct3D 12 on its GeForce RTX 4070 SUPER), as players
  would have it, and its startup (power-on, menus, the level loading, to
  frame 6,600) apart from its play (frames 6,600 to 17,500), stock PCSX2
  timed in turn with the compiled game. No pictures and no comparison
  during timing. The profile looks at play alone. The host gained what that
  needs: `-mark` (the time at chosen frames) and `-profile-from`.
- **Item 2**: the BIOS's code is G8's (ADR 0017), the other session's; the
  VU0 instructions are done (G7, G9). Nothing gets a promised speedup;
  every change is measured, and the comparisons with stock PCSX2 and with
  the interpreter are kept and run on each.
- **Items 3 and 4** are taken one change at a time, in the order the
  measurement says, each measured, as in G6
  ([ADR 0015](0015-a-speed-step-before-phase-3-ends.md)).
- **What would change how time is counted, or what is compared with stock
  PCSX2**, is put to the maintainer before it is made, as ADR 0015 has it:
  time is counted as PCSX2's recompiler counts it
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)), so
  "larger regions where timing permits" means regions whose blocks, and so
  whose time, stay the same; replacing a routine with a native
  implementation that does not keep the stock run at every sync point is
  such a change.

## End

On 2026-11-13, with play in the first level as long as under stock PCSX2
and the whole level run at 1.01 times its time, still the stock run, the
session recommended ending the step there ("its aim, the game's code no
slower than PCSX2's, is met in play"). The maintainer's word: "Yep." The
step ended where it stood; the next is G11
([ADR 0022](0022-vu-micro-programs-compiled-as-step-g11.md)).
