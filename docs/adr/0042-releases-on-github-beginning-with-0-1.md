# ADR 0042: Releases on GitHub, beginning with 0.1

- Date: 2026-11-19
- Status: accepted (the maintainer's words, below).

## Context

On 2026-11-19, with P4.7 under way, the maintainer asked: "Should we create
a release on Github, like version 0.1?" The session recommended a release
0.1 once the Linux run was made, as the one that closes Phase 4, with both
folders as downloads (Windows' from `kit.ps1 -Out`, zipped; Linux' from
`kit.sh --archive`), built from the tagged commit, marked as a pre-release
on GitHub, a version the programs show, and release notes the maintainer
reads before the release is public. After the Linux run (ADR 0041) the
maintainer wrote: "Yep software render seems to fix it. Let's create
releases on Github."

GitHub's command-line tool was not installed on the development computer.
Asked how the release is made, the maintainer chose: "Install gh; you make
it", the session's recommendation: GitHub CLI installed with winget, signed
in once by the maintainer, and the release created by the session as a
draft, which the maintainer reads on GitHub and has published.

## Decision

1. PS2Native is released on GitHub (the repository's releases), beginning
   with **0.1**, tag `v0.1`.
2. A release holds both folders as downloads, made from the tagged,
   committed and pushed tree: `PS2Native-<version>-windows-x64.zip` (the
   folder `kit.ps1 -Out` makes) and `PS2Native-<version>-linux-x64.tar.xz`
   (`kit.sh --archive`), each with the licence of everything in it and
   where each source is; nothing of a game, a BIOS or PCSX2's patches. The
   source is the tag (PCSX2 the pinned submodule, `v2.9.170`).
3. 0.1 is marked as a pre-release: tried with five games, on two Windows
   computers, and on Linux only inside WSL 2 (ADR 0041), which its notes
   say.
4. The version is the file `version.txt` at the repository's root, which
   the build gives the programs (as a header it writes, `ps2n_version.h`):
   the wizard's title, the runtime's `-help`, the licences' README. (Not a
   file named `VERSION`: the root is on the include path, and on Windows it
   is found for the standard header `<version>`.)
5. The session creates a release as a draft, with its downloads and its
   notes; it is published at the maintainer's word.
