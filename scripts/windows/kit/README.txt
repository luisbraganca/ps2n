PS2Native: a PS2 game compiled and played on this computer
==========================================================

This folder needs nothing installed: no Visual Studio, no compiler, no emulator.
It holds PS2Native.exe, the application a game is made with; the runtime that
plays a game (runtime); the tools that compile one (toolchain: LLVM's clang-cl
and lld-link, PS2Native's recompiler and headers); and make-game.ps1, which does
what PS2Native.exe does from a command line. You bring the game's disc image
and a BIOS image of your own console; both are read where they are.

What the computer needs: Windows 10 version 1803 or later, or Windows 11,
64-bit (winver says which); a processor with SSE4.1 (Intel from 2008, AMD from
2011); a graphics driver with Vulkan, or else choose another renderer in the
game's settings; about 1 GB of free disk for each game; and, while it compiles,
up to about 0.6 GB of memory for each processor.

Make a game: start PS2Native.exe, and follow its five steps.

  1. The game: its disc image, an .iso file of your own disc. PS2Native
     recognizes it with PCSX2's database, and finds the executables on it.
  2. The BIOS: an image dumped from your own console.
  3. The settings: PCSX2's own settings for the game, each at PCSX2's default
     but for PS2Native's (Vulkan, three times the console's resolution, the
     widescreen and frame-rate patches). What PS2Native carries out in PCSX2's
     place, the processors and the time they count, is shown but fixed.
  4. The recompile: a few minutes, every processor at work.
  5. The game's folder: games\<name>, which plays it.

A game's widescreen and frame-rate patches come from PCSX2's own archive of
them, patches.zip, which PS2Native does not ship: an installed PCSX2's is taken
by itself, and the game's own patch file is copied into its folder. Without it
the game plays as its disc has it, 4:3.

The game's folder plays it: start <name>.exe there (double-click it, or make a
shortcut to it). Beside it are its modules, a copy of the runtime, settings.txt
(what the runtime is started with: the disc image and the BIOS image, read
where they were when the folder was made, one option to a line), and the game's
own settings and patch file in data\PCSX2. In the folder too are its memory
cards and the console's settings (data), every session played, written down to
be run again with what it had to interpret (sessions), which the next time the
game is made is compiled too, and last-run.log, what the runtime said the last
time; when it could not play the game, <name>.exe says why. The folder can be
moved or copied whole; work\, the generated code, can be deleted. It holds code
translated from your game and your BIOS: it is yours alone, not to be given to
anyone.

In the window: the keyboard and the first controller play; F11 or Alt+Enter
covers the screen; closing the window ends the session.

From a command line, from a PowerShell window in this folder:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> -Title Killzone -Play

For a game made of several executables that it loads itself (Agent Under Fire),
name them, and give what earlier sessions had to interpret, if you have it:

    powershell -ExecutionPolicy Bypass -File .\make-game.ps1 -Image <disc image> -Bios <BIOS image> `
        -Title 'Agent Under Fire' -More MOVIE.ELF,ACTION.UK,DRIVING.ELF -Captures <folder> -Play

Licences: licences\ (PS2Native and PCSX2: GPL-3.0; LLVM: Apache-2.0 with LLVM
exceptions). The other libraries in runtime\ are PCSX2's dependencies and Qt,
under their own licences. Microsoft's C++ runtime libraries in runtime\ and
toolchain\bin come from Visual Studio's redistributable files.
