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
| `--no-window` | Do not open a window (builds with SDL2). |
| `--fpu-round MODE` | `zero` (default, like the EE) or `nearest`. |
| `--strict` | Stop at the first unimplemented path, with exit code 4. |
| `--trace-syscalls` | Log every syscall with its arguments. |
| `--trace-functions` | Log every function the dispatcher enters (not direct calls between recompiled functions). |
| `--log-level LEVEL` | `error`, `warn`, `info` (default), `debug`, `trace`. |
| `--pad-script FILE` | Controller input from a script instead of the keyboard and game controllers. See [Controllers](#controllers). |
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
disable), the runtime opens a window when the game first displays something.
Without SDL2, or with `--no-window`, use `--dump-frames`; most image viewers
open PPM files, and ImageMagick or Pillow convert them. The window runs on the
main thread and the game on a worker thread, as macOS requires.

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

The whole R5900 instruction set runs in recompiled and interpreted code:
MIPS III, the R5900 extensions, 128-bit MMI, COP0, the FPU and VU0 macro mode
(see the [README](../README.md#status) for details). Instruction semantics
live in `runtime/include/ps2/ops_*.h` and are shared by both modes.

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
| GS | privileged registers (`PMODE`, `SMODE2`, `DISPFB1/2`, `DISPLAY1/2`, `CSR` with `SIGNAL`/`FINISH`/`LABEL` events, `IMR`); a software rasterizer for points, lines, line strips, triangles, strips, fans and sprites with flat and Gouraud shading, textures in every pixel format including 4/8-bit CLUT and `TEXA`, the four texture functions, `REPEAT`/`CLAMP`/`REGION_CLAMP` wrapping, fog, alpha test with `AFAIL`, depth test (depth interpolated exactly, so layers drawn at one depth do not fight), alpha blending with `PABE` and `FBA`, scissoring, frame-buffer masks; host-to-local and local-to-local transfers; display read-out of both circuits at their `DISPLAY` positions, circuit 1 blended over circuit 2 or `BGCOLOR` (`SLBG`) by its pixels' alpha or `ALP` (`MMOD`), and interlaced FRAME mode (`FFMD`), where each field is shown at full height with every line doubled |
| IPU | the MPEG-2 decoder, every command: `BCLR`, `IDEC`, `BDEC`, `VDEC`, `FDEC`, `SETIQ`, `SETVQ`, `CSC`, `PACK`, `SETTH`. Decoding follows ISO/IEC 13818-2: the Annex B code tables, zigzag and alternate scan, inverse quantisation with MPEG-2 mismatch control or MPEG-1 rounding, an inverse DCT within IEEE 1180's error limits, frame and field DCT, `SCD`/`ECD`. CSC is ITU-R BT.601 with the alpha thresholds of `SETTH` and dithered RGB16. Input and output go through the 8-qword FIFOs at `0x10007010`/`0x10007000`, which DMA channels 4 and 3 fill and empty as the IPU uses them; a command that runs out of data waits, and `IPU_BP` (BP, IFC, FP), `IPU_TOP` and the channels' `MADR`/`QWC` show where it stopped. Each finished command raises the IPU interrupt. |

Not covered yet: SPU2 (sound is silent), GS local-to-host transfers,
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
| `libsd` (IOP library) | the SPU2 without sound: registers keep what is written, voice transfers move data to and from a 2 MB sound memory and raise their completion interrupt, looping block transfers (streamed PCM) raise one per half buffer at the 48 kHz playback rate, voices report that they have finished. Sound drivers run; nothing is heard. |
| MTAPMAN (`0x80000901`-`905`) | no multitap connected: ports 0-3 open, `GetConnection` reports none, thread priorities in the valid range (9-123) are accepted, and the version query answers the IRX version of the module the game loaded (3.01 when it is not a file), whose major SDK 2.x's libmtap checks |
| SDRDRV (`0x80000701`) | null service: it binds, answers every call with zeros, and reports it |

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

Port 1 has a DualShock 2. In SDL builds the keyboard drives it: arrows for
the d-pad, `W`/`D`/`S`/`A` for triangle/circle/cross/square, `Q`/`E` for
L1/R1, `1`/`3` for L2/R2, Enter for START and Backspace for SELECT. Game
controllers connected to the host drive ports 1 and 2, in the order they were
connected, with their sticks and analog triggers. Without SDL, port 1 has a
controller with nothing pressed.

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
| A stub | A `PS2_STUB(name, summary)` line in [stub_list.inc](../runtime/include/ps2/stub_list.inc) and `void ps2::stub::name_()` in [stubs/stubs.cpp](../runtime/src/stubs/stubs.cpp). The recompiler reads the same list, so configs can bind the new name once it is rebuilt. |
| An instruction fix | The `ops_*.h` headers in [runtime/include/ps2](../runtime/include/ps2). Recompiled code and the interpreter both pick it up. |
| Game-specific behaviour | Not in the runtime: use `[functions] override` and `[output] extra_sources` ([config.md](config.md#functions-skip-or-override)). |

Report what you do not implement with `PS2_UNIMPL(category, key, format, ...)`
([log.h](../runtime/src/log.h)): it logs the first few occurrences, counts
the rest under `key` for the exit summary, and stops a `--strict` run. Use
`PS2_FALLBACK` for paths that are handled correctly but worth knowing about.
