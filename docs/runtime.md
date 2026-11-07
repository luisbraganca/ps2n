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
| `--no-throttle` | Host clock, but skip idle time instead of sleeping, and let the EE's work run ahead of the clock (it is held to it otherwise: see [architecture.md](architecture.md#safepoints-interrupts-timers-and-preemption)). |
| `--max-vsyncs N` | Exit after N vertical blanks. |
| `--dump-frames DIR` | Write each displayed frame as `DIR/frame_NNNNNN.ppm`. |
| `--dump-every N` | With `--dump-frames`, only every Nth frame. |
| `--dump-audio FILE` | Write the sound to `FILE`, a 16-bit stereo 48 kHz WAV: what the SPU2 puts out, before the host's volume. With `--virtual-clock` it is the same on every run. |
| `--no-window` | Do not open a window (builds with SDL2). |
| `--deinterlace MODE` | How a game that shows interlaced fields becomes whole pictures: `adaptive` (default), `weave` or `bob`. See Video below. |
| `--renderer MODE` | Who draws, for this run (not saved): `auto` (default: Vulkan where the graphics card can, the software renderer otherwise), `software` or `vulkan`. See [Rendering](#rendering). |
| `--upscale N` | With Vulkan, draw at N times the console's resolution, 1 to 8, for this run (not saved). |
| `--round-sprite MODE` | `off` (default) or `full`: keep textured sprites' edges inside their source rectangles above 1x. See the `round_sprite` setting below. |
| `--gpu-debug` | Check every Vulkan call with the validation layer, which comes with the Vulkan SDK; what it reports goes to the log as `[warn:gpu] validation: ...`. |
| `--gs-threads N` | Threads that draw with the software renderer: `0` (default) is half the host's threads, at most 8. They share every primitive, each drawing its own bands of four rows. In a real-time run the GS also has a thread of its own, which decodes what the game sends while the game runs on, up to two vsyncs behind it; with `--virtual-clock` or `--strict` the game's thread decodes it. `1` draws everything on the game's thread. The picture is the same either way. With Vulkan the card draws, and the GS's own thread records its work in real-time runs, whatever this says. |
| `--no-vu1-thread` | Run VU1's micro programs on the game's thread. A real-time run otherwise gives VU1 a thread of its own beside the GS's, as PCSX2's MTVU does ([architecture.md](architecture.md#the-vu1-thread)); with `--virtual-clock`, `--strict`, or `--gs-threads 1` and the software renderer, VU1 runs on the game's thread anyway. What the game sees and draws is the same either way. |
| `--fpu-round MODE` | `zero` (default, like the EE) or `nearest`. |
| `--strict` | Stop at the first unimplemented path, with exit code 4. |
| `--trace-syscalls` | Log every syscall with its arguments. |
| `--trace-functions` | Log every function the dispatcher enters (not direct calls between recompiled functions). |
| `--log-level LEVEL` | `error`, `warn`, `info` (default), `debug`, `trace`. |
| `--pad-script FILE` | Controller input from a script instead of the keyboard and game controllers. See [Controllers](#controllers). |
| `--settings FILE` | The settings file with the controller bindings and hotkeys, and where a tester's log goes ([Axiom](#a-testers-log-in-axiom)). Default: `ps2n.ini` next to the executable when there is one, else the per-user one. Runs without a window ignore the default file, keep the default bindings and send nothing. See [input.md](input.md). |
| `--region REGION` | The console's region as `ROMVER` reports it: `usa` (default), `europe`, `japan`, `asia`, `china`. Libraries such as libgraph pick PAL for `europe`. |
| `--missing-out FILE` | Where to write the addresses that had to be interpreted (default `ps2n_missing.toml`; an empty string disables it). The game's other executables get their own file next to it, named after the executable (`ps2n_missing.MAIN.ELF.toml`), and the VU micro code the interpreter ran goes to `ps2n_missing.vu.toml` beside it. |

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
messages go to stderr as `[level:category] message`. The first one names the
game, the recompiler and the runtime's commit, as `git describe --always
--dirty` gave it when the runtime was built.

**Video.** Built with SDL2 (found automatically; `-DPS2N_WITH_SDL2=OFF` to
disable), the runtime opens a window at start-up. The picture keeps the
shape of the television the game drew for, centred in the window with black
bars where the shapes differ; F11 or Alt+Enter switches to fullscreen, and
closing the window ends the program.
Without SDL2, or with `--no-window`, use `--dump-frames`; most image viewers
open PPM files, and ImageMagick or Pillow convert them. The window runs on the
main thread and the game on a worker thread, as macOS requires. Play with the
real-time clock: `--virtual-clock` runs the game as fast as the host can.

The renderer, its resolution and the picture's shape come from PCSX2's
settings, kept in the settings file's `[EmuCore/GS]` and set in the pause
menu's Graphics Settings:

| Key | Default | Meaning |
|---|---|---|
| `Renderer` | `-1` | Who draws, by PCSX2's numbers: `-1` Automatic (Vulkan where the graphics card can, the software renderer otherwise), `13` Software, `14` Vulkan (the software renderer still draws where Vulkan cannot, with a warning). Another value counts as `-1`, with a warning. A change in the settings page takes effect when the game starts again. See [Rendering](#rendering). |
| `upscale_multiplier` | `1` | With Vulkan, the internal resolution: `N` draws at N times the console's resolution, 1 to 8 ("1x Native (PS2)" to "8x Native (~2880p/5K)" in the settings page). PCSX2 writes a number such as `2.000000`: it is rounded to the nearest whole number and kept within 1 to 8, with a warning where it changes. A change in the settings page takes effect at the next vsync. |
| `round_sprite` | `0` | `0` Off, `2` Full. Above 1x, a textured sprite with UV coordinates or one Q at both ends has the far end of its sampled rectangle pulled one texel towards its start on each axis, when the source span does not exceed the screen span by more than two texels. This can remove seams from transparent padding, but changes legitimate edge filtering too. It does not change 1x or primitive coverage. `1` (Half) is not implemented: it warns and becomes 0. |
| `linear_present_mode` | `1` | How the picture is scaled to the window: `0` Off (nearest), `1` Bilinear (Smooth), `2` Bilinear (Sharp): each picture pixel stays flat over most of the window pixels it covers and blends only at its edges. |
| `IntegerScaling` | `false` | The picture's height a whole multiple of its own, with black bars around it, so every picture pixel covers as many window pixels as the next. |
| `AspectRatio` | `Auto 4:3/3:2` | `Auto 4:3/3:2`: 4:3, or what the game's patches ask for (their `gsaspectratio`, applied when the game was recompiled: see [config.md](config.md#pnach-pcsx2s-patch-files)). `4:3`, `16:9`, `10:7`: that shape. `Stretch`: the window's. |
| `FMVAspectRatioSwitch` | `Off` | The shape while a film plays: `Off` keeps `AspectRatio`'s; `Auto 4:3/3:2`, `4:3`, `16:9` and `10:7` as above, the patches having their say under Auto here too, as in PCSX2. A film plays from the seventh time the IPU decodes a bit stream (`VDEC`) until 60,000,000 EE cycles (a fifth of a second) pass without one, as PCSX2 tells them. |

A game patched for 16:9 draws a wider view into as many pixels as before,
which the window stretches to 16:9: the picture is wider, not sharper, and
drawing it costs the same. Patches that write to memory the game fills at
run time (Code: Veronica X's) are written by the runtime: once when it loads
the boot executable, or at the start of every vblank while that executable
runs, before the game hears of the vblank. Cycle Aspect Ratio (F6) goes
through `AspectRatio`'s values for the run without saving them. The window
opens in the shape the settings give. Not covered: the 3:2 of Auto for a progressive (480p)
picture, which the runtime does not show (Auto is 4:3), and PCSX2's other
display settings (crop, vertical stretch, the picture's alignment and
offsets, its other renderers and their accuracy settings).

Games that render each field of an interlaced picture (FIELD mode: half the
lines, redrawn every vsync a line lower on odd fields) are deinterlaced as
PCSX2 offers it. `adaptive`, the default, takes the lines a field lacks from
the previous field where the picture stands still (a still menu is the whole,
steady picture) and interpolates them where it moves, as PCSX2's adaptive
mode does. A line moves where a colour channel changed by more than 16: in
the field's lines above or below it since the last field of the same parity,
or in the line itself since the field before that. After a smaller change the
line stays if it did not change itself, or if it fits between the lines
above and below. One that stands out from both is the comb of something
moving slowly, and is interpolated; so is a thin, translucent line over a
moving background. It also puts each field on the lines the game drew it
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

### Rendering

Two renderers draw what the game sends the GS. The Vulkan renderer draws on
the graphics card, at 1 to 8 times the console's resolution
(`upscale_multiplier`). The software renderer draws on the CPU, at the
console's resolution, on `--gs-threads` threads. Automatic (`Renderer = -1`,
the default) takes Vulkan where the card can. A line near the start of the
log says which draws, and why:

```
[info:gpu] rendering with Vulkan on NVIDIA GeForce RTX 4070 SUPER (NVIDIA 621.01, Vulkan 1.4), at 2x the console's resolution
[info:gpu] rendering with the software renderer, as the settings ask (Renderer = 13)
[warn:gpu] rendering with the software renderer: no Vulkan loader (vulkan-1.dll) on this computer: the graphics driver installs it
```

Vulkan needs nothing installed beyond the graphics driver: the runtime loads
the driver's Vulkan library when it starts, and its shaders come compiled
with it. The card needs Vulkan 1.3 and `VK_EXT_fragment_shader_interlock`,
which lets each pixel's depth test, blending and writes run in the order the
primitives come; current NVIDIA and Intel drivers and Mesa's RADV have it,
and `vulkaninfo` says whether an AMD card's Windows driver does. The warning
names what a card lacks.

At the console's resolution the Vulkan renderer draws the software
renderer's pixels exactly: each pixel is worked out in one fragment shader,
with the GS's integer arithmetic and the software renderer's float steps
rounded the way the EE rounds them (toward zero). The tests compare the two,
and so did runs of Agent Under Fire and Code: Veronica X (every frame
compared, bit for bit; see [games.md](games.md)). At N times the
resolution, pixel (N x, N y) of what the card drew is what the console draws
at (x, y), and the pixels between come from the same arithmetic at the
positions between: edges are finer. A texture the card drew is read at its
own resolution, which is where the console's pixels can differ too, by
design. What the EE sends (textures, films, CLUTs) stays at the console's
resolution.

The card keeps the pages of GS memory it drew in, at its resolution, and
resolves them to its 1x mirror when something reads them in another layout.
A host-to-local transfer over such pages merges into the mirror without a
read back; the CPU's copies stay stale until the EE or a test reads them. A
transfer that writes a page whole, in a format that writes every bit of it
(CT32, CT16, CT16S, the Z formats but Z24, T8, T4), instead makes the CPU's
page current as the transfer writes it. Wrapping rectangles keep the CPU's
path. Each actual read back waits for the card to finish, so the exit summary
counts them, by why
(`pages read back: written in another layout`, `... a local copy reads them
in another layout`, `... the CPU reads GS memory`, `... the software
rasterizer draws a primitive`). Other entries say where
the card's result is not the console's: `primitive reads what it writes` (a
primitive that samples the memory it draws into reads it as it was before the
primitive, where the console reads its own new pixels), `transfer reads what
it writes` (the same for a local copy down or right over itself), `frame and
depth overlap` (a depth buffer in the frame's memory: the card draws those
primitives without their depth test), `picture shown at the console's
resolution` (the display has to read pages from the 1x mirror because the
card's target has a layout it cannot translate), `drawn by the software rasterizer` (a primitive whose frame or depth buffer
goes round the end of GS memory, or past what the card's images hold at its
scale: the software rasterizer draws it in GS memory, which takes the pages
it touches back from the card; games park the scissor past the end of memory
to draw nothing, which is not reported), `transfer ended early over drawn
pages` (a transfer that was to write pages whole stopped before its end: the
rest of those pages holds what GS memory held before the card drew there).

With a window, the picture reaches it through Vulkan as well when the card
can show pictures, filtered and scaled as `linear_present_mode` and
`IntegerScaling` ask; otherwise SDL's renderer shows it, as with the software
renderer. `--dump-frames` writes the pictures at the resolution drawn. Runs
on the virtual clock draw the same frames every time with either renderer;
the tests and the recorded frames use `--renderer software`, which gives the
same pixels on every machine. `--renderer software` also rules the card out
when a picture looks wrong, and `--gpu-debug` checks every Vulkan call.

Not covered: what the software renderer leaves out too (the destination
alpha test, dithering, anti-aliasing, scan masks, the CLUT buffer, depth
buffers sampled as textures, transfers back to the EE); scales that are not
whole numbers; texture filtering beyond what the game asks (PCSX2's
anisotropic and forced bilinear filtering); the other per-game fixes PCSX2
applies when it upscales (half-pixel offsets, Round Sprite Half and merged
sprites): a
copy of a picture the card drew through texel edges, rather than texel
centres, comes out half a console pixel to the left above 1x
([architecture.md](architecture.md#the-vulkan-renderer)); a
depth test whose buffer is in the frame's memory; a renderer for cards
without fragment shader interlock; changing the renderer while a game runs
(its resolution changes at the next vsync); macOS.

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
- **Missing micro code.** VU micro programs the interpreter ran go to
  `ps2n_missing.vu.toml` (next to `--missing-out`), each once wherever it
  ran, with a copy of micro memory. List it in `[vu] code_files` and
  recompile, and they run recompiled wherever the game puts them (see
  [config.md](config.md#vu-recompile-micro-code)).
- **`--strict`** stops at the first unimplemented path instead of carrying on,
  so the log ends at the first point where emulation went wrong. Interpreted
  code and skipped functions are not unimplemented paths: they run correctly,
  and do not stop a strict run.
- **`--interpret`** runs the same build through the interpreter. If a problem
  happens in both modes it is a runtime gap; if only in recompiled mode, it is
  a recompiler bug. Functions the config replaced run in both modes.
- **`--trace-syscalls`**, **`--trace-functions`** and `--log-level debug` or
  `trace` (SIF, file, disc and DMA activity, thread scheduling) show what the
  game is doing. At `trace`, every disc read appears with its sector, length
  and destination (`[trace:cdvd]`).
- **`report.txt`**, next to the generated code, lists unresolved indirect
  jumps and unknown instructions found at recompile time.

### A tester's log in Axiom

A run can send its log to [Axiom](https://axiom.co), a log service, so that
a developer sees what happened in a tester's runs without asking for files.
It is off unless the settings file ([input.md](input.md#the-settings-file))
has an `[Axiom]` section:

```ini
[Axiom]
Token = xaat-...
Dataset = ps2n-beta
Domain = eu-central-1.aws.edge.axiom.co
```

`Token` is an API token that may ingest into `Dataset`. Give each tester a
token of their own, so that one can be revoked alone; the file holds it in
plain text. `Domain` is the dataset's edge deployment,
`us-east-1.aws.edge.axiom.co` or `eu-central-1.aws.edge.axiom.co` (Axiom
shows the organisation's under Settings > General); the other one answers
"mismatched region". `Enabled = false` keeps the section and sends nothing.
Runs that read no settings file (without a window and without `--settings`,
as the tests run) never send.

Each event is a flat object with `_time`, `session` (one per run), `kind` and
`game`, and the fields of its kind:

| `kind` | Fields |
|---|---|
| `start` | `build` (the runtime's commit), `generator`, `functions`, `os`, `cpu`, `threads`, `display` (`x11` or `wayland` on Linux), `args` |
| `log` | `level`, `category`, `message`, `vsync`: every line at info level and above |
| `speed` | `vsync`, `vblanks`, `seconds`, `vblanks_per_second`: every ten seconds |
| `issue` | `key`, `count`: the lines of the exit summary |
| `end` | `exit_code`, `vsync`, `seconds`, `dropped` (events that did not fit) |

The user's home folder reads `~` in the command line, the messages and the
exit summary's keys. The events go in batches
from a thread of their own, through the `curl` program (part of Windows since
version 1803; on Linux, install it where `curl --version` fails): at once
after a warning or an error, otherwise every ten seconds, and at exit, which
waits for the last request (at most 20 seconds). A request that fails goes
again after 30 seconds. A refusal that another try would not change (a wrong
token, region or dataset) ends the sending, with a warning. The lines about
it (`[info:axiom]`) are not sent. A whole Agent Under Fire session logs about
40 lines.

Not covered yet: a run that crashes loses what it had not sent (up to ten
seconds of info lines; its log ends without an `end` event), and nothing says
where it crashed.

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
`ERET` returns to EPC (reported), TLB instructions are ignored (the default TLB
layout is assumed). An `ERET` with `Status.ERL` set returns to ErrorEPC and
clears ERL, as on the console: libkernel's timer functions (`SetT2`) set ERL
themselves to write a timer register in kernel mode, then return that way.

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
its own small structures there (the syscall table, the interrupt stack, kernel
scratch data).
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
| Program control | `Exit`, `ExecOSD` (exit), `LoadExecPS2` (starts another executable, see [Several executables](#several-executables); `rom0:` paths such as `rom0:OSDSYS` exit), `_print` (debug output), `SetSyscall` (see below), `Deci2Call` (see below) |

Not implemented (reported, return 0): `ExecPS2`, event flags
(`CreateEventFlag` and friends), the TLB entry calls, `SetCPUTimerHandler` and
`SetCPUTimer`, and any syscall number not listed above that the game has not
installed a handler for.

`SetSyscall` writes a handler into a syscall table in kernel memory, as on the
console, where the game can search for it and read it. Sony's libkernel does
both at start-up: it installs its own `Copy` (90) and `FindAddress` (131) and
searches kernel memory for them to find the table. A handler in user memory
runs for its number from then on, whatever the number, the way the kernel runs
one: called like a function with the caller's registers, its result in v0 and
every other register as the caller left it, with interrupts and thread
switches held off until it returns. A negative number, as libkernel's
interrupt-context calls use, reaches the same entry. A handler in kernel memory
is one of the patches libkernel copies there for the console's own kernel
(alarms, `ExecPS2`, the TLB functions), so it never runs: the runtime's own
syscall stands in, or the call is reported. Setting 0 gives the number back to
the runtime. A number outside the table's 512 entries (on the console, a write
into the kernel's own memory) is ignored and reported. `LoadExecPS2` starts the
next program with an empty table.

`Deci2Call` is the link to the debugger of development consoles. Sony's
libkernel sends `printf` output through it: `sceTtyWrite` opens a socket for
the TTY protocol, asks to send, and polls until the socket's handler has
handed the packet over and been told it went out. The runtime plays the
debugger's part when the game polls: it calls the handler with `DECI2_WRITE`
until the whole packet has arrived, prints its text, then calls it with
`DECI2_WRITEDONE`. `kputs` prints directly. Nothing ever arrives to be read,
and sockets for other protocols are refused (reported).

Interrupt handlers run nested on the interrupt stack, as on the real kernel,
at safepoints (loop back-edges and syscalls). They and alarms run with the
`$gp` the program had when it registered them, which the kernel keeps, not
with the interrupted code's. Thread switches happen when a thread blocks, when
a handler wakes a higher-priority thread, and on `RotateThreadReadyQueue`.

`SetVSyncFlag(flag, csr)` registers one wait: the next vblank writes 1 to
`flag` and the GS `CSR` to `csr`, and the kernel forgets both addresses.
libkernel's `VSync2` (behind `sceGsSyncV`) passes two of its locals and
returns without unregistering them, so writing them again at a later vblank
would land in another function's stack frame.

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
| INTC | `I_STAT`, `I_MASK`; the sources raised so far are GS, VBLANK start/end, VIF0 and VIF1 (a VIF code's i bit), timers 0-3 and the IPU (DMA completion goes through the DMAC interrupt). Each event runs its handlers once without clearing `I_STAT`, so the guest can poll the latched bit and acknowledge it explicitly; a later event runs them again |
| Timers | all four: count, mode, compare, hold; compare and overflow interrupts |
| Vsync | NTSC or PAL timing from `SetGsCrt`; VBLANK start/end interrupts, GS `CSR.VSINT`; in real time a vblank waits until the threads have had half a frame over the last two frames, so interrupt handlers that keep overrunning frames (drawing through the vector units, which a console does beside the CPU) slow the game down instead of starving its threads |
| SIO | the debug TX register (`0x1000F180`) prints to stdout |
| DMAC | channel and global registers, `D_STAT` interrupts, `CPCOND0`, the `D_ENABLEW` hold (a channel started while held runs on release); VIF0, VIF1, GIF and RAM-to-scratchpad in normal and source-chain mode (tags `refe`, `cnt`, `next`, `ref`, `refs`, `call`, `ret`, `end`), scratchpad to RAM in normal mode. The IPU channels (3 and 4; 4 also in source-chain mode) move data only as the IPU's FIFOs take or give it, so they stay in progress until the IPU has used their data. A VIF channel whose VIF stalls on an i bit stops after the quadword the VIF stalled in, with `STR` still set, and goes on from `MADR`, `QWC` and `TADR` when the stall ends; it ends at once if the VIF has taken all its data. SIF transfers go through the SIF syscalls. Reverse VIF1 DMA runs through the GS thread, and the guest publishes its result when complete. Other channels complete immediately and are reported. |
| VIF | VIF0 and VIF1: `UNPACK` into VU memory (every format, write masks with `STROW`/`STCOL`, the `STMOD` offset and difference modes, skipping and filling writes, `TOPS`-relative addresses), `MPG` uploads, `MSCAL`/`MSCALF`/`MSCNT` (VIF1 flips the `TOP`/`TOPS` double buffer), VIF1 `DIRECT`/`DIRECTHL` to the GIF, and the register codes (`STCYCL`, `OFFSET`, `BASE`, `ITOP`, `STMOD`, `STMASK`, `STROW`, `STCOL`, `MSKPATH3`, `MARK`, `FLUSH*`). A code with the i bit raises the VIF's interrupt once it has been carried out (`STAT.INT`) and stalls the VIF (`STAT.VIS`) until `FBRST.STC`, unless `ERR.MII` masks it; the words that arrive meanwhile wait in its FIFO. `FBRST.RST` resets the VIF and ends a stall. ForceBreak and STOP (`FBRST.FBK`, `STP`) are reported, not modelled, and `STAT` shows neither the FIFO's count nor the decoding state |
| VU | micro mode for VU0 (`VCALLMS`, `VCALLMSR`) and VU1 (`MSCAL`, `CMSAR1`): an interpreter that keeps what micro code can observe of the pipeline. MAC, status and clip flags arrive 4 cycles after the instruction that set them, Q (DIV/SQRT/RSQRT) and P (VU1 EFU) after their latencies (`WAITQ`/`WAITP` and register hazards stall, and a stall holds the whole pair: an operation on Q paired with `WAITQ` reads the new Q), branches have a delay slot, the E bit ends a program after one more instruction, upper and lower instructions read their inputs before either writes, `XGKICK` sends GIF packets down PATH1, and VU0 reaches VU1's registers at `0x4000`, in micro and macro mode: VF0-31, then VI0-31, a quadword each, an integer register in the x word (stores to VF0 and VI0 are lost; the control registers, VI16-31, read and write as held and report a gap). A program runs to completion when it is started, as far as the EE can tell: in real-time runs VU1's programs run on a thread of their own, in the order VIF1 and `CMSAR1` started them and after the `UNPACK` and `MPG` data sent before them, and whatever reads or writes VU1's state from the EE (loads and stores in its memories, VU0's window, the reset) waits for them ([architecture.md](architecture.md#the-vu1-thread)). Each instruction pair is decoded once, and the decoding kept while micro memory holds the same words. Programs listed in `[vu] code_files` run recompiled wherever micro memory holds them (games upload a program wherever there is room: the runtime finds it by its first pairs, and the compiled code adds where it sits to every address it uses), with the pipeline's timing worked out at recompile time; the interpreter runs the rest, and goes on where a recompiled program hands over. |
| GIF | `PACKED`, `REGLIST` and `IMAGE` modes from PATH1 (VU1's `XGKICK`), PATH2 (VIF1) and PATH3 (DMA channel 2 or the FIFO), PATH3 masking. The paths are decoded in the order the game sent their data; what VU1's own thread kicks keeps the place where the game started the program |
| GS | privileged registers (`PMODE`, `SMODE2`, `DISPFB1/2`, `DISPLAY1/2`, `CSR` with `SIGNAL`/`FINISH`/`LABEL` events, `IMR`); a software rasterizer for points, lines, line strips, triangles, strips, fans and sprites with flat and Gouraud shading (a triangle carries its colours, texture coordinates and fog from its first vertex, so a value its vertices share stays exact; triangles and sprites cover the pixels whose centres are inside, a centre on an edge only for a left or a top edge), textures in every colour format, 4/8-bit CLUT ones included, and `TEXA` (which 16-bit CLUT entries take their alpha from too, `AEM` making only an all-zero entry transparent), the four texture functions, `REPEAT`/`CLAMP`/`REGION_CLAMP` wrapping, nearest, bilinear and trilinear filtering as `TEX1` asks (the level of detail, `log2(1/\|Q\|) * 2^L + K`, or `K` alone with `LCM` and for texture coordinates by UV, where Q is not interpolated, in sixteenths, picks `MMAG` or `MMIN`, but with `MXL` 0 `MMAG` alone, as PCSX2 found on a console; bilinear weighs the four texels around the point half a texel up and left, in sixteenths of a texel), mipmaps (up to six more levels from `MIPTBP1`/`MIPTBP2` or placed by `MTBA`, each half the size of the one before, `CLAMP`'s region halved too; `MMIN` rounds the level of detail to a level or blends the two levels around it by its sixteenths), fog, alpha test with `AFAIL`, depth test (depth interpolated exactly, so layers drawn at one depth do not fight), alpha blending with `PABE` and `FBA`, scissoring, frame-buffer masks; host-to-local, local-to-local and local-to-host transfers (drained by normal-mode reverse VIF1 DMA); display read-out of both circuits at their `DISPLAY` positions, circuit 1 blended over circuit 2 or `BGCOLOR` (`SLBG`) by its pixels' alpha or `ALP` (`MMOD`), and interlaced FIELD mode (`FFMD`), where each field's lines are deinterlaced (see Video). In real-time runs the GS draws on its own thread, up to two vsyncs behind the game, as PCSX2's MTGS does ([architecture.md](architecture.md#the-gs-thread)); `FINISH` and `SIGNAL` reach the EE at its next safepoint, and `GIF_STAT` shows the GS idle as soon as data is queued, not when it is drawn. Several draw threads (`--gs-threads`) share every primitive, each drawing its own bands of four rows of a queued batch, in order. A primitive that depends on the batch (a texture it drew, memory it sampled, the same memory under another layout) waits for it, as do image transfers and the display; one that reads memory it draws into (texture or CLUT), or whose frame and depth buffers overlap, is drawn by one thread. So the picture is always the one a single thread draws. The drawing state is decoded once per primitive, and the swizzles are tables; each texture format samples through its own code, with each level's addressing, the CLUT's layout and the frame and depth buffers' addressing worked out with the drawing state, the depth test comes before texturing, and triangles work out eight pixels' weights at a time, with the same operations as one at a time ([architecture.md](architecture.md#pixels)). The Vulkan renderer draws all of it on the graphics card instead, at 1 to 8 times the console's resolution, with the software rasterizer's pixels at the console's ([Rendering](#rendering), [architecture.md](architecture.md#the-vulkan-renderer)) |
| IPU | the MPEG-2 decoder, every command: `BCLR`, `IDEC`, `BDEC`, `VDEC`, `FDEC`, `SETIQ`, `SETVQ`, `CSC`, `PACK`, `SETTH`. Decoding follows ISO/IEC 13818-2: the Annex B code tables, zigzag and alternate scan, inverse quantisation with MPEG-2 mismatch control or MPEG-1 rounding, an inverse DCT within IEEE 1180's error limits, frame and field DCT, `SCD`/`ECD`. CSC is ITU-R BT.601 with the alpha thresholds of `SETTH` and dithered RGB16. Input and output go through the 8-qword FIFOs at `0x10007010`/`0x10007000`, which DMA channels 4 and 3 fill and empty as the IPU uses them; a command that runs out of data waits, and `IPU_BP` (BP, IFC, FP), `IPU_TOP` and the channels' `MADR`/`QWC` show where it stopped. Each finished command raises the IPU interrupt. |

Not covered yet:

- **GS**, each reported when a game uses it: depth
  formats as textures; the destination alpha test (`TEST`'s `DATE`), on any
  frame (on a 24-bit one the console draws nothing); dithering (`DTHE`) into
  a 16-bit frame; anti-aliasing (`AA1`, from `PRIM` or `PRMODE`) of lines
  and triangles; scan masks (`SCANMSK` 2 and 3); `CSM2`, drawn as `CSM1`;
  and the CLUT buffer. A texel reads its CLUT entry from GS memory at `CBP`
  when the pixel is drawn, whatever `CLD` says, so a 4-bit texel's `CSA`
  picks entries in that memory rather than in the buffer. The runtime keeps
  where the console's buffer would have loaded each of its 32 slots from,
  under `CLD`'s conditions (`CBP0` and `CBP1` too, `TEX2` as well as `TEX0`),
  and reports a draw whose slots would hold other entries: loaded from
  another CLUT or at another `CSA`, never loaded, or loaded before a
  transfer changed them. A CLUT that primitives draw into after its load is
  not checked ([architecture.md](architecture.md#the-clut-buffer)). Not
  reported: the GS's own precision in texture filtering (sixteenths of a
  texel and of a level here, rounded down; no public source gives the
  hardware's). The Vulkan renderer leaves out the same, draws the rest as the
  software renderer does, and reports what it does differently (see
  [Rendering](#rendering)).
- **DMA and the VIFs**: scratchpad-to-RAM DMA in chain mode, VIF1
  GS-to-memory chain transfers, the VIFs' ForceBreak and STOP, and their FIFOs'
  depth (on the console a channel keeps filling the FIFO of a stalled VIF;
  here it stops at once).
- **Speed**: the IPU's (a command takes no emulated time) and the vector
  units' (a program takes no emulated time). VU1's programs run beside the
  EE on a thread of their own in real-time runs, but the EE sees each
  finished the moment it starts, and VU0's always run on the game's thread.

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
  whether the server is host code or an IRX module. A bind to a sid nobody
  serves gets no server, as on the console, so the game retries while a
  module that is still starting may register it; after five seconds of
  emulated time a null service answers (reported), so a missing module does
  not hang the game. A command with no registered handler is ignored, as in
  `sifcmd`, and reported as a fallback rather than an unimplemented path.
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
| FILEIO (`0x80000001`) | `open`, `close`, `read`, `write`, `lseek`, `remove`, `mkdir`, `rmdir`, `dopen`, `dclose`, `dread`, `getstat`, and `chstat` (accepted, no effect), over the host directories and the disc; `tty:` writes go to stdout. Others (`ioctl`, `format`, driver management) return -1 and are reported. The version query (function 255) answers the module release. Release 2100 (SDK 2.1) has its own `open` layout, a mode and a name of up to 1024 bytes; its `close`, `read`, `write` and `lseek` are the BIOS's, and its other calls are reported. Releases from 2200 on use a command header and write a result block to the EE address supplied by the version query, then send SIF command `0x80000011` and the RPC_END that releases the request. Directory replies carry an extended stat and the entry name; getstat replies carry the stat itself. |
| IOP heap (`0x80000003`) | `SifAllocIopHeap`, `SifFreeIopHeap`, `SifLoadIopHeap` |
| LOADFILE (`0x80000006`) | module loading from a path or an IOP memory buffer, IOP memory peek/poke, search by name, the version query (the module release); stop and unload are refused (not removable). Loading an EE ELF through the IOP is reported. |
| CDVDFSV (`0x80000592`, `0x80000593`, `0x80000595`, `0x80000596`, `0x80000597`, `0x8000059A`, `0x8000059C`) and the `cdvdman` library | resident from start-up, like on the console. See [Discs](#discs). |
| PADMAN (`0x80000100`/`0x80000101`, and the original PADMAN's `0x8000010F`/`0x8000011F`) | a DualShock 2 in port 1: digital, analog and pressure-sensitive modes, actuator set-up, the per-frame data area libpad reads. That area also holds what PADMAN 4 keeps there: the controller's button mask and the last pressure-mode request (`scePadSetButtonInfo`); Sony's libpad reads the mask there, not over RPC, before it enters pressure mode. The version query answers the IRX version of the module the game loaded (3.06 when it is not a file), whose major libpad checks. See [Controllers](#controllers). |
| XMCSERV / MCSERV (`0x80000400`) | both protocols. See [Memory cards](#memory-cards). |
| `libsd` (IOP library) | Sony's library on the SPU2 (see the IOP's SPU2 below): each call reads and writes the registers the library does, after ps2sdk's open libsd. `sceSdInit` leaves every voice keyed off on a silent loop at `0x5000`, as libsd does. Voice transfers move their data at once and finish a little later with the core's DMA interrupt (`0x24`, `0x28`). Block transfers stream PCM through AutoDMA; the interrupt handler arms the next half of a looping buffer, as the library does, and calls the driver's handler. Line 9 means a core's interrupt address was touched: the library turns that core's interrupt off and calls the driver. The reverb presets are libsd's. Not covered: reading the output back (a block transfer from the SPU2 gives silence, reported) and the S/PDIF modes (kept, no effect). |
| MTAPMAN (`0x80000901`-`905`) | no multitap connected: ports 0-3 open, `GetConnection` reports none, thread priorities in the valid range (9-123) are accepted, and the version query answers the IRX version of the module the game loaded (3.01 when it is not a file), whose major SDK 2.x's libmtap checks |
| SDRDRV (`0x80000701`) | null service: it binds, answers every call with zeros, and reports it. A module that registers the sid itself gets the binds from then on (Code: Veronica X's Tamsoft sound driver serves it), as does any module that registers a sid only a null service held. |

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
interrupt, so the IOP answers before it mixes on. While it is stopped, a
register access mixes nothing, so the answer (libsd arming the next half of
a looping transfer) acts at that sample. Not covered: the IOP's DMA
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
| GS drawing | The software rasterizer in [hw/gs.cpp](../runtime/src/hw/gs.cpp), and the same in the Vulkan renderer: the per-pixel steps in [gpu/shaders/gs.frag](../runtime/src/gpu/shaders/gs.frag), the drawing state's record in `record()` in [gpu/renderer.cpp](../runtime/src/gpu/renderer.cpp) with its layout in [gpu/shaders/layout.h](../runtime/src/gpu/shaders/layout.h). The `gpu_` tests run the GS's tests on the card, so a change to one renderer shows in the other's tests. After changing a shader, build the `ps2n_update_shaders` target and commit `gpu/shaders/ps2n_spirv.h`, which builds without glslc use. |
| A Vulkan function or feature | The function lists in [gpu/vk.h](../runtime/src/gpu/vk.h) (the loader resolves them by name), the device's required features in the table at the top of [gpu/device.cpp](../runtime/src/gpu/device.cpp), each with the sentence that says a card lacks it. |
| Game-specific behaviour | Not in the runtime: use `[functions] override` and `[output] extra_sources` ([config.md](config.md#functions-skip-or-override)). |

Report what you do not implement with `PS2_UNIMPL(category, key, format, ...)`
([log.h](../runtime/src/log.h)): it logs the first few occurrences, counts
the rest under `key` for the exit summary, and stops a `--strict` run. Use
`PS2_FALLBACK` for paths that are handled correctly but worth knowing about.
