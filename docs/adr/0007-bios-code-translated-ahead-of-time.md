# ADR 0007: The BIOS's code is translated ahead of time

| | |
|---|---|
| Status | Accepted. The maintainer confirmed the reading under "How this record reads the answer" on 2026-11-11: "Yes, do not interpret." Begun on 2026-11-12 as step G8 of Phase 3 ([ADR 0017](0017-bios-ee-code-compiled-as-step-g8.md)). |
| Date | 2026-11-10 (recorded 2026-11-11) |
| Decided by | Maintainer |
| Settles | Question 4, second half, of [phase1-feasibility.md](../phase1-feasibility.md), section 16 |

## Context

A BIOS is required ([ADR 0002](0002-static-cpus-on-pcsx2-core.md)): the
kernel, the program loader and the IOP's modules are Sony's code and run as
they are. Today the part of it that runs on the EE runs on PS2Native's
fallback interpreter, and every system call, interrupt and thread switch of
a program goes through it. [ADR 0003](0003-no-jit-in-generated-games.md)
wants it translated ahead of time like the game's own code. Translated BIOS
code is derived from Sony's BIOS, so whether it may exist was a question of
legal risk, and the maintainer's to answer.

## Decision

The question put to the maintainer: may a generated game contain translated
BIOS code? The recommendation: allow it, under three conditions. The
maintainer's answer: "If it's more efficient to not use BIOS let's translate
it instead."

## How this record reads the answer

1. **The BIOS's EE code is translated ahead of time** instead of being
   interpreted, because that is more efficient. The measurements are below.
2. **"Not use BIOS" is read as "not run the BIOS on the interpreter"**, not
   as "run without a BIOS file". A BIOS image is still required when a game
   is built and when it runs. The core loads the image as the console's ROM;
   the kernel copies itself and its data out of it; the IOP starts its
   modules from it; and translated code runs only after it has been compared
   with the code in memory, which comes from that image. A game that needs no
   BIOS file at all would need the kernel and the IOP's modules replaced by
   code of our own. The assessment set that aside (section 7, alternatives A
   and B) and nothing here reopens it.
3. **The three conditions of the recommendation are part of the decision.**
   The answer did not repeat them and did not object to them:
   - the translation is made on the user's own machine, from the user's own
     BIOS dump;
   - it is a file of its own beside the game, not part of the game's
     executable, so that what is derived from the BIOS stays apart from what
     is derived from the game, and one translation serves every game built
     for that BIOS;
   - it is never in the repository and never in anything PS2Native
     distributes.

   The first and the third only repeat the brief ("Never distribute
   proprietary BIOS files"). The second is a design constraint.

Asked on 2026-11-11 whether this reading is right, the maintainer answered:
"Yes, do not interpret."

## Is it more efficient? What was measured

On the development machine, 2026-11-10 and 2026-11-11; the tables are in
[poc-log.md](../poc-log.md), step P4.

- **Per instruction: yes.** PS2Native's interpreter takes 12 to 39 times as
  long as compiled code for the same work (the six loops of step P4).
- **In the test programs: yes, and it is mostly the boot.** With a program's
  own code compiled, a run still interprets 34 to 43 million BIOS
  instructions. At least 34.2 million are interpreted in every program,
  whatever it does, which points to the boot. Five programs took 0.31 to
  0.46 s longer than under stock PCSX2, whose recompiler compiles the BIOS
  like any other code. That is about what interpreting those instructions
  costs at the interpreter's speed.
- **In a game: not measured.** How much of a game's time is spent in the
  kernel is not known. The first real game will show it: the provider
  already counts the instructions it interprets, inside and outside the
  game's own code.

## When

After the EE, in the order ADR 0003 gives: EE, then the BIOS kernel, then
the VUs, then the IOP. It is not part of the proof of concept (Phase 2).

## Consequences

- The recompiler has to take a BIOS image as input, not only an executable,
  and the provider has to run more than one image at a time. Today it picks
  one image, by the checksum of the executable being started.
- The kernel runs from RAM, at the addresses it copies itself to. The check
  of compiled code against the code in memory covers that as it is.
- A translation belongs to one BIOS image. Another version or region of the
  BIOS needs its own, made the same way.
- Tests that use a translated BIOS must make it in the build tree, from the
  image `PS2N_BIOS` names, and may never add it to the repository.
- Packaging (Phase 4) has to keep the two files apart.
