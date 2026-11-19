PS2Native: a PS2 game compiled and played on this computer
==========================================================

This folder needs nothing installed: no compiler, no emulator. It holds
PS2Native, the application a game is made with; the runtime that plays a game
(runtime); and the tools that compile one (toolchain: LLVM's clang and ld.lld,
PS2Native's recompiler and headers). You bring the game's disc image and a BIOS
image of your own console; both are read where they are.

What the computer needs: a 64-bit Linux with a desktop on X11 or Wayland and
glibc 2.35 or later (Linux Mint 21 or later, Ubuntu 22.04 or later, and the
like; ldd --version says which glibc); a processor with SSE4.1 (Intel from 2008,
AMD from 2011); a graphics driver with Vulkan (NVIDIA's own driver, or Mesa's
for AMD and Intel; vulkaninfo --summary says), or else choose another renderer
in the game's settings; about 1.5 GB of free disk for each game; and, while it
compiles, up to about 0.6 GB of memory for each processor.

Unpack the folder where you want it, keeping who may run its programs (tar
-xf PS2Native-linux.tar.xz does; a copy through a USB stick formatted for
Windows does not).

Make a game: start PS2Native (double-click it, or ./PS2Native in a terminal in
this folder), and follow its five steps.

  1. The game: the disc image of your own disc, in any format PCSX2 plays
     (.iso, .chd, .cso or .zso, .bin, .img or .mdf, .gz; for a .bin, its .cue
     sheet does too). PS2Native recognizes it with PCSX2's database, and finds
     the executables on it; each is compiled, but those you leave out (a
     game's other languages): one left out runs interpreted, slower, until the
     game has compiled what a session met of it.
  2. The BIOS: an image dumped from your own console.
  3. The settings: PCSX2's own settings for the game, each at PCSX2's default
     but for PS2Native's (Vulkan, three times the console's resolution, a 16:9
     picture, the widescreen and frame-rate patches). What PS2Native carries out in PCSX2's
     place, the processors and the time they count, is shown but fixed.
  4. The recompile: a few minutes, every processor at work.
  5. The game's folder: games/<name>, which plays it.

A game's widescreen and frame-rate patches come from PCSX2's own archive of
them, patches.zip, which PS2Native does not ship: a PCSX2 installed from your
distribution's packages or from Flathub has one, taken by itself. Otherwise the
settings step offers to fetch it, at your word, from PCSX2's patches project on
GitHub (the file PCSX2 itself comes with), or to take a patches.zip you have; it
is kept in this folder (patches/) for the games made after. The game's own patch
file is copied into its folder. Without it the game plays as its disc has it, 4:3.

The game's folder plays it: start <name> there (double-click it). Beside it are
its modules, a copy of the runtime, a copy of the toolchain (toolchain, about
400 MB, which the game compiles new code with), settings.txt (what the runtime
is started with: the disc image and the BIOS image, read where they were when
the folder was made, one option to a line), make.txt (how its modules are
made), and the game's own settings and patch file in data/PCSX2 (what PS2Native
fixes, set there by hand, makes the game refuse to start and say which it is).
In the folder too are its memory cards and the console's settings (data), every
session played, written down to be run again with what it had to interpret
(sessions), and last-run.log, what the runtime said the last time; when it
could not play the game, <name> says why. The folder can be moved or copied
whole; work/, the generated code, can be deleted (the next compile then
compiles everything again). It holds code translated from your game and your
BIOS: it is yours alone, not to be given to anyone.

A game's first session can be slower than the console wherever it meets code
no compile has seen: the vector units' and the I/O processor's code, and the
code a game loads from its own data rather than from an executable on the disc
(Gran Turismo 3 does). While the game runs such code, a notice in the top left
corner of its picture says so for a few seconds. The session writes that code
down, and when you close the game's window the game compiles it itself, with
the toolchain in its folder, in a small window that says how far it has got: a
few minutes, every processor at work. From its next start the game has it.
"Later", or closing that window, stops the compile, what it compiled kept; the
game never starts with anything left to compile, so its next start compiles
the rest first, in the same window, and the game starts when that is done
(Later there leaves it unstarted). A session that met nothing new compiles
nothing and shows nothing. Making the game again with PS2Native from the same
disc image compiles it too, and keeps the folder's settings, its memory cards
and its sessions.

In the window: the keyboard and the first controller play; F11 or Alt+Enter
covers the screen; closing the window ends the session.

Licences: licences/ has the licence of everything in this folder, and
licences/README.txt says which is which and where the source of each is
(PS2Native and PCSX2: GPL-3.0; Qt: LGPL-3.0; LLVM: Apache-2.0 with LLVM
exceptions; the other libraries, PCSX2's dependencies and those of Ubuntu's
the folder carries, under their own).
