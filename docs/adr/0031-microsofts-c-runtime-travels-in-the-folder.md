# ADR 0031: Microsoft's C++ runtime travels in the folder

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-17 |
| Decided by | Maintainer, on the session's recommendation |

## Context

The runtime PS2Native ships (`ps2n-host.exe`, built once by the project)
and the libraries of PCSX2's it uses (SDL3, zstd, freetype and the others,
from PCSX2's dependency package) are built against Microsoft's C++ runtime:
`msvcp140.dll`, `vcruntime140.dll` and `vcruntime140_1.dll`, 0.85 MB
together. Windows 10 and 11 have the rest of what they need (the Universal
C Runtime), but not these, unless something else installed them: a game
folder without them does not start. LLVM's `clang-cl` and `lld-link`
(ADR 0030) need none of them. For step P4.3 the session carried them in the
folder, from Visual Studio's redistributable files, for the maintainer's
own computers only, and left whether a packaged game may carry them to the
start of step P4.4 ([app-log.md](../app-log.md), "P4.3").

Microsoft's own page on redistributing them ("Redistribute Visual C++
Files", Microsoft Learn, 2026-04-13): "Distribution of the Visual C++
Runtime Redistributable package, merge modules, and individual binaries is
limited to licensed Visual Studio users and is subject to Microsoft
Software License Terms"; "It's also possible to directly install the
Redistributable DLLs in the application local folder ... For servicing
reasons, we don't recommend that you use this installation location"; the
packages it recommends instead "enable automatic updating of the Visual
C++ libraries".

The session put to the maintainer: "The runtime the wizard ships, and every
game folder it makes, needs Microsoft's C++ runtime (three DLLs, 0.85 MB),
which a fresh Windows 10/11 may lack. Where should it come from on a user's
computer?" Recommended: in the folder, the three DLLs beside the runtime,
nothing to install, on any Windows 10 or 11; allowed for licensed Visual
Studio users (Visual Studio Community is free for open-source projects such
as this one), but Windows Update does not patch those copies, so
PS2Native's own updates would bring newer ones. The other ways: Microsoft's
installer, downloaded from Microsoft at the user's word where the runtime
is missing (one administrator prompt; a small launcher that starts without
the runtime); or the runtime and all of PCSX2's libraries rebuilt with
Microsoft's runtime linked in (a large job, away from PCSX2's own build).

The maintainer chose "In the folder (Recommended)".

## Decision

As read: **Microsoft's C++ runtime travels in the folder.** The wizard's
folder and every game folder it makes carry `msvcp140.dll`,
`vcruntime140.dll` and `vcruntime140_1.dll` beside the runtime, taken from
Visual Studio's redistributable files (`scripts\windows\kit.ps1` does so),
as Visual Studio's licence terms allow a licensed user. Nothing is
installed for them; newer copies come with PS2Native's own updates, not
with Windows Update.
