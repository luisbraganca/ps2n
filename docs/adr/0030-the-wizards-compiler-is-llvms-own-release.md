# ADR 0030: The wizard's compiler is LLVM's own release, checked on another computer

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-17 |
| Decided by | Maintainer, on the session's recommendations |

## Context

The wizard brings its own `clang` and `lld`, and a game is compiled on the
user's computer ([ADR 0028](0028-phase-4s-design-a-shipped-compiler-and-game-modules.md),
decision 1). Step P4.2 made a game's generated code need nothing but those
and PS2Native's own headers ([app-log.md](../app-log.md), "P4.2"): of the
compiler, `clang` (as `clang-cl`), `lld-link` and 18 of its headers (569 KB).
Step P4.3 ships them: its criterion is a game compiled and run on a
computer with nothing else installed.

The `clang` and `lld-link` the development builds use are the ones that
come with Visual Studio's build tools: LLVM 22.1.3, built by Microsoft and
installed under Visual Studio's licence. LLVM publishes the same version
itself, for Windows on x64 among others (released on 2026-04-07: an
installer, `LLVM-22.1.3-win64.exe`, and an archive,
`clang+llvm-22.1.3-x86_64-pc-windows-msvc.tar.xz`), under Apache-2.0 with
the LLVM exception, which lets it be redistributed.

The session put two questions to the maintainer:

1. "P4.3 needs a clang and lld-link that PS2Native can ship with the
   wizard. Where should they come from?" Recommended: LLVM's own 22.1.3
   release, its Windows archive downloaded once into the git-ignored
   `deps` folder, of which only `clang`, `lld-link` and the 18 headers are
   kept: the same version as the clang that builds and tests everything
   here, under a licence that lets it be shipped. The other way: Visual
   Studio's copy, nothing to download, but Microsoft's build under Visual
   Studio's licence, whose shipping is not clearly allowed.
2. "P4.3 passes when a game is compiled and run on a computer with nothing
   else installed. Where should that final check happen?" Recommended:
   another computer of the maintainer's, one without Visual Studio, to
   which the maintainer copies one folder and on which they run one
   command: the only way that also shows that every library the runtime
   needs is shipped, with nothing to install. The other ways: a new
   Windows user on this computer (on which Visual Studio and Microsoft's
   C++ runtime stay installed for every user, so that something missing
   would not show), or a virtual machine here (Windows 11 Home has neither
   Hyper-V nor Windows Sandbox, so it means installing one first).

The maintainer chose "LLVM's own 22.1.3 release (Recommended)" and "Another
computer of yours (Recommended)".

## Decision

As read:

- **The compiler the wizard ships is LLVM's own release**, 22.1.3, the
  version the development builds use, from LLVM's Windows x64 archive,
  downloaded once into `deps` (git-ignored, as PCSX2's dependencies are).
  Of it, what a module needs is kept and shipped: `clang`, `lld-link` and
  the compiler's headers a module includes. Visual Studio's copy is not
  shipped. A later version is a decision of its own, as moving PCSX2's pin
  is.
- **P4.3's criterion is checked on another computer of the maintainer's**,
  one without Visual Studio: the session prepares a folder and one command,
  and the maintainer runs them there and says what happened. Until then the
  session checks what it can on this computer.
