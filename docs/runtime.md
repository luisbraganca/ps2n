# Runtime reference

`ps2n_runtime` is what a recompiled game links against. It loads the game's
data from the original ELF, provides the CPU state and memory map the
generated code uses, and emulates enough of the rest of the console (EE
kernel, DMAC, GIF/GS, VIF, IPU, timers, SIF and IOP services) for the game to run.
This page lists what it covers, how to run and debug a game, and where to add
what is missing. [architecture.md](architecture.md) explains the execution
model.

## Running a recompiled game

```
<game> [options] [-- guest args...]
```

| Option | Meaning |
|---|---|
| `--elf PATH` | ELF to load code and data from. Default: the file the code was recompiled from, or a file of that name next to the executable. A file with a different CRC-32 is loaded with a warning. |
| `--host0 DIR` | Directory behind `host:` / `host0:` (default `.`). |
| `--cdrom PATH` | The disc: an ISO image (2048-byte sectors, or raw 2352-byte `.bin` sectors) or a directory with the extracted files. See [Discs](#discs). |
| `--memcards DIR` | Directory holding the memory cards (default `memcards`): `DIR/mc0` is the card in port 1, `DIR/mc1` the one in port 2. See [Memory cards](#memory-cards). |
| `--interpret` | Run everything through the interpreter, except functions the config replaced. See [Diagnosing a run](#diagnosing-a-run). |
| `--virtual-clock` | Time advances with execution instead of the host clock: runs are reproducible, which is what the tests use. |
| `--no-throttle` | Host clock, but skip idle time instead of sleeping. |
| `--max-vsyncs N` | Exit after N vertical blanks. |
| `--dump-frames DIR` | Write each displayed frame as `DIR/frame_NNNNNN.ppm`. |
| `--dump-every N` | With `--dump-frames`, only every Nth frame. |
| `--dump-audio FILE` | Write the sound to `FILE`, a 16-bit stereo 48 kHz WAV: what the SPU2 puts out, before the host's volume. With `--virtual-clock` it is the same on every run. |
| `--no-window` | Do not open a window (builds with SDL2). |
| `--deinterlace MODE` | How a game that shows interlaced fields becomes whole pictures: `adaptive` (default), `weave` or `bob`. See Video below. |
| `--gs-threads N` | Threads that draw: `0` (default) is half the host's threads, at most 8. In a real-time run one of them is the GS's own thread, which draws while the game runs on, up to two vsyncs behind it; the others are helpers that share a large primitive in bands of rows. With `--virtual-clock` or `--strict` the game's thread draws, with the helpers. `1` draws everything on the game's thread. The picture is the same either way. |
| `--fpu-round MODE` | `zero` (default, like the EE) or `nearest`. |
| `--strict` | Stop at the first unimplemented path, with exit code 4. |
| `--trace-syscalls` | Log every syscall with its arguments. |
| `--trace-functions` | Log every function the dispatcher enters (not direct calls between recompiled functions). |
| `--log-level LEVEL` | `error`, `warn`, `info` (default), `debug`, `trace`. |
| `--pad-script FILE` | Controller input from a script instead of the keyboard and game controllers. See [Controllers](#controllers). |
| `--settings FILE` | The settings file with the controller bindings and hotkeys. Default: `ps2n.ini` next to the executable when there is one, else the per-user one. Runs without a window ignore the default file and keep the default bindings. See [input.md](input.md). |
| `--region REGION` | The console's region as `ROMVER` reports it: `usa` (default), `europe`, `japan`, `asia`, `china`. Libraries such as libgraph pick PAL for `europe`. |
| `--missing-out FILE` | Where to write the addresses that had to be interpreted (default `ps2n_missing.toml`; an empty string disables it). The game's other executables get their own file next to it, named after the executable (`ps2n_missing.MAIN.ELF.toml`). |

Exit code: the value the game passed to `Exit()` (or returned from `main`),
1 if the ELF could not be loaded or every thread is blocked with nothing left
to wake them, 2 for a bad command line, 4 for a `--strict` stop, 130 for
Ctrl+C. Ctrl+C stops at the next safepoint, so the exit summary still prints.

**Files.** PS2 paths are mapped to host paths case-insensitively (disc file
names are upper case, host copies often are not). Backslashes work as
separators, and the `;1` version suffix of ISO 9660 names is dropped.
`rom0:ROMVER` reads the console's version string.

**Output.** The game's console output (`printf` stubs, the `_print` syscall,
the SIO debug port, FILEIO writes to `tty:`, and the DECI2 TTY and `kputs`
that Sony's libkernel prints through) goes to stdout. The runtime's own
messages go to stderr as `[level:category] message`.

**Video.** Built with SDL2 (found automatically; `-DPS2N_WITH_SDL2=OFF` to
disable), the runtime opens a window at start-up. The picture keeps the 4:3
of the television the game drew for, letterboxed in the window; F11 or
Alt+Enter switches to fullscreen, and closing the window ends the program.
Without SDL2, or with `--no-window`, use `--dump-frames`; most image viewers
open PPM files, and ImageMagick or Pillow convert them. The window runs on the
main thread and the game on a worker thread, as macOS requires. Play with the
real-time clock: `--virtual-clock` runs the game as fast as the host can.

Games that render each field of an interlaced picture (FIELD mode: half the
lines, redrawn every vsync a line lower on odd fields) are deinterlaced as
PCSX2 offers it. `adaptive`, the default, takes the lines a field lacks from
the previous field where the picture stood still since the last field of the
same parity (a still menu is the whole, steady picture) and interpolates
them where it moved. It also puts each field on the lines the game drew it
for. Such a game draws the odd field with a half-line Y offset, and when it
is late with a picture, the picture reaches the screen a vsync after the one
it was drawn for, where the FIELD bit would put it a line off. A picture
shown twice stays as it was. A game that draws every field alike gets the
FIELD bit. `weave` always takes the previous field's lines (steady; moving
edges comb), `bob` doubles each field's lines as the PS2 sends them (still
edges shake by a line). Both go by the FIELD bit. F5 (Cycle Deinterlace
Mode) switches while playing. It needs consecutive fields, so the window has
it and frames dumped every N vsyncs show each field doubled.

**Sound.** While the window is open, the sound plays through the host's
default output device (SDL2), not with `--no-window`; `--dump-audio` writes
it to a file either way. The settings file holds PCSX2's keys for it, in
`[SPU2/Output]`:

| Key | Default | Meaning |
|---|---|---|
| `StandardVolume` | 100 | Volume in percent, 0-200. |
| `FastForwardVolume` | 100 | The volume while turbo or the unlimited frame rate is on. |
| `OutputMuted` | false | No sound; the volume hotkeys cannot unmute it. |
| `Backend` | Cubeb | `Null` turns the sound off. Any other value plays through SDL: Cubeb is not built in. |
| `BufferMS` | 50 | Sound buffered before it plays, again after it ran dry. |
| `OutputLatencyMS` | 20 | The device's own buffer, rounded up to a power of two of frames. |
| `OutputLatencyMinimal` | false | The smallest device buffer (256 frames). |

The SPU2 runs in emulated time, which in real time keeps pace with the
device, so there is nothing to stretch: what fast forward makes too much is
dropped, and when the game stalls longer than the buffer (a frame slow to
draw) there is a gap. The Toggle Mute, Increase Volume and Decrease Volume
hotkeys (5% steps) are unbound by default, as in PCSX2. Not covered: PCSX2's
audio settings page, surround expansion (`ExpansionMode`) and time
stretching (`SyncMode`), which are ignored.

On Windows, SDL2's development package for Visual C++
(`SDL2-devel-<version>-VC.zip` from SDL's GitHub releases) works as it comes:
unpack it and pass `-DSDL2_DIR=<unpacked folder>/cmake` when configuring the
repository or a generated game project. The build copies `SDL2.dll` next to
every executable that links the runtime.

## Diagnosing a run

Every unimplemented path the game reaches is logged the first three times and
counted. When the program exits (including Ctrl+C), the counts are printed:

```
---- unimplemented / unexpected paths hit during this run ----
       120  RPC PADMAN (sid 0x80000100) fno 1
         2  syscall 109 (SetCPUTimer)
         1  skipped function MoviePlay
---------------------------------------------------------------
```

That list is the to-do list for a port. The tools for working through it:

- **Missing code.** Addresses the dispatcher had no recompiled code for are
  interpreted and written to `ps2n_missing.toml` at exit. Add the file to
  `[analysis] entries_files` and recompile, and the next run uses recompiled
  code there. Each of the game's other executables has its own file
  (`ps2n_missing.MAIN.ELF.toml`), for the `entries_files` of its config.
- **`--strict`** stops at the first unimplemented path instead of carrying on,
  so the log ends at the first point where emulation went wrong. Interpreted
  code and skipped functions are not unimplemented paths: they run correctly,
  and do not stop a strict run.
- **`--interpret`** runs the same build through the interpreter. If a problem
  happens in both modes it is a runtime gap; if only in recompiled mode, it is
  a recompiler bug. Functions the config replaced run in both modes.
- **`--trace-syscalls`**, **`--trace-functions`** and `--log-level debug` or
  `trace` (SIF, file and DMA activity, thread scheduling) show what the game
  is doing.
- **`report.txt`**, next to the generated code, lists unresolved indirect
  jumps and unknown instructions found at recompile time.

## What is covered

### CPU

The whole R5900 instruction set runs in recompiled and interpreted code: the
MIPS III base the EE implements, the R5900 extensions (three-operand
MULT/MADD, pipeline-1 HI1/LO1, the SA register), all 128-bit MMI
instructions, COP0, the single-precision FPU with the EE's non-IEEE behaviour
(see [architecture.md](architecture.md#floating-point)), and VU0 macro mode
with MAC, status and clip flags. Encodings were cross-checked against
rabbitizer's R5900 tables, and corner cases (division by zero, LWL/LWR
merging, MMI lane layouts, VU flag updates) against PCSX2's interpreter.
Instruction semantics live in `runtime/include/ps2/ops_*.h` and are shared by
both modes.

Guest exceptions are not delivered: integer overflow leaves the destination
unchanged, trap and `BREAK` instructions are reported and execution continues,
`ERET` returns to EPC, TLB instructions are ignored (the default TLB layout is
assumed).

### Memory map

| Address | |
|---|---|
| `0x00000000`-`0x01FFFFFF` | 32 MB RDRAM, also through kseg0/kseg1 and the uncached views at `0x20000000` and `0x30000000` |
| `0x70000000`-`0x70003FFF` | scratchpad |
| `0x10000000`-`0x1000FFFF` | EE hardware registers (below) |
| `0x11000000`-`0x1100FFFF` | VU0/VU1 micro and data memory |
| `0x12000000`-`0x12001FFF` | GS privileged registers |
| `0x1C000000`-`0x1C1FFFFF` | IOP RAM (2 MB) |
| `0x1FC00000`-`0x1FFFFFFF` | BIOS window: zeros except the `ROMVER` string some games read (region from `--region`) |

The first 512 KB of RDRAM hold the kernel on a real console; the runtime keeps
its own small structures there (the interrupt stack, kernel scratch data).
Accesses to unmapped addresses are reported.

### EE kernel

The kernel is implemented at the syscall level. Its objects (threads,
semaphores, alarms, handler lists) are host data structures, and all guest
threads run on one host thread.

| Area | Syscalls |
|---|---|
| Threads | `CreateThread`, `DeleteThread`, `StartThread`, `ExitThread`, `ExitDeleteThread`, `TerminateThread`, `ChangeThreadPriority`, `RotateThreadReadyQueue`, `ReleaseWaitThread`, `GetThreadId`, `ReferThreadStatus`, `SleepThread`, `WakeupThread`, `CancelWakeupThread`, `SuspendThread`, `ResumeThread`, `DisableDispatchThread`, `EnableDispatchThread`, and the `i*` interrupt-context variants |
| Semaphores | `CreateSema`, `DeleteSema`, `SignalSema`, `WaitSema`, `PollSema`, `ReferSemaStatus`, and `i*` variants |
| Interrupts | `AddIntcHandler`, `RemoveIntcHandler`, `AddDmacHandler`, `RemoveDmacHandler`, `EnableIntc`/`DisableIntc`, `EnableDmac`/`DisableDmac`, `EnableIntcHandler`/`DisableIntcHandler`, `EnableDmacHandler`/`DisableDmacHandler`, and `i*` variants |
| Alarms | `SetAlarm`, `ReleaseAlarm`, `iSetAlarm`, `iReleaseAlarm` (also at their alternate numbers) |
| Start-up and memory | `SetupThread`, `SetupHeap`, `EndOfHeap`, `GetMemorySize`, `FlushCache`, `GetCop0`, `ResetEE`, `Copy`, `GetEntryAddress`, `ExpandScratchPad`, cache and memory-mode calls |
| Video | `SetGsCrt` (NTSC/PAL timing, interlace), `GsGetIMR`, `GsPutIMR`, `SetVSyncFlag`, `Get/SetGsHParam`, `Get/SetGsVParam` |
| SIF | `SifSetDma`, `SifDmaStat`, `SifSetDChain`, `SifSetReg`, `SifGetReg`, `SifStopDma` |
| Configuration | `Get/SetOsdConfigParam` (English, 4:3), `Get/SetOsdConfigParam2`, `MachineType`, `PSMode` |
| Program control | `Exit`, `ExecOSD` (exit), `LoadExecPS2` (starts another executable, see [Several executables](#several-executables); `rom0:` paths such as `rom0:OSDSYS` exit), `_print` (debug output), `SetSyscall` (ignored), `Deci2Call` (see below) |

Not implemented (reported, return 0): `ExecPS2`, event flags
(`CreateEventFlag` and friends), the TLB entry calls, `SetCPUTimerHandler` and
`SetCPUTimer`, and any syscall number not listed above.

`Deci2Call` is the link to the debugger of development consoles. Sony's
libkernel sends `printf` output through it: `sceTtyWrite` opens a socket for
the TTY protocol, asks to send, and polls until the socket's handler has
handed the packet over and been told it went out. The runtime plays the
debugger's part when the game polls: it calls the handler with `DECI2_WRITE`
until the whole packet has arrived, prints its text, then calls it with
`DECI2_WRITEDONE`. `kputs` prints directly. Nothing ever arrives to be read,
and sockets for other protocols are refused (reported).

Interrupt handlers run nested on the interrupt stack, as on the real kernel,
at safepoints (loop back-edges and syscalls). Thread switches happen when a
thread blocks, when a handler wakes a higher-priority thread, and on
`RotateThreadReadyQueue`.

### Several executables

`LoadExecPS2(path, argc, argv)` replaces the running program with another
executable, as on the console:

- The file is read through the same devices as the game's own files: the disc
  (`cdrom0:`), `host:` or a memory card. It runs on the code recompiled from
  that very file, recognised by its CRC-32: the boot program's, or one of
  `[input] programs` ([config.md](config.md#games-with-several-executables)).
  Any other executable runs on the interpreter, with a warning.
- Its main thread gets the path as `argv[0]`, followed by the arguments.
- The EE kernel starts over: every thread, semaphore, alarm and interrupt or
  DMA handler of the old program is gone, interrupts are masked and cleared,
  and the CPU registers are reset. The IOP restarts with only its resident
  services, as the BIOS reboots it before loading the next executable, so the
  new program loads its modules again.
- RAM outside the new program's segments, the scratchpad, the GS and the VUs
  keep their contents.

Some loaders read the next executable themselves and jump into it, with no
`LoadExecPS2`. Recompiled code therefore only runs while RAM holds the
program it was made from: each function carries a checksum of its
instructions.

- When the game jumps to an address the running program has no code for,
  the runtime looks for another of the game's programs with code there whose
  functions are *all* in RAM, and runs that one from then on. Otherwise the
  code is interpreted.
- When the game invalidates the instruction cache (`FlushCache(2)`), as it
  must after loading code, each function is checked against RAM again before
  it next runs; one that changed runs on the interpreter.

Not covered: `ExecPS2`, which reports and returns; and changes to part of the
running program (an overlay loaded over its own code). Its functions call
each other directly, so such a change is only noticed where the dispatcher
enters the code (function pointers, a resumed thread).

### Hardware

| Unit | Covered |
|---|---|
| INTC | `I_STAT`, `I_MASK`; the sources raised so far are GS, VBLANK start/end, timers 0-3 and the IPU (DMA completion goes through the DMAC interrupt) |
| Timers | all four: count, mode, compare, hold; compare and overflow interrupts |
| Vsync | NTSC or PAL timing from `SetGsCrt`; VBLANK start/end interrupts, GS `CSR.VSINT` |
| SIO | the debug TX register (`0x1000F180`) prints to stdout |
| DMAC | channel and global registers, `D_STAT` interrupts, `CPCOND0`, the `D_ENABLEW` hold (a channel started while held runs on release); VIF0, VIF1, GIF and RAM-to-scratchpad in normal and source-chain mode (tags `refe`, `cnt`, `next`, `ref`, `refs`, `call`, `ret`, `end`), scratchpad to RAM in normal mode. The IPU channels (3 and 4; 4 also in source-chain mode) move data only as the IPU's FIFOs take or give it, so they stay in progress until the IPU has used their data. SIF transfers go through the SIF syscalls. Other channels complete immediately and are reported. |
| VIF | VIF0 and VIF1: `UNPACK` into VU memory (every format, write masks with `STROW`/`STCOL`, the `STMOD` offset and difference modes, skipping and filling writes, `TOPS`-relative addresses), `MPG` uploads, `MSCAL`/`MSCALF`/`MSCNT` (VIF1 flips the `TOP`/`TOPS` double buffer), VIF1 `DIRECT`/`DIRECTHL` to the GIF, and the register codes (`STCYCL`, `OFFSET`, `BASE`, `ITOP`, `STMOD`, `STMASK`, `STROW`, `STCOL`, `MSKPATH3`, `MARK`, `FLUSH*`) |
| VU | micro mode for VU0 (`VCALLMS`, `VCALLMSR`) and VU1 (`MSCAL`, `CMSAR1`): an interpreter that keeps what micro code can observe of the pipeline. MAC, status and clip flags arrive 4 cycles after the instruction that set them, Q (DIV/SQRT/RSQRT) and P (VU1 EFU) after their latencies (`WAITQ`/`WAITP` and register hazards stall), branches have a delay slot, the E bit ends a program after one more instruction, upper and lower instructions read their inputs before either writes, `XGKICK` sends GIF packets down PATH1, and VU0 reaches VU1's registers at `0x4000`. A program runs to completion when it is started. |
| GIF | `PACKED`, `REGLIST` and `IMAGE` modes from PATH2 (VIF1) and PATH3 (DMA channel 2 or the FIFO), PATH3 masking |
| GS | privileged registers (`PMODE`, `SMODE2`, `DISPFB1/2`, `DISPLAY1/2`, `CSR` with `SIGNAL`/`FINISH`/`LABEL` events, `IMR`); a software rasterizer for points, lines, line strips, triangles, strips, fans and sprites with flat and Gouraud shading, textures in every pixel format including 4/8-bit CLUT and `TEXA`, the four texture functions, `REPEAT`/`CLAMP`/`REGION_CLAMP` wrapping, fog, alpha test with `AFAIL`, depth test (depth interpolated exactly, so layers drawn at one depth do not fight), alpha blending with `PABE` and `FBA`, scissoring, frame-buffer masks; host-to-local and local-to-local transfers; display read-out of both circuits at their `DISPLAY` positions, circuit 1 blended over circuit 2 or `BGCOLOR` (`SLBG`) by its pixels' alpha or `ALP` (`MMOD`), and interlaced FIELD mode (`FFMD`), where each field's lines are deinterlaced (see Video). In real-time runs the GS draws on its own thread, up to two vsyncs behind the game, as PCSX2's MTGS does ([architecture.md](architecture.md#the-gs-thread)); `FINISH` and `SIGNAL` reach the EE at its next safepoint, and `GIF_STAT` shows the GS idle as soon as data is queued, not when it is drawn. Large primitives are drawn by several threads (`--gs-threads`), each taking bands of four rows; one that reads memory it draws into (texture or CLUT), or whose frame and depth buffers overlap, is drawn by one thread, so the picture is always the one a single thread draws. The drawing state is decoded once per primitive, and the swizzles are tables |
| IPU | the MPEG-2 decoder, every command: `BCLR`, `IDEC`, `BDEC`, `VDEC`, `FDEC`, `SETIQ`, `SETVQ`, `CSC`, `PACK`, `SETTH`. Decoding follows ISO/IEC 13818-2: the Annex B code tables, zigzag and alternate scan, inverse quantisation with MPEG-2 mismatch control or MPEG-1 rounding, an inverse DCT within IEEE 1180's error limits, frame and field DCT, `SCD`/`ECD`. CSC is ITU-R BT.601 with the alpha thresholds of `SETTH` and dithered RGB16. Input and output go through the 8-qword FIFOs at `0x10007010`/`0x10007000`, which DMA channels 4 and 3 fill and empty as the IPU uses them; a command that runs out of data waits, and `IPU_BP` (BP, IFC, FP), `IPU_TOP` and the channels' `MADR`/`QWC` show where it stopped. Each finished command raises the IPU interrupt. |

Not covered yet: GS local-to-host transfers,
bilinear texture filtering and mipmaps, dithering and anti-aliasing,
scratchpad-to-RAM DMA in chain mode, VIF1 GS-to-memory transfers, the IPU's
speed (a command takes no emulated time), and VU programs running alongside
the EE (they finish the moment they start).

### IOP

IRX modules run on an R3000A interpreter. The IOP kernel and Sony's system
modules are host code:

- **Kernel**: threads with priorities, semaphores, event flags, message boxes,
  fixed and variable memory pools, alarms, `DelayThread`, interrupt handlers,
  vblank waits and the system memory allocator; the libraries `sysclib`,
  `stdio`, `ioman`, `iomanX`, `heaplib`, `timrman`, `loadcore`, `modload`,
  `sifcmd` and `sifman`, and stubs for `dmacman`, `ssbusc`, `excepman` and
  `secrman`. A module that imports something the host does not provide links
  to a trap that reports the call.
- **Modules**: loading an IRX (through LOADFILE from the EE, or `modload` on
  the IOP) relocates it into IOP memory, links its imports to the host
  libraries or to libraries other modules registered, and runs its start
  function on a thread. Resident modules keep running: their threads, RPC
  servers and interrupt handlers. Modules for devices the runtime emulates at
  a higher level are replaced by those services when they load (`sio2man`,
  `padman`, `mtapman`, `mcman`, `mcserv`, `cdvdman`, `cdvdfsv`, `cdvdstm`,
  `libsd` and their `x`/`free` variants); modules for hardware that is not
  emulated (network, hard disk, USB, i.LINK, `dev9`) load and do nothing,
  which is reported.
- **SIF**: the wire protocol (SIF0/SIF1 transfers, command packets, RPC bind
  and call, IOP reset), so the game's EE libraries talk to the IOP unmodified,
  whether the server is host code or an IRX module.
- **Module releases**: games built with SDK 2.x reboot the IOP with a
  replacement image (`rom0:UDNL cdrom0:\MODULES\IOPRP214.IMG;1`), and their EE
  libraries refuse to use FILEIO and LOADFILE unless these report the image's
  release. The runtime does not run the image's modules, but on a reset it
  reads the release of each from its id in the image (`PsIIfileio  2140`), or
  from the image's name when it has none (`IOPRP214` is 2140), and the
  services report and speak that release. Without an image they are the BIOS's,
  release 1000.

| Service | Status |
|---|---|
| FILEIO (`0x80000001`) | `open`, `close`, `read`, `write`, `lseek`, `remove`, `mkdir`, `rmdir`, `dopen`, `dclose`, `dread`, `getstat`, and `chstat` (accepted, no effect), over the host directories and the disc; `tty:` writes go to stdout. Others (`ioctl`, `format`, driver management) return -1 and are reported. The version query (function 255) answers the module release. Release 2100 (SDK 2.1) has its own `open` layout, a mode and a name of up to 1024 bytes; its `close`, `read`, `write` and `lseek` are the BIOS's, and its other calls are reported. Releases from 2200 on, whose replies are asynchronous, are not implemented: every call is reported. |
| IOP heap (`0x80000003`) | `SifAllocIopHeap`, `SifFreeIopHeap`, `SifLoadIopHeap` |
| LOADFILE (`0x80000006`) | module loading from a path or an IOP memory buffer, IOP memory peek/poke, search by name, the version query (the module release); stop and unload are refused (not removable). Loading an EE ELF through the IOP is reported. |
| CDVDFSV (`0x80000592`, `0x80000593`, `0x80000595`, `0x80000596`, `0x80000597`, `0x8000059A`, `0x8000059C`) and the `cdvdman` library | resident from start-up, like on the console. See [Discs](#discs). |
| PADMAN (`0x80000100`/`0x80000101`, and the original PADMAN's `0x8000010F`/`0x8000011F`) | a DualShock 2 in port 1: digital, analog and pressure-sensitive modes, actuator set-up, the per-frame data area libpad reads. The version query answers the IRX version of the module the game loaded (3.06 when it is not a file), whose major libpad checks. See [Controllers](#controllers). |
| XMCSERV / MCSERV (`0x80000400`) | both protocols. See [Memory cards](#memory-cards). |
| `libsd` (IOP library) | Sony's library on the SPU2 (see the IOP's SPU2 below): each call reads and writes the registers the library does, after ps2sdk's open libsd. `sceSdInit` leaves every voice keyed off on a silent loop at `0x5000`, as libsd does. Voice transfers move their data at once and finish a little later with the core's DMA interrupt (`0x24`, `0x28`). Block transfers stream PCM through AutoDMA; the interrupt handler arms the next half of a looping buffer, as the library does, and calls the driver's handler. Line 9 means a core's interrupt address was touched: the library turns that core's interrupt off and calls the driver. The reverb presets are libsd's. Not covered: reading the output back (a block transfer from the SPU2 gives silence, reported) and the S/PDIF modes (kept, no effect). |
| MTAPMAN (`0x80000901`-`905`) | no multitap connected: ports 0-3 open, `GetConnection` reports none, thread priorities in the valid range (9-123) are accepted, and the version query answers the IRX version of the module the game loaded (3.01 when it is not a file), whose major SDK 2.x's libmtap checks |
| SDRDRV (`0x80000701`) | null service: it binds, answers every call with zeros, and reports it |

#### Sound

The SPU2 is emulated at its registers (`0x1F900000`), which libsd's host
code and IOP code both reach ([iop/spu2.cpp](../runtime/src/iop/spu2.cpp)).
Its voices follow the PS1 SPU's, as psx-spx documents them:

- **Voices**: two cores of 24 voices playing ADPCM from 2 MB of sound memory.
  The loop flags set `ENDX` and `LSAX` (a written `LSAX` stays until key on)
  and move `NAX`. A voice keeps reading memory after its envelope ends.
- **Sound shaping**: pitch with pitch modulation, the 4-point interpolation
  table, ADSR envelopes, volume sweeps and the noise generator.
- **Mixing**: each core's dry and reverb mixes (`VMIX*`, `MMIX`), its PCM
  input (`BVOL`) and the reverb (`EVOL`). Core 0's output reaches core 1
  (`AVOL`), whose output, after the master volumes, is what is heard.
- **Reverb**: it runs in its work area (`ESA` to `EEA`) at half rate, left and
  right in turn, through the 39-tap resampling filter.
- **PCM input**: AutoDMA fills it a 1 KB block at a time as each half of the
  input area plays out.
- **Output areas**: voices 1 and 3, the cores' mixes and core 0's output are
  written at `0x400`-`0x1FFF` every sample, as drivers that read them back
  expect.
- **Interrupt address**: each core's (`IRQA`) is watched on voice reads,
  transfers, the input, those writes and the reverb. Either core's raises IOP
  interrupt 9 once until it is acknowledged, by turning it off in `ATTR`.

It mixes in emulated time: when a register is touched, and every 64 samples
(1.3 ms). Having fallen behind, it stops after each sample that raises an
interrupt, so the IOP answers before it mixes on. Not covered: the IOP's DMA
channels 4 and 7 themselves (libsd's transfers are host code, so a driver
that programs them gets nothing), reading the output back by DMA, S/PDIF
bypass and compressed output, and `ATTR`'s mute bit (14), which libsd sets
at start-up and the SPU2 here ignores.

#### Discs

`--cdrom` takes an ISO image or a directory. A directory is presented as an
ISO 9660 volume built when the runtime starts (descriptors, path tables,
directory records; file sectors come from the host files), so code that reads
sectors and parses the file system itself sees a consistent disc. Images with
2048-byte sectors and directories are DVDs; raw 2352-byte images are CDs.

The EE's libcdvd works unchanged: `sceCdInit`, `sceCdDiskReady`,
`sceCdGetDiskType`, `sceCdStatus`, `sceCdTrayReq` (the first check reports a
new disc), `sceCdSearchFile`, `sceCdRead` into aligned or unaligned buffers
(2048-byte sectors, and 2328/2340-byte CD sectors rebuilt around the data),
`sceCdReadIOPMem`, `sceCdReadChain`, the streaming calls (`sceCdSt*`, with
the ring buffer always reported full), `sceCdReadClock` (Japan time, like the
console's clock; `--virtual-clock` starts it on 2001-07-04), and the S-command
queries. EE reads finish before their RPC completes. SDK 2.x's `sceCdInit`
also asks to be told when the power button is pressed (`0x80000596`); that
call stays pending, as on a console nobody switches off. IOP modules get the
same from the `cdvdman` library; their reads finish a little after the call
and raise the `sceCdCallback` callback, and `sceCdSync` waits for them.

Not covered: dual-layer DVDs (layer 1 is not searched), CD audio (reads
return silence), raw-sector reads (`sceCdRead0`, `sceCdRV`), and the
decryption commands of copy-protected discs.

#### Memory cards

Each card is a directory: `--memcards DIR` holds `mc0` (port 1) and `mc1`
(port 2). Port 1 always has a card, created on the first write; port 2 has
one when its directory exists. Cards are formatted 8 MB PS2 cards, and free
space is counted in 1 KB clusters the way the real file system would use them.

The service answers the old MCSERV protocol and XMCSERV's (libmc picks
XMCSERV): `GetInfo` (the first call after start-up reports a newly inserted
card, as games expect), `Open` (create, truncate, `mkdir`), `Close`, `Read`,
`Write`, `Seek`, `Flush`, `ChDir`, `GetDir` (wildcards, `.` and `..` outside
the root), `SetFileInfo` (renaming; times and attributes are accepted but not
kept), `Delete` and `GetEntSpace`, with the card's error codes and at most
three open files. `Format` and `Unformat` are accepted and leave the directory
alone. The `mc0:` file device reaches the same directories.

#### Controllers

Each port has a DualShock 2 (port 2 none, until the settings say so), driven
by host inputs bound to it the way PCSX2 binds them: the keyboard and SDL
game controllers, edited in the window's Controller Settings (Escape) and
kept in a settings file in PCSX2's format. [input.md](input.md) describes
it all: the defaults, the settings UI, the file and the hotkeys. The Analog
button switches between digital and analog mode unless the game locked the
mode, digital sources give full pressure (less with Apply Pressure held),
and the game's vibration drives the motors bound to the port. Without SDL,
or without a window, port 1 has a controller with nothing pressed.

`--pad-script FILE` replaces all of that with scripted input for port 1, for
reproducible runs. Each line gives the vblank from which a state holds, the
buttons (joined with `+`, or `-` for none) and stick positions (0-255, 128 is
centred); `#` starts a comment:

```
# vblank  buttons        sticks
40        cross
44        -
50        up+square      lx=255
56        -              lx=0 ly=255
```

Button names: `select`, `start`, `up`, `down`, `left`, `right`, `triangle`,
`circle`, `cross`, `square`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`.

### Stubs

`ps2n stubs` lists the host implementations a config can bind
guest functions to; [config.md](config.md#stubs-replace-functions-with-runtime-implementations)
describes them.

## Extending the runtime

The runtime is meant to be grown game by game. Rebuild after changing it; the
generated code does not need to be regenerated.

| To add | Where |
|---|---|
| A syscall | `dispatchSyscall()` and `syscallName()` in [kernel/kernel.cpp](../runtime/src/kernel/kernel.cpp). Arguments are `arg(0)`..., results go through `ret()`. Interrupt-context variants use negative numbers. |
| A hardware register | `readEe32()`/`writeEe32()` in [hw/hw.cpp](../runtime/src/hw/hw.cpp), or the DMAC, VIF, GS and IPU files next to it. |
| An IOP service | Subclass `iop::RpcServer` ([iop/iop.h](../runtime/src/iop/iop.h)): `call(fno, data, size, recvSize, reply)` receives the send buffer and sends the reply bytes through `reply`, now or later. Register it with `registerServer(sid, ...)`: from `enableModuleServices()` in [iop/services.cpp](../runtime/src/iop/services.cpp) so it appears when the game loads the module, or from `registerBuiltinServers()` in [iop/iop.cpp](../runtime/src/iop/iop.cpp) for services resident in the BIOS. |
| An IOP library function | `defineLibrary(name, version, {{index, "name", fn}, ...})` ([iop/hle.h](../runtime/src/iop/hle.h)): `fn` reads its arguments with `arg(i)` and returns with `ret()`; `kernel::blockCurrent()` and `wakeThread()` let it wait, `kernel::callHandler()` calls back into module code, and `scheduleEvent()` ([clock.h](../runtime/src/clock.h)) runs host code later in emulated time. The kernel libraries are in [iop/kernel.cpp](../runtime/src/iop/kernel.cpp) and [iop/libs.cpp](../runtime/src/iop/libs.cpp). |
| An SPU2 register | `readCore()`/`writeCore()` in [iop/spu2.cpp](../runtime/src/iop/spu2.cpp). libsd's calls ([iop/sound.cpp](../runtime/src/iop/sound.cpp)) are register reads and writes through `spu2::read16()`/`write16()`. |
| A stub | A `PS2_STUB(name, summary)` line in [stub_list.inc](../runtime/include/ps2/stub_list.inc) and `void ps2::stub::name_()` in [stubs/stubs.cpp](../runtime/src/stubs/stubs.cpp). The recompiler reads the same list, so configs can bind the new name once it is rebuilt. |
| An instruction fix | The `ops_*.h` headers in [runtime/include/ps2](../runtime/include/ps2). Recompiled code and the interpreter both pick it up. |
| Game-specific behaviour | Not in the runtime: use `[functions] override` and `[output] extra_sources` ([config.md](config.md#functions-skip-or-override)). |

Report what you do not implement with `PS2_UNIMPL(category, key, format, ...)`
([log.h](../runtime/src/log.h)): it logs the first few occurrences, counts
the rest under `key` for the exit summary, and stops a `--strict` run. Use
`PS2_FALLBACK` for paths that are handled correctly but worth knowing about.
