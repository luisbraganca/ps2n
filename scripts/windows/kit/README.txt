PS2Native: a PS2 game compiled and played on this computer
==========================================================

This folder needs nothing installed: no Visual Studio, no compiler, no emulator.
It holds the runtime that plays a game (runtime), the tools that compile one
(toolchain: LLVM's clang-cl and lld-link, PS2Native's recompiler and headers)
and one command, make-game.ps1. You bring the game's disc image and a BIOS
image of your own console; both are read where they are and nothing of them is
copied but into the game's folder this command makes.

Compile a game and play it, from a PowerShell window in this folder:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> -Play

For a game made of several executables that it loads itself (Agent Under Fire),
name them, and give what earlier sessions had to interpret, if you have it:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> `
        -More MOVIE.ELF,ACTION.UK,DRIVING.ELF -Captures <folder> -Play

The game goes into games\<name>: its modules beside a copy of the runtime, its
memory cards, every session played (written down to be run again), and what a
session had to interpret, which the next run of make-game.ps1 compiles too.
Once compiled, -PlayOnly opens it again without compiling.

In the window: the keyboard and the first controller play; F11 or Alt+Enter
covers the screen; closing the window ends the session.

Licences: licences\ (PS2Native and PCSX2: GPL-3.0; LLVM: Apache-2.0 with LLVM
exceptions). Microsoft's C++ runtime libraries in runtime\ and toolchain\bin
come from Visual Studio's redistributable files.
