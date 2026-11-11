# ADR 0004: Proof-of-concept environment

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-10 |
| Decided by | Maintainer |

## Decision

- **PCSX2** is a git submodule pinned to tag `v2.9.170` (commit `a886f8ef`).
  Its prebuilt Windows dependency package may be downloaded from PCSX2's
  GitHub releases, and its core built locally.
- **BIOS:** the proof of concept reads the maintainer's `SCPH-70004` dump
  (v2.00, PAL) in place from `Documents\PCSX2\bios`. It is never copied into
  the repository, a build tree that is committed, or a test fixture.

  How "in place" is implemented (2026-11-10): PCSX2 writes the console's
  NVRAM beside whatever BIOS path it is given. So that nothing is written
  into the maintainer's PCSX2 folder, each test run links the image into its
  own scratch directory under the git-ignored build tree (a hard link on
  Windows, a symbolic link elsewhere) and removes the link when the run ends.
  A link is a second name for the same file, not a copy.
- **Linux:** builds and tests run on the maintainer's own Linux machine, by
  the maintainer. WSL and CI are not used for now.
- **Reference:** the PCSX2 already installed on the development machine, and
  a stock build of the pinned core, are the baselines for comparison.

## Consequences

- Nothing about Linux may be reported as working unless it comes from a run
  on the maintainer's machine. The repository must carry a script that builds
  and runs the headless tests there in one command, and its output is what
  gets reported.
- Windows is the only platform an AI session can build and run on directly.
- Tests that need the BIOS take its path from an environment variable or a
  local, git-ignored settings file, and skip with a stated reason when it is
  absent. They never fail silently and never pass without it.
