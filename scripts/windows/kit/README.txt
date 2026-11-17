PS2Native: a PS2 game compiled and played on this computer
==========================================================

This folder needs nothing installed: no Visual Studio, no compiler, no emulator.
It holds the runtime that plays a game (runtime), the tools that compile one
(toolchain: LLVM's clang-cl and lld-link, PS2Native's recompiler and headers)
and one command, make-game.ps1. You bring the game's disc image and a BIOS
image of your own console; both are read where they are and nothing of them is
copied but into the game's folder this command makes.

What the computer needs: Windows 10 version 1803 or later, or Windows 11,
64-bit (winver says which); a processor with SSE4.1 (Intel from 2008, AMD from
2011); a graphics driver with Vulkan, or else add -Renderer dx11 to the
command; about 1 GB of free disk; and, while it compiles, up to about 0.6 GB of
memory for each file compiled at once, one per processor unless -Jobs says
fewer (-Jobs 4 on a computer with 8 GB of memory, -Jobs 2 with 4 GB).

Make a game into a folder of its own and play it, from a PowerShell window in
this folder:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> -Title Killzone -Play

For a game made of several executables that it loads itself (Agent Under Fire),
name them, and give what earlier sessions had to interpret, if you have it:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> `
        -Title 'Agent Under Fire' -More MOVIE.ELF,ACTION.UK,DRIVING.ELF -Captures <folder> -Play

A game's widescreen and frame-rate patches come from PCSX2's own archive of
them, patches.zip, which PS2Native does not ship: an installed PCSX2's is taken
by itself; where PCSX2 is not installed, give one with -PatchArchive <file>
(it is in the resources folder of PCSX2's release). Without it the game plays
as its disc has it, 4:3.

The game goes into games\<title>, a folder that plays it: start <title>.exe
there (double-click it, or make a shortcut to it). Beside it are its modules, a
copy of the runtime, and settings.txt, what the runtime is started with: the
disc image and the BIOS image, read where they were when the folder was made
(move them, and make the folder again or change the file), the patches, the
window's title, one option to a line. In the folder too are its memory cards
and the console's settings (data), every session played, written down to be run
again with what it had to interpret, which the next run of make-game.ps1
compiles too (sessions), and last-run.log, what the runtime said the last time;
when it could not play the game, <title>.exe says why. The folder can be moved
or copied whole; work\, the generated code, can be deleted. -PlayOnly opens the
game from this window again without compiling, and writes its settings again.

In the window: the keyboard and the first controller play; F11 or Alt+Enter
covers the screen; closing the window ends the session.

Licences: licences\ (PS2Native and PCSX2: GPL-3.0; LLVM: Apache-2.0 with LLVM
exceptions). Microsoft's C++ runtime libraries in runtime\ and toolchain\bin
come from Visual Studio's redistributable files.
