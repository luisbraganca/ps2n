# ADR 0008: The first Linux run is deferred

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 (recorded 2026-11-11) |
| Decided by | Maintainer |
| Affects | Step P5 of the proof of concept ([phase1-feasibility.md](../phase1-feasibility.md), section 14) |

## Context

Linux x64 is a required platform ([ADR 0001](0001-platform-priorities.md)),
and only the maintainer can build and test there
([ADR 0004](0004-proof-of-concept-environment.md)). At the end of the proof
of concept on Windows, `scripts/linux/build.sh` and `scripts/linux/test.sh`
had been written from PCSX2's own build recipe and never executed. The
maintainer was asked to run them, with the recommendation to do so before
approving Phase 3, because problems are cheaper to fix before more is built
on top.

## Decision

The maintainer's answer: "Don't worry about it for now."

- Step P5 stays open. It is not done, and it is not dropped.
- The first Linux run happens when the maintainer chooses. Until then it
  holds nothing else up.

## Consequences

- Nothing may be reported as working on Linux. That part of ADR 0004 is
  unchanged, and so is ADR 0001: Linux is still required.
- Everything written from here on is untested on Linux until that run. What
  is known to depend on the platform, and so most likely to need work then:
  the build scripts themselves; PCSX2's Linux dependency build; the links
  the tests make to the BIOS image; the frame fingerprint of step P3, which
  was recorded on one Windows machine and one graphics card.
- The Linux scripts are kept in step with the Windows ones as those change,
  without being able to run them. Whoever changes a build or test script
  changes both.
