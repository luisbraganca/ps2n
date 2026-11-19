# ADR 0041: P4.7's run is the maintainer's, inside the Ubuntu of WSL 2 on the development computer

- Date: 2026-11-19
- Status: accepted (the maintainer's word, below). It changes the machine of
  P4.7's criterion ([ADR 0038](0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md));
  [ADR 0004](0004-proof-of-concept-environment.md)'s rule, that Linux is
  reported as working only after the maintainer's own run, stands, with the
  run's place said wherever Linux is.

## Context

P4.7's criterion (ADR 0038, the session's proposal, accepted) was a run on
the maintainer's Linux machine, a laptop with Linux Mint: a game made with
the wizard from its disc image plays from its folder at the console's
pace, and what a session meets for the first time is compiled for its next
start. The folder for it was made (`scripts/linux/kit.sh`), and inside the
Ubuntu of WSL 2 on the development computer the session had made a game
with it and played it from its folder.

On 2026-11-19 the maintainer asked: "Do you think you can do the tests on
the Ubuntu we just installed on this machine? My sister isn't very
interested in helping it seems......" (the laptop is not the maintainer's
own to use).

The session ran Silent Hill 3 from the Linux folder there on PCSX2's OpenGL
renderer, which inside WSL is Mesa's D3D12 driver on the computer's NVIDIA
RTX 4070 SUPER ("D3D12 (NVIDIA GeForce RTX 4070 SUPER)"): 1,799 frames at
49.67 a second, the console's 50, at three times the console's resolution,
0.00% of the EE's cycles interpreted. Vulkan inside WSL is only the CPU's
(llvmpipe). It set out what a run there shows and what it does not: not
another distribution than the one the folder is built on, nor a desktop of
Linux's own, NVIDIA's own Linux driver, Linux's own sound, or a gamepad
(USB devices do not reach WSL without `usbipd-win`); and recommended that a
run there count, Linux then described as tested under WSL until a Linux
machine of its own runs it.

The maintainer's answer: "Yes".

## Decision

1. P4.7's criterion is met by the maintainer's own run inside the Ubuntu
   22.04 of WSL 2 on the development computer: a game made with the Linux
   wizard (`PS2Native`, the folder `kit.sh` makes) from its disc image plays
   from its folder at the console's pace, and what a session meets for the
   first time is compiled for its next start (in its window, ADR 0040).
2. Until a Linux machine of its own runs PS2Native, Linux is described in
   the documents, the website and any release as tested under WSL 2
   (Ubuntu 22.04) on the development computer, never as working on Linux
   machines in general.

## How the run is made

- The folder is the one made from the committed tree (`build/PS2Native`
  inside the Ubuntu, `f30e7cc`), with nothing added by hand.
- A shortcut on the maintainer's desktop starts its `PS2Native` through
  WSLg (`wslg.exe`), without a console window; the game made gets one of its
  own once it is made.
- In the wizard's settings the renderer is OpenGL: Vulkan inside WSL is the
  CPU's. PCSX2's patches are fetched at the button (no PCSX2 is installed
  inside the Ubuntu).
- The keyboard plays (the first controller does not reach WSL).
- The disc image and the BIOS are read where they are, on the Windows
  drive (`/mnt/c/...`), as everywhere.

## The maintainer's run (2026-11-19)

The maintainer made Jak and Daxter (`SCES-50361`) with the Linux wizard
from its `.iso`, on OpenGL, and played five sessions from its folder, each
started by its own launcher. What the folder's files say:

- **The first session** (1,971 frames), its code not compiled yet, Jak's
  loaded from its data: 47.18 frames a second where the console shows
  59.94, 19.73% of the EE's cycles interpreted, the IOP's code and the
  vector units' all interpreted, the notice over the picture due
  throughout. Closed, its compile window compiled what it met, 128 files on
  20 processors, in 18 seconds.
- **The sessions after**, each on what the ones before met, compiled in the
  window when they ended: the third (1,133 frames) 55.39 frames a second,
  0.00% of the EE's cycles interpreted, 176 of the IOP's 7,929 blocks; the
  fifth, on PCSX2's software renderer (3,380 frames), 54.13 a second, none
  of the IOP's code interpreted and 0.00% of the EE's cycles. About 90% of
  the console's pace inside WSL, where Jak keeps it on Windows and Silent
  Hill 3 kept 99.3% inside WSL on OpenGL.
- **"Some characters have transparent parts but I guess it's just
  OpenGL"**: OpenGL inside WSL is Mesa's translation to Direct3D 12. The
  maintainer played it on PCSX2's software renderer, which draws on the
  processor: "Yep software render seems to fix it". The machine's work is
  the same on either renderer; what differs is WSL's OpenGL.

The maintainer's words: "I guess it's good", and then "Let's create
releases on Github". Read as P4.7 done, its pace inside WSL about 90% of
the console's for Jak and Daxter, which is what the release says of Linux.
