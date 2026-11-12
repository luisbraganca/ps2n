# ADR 0017: The BIOS's EE code compiled ahead of time, as step G8 of Phase 3

| | |
|---|---|
| Status | Accepted; done (2026-11-12) |
| Date | 2026-11-12 |
| Decided by | Maintainer, on the session's recommendation |

## Context

Step G7 ([ADR 0016](0016-vu0-instructions-with-semantics-of-ps2natives-own.md))
ended on 2026-11-12. All of the game's own code that has run is compiled
ahead of time. What the EE still interprets is the BIOS. Under PCSX2's fast
boot, which `ps2n-host` uses, the EE runs four pieces of it, each a file of
the ROM's directory:

- the reset code, in the ROM itself (`BFC00000`);
- the kernel (`KERNEL`, 94 KB), which runs at `80000000`;
- the program loader `EELOAD` (`00082000`);
- the idle thread `EENULL` (`00081FC0`).

In the level run that is 173.5 million instructions, 0.21% of the EE's cycles
([game-log.md](../game-log.md), "G4"). By the samples of G7's profile, the
interpreter's own functions take about one second of the 92 s run (679 of
61,526 samples, in `RunRecSteps`, `RunRecStep`, `ExecuteOwn`, the decoder
and `FindStep`). That is an estimate, not a measurement. In each test program
at least 34.2 million BIOS instructions are interpreted at boot, and they
cost 0.31 to 0.46 s against stock PCSX2 ([poc-log.md](../poc-log.md), "P4").

[ADR 0007](0007-bios-code-translated-ahead-of-time.md) decided that the
BIOS's EE code is translated ahead of time ("Yes, do not interpret"). Its
order is that of [ADR 0003](0003-no-jit-in-generated-games.md): the EE, then
the BIOS kernel, then the VUs, then the IOP. The assessment (section 9.1)
kept the kernel on the fallback interpreter during Phases 2 and 3, as an
interim.

The session proposed the step, with its work and its pass criterion (now in
the game log, "G8"), and put three questions. As they were put:

1. "Do G8 now, as a step inside Phase 3? I recommend yes: it's next in the
   order you already approved (the EE, then the BIOS kernel); it stands on
   its own; the existing comparisons can test it. The original assessment
   planned to keep the BIOS interpreted until after Phase 3, so this is
   ahead of that plan, not against it. If you say no, Phase 3 ends here with
   the BIOS still interpreted, and the next question is Phase 4."
2. "While developing, link the compiled BIOS into `ps2n-host` from the build
   tree (its own library and image), and make it a separate file beside the
   game only at packaging in Phase 4. ADR 0007's consequences already put it
   there, so I recommend this. The alternative, a separately loaded library
   from the start, is more work now: the runtime and PCSX2's state would
   have to be reached across a library boundary. The tests wouldn't show
   anything different."
3. "Commit G7 before G8 starts, so the two steps don't share a commit. I
   recommend yes. The other session knows that work best, but I can do it
   if you prefer."

## Decision

The maintainer's words: "1. Yes. 3. Yes. 2. Agree."

How they were read:

- **G8 is a step of Phase 3**, as proposed: the BIOS's EE code compiled
  ahead of time, beginning with the four pieces that run under fast boot.
  Its work comes in the order proposed, each part checked before the next,
  and measurement comes first. Its pass criterion is the one proposed, in
  the game log.
- **During development the translation is linked into `ps2n-host`** from the
  build tree, as a library and images of its own, made from the BIOS dump
  the build is given. It is never in the repository. A file of its own
  beside a packaged game belongs to packaging, in Phase 4 (ADR 0007,
  "Consequences").
- **G7 was committed before G8 began**, so that the two steps do not share a
  commit. The session that did G7 committed it (`53e68ae`), as recommended.

## What does not change

- A BIOS image is still required at run time. Translated code runs only
  after its words have been compared with the code in memory (ADR 0007).
- Time is counted as PCSX2's recompiler counts it
  ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)). The
  compiled BIOS is held to the stock run and to the interpreted run, like
  the game.
- The BIOS's IOP modules stay on PCSX2's IOP recompiler, one of the stand-ins
  of ADR 0003, until the IOP's own step.
- Full boot (the BIOS's own menu: `OSDSYS`, `PS2LOGO`) is not part of this
  step. What it runs stays interpreted and reported.

## What was done

On 2026-11-12 ([game-log.md](../game-log.md), "G8"):

- **The BIOS's EE code is compiled ahead of time** from the dump the build
  is given (`build.ps1 -Bios`), into the build tree: four images
  (`ps2n-recomp bios`) of the five files of the ROM the EE runs under fast
  boot (`RESET` and `RDRAM` in the ROM, through either of its mirrors; the
  kernel; the idle thread and the program loader). Their code is followed
  from where it is entered rather than swept, because the kernel's file
  holds data among its code. The runtime uses them from the first
  instruction on, beside the game's.
- **Of the BIOS, 51 instructions at boot are interpreted**, each with its
  cause: three blocks the reset code forms before it sets `Config`, which
  are counted double, and a `DI` before an `ERET`. Where there were 34 to
  43 million in a test program and 213.6 million in the game's first 4,200
  frames.
- **What the game copies into kernel memory is the game's code, not the
  BIOS's**: its words are in the game's executable and not in the ROM. It
  is captured and compiled with the game, as an image of the code it ran
  outside its executable (`game_outside`).
- **Every test passes, and the game is still the stock run** over the level
  script and the interpreted run over its first 4,200 frames.
- **The level run takes 1.1 s less**: 1.18 times stock PCSX2's time, where
  it took 1.19 with the BIOS interpreted in the same build. A test
  program's boot takes as long as under stock PCSX2, where it took 109 to
  157 ms more.

G8 was made on G7 while the other session made G9 (ADR 0018) in a worktree
of its own; G9 was committed first, and G8 was rebased onto it and checked
again before it was committed.
