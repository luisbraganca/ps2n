# ADR 0009: Phase 3 is approved; the first game is Killzone

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-11 |
| Decided by | Maintainer |

## Context

The proof of concept (Phase 2) is done on Windows; its Linux step is
deferred ([ADR 0008](0008-linux-run-deferred.md)). Phases are gated. The
maintainer was asked whether to approve Phase 3, the first real game, with
the recommendation to do so after one piece of timing work (reproducing
PCSX2's timeout loop skip), and which game it should be; the assessment had
suggested Killzone.

## Decision

The maintainer's answer: "Yes, let's work with Killzone I guess? On the
previous project, the game files were located at C:\ps2, does that work? If
yes, it should be there already."

- **Phase 3 is approved.**
- **The game is Killzone**, the European release (`SCES-52004`), the
  maintainer's own copy.
- **Its files are used where they are**, in `C:\ps2\killzone`: the disc
  image, which the core boots from, and the files extracted from it, of
  which the recompiler reads the executable. That works: nothing has to be
  moved or copied.
- The timing work came first and is done ([poc-log.md](../poc-log.md),
  "Timeout loops").

## What Phase 3 is, from the brief

Attempt genuine native recompilation of one game; implement what is missing
through reusable solutions; compare the results with PCSX2; document
compatibility, visual issues and performance; record limitations and
regressions. No game-specific hardcoding unless unavoidable. No wizard, no
packaging, no user interface beyond what judging the game needs.

## Consequences

- Nothing from the game enters the repository, and nothing generated from
  it: not the executable, not captures of its code, not generated C++, not
  pictures of it. Generated code and run files go to the git-ignored build
  tree. What the log reports about the game is numbers, addresses and words.
- The steps, their pass criteria and what has been measured are in
  [game-log.md](../game-log.md).
- Phase 4 needs its own approval.
