# ADR 0027: Phase 3 ends; Phase 4 begins with a design proposal

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-16 |
| Decided by | Maintainer, on the session's recommendation |

## Context

With Agent Under Fire's first five missions played by the maintainer and
its fourth mission's slowdown put right ([ADR 0026](0026-a-second-game-and-games-of-several-executables.md)),
the maintainer asked: "What should we work on next?"

The session's answer: "My recommendation: close Phase 3 and start Phase 4,
the user application." Why: what the brief asks of Phase 3 is there, for
two games (every processor's code compiled ahead of time, the runs held to
stock PCSX2 at every sync point, Killzone at stock PCSX2's speed, Agent
Under Fire at the console's pace in the window), and what is open is listed
as known limitations, none of which blocks the next phase; while today a
game exists only inside the development host, built with Visual Studio from
the build tree, one game to a build. How: a closing report, the
maintainer's approval, then a design proposal for Phase 4, beginning with
how a user's computer compiles a game. Other courses offered: speed parity
without profile-guided optimization; playing on (G13); Linux. Then: "Shall
I write the Phase 3 closing report?" The maintainer: "Yes".

The report was written ([phase3-report.md](../phase3-report.md)), and put
to the maintainer with: "End Phase 3 and start Phase 4? Recommended: yes.
I'd record your answer as ADR 0027. Phase 4 would begin with a design
proposal for you to decide on, before any code, starting with how a user's
computer compiles a game", and whether to commit and push the report. The
maintainer: "1. Yes 2. Yes".

## Decision

As it was read:

- **Phase 3 ends on 2026-11-16**, with what the report records: its steps
  G0 to G18 ([game-log.md](../game-log.md)), compatibility, the comparisons
  with stock PCSX2, visual issues, performance, regressions and limitations.
- **G13, playing on, ends with it.** How far was the maintainer's to say.
  The tools stay: a game can still be played in the window, its sessions
  written down, captured and run again.
- **Phase 4, the user application, is approved**: in the brief's words, the
  compilation wizard, game recognition and settings, the compilation
  pipeline, packaged native applications, meaningful diagnostics, and more
  platforms.
- **It begins with a design proposal, which the maintainer decides on
  before any code is written**, first of all how a user's computer compiles
  a game: a compiler brought with the wizard, or a game's code compiled into
  a module a prebuilt runtime loads (the assessment, risk 10). The report's
  "What Phase 4 has to decide first" lists the rest. Phase 4's steps and
  their pass criteria come with the proposal.
