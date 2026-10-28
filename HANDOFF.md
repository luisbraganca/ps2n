# ps2-uncompiler: handoff to the Windows PC

Written 2026-10-27 at the end of the Mac sessions, to carry the context to a
new Claude Code session on Windows.

- **Part 1** is for you: what to do before leaving the Mac and on Windows.
- **Part 2** is the prompt. Paste it as your first message, or tell Claude
  "Read HANDOFF.md (part 2 is your briefing) and start with the first tasks."
- **Appendices A and B** are CD/DVD and memory card protocol notes extracted
  from ps2sdk with file:line citations. They existed only in a temporary
  folder on the Mac; Part 2 tells Claude when to read them.

---

## Part 1: for you

### Before leaving the Mac

1. **The repo has no commits yet.** Everything, this file included, is
   staged in `Personal/ps2-uncompiler`. Review and make the first commit
   (`git commit`). If you would rather not keep this file in the history,
   `git restore --staged HANDOFF.md` first and carry it separately.
2. **Move the repo.** It has no remote. Either push it to a private remote
   (`git remote add origin <url>` and `git push -u origin main`) or copy the
   whole folder, `.git` included. Everything the tests need is in the repo,
   including the prebuilt test programs in `tests/sdk/bin`.
3. **Homebrew.** For the ps2dev toolchain, Claude installed `libmpc`, which
   pulled in `gmp` and `mpfr`. To remove them: `brew uninstall libmpc mpfr gmp`.

### On Windows

1. **Install** Visual Studio 2022 with the "Desktop development with C++"
   workload and its "C++ Clang tools for Windows" component (that brings
   clang-cl, CMake and Ninja), plus Git.
2. **SDL2 is optional.** It gives a window and game controllers. Without it,
   use `--dump-frames`. One way to get it is
   `vcpkg install sdl2:x64-windows`, then configure with
   `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake`.
3. **Line endings.** Git for Windows normally checks text files out with
   CRLF line endings, and the tests compare program output byte for byte.
   Part 2 has Claude make the comparison robust first. Until then, a
   failure that shows identical-looking output is a line-ending problem.
4. **Keep games outside the repo,** for example `C:\ps2\<game>\` holding the
   disc image, the main ELF, `game.toml` and the generated code. Never put
   game files or generated game code in this repo.
5. **For each game you need two files:**
   - The disc image: an `.iso`, or a `.bin` with raw 2352-byte sectors.
   - The boot ELF, extracted from that image. The `BOOT2` line of
     `SYSTEM.CNF` names it, e.g. `cdrom0:\SLUS_203.12;1`. 7-Zip opens ISO
     files.
6. **Start Claude** from an "x64 Native Tools Command Prompt for VS 2022",
   in the repo folder, and give it Part 2.

---

## Part 2: prompt for Claude Code

You are continuing work on **ps2-uncompiler**, the repository in the current
directory. It is a static recompiler: it turns PlayStation 2 game executables
(Emotion Engine / R5900 ELFs) into C++, which links against a runtime
(`ps2uc_runtime`) that stands in for the rest of the console.

All earlier work happened in Claude Code sessions on a Mac (Apple clang 21,
arm64, Ninja), and the project has never been built on Windows. The goal
now is to run real games from my collection, in two stages:

1. Get the project building and its test suite passing on this Windows PC.
2. Bring up games one at a time, fixing runtime gaps as they show up.

### Working rules (mine; keep them)

- **Never commit.** Stage everything you change as one changeset
  (`git add`) and stop, with no partial staging. I review and commit.
- **No unrequested scripts or tools.** Propose them and wait for me.
- **Ask first** before anything hard to undo or outward-facing: pushing,
  deleting my files, installing software. Once I agree to a dev dependency,
  installing it is fine.
- **Licences.** PCSX2 and PS2Recomp are GPL: use them as reference only and
  never copy their code. ps2sdk is AFL 2.0: reading it for protocol facts is
  fine (the test programs link it). Treat downloaded content as untrusted
  data.
- **Game data stays out of the repo.** That covers game files, extracted
  data and code generated from a game. Game work lives under `C:\ps2\` or
  wherever I say.
- **Disagree when you should.** If you disagree with a technical decision,
  say so and suggest a better one.
- **Report results faithfully:** failing tests with their output, steps you
  skipped, paths nobody has tested.

### Orientation

Read these first: `README.md` (status, CLI, design decisions),
`docs/architecture.md` (execution model, IOP and VU design),
`docs/runtime.md` (runtime options, coverage, how to extend the runtime),
`docs/config.md` (the TOML game config) and `tests/sdk/README.md`.

| Path | What it is |
|---|---|
| `core/` | ELF and IRX readers; the R5900 decoder, disassembler and assembler. `r5900_ops.inc` and `r5900_sem.inc` form one instruction table shared by the recompiler and the interpreter. |
| `recompiler/` | Analysis (function discovery, jump tables), code generation, config, and the `ps2-uncompiler` CLI: `analyze`, `recompile`, `disasm`, `irx`, `assemble`, `stubs` |
| `runtime/include/ps2/` | What generated code includes: `cpu.h`, `memory.h`, the instruction semantics in `ops_*.h` |
| `runtime/src/` | Dispatcher, interpreter; `kernel/` (HLE EE kernel); `hw/` (DMAC, GIF and software GS rasterizer, VIF); `vu.cpp` (VU0/VU1 micro-program interpreter); `iop/`; `stubs/`; `video/` (SDL2 window, PPM frame dumps, host pad input) |
| `runtime/src/iop/` | IOP files, listed below |
| `tests/` | Unit tests (core, recompiler, runtime); `tests/programs` (assembled programs); `tests/sdk` (ps2sdk-built programs, binaries in `tests/sdk/bin`, and a `Makefile` that rebuilds them with the ps2dev toolchain) |

The files in `runtime/src/iop/`:

| File | Contents |
|---|---|
| `cpu` | R3000A interpreter |
| `kernel` | HLE IOP kernel |
| `hle` | Library and export plumbing, trap addresses |
| `libs` | sysclib, stdio, ioman, iomanX, heaplib, timrman |
| `modules` | IRX loading, loadcore, modload |
| `sif` | sifcmd, sifman |
| `iop.cpp` | SIF wire protocol, RPC dispatch, IOP reset |
| `services.cpp` | FILEIO, IOP heap, LOADFILE, and the list of modules replaced by host services |
| `hostfs` | The file devices `host:`, `cdrom0:`, `mc0:`, `rom0:ROMVER` |
| `disc` | ISO images and directories presented as a disc |
| `cdvd` | CDVDFSV RPC and the cdvdman library |
| `memcard` | MCSERV and XMCSERV |
| `pad` | PADMAN |
| `sound` | libsd, without audio output |

On the Mac the build and test commands were:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
ctest --test-dir build --output-on-failure
```

All 29 ctest tests passed in three trees: Debug, a fresh tree, and Release
with `-DPS2UC_WARNINGS_AS_ERRORS=ON`. Every end-to-end test runs twice:
once with recompiled code and once with `--interpret`.

### How it works (short)

- **Recompiled code.** Every guest function becomes a C++ function. Calls
  are direct C++ calls, each followed by a return-address check; that check
  is what keeps thread switches, longjmp and unusual returns correct. CPU
  state is one global `ps2::cpu`.
- **Missed code** runs on a built-in interpreter. It records the start
  addresses in `ps2uc_missing.toml`; add that file to `[analysis]
  entries_files` and recompile.
- **EE side:**
  - HLE kernel with 100+ syscalls, INTC, timers, DMAC, GIF.
  - Software GS rasterizer. Frames go to an SDL2 window or to PPM dumps.
  - VIF0 and VIF1.
  - VU0/VU1 micro-program interpreter. Each program runs to completion as
    soon as it starts. It models flag delays, Q/P latencies, branch delay
    slots, the E bit and XGKICK.
- **IOP side:**
  - IRX modules run on the R3000A interpreter. The IOP kernel and its
    libraries are host code, reached through trap addresses.
  - Sony system modules for emulated devices are replaced by host services
    when the game loads them: sio2man, padman, mtapman, mcman, mcserv,
    cdvdman, cdvdfsv, cdvdstm and libsd, plus their x/free variants.
  - Network, hard disk, USB and dev9 modules load and do nothing.
- **IOP services by SIF RPC id (sid):**

  | Group | Services |
  |---|---|
  | Resident from start-up | FILEIO `0x80000001`, IOP heap `0x80000003`, LOADFILE `0x80000006`, CDVDFSV `0x80000592`/`593`/`595`/`597`/`59A`/`59C` |
  | Registered when the module loads | PADMAN `0x80000100`/`101` (old PADMAN: `0x8000010F`/`11F`), MCSERV `0x80000400` |
  | Null services (reply with zeros, reported) | MTAPMAN `0x80000901`-`903`, SDRDRV `0x80000701` |
- **IOP reset.** `SifIopReset`, e.g. with `rom0:UDNL cdrom0:\IOPRP300.IMG`,
  re-initialises the HLE IOP. The IOPRP image itself is not read. A `rom0:`
  module the HLE does not know is reported as "IOP module without HLE".
- **Discs.** `--cdrom` accepts an ISO, a raw `.bin`, or a directory. A
  directory is turned into an in-memory ISO 9660 volume. macOS mounts the
  generated images, and the reader parses images made by `hdiutil`.
- **Memory cards.** `--memcards DIR` holds `DIR\mc0` (port 1, always
  present) and `DIR\mc1` (port 2, present when the folder exists).
- **Region.** `--region usa|europe|japan|asia|china` sets the region in
  ROMVER (default usa). libgraph picks PAL for europe.
- **Input.** Keyboard and SDL game controllers feed the pad service. Use
  `--pad-script` for reproducible input.

### State and known gaps

- **Tested and working:**
  - The whole R5900 instruction set.
  - The VU micro mode tests (`tests/sdk/vu`, checked against a recorded frame).
  - IRX modules with their own RPC servers.
  - libpad and libpadx.
  - libcdvd: search, aligned and unaligned reads, streaming, `cdrom0:`.
  - libmc: both protocols.
  - libsd driven from an IRX.
  - ps2sdk's VU1 cube sample, which renders correctly.
- **Never tried:** a retail game. Commercial games link Sony's libraries;
  the services follow their protocols, but expect gaps in the less common
  calls.
- **Silent sound.** libsd keeps drivers running, but there is no SPU2
  emulation. SDRDRV, the server behind the EE's libsdr, is a null service.
- **Missing hardware and devices:**
  - IPU, so MPEG FMVs will not play.
  - Dual-layer DVDs (layer 1 is not searched), CD audio, raw-sector CD reads.
  - Multitap, network, hard disk.
  - GS: bilinear filtering and mipmaps, local-to-host transfers.
- **VU limits.** Programs are interpreted and finish the instant they start,
  so the VU never runs in parallel with the EE.
- **Memory cards.** Format and Unformat are deliberately no-ops, so a game
  cannot wipe a save folder. Per-file attributes and timestamps are not
  kept.
- **Untested platforms:** the SDL window and controller code has only been
  compile-checked. Windows and Linux have never been built.

### First tasks on Windows

1. **Build.** clang-cl with Ninja is the recommended setup, because MSVC's
   optimizer is slow on the huge functions in generated game code:

   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
   ninja -C build
   ctest --test-dir build --output-on-failure
   ```

   Also try plain MSVC (`cl`), since that is what most users have.
2. **Portability risks** found by a scan on the Mac. Check these first:
   - **Line endings.**
     - The runtime prints guest output with `fwrite` to `stdout` in text
       mode, which writes CRLF on Windows.
     - `cmake/RunAndCompare.cmake` compares the output byte for byte with
       `*.expected`, and Git may have checked those out with LF or CRLF.
     - Robust fix: normalise CRLF to LF on both sides in `RunAndCompare`.
       Also consider `_setmode(_fileno(stdout), _O_BINARY)` at runtime
       start-up, so guest output bytes stay exact.
     - `tests/sdk/disc/SYSTEM.CNF` must keep its CRLF bytes: the cdvd test
       prints its size, 57. Protect it with a `.gitattributes` line:
       `tests/sdk/disc/** -text`.
   - **`long` is 32 bits on Windows.** hostfs `lseek` and the memory card
     seek go through `fseek`/`ftell` with `long`, so host files over 2 GB
     would break. Disc images are fine: they use 64-bit stream offsets.
   - **Non-ASCII host paths.** `std::filesystem::path::string()` can throw.
   - **Floating-point contraction.** `-ffp-contract=off` is only passed for
     Clang and GCC (`runtime/CMakeLists.txt`). Check that clang-cl accepts
     it, and that MSVC does not fuse `a*b+c`. Its default `/fp:precise`
     should not.
   - **MSVC warnings.** `/W4` may report warnings clang never showed.
     `PS2UC_WARNINGS_AS_ERRORS` is off by default.
   - **Already handled for Windows:**
     - The large-stack guest thread uses `_beginthreadex`.
     - `gmtime_s` branches exist.
     - Generated code builds with `/bigobj`.
     - SDL needs no SDL2main, because `main()` is not in the file that
       includes `SDL.h`.
3. **Fix what fails,** run the full suite again, and stage.

### Bringing up a game

1. **Work outside the repo,** e.g. in `C:\ps2\<game>\`, holding the disc
   image and the boot ELF extracted from it.
2. **Analyze.** Run `ps2-uncompiler analyze SLUS_XXX.XX --write-config
   game.toml` and read what it finds. Retail ELFs are usually stripped, so
   a Ghidra function map gives far better boundaries:
   `tools/ghidra/ExportPs2ucFunctions.java`, then set `[input] ghidra_map`.
3. **Recompile.** `ps2-uncompiler recompile game.toml` writes `out/<name>/`:
   a CMake project, plus `report.txt` listing unknown instructions,
   unresolved jumps and stubs. The generated `CMakeLists.txt` stores the
   repo path in `PS2UC_ROOT`, overridable with `-DPS2UC_ROOT=...`.
4. **Build in Release** (clang-cl) and run:

   ```
   game.exe --elf SLUS_XXX.XX --cdrom game.iso --memcards C:\ps2\memcards
   ```

5. **Iterate on the exit summary.** It counts every unimplemented syscall,
   hardware register and IOP call. Tools for narrowing things down:

   | Tool | Use |
   |---|---|
   | `ps2uc_missing.toml` | Add to `[analysis] entries_files`, then recompile |
   | `--strict` | Stop at the first unimplemented path |
   | `--interpret` | Tell a recompiler bug from a runtime gap |
   | `--trace-syscalls`, `--log-level debug` | Detailed logs |
   | `--max-vsyncs N` with `--dump-frames DIR` | Headless runs |
   | `--virtual-clock` | Reproducible runs |
   | `ps2-uncompiler irx <file.irx>` | A game module's imports and exports |
   | `ps2-uncompiler disasm --cpp <fn>` | How one function was translated |
   | `[functions] skip` / `override`, `[stubs] bind` | Get past code that blocks progress, such as an FMV player while IPU is missing (see `docs/config.md`) |

6. **Where to expect the first gaps:**
   - IOP library exports that Sony modules import but the HLE lacks. They
     are reported as unimplemented export traps; add them in the matching
     `defineLibrary` table.
   - SDRDRV (the EE's libsdr).
   - IPU, so FMVs will not play.
   - GS features.
   - DMA channels other than VIF, GIF and SIF.
   - Game middleware IRX modules (sound drivers, ADX/Sofdec streaming) that
     call `dmacman`, `timrman` or `cdvdman` functions with no
     implementation yet.

### Gotchas learned so far

- **`mcOpen` takes the IOP's open flags:** RDONLY 1, WRONLY 2, CREAT 0x200.
  newlib's `O_RDONLY` is 0, and real mcman then refuses reads. So does the
  HLE.
- **mcChdir.** ps2sdk's EE libmc copies the `mcChdir` result without its
  terminator.
- **sceCdSearchFile.** Sony's version is case-sensitive and needs the `;1`
  suffix; the HLE accepts either.
- **VU microcode.** dvp-as puts VU microcode in `.vutext`, which is flagged
  executable. The analysis now skips it, and `report.txt` lists it as
  `vu code`.
- **First-call results.** Games rely on two of them. The first
  `mcGetInfo` after start-up or an IOP reset returns -1 ("new card"), and
  the first `sceCdTrayReq` check reports a disc change.
- **Protocol details.** When working on the CD/DVD or memory card
  services, read Appendix A and Appendix B of `HANDOFF.md`. They hold sids,
  function numbers, packet layouts, error codes and the quirks, each with a
  ps2sdk file:line citation.

### Rebuilding `tests/sdk` binaries (only after changing their sources)

This needs the ps2dev toolchain: EE gcc, IOP gcc and dvp-as. On the Mac,
Claude used the macOS "latest" release tarball from
github.com/ps2dev/ps2dev, which needed Homebrew `libmpc`. On Windows, check
what ps2dev provides (a Windows build, MSYS2 or WSL), and ask me before
downloading or installing anything. The commands are in
`tests/sdk/README.md`.

---

## Appendix A: CD/DVD services protocol (cdvdman, cdvdfsv, cdvdstm)

Extracted from the ps2sdk sources (AFL 2.0) by a research agent. It records interface facts only, each with a `file:line` citation in that tree. The runtime implements this in `runtime/src/iop/cdvd.cpp`, `disc.cpp` and `hostfs.cpp`; `docs/runtime.md` (Discs) lists what is left out.

Extracted from ps2sdk @ `329e2bb` (shallow clone, 2026-10-12). Interface facts only.

### 0. Conventions and provenance

Citation keys (paths relative to `ps2sdk/`):

| key | file |
|---|---|
| RPC | `common/include/libcdvd-rpc.h` |
| COM | `common/include/libcdvd-common.h` |
| IOC | `common/include/cdvd-ioctl.h` |
| EL / EN / ES | `ee/rpc/cdvd/src/libcdvd.c` / `ncmd.c` / `scmd.c` |
| FSV / FSVI / FSVX | `iop/cdvd/cdvdfsv/src/cdvdfsv.c` / `imports.lst` / `exports.tab` |
| MAN / MANX / MANH | `iop/cdvd/cdvdman/src/cdvdman.c` / `src/exports.tab` / `include/cdvdman.h` |
| STM | `iop/cdvd/cdvdstm/src/cdvdstm.c` |
| SIFR / SIFM | `ee/kernel/src/sifrpc.c` / `iop/system/sifman/src/sifman.c` |

- **IOP side is a decompilation** "based on the module from SCE SDK 3.1.0" (FSV:18, MAN:26, STM:19). Facts below are from the default build (not `CDVD_VARIANT_OSD`/`XOSD`/`DNAS`) unless marked. Treat it as Sony behaviour, except for the decompilation slips flagged in §12.
- **EE side (libcdvd) is an independent 2002–2004 reimplementation** (EL:6-8, EN:6-8). Games link Sony's EE libcdvd, which can send newer packet variants (e.g. search 0x128/0x12C). The IOP accepts all of them. Where ps2sdk-EE behaviour is only a client-side detail, it is marked "ps2sdk-EE".
- All values are little-endian. "EE addr" fields are EE pointers that the IOP uses as SIF-DMA destinations.

---

### 1. RPC servers registered by cdvdfsv

| SID | Purpose | Handler | Receive buffer | IOP thread / queue |
|---|---|---|---|---|
| `0x80000592` | sceCdInit | `cbrpc_rpc1_cdinit` FSV:334 | `int[4]` = **16 B** (FSV:104) | rpc1_th, queue 1 (FSV:3320) |
| `0x8000059A` | sceCdDiskReady | `cbrpc_rpc2_diskready` FSV:1803 | `int[4]` = **16 B** (FSV:106) | rpc1_th, queue 1 (FSV:3321) |
| `0x80000593` | S-commands (blocking; dispatch on fno) | `cbrpc_rpc3_cdvdscmds` FSV:2990 | `int[260]` = **1040 B** (FSV:102) | rpc1_th, queue 1 (FSV:3322) |
| `0x8000059C` | DiskReady, second instance with the same handler on its own thread; not used by ps2sdk-EE | `cbrpc_rpc2_diskready` | shares the 16 B `g_rpc_buffer2` with 0x8000059A (FSV:3332) | rpc3_th, queue 3 (FSV:3327-3335) |
| `0x80000597` | sceCdSearchFile / sceCdLayerSearchFile | `cbrpc_rpc4_fscall` FSV:362 | `int[76]` = **304 B** (FSV:105) | rpc2_th, queue 2 (FSV:3342) |
| `0x80000595` | N-commands (dispatch on fno) | `cbrpc_rpc5_cdvdncmds` FSV:2881 | `int[256]` = **1024 B** (FSV:103) | rpc2_th, queue 2 (FSV:3343) |

- `0x80000596` (EL:38-39, "XCDVDFSV only") is **not** registered, and no EE function uses it.
- Threads: the main thread (priority def−1, stack 0x800) runs the power-off loop (FSV:220-230, 282). rpc1_th and rpc2_th run at def with stack 0x1900; rpc3_th runs at def with stack 0x800 (FSV:245-265). def is 81. The `thpri=N` module argument overrides it; the valid range is 9..123 and 9 becomes 10 (FSV:294-313).
- **Concurrency:** queue 1 (Init, DiskReady, S-cmds) and queue 2 (Search, N-cmds) are served by different IOP threads. So `sceCdBreak`, `sceCdStatus`, `sceCdGetError` and similar are serviced while a read RPC is still running. Search and N-cmds share queue 2, so a search waits behind an in-flight read.
- **RPC completion means operation completion.** N-cmd handlers block until the drive operation ends (e.g. `sceCdSync(6)` after a seek, FSV:2913). The reply/RPC-end is only sent after the handler returns.
- **Unsolicited IOP→EE traffic:** cdvdfsv sends no callback messages. Its only unsolicited traffic is SIF **command** (not RPC) cid `0x80000012`, 16-byte payload (uninitialised). It is sent each time cdvdman's interrupt event-flag bit 0x04 is set, i.e. on the power-button interrupt (FSV:3220-3247; set at MAN:4278-4288). The OSD build also sends `0x80000015` (eject) and `0x80000016` (BS power) (FSV:3249-3313). ps2sdk-EE has no handler for these.

---

### 2. Shared data layouts

#### sceCdRMode (COM:233-243), 4 bytes

| off | field | notes |
|---|---|---|
| 0 | u8 trycount | sent to mechacon as the read retry count (MAN:6636) |
| 1 | u8 spindlctrl | `SCECdSpin*` (§11), mapped to a speed byte (MAN:6411-6476). Timing only. |
| 2 | u8 datapattern | `SCECdSecS2048=0 / S2328=1 / S2340=2`. CDDA aliases: `S2352=0 / S2368=1 / S2448=2` |
| 3 | u8 pad | |

On the wire it is one u32: `trycount | spindlctrl<<8 | datapattern<<16` (EN:196). That is the same byte layout as the struct.

#### Bytes per sector delivered

| case | dp 0 | dp 1 | dp 2 |
|---|---|---|---|
| CD, sceCdRead (EE fno 1 or IOP) | 2048 = raw[12..] | 2328 = raw[12..] | 2340 = whole raw sector: 4-byte header (BCD MSF + mode), 8-byte subheader, then 2328 bytes |
| PS2 DVD (disc type 0x14) | 2048 | 2048 | 2048 (datapattern is forced to 0) |
| CDDA (fno 2, IOP sceCdReadCDDA) | 2352 | 2368 | 2448 on the EE side; the IOP cdvdfsv path uses 2352 (G-cdda) |

Sources: FSV:1023-1044 (secsize; the drive is always read raw in 2340 mode), FSV:742-748 (copy from +12), FSV:697-725 (2340 copies whole sectors), MAN:6711-6735 (IOP path, same offsets), MAN:6580-6598 (DMA sizes 2048/2328/2340), EN:200-206 (EE sizes), FSV:1613-1623, MAN:7064-7082.

#### sceCdlFILE (COM:208-218), 32 bytes, as filled by cdvdman `CD_cachefile` (MAN:2943-2968)

| off | field | value |
|---|---|---|
| 0 | u32 lsn | ISO extent (LE half) + layer base (MAN:2737) |
| 4 | u32 size | ISO data length (bytes) |
| 8 | char name[16] | ISO identifier including `;1`, NUL-terminated. Records 0 and 1 are named `.` and `..` |
| 24 | date[0] | **never written, so 0.** COM:216 claims "iso file flags" (see G-date) |
| 25..29 | date[1..5] | second, minute, hour, day, month (binary, **not BCD**) |
| 30..31 | date[6..7] | full year as u16 LE (ISO years-since-1900 + 1900). GMT offset is dropped |

In IOP memory the record is followed by `int m_flags` = the ISO dir-record flags byte (MAN:63-67, 2955). The search copies all **36** bytes into the caller's struct (MAN:2736). These 4 bytes become `m_file_attributes` in the 0x128/0x12C search packets.

#### Other small types

- `sceCdCLOCK` (COM:188-206), 8 bytes BCD: stat, second, minute, hour, pad, day, month, year.
- `sceCdlLOCCD` (COM:221-231): minute, second, sector (BCD), track. `IntToPos`/`PosToInt` add/subtract a 150-sector offset (MAN:2402-2415).
- `sceCdRChain` (COM:245-253): `{u32 lbn, u32 sectors, u32 buffer}`, 12 bytes. `buffer` bit 0 set means an IOP address (`buffer & ~1`); bit 0 clear means an EE address, which must be 64-byte aligned (COM:251).

#### cdvdfsv_unaligned_data_outpacket_t (RPC:87-95), 0x90 = 144 bytes

Written by the IOP to the EE "remaindest" address.

| off | field |
|---|---|
| 0 | u32 m_b1len: head byte count (0..63) |
| 4 | u32 m_b2len: tail byte count (0..63) |
| 8 | u32 m_b1dst: EE address of the head (= original buffer) |
| 12 | u32 m_b2dst: EE address of the tail (= last 64-byte boundary) |
| 16 | u8 m_pbuf1[64]: head bytes |
| 80 | u8 m_pbuf2[64]: tail bytes |

The old layout (cdvdfsv version ≤ 0x104) has the same first 16 bytes, then `u8[16]` + `u8[16]`, 48 bytes total (EN:127-145, 153).

#### SIF-DMA granularity

IOP `sceSifSetDma` rounds every transfer up to whole 16-byte qwords on the EE side (SIFM:252-255). So the 4-byte readpos writes 16 bytes, and the 36-byte search result writes 48 bytes. The EE targets are 64-aligned for this reason (EN:88-102, EL:96-102).

---

### 3. SID 0x80000592: sceCdInit

**EE call:** fno 0, blocking, send 4 bytes, receive 16 bytes (EL:143-145).

**Request** (RPC:64-67): `+0 int m_mode`. Values: `SCECdINIT=0`, `SCECdINoD=1`, `SCECdEXIT=5`.

**Reply**, 16 bytes (RPC:69-75, FSV:334-350):

| off | field | value |
|---|---|---|
| +0 | m_retres | always 1 |
| +4 | m_cdvdfsv_ver | `(u16)` IRX version = **0x0226** (FSV:17, 345) |
| +8 | m_cdvdman_ver | `sceCdSC(0xFFFFFFF7)` = 0x0226 (MAN:25, 4739-4740) |
| +12 | m_debug_mode | 254 if verbose, else 0 |

**IOP semantics** (`sceCdInit`, MAN:4861-4936):

- **EXIT (5):** sceCdBreak, `sceCdSync(3)`, dummy-read loop (ncmd 0x06), `sceCdSync(0)`, then disable and release the CDVD and CDVD-DMA interrupts. The driver is dead until the next init.
- **Any other value:** clears read/decrypt state, `sceCdDecSet(0,0,0)`, re-runs `cdvdman_init` (re-registers interrupt handlers, reads the RTC, cancels power-off; MAN:4767-4859).
- **INIT (0) only:** additionally polls `sceCdDiskReady(8)` every 10 ms until `(status & 0xC0) == 0x40`. INoD does not wait.
- **All modes:** last error cleared, wait flags and event flags set to idle. cdvdfsv also resets its spindle override (FSV:343).

**EE side:** stores the two versions (EL:146-147). They are later used to choose the unaligned-packet layout (EN:153). EXIT deletes the semaphores and stops the callback thread (EL:148-155, 307-318).

---

### 4. SID 0x8000059A / 0x8000059C: sceCdDiskReady

**EE call:** fno 0, blocking, send `+0 int mode`, receive 4 bytes (EL:265-275).
- EE returns `SCECdNotReady` without calling if its S-cmd lock is busy (EL:243-248).
- EE returns 6 if the RPC fails (EL:269).

**Reply:** `+0 int` = IOP `sceCdDiskReady(mode)` (FSV:1811, RPC:82-85).

IOP behaviour (MAN:2221-2291):

| mode | result |
|---|---|
| 0 | Returns **2** (SCECdComplete). It only waits if an IOP multi-sector read is active **and** the disc type is still DETCT* (1..4). |
| 8 | Raw drive status register 0x1F402005 (u8). Ready means `(v & 0xC0) == 0x40`. |
| 1 / other | 2 if ready and no IOP read is in progress, otherwise **6** (SCECdNotReady). |

---

### 5. SID 0x80000597: sceCdSearchFile / sceCdLayerSearchFile

The handler dispatches on the **received length**; fno is ignored (FSV:374-397).

| size | variant | layout | IOP call | bytes DMA'd back |
|---|---|---|---|---|
| **0x124** | "old library" (also any unrecognised length) | `+0 sceCdlFILE m_fp; +0x20 char m_path[256]; +0x120 u32 m_eedest` | `sceCdSearchFile` (layer 0) | 32 |
| **0x128** | "Not_Dual_layer Version" | `+0 m_fp; +0x20 int m_file_attributes; +0x24 m_path[256]; +0x124 m_eedest` | `sceCdSearchFile` (layer 0) | 36 |
| **0x12C** | dual-layer-aware libcdvd (sceCdLayerSearchFile) | as 0x128, plus `+0x128 int m_layer` | `sceCdLayerSearchFile(.., m_layer)` | 36 |

Layouts: RPC:97-126.

**Flow:**
1. The result is written into the *receive buffer's* `m_fp`, plus the attribute int (MAN:2736).
2. SIF-DMA from the start of the IOP receive buffer to `m_eedest`: 32 or 36 bytes, which becomes 48 bytes on the EE side. This happens **before** the reply (FSV:379-409).
3. Reply: `cdvdfsv_rpc4_outpacket_t`, 16 bytes: `+0 m_retres` (1 = found, 0 = not), then 3 padding ints (RPC:128-132, FSV:412). ps2sdk-EE requests 4 bytes (EL:212).

Notes:
- The DMA happens **even when the file is not found**, so the EE gets its own `m_fp` bytes back.
- While the search runs, cdvdfsv holds the EE-RPC-busy lock (`sceCdSC(0xFFFFFFF6,255)` … `0`; FSV:371-372, 410-411). This blocks IOP `sceCdRead` and `cdrom0:` I/O (MAN:4727-4738, 6866-6871).
- ps2sdk-EE sends the 0x124 variant. `m_fp` is 32 padding bytes, `m_eedest` points at the send buffer itself, and the path is strlcpy'd (max 255 chars). It then copies 32 bytes from the uncached send buffer to the user struct and returns `reply[0]` (EL:57-63, 180-233). It also serialises the search with N-cmds through `nCmdSemaId` and `sceCdSync(1)` (EL:183-190).
- ps2sdk-EE has no sceCdLayerSearchFile; it is only declared (COM:958).

Path rules: see §12 G-search.

---

### 6. SID 0x80000595: N-commands

#### 6.1 Summary (default build)

| fno | EE API (ps2sdk) | EE mode / end function | send | recv | IOP semantics |
|---|---|---|---|---|---|
| 0x01 | sceCdRead | NOWAIT, `_CdAlignReadBuffer(_rd_intr_data)` (EN:218) | 24 | 0 | read to EE memory, §6.2 |
| 0x02 | sceCdReadCDDA | NOWAIT, same end function (EN:299) | 24 | 0 | CDDA to EE, §6.3 |
| 0x03 | sceCdReadDVDV | NOWAIT | 24 | 0 | **OSD build only**, otherwise "unrecognized" |
| 0x04 | sceCdGetToc | wait (EN:323) | 12 | 8 | §6.4 |
| 0x05 | sceCdSeek | NOWAIT, `_CdGenericCallbackFunction(&CdCallbackNum)` | 4 | 0 | `sceCdSeek(lbn)`, then `sceCdSync(6)` (FSV:2909-2914) |
| 0x06 | sceCdStandby | NOWAIT, generic | 0 | 0 | `sceCdStandby()`, then `sceCdSync(4)` (FSV:2915-2920) |
| 0x07 | sceCdStop | NOWAIT, generic | 0 | 0 | `sceCdStop()`, then `sceCdSync(4)` (FSV:2921-2925) |
| 0x08 | sceCdPause | NOWAIT, generic | 0 | 0 | `sceCdPause()`, then `sceCdSync(6)` (FSV:2926-2930) |
| 0x09 | sceCdSt* (via `sceCdStream`) | wait (EN:726) | 20 | 4 | `devctl("cdrom_stm0:",0x4396)`, §9 |
| 0x0A | `sceCdCddaStream` (internal) | wait | 20 | 4 | `devctl 0x4398`, §9 |
| 0x0B | sceCdReadKey | wait | 12 | 0x18 | **OSD only** |
| 0x0C | sceCdApplyNCmd | wait (EN:461) | 20 | 16 | §6.6 |
| 0x0D | sceCdReadIOPMem | NOWAIT, generic (EN:488) | 24 | 0 | §6.7 |
| 0x0E | sceCdNCmdDiskReady | wait (EN:510) | 0 | 4 | §6.8 |
| 0x0F | sceCdReadChain | NOWAIT, generic (EN:579) | 788 | 0 | §6.9 |
| 0x13 | (none in ps2sdk-EE) | | 28 | | as 0x01 plus on-the-fly decrypt using `m_decval` (FSV:2966-2970) |
| other | | | | | `m_retres = 0`, "unrecognized code" (FSV:2976-2979) |

- OSD-only fnos: 3, 11, 16. DNAS-only: 17 (ReadDiskID), 23 (DoesUniqueKeyExist). Fno numbers come from EN:51-68 and FSV:2891-2980.
- **Reply buffer** `g_crr` = `cdvdfsv_rpc5_outpacket_t`: `+0 m_retres`, and fno 4 adds `+4 m_isdvd` (RPC:839-872). For NOWAIT calls the EE passes `rbuf=NULL, rsize=0`, so **nothing is returned; only the RPC-end fires.**
- Every N-cmd handler takes cdvdman's fio/fsv lock via `sceCdSC(0xFFFFFFF6, fno)` on entry and releases it on exit (FSV:2889-2890, 2981-2983).

ps2sdk-EE pre-flight:
- Read, ReadDVDV, ReadCDDA, Seek, Standby, Stop, Pause, ApplyNCmd, ReadIOPMem and ReadChain all call fno 0x0E first and return 0 if it answers 6 (e.g. EN:188-189).
- N-cmds are serialised by `nCmdSemaId`: polled in `_CdCheckNCmd` (EN:802), released by the end function for NOWAIT calls (EL:391) or right after the RPC for blocking calls.
- A new call returns 0 while `sceCdSync(1)` reports busy (EN:809). There is no queueing.

#### 6.2 fno 0x01: sceCdRead into EE memory

**Request:** 24 bytes are sent; the struct is 28 (RPC:750-759, EN:193-198).

| off | field | EE value |
|---|---|---|
| 0 | u32 m_lbn | lbn |
| 4 | u32 m_sectors | sector count |
| 8 | u32 m_paddr | EE destination (any alignment) |
| 12 | sceCdRMode m_rmodeee | packed mode |
| 16 | u32 m_eeremaindest | EE address of the 144-byte packet (`_rd_intr_data`, 64-aligned) |
| 20 | u32 m_eedest | EE address of the u32 read-position counter (`&curReadPos`) |
| 24 | u32 m_decval | only read for fno 0x13 |

**IOP algorithm** (FSV:969-1231):

1. **Media check.** DVD means disc type == 0x14 (FSV:2896).
   - DVD: if `DvdDual_infochk` fails, error 0xFE (READCFR) and nothing is read. Otherwise `lbn = sceCdLsnDualChg(lbn)` (layer-1 remap, G-dual) and secsize = 2048.
   - CD: secsize comes from datapattern (2048/2328/2340) and the drive is read raw in 2340 mode (FSV:1007-1045).
2. **Split the range** (FSV:1051-1058):
   - `total = secsize*sectors`
   - `psize = (paddr&63) ? 64-(paddr&63) : 0`
   - `saddr = (paddr+total) & ~63`
   - `bsize = saddr-(paddr+psize)`
   - `ssize = paddr+total-saddr`
3. **Head** (if `psize`): read 1–2 sectors into IOP scratch and copy the first `psize` data bytes into `m_pbuf1` (FSV:1062-1107).
4. **Body:** read in chunks of ≤64 sectors on 16-sector boundaries. Each 16-sector piece is SIF-DMA'd straight to `paddr+psize+off` (64-aligned) as it arrives. After each piece, `readpos += bytes` and the 4-byte readpos is DMA'd to `m_eedest` (FSV:1109-1154, 848-852).
5. **Tail** (if `ssize`): read 1–2 sectors and copy the last `ssize` bytes into `m_pbuf2` (FSV:1157-1202).
6. **Finish:** set `m_b1len=psize, m_b2len=ssize, m_b1dst=paddr, m_b2dst=saddr`. DMA the 144-byte packet to `m_eeremaindest` and the final readpos (total bytes delivered) to `m_eedest`, and wait for the DMAs. Then `g_crr.m_retres = bytes delivered`, which the EE never receives (FSV:1203-1230).

**Errors:**
- The failing part's length is zeroed (FSV:1083-1085, 1145-1147, 1176).
- The error code is left in cdvdman's last-error slot and **only reaches the EE through S-cmd 0x04** (sceCdGetError).
- Codes: 0xFD READCF (issue failure or timeout), 0x20 IPI (sector-ID / no-data-zone), drive error register values, 0x01 ABRT (break or read timeout) (FSV:591-596, 661, 873-886; MAN:3842-3852, 4268-4271).
- Sector-ID verification with re-seek retries (FSV:488-554, 877-882) is internal and can be skipped in HLE.

**EE fix-up** (EN:150-181):
- The RPC-end handler runs in interrupt context (SIFR:291-299) and calls `_CdAlignReadBuffer(_rd_intr_data)`.
- It reads the packet uncached. It uses the 64-byte layout if the cdvdfsv version from sceCdInit is > 0x104, else the 16-byte layout.
- It does `memcpy(b1dst, pbuf1, b1len)` if `b1len && b1dst`, the same for the tail, then calls `_CdGenericCallbackFunction` (§7).
- Before issuing the call, the EE sets `curReadPos = 0` and writes back the D-cache over `buf` (EN:200-210).

**Completion with an unaligned buffer** works exactly like the aligned case. Body bytes arrive during the read; the ≤63 head and ≤63 tail bytes arrive only when the end function runs. `sceCdSync(0)` cannot return before that (§7).

#### 6.3 fno 0x02: CDDA read into EE memory

- **Request** (RPC:761-769): `+0 lbn, +4 sectors, +8 m_buf (EE), +12 mode, +16 m_eeremaindest, +20 m_eedest`.
- Same head/body/tail scheme and 144-byte packet as fno 1, with sector size 2352 (0x930) or 2368 (0x940).
- Readpos is DMA'd only if the request is ≥25 bytes; ps2sdk sends 24, so never (FSV:1585-1801).
- See **G-cdda** for a likely bug.
- ps2sdk-EE also uses fno 2 with `sectors=0, buf=0` to probe the packet size when sceCdInit returned version 0 (EN:831-866). The IOP must still DMA a 144-byte packet with zero lengths.

#### 6.4 fno 0x04: sceCdGetToc

- **Request** (RPC:780-783): `+0 u32 m_eedest` (EE TOC buffer). ps2sdk sends 12 bytes; words 1–2 are unused (EN:320-323).
- **IOP** (FSV:1815-1852): `m_retres = sceCdGetToc(scratch)` (1 = ok / 0 = fail), then DMA **0x810 (2064) bytes** to `m_eedest`. `m_isdvd = 1` if the disc type is 0x14, 0xFC or 0xFE, otherwise 0.
- **Reply:** 8 bytes, `+0 m_retres, +4 m_isdvd`.
- TOC sizes from cdvdman: CD 1024 bytes (32 words × 8), DVD 2064 bytes (4 words × 129) (MAN:6313-6338). The DVD bytes cdvdman itself consumes are [14] and [21..23] (G-dual).
- ps2sdk-EE copies 1024 bytes when `m_isdvd≠0` and 2064 otherwise (EN:328-346). That looks inverted (G-toc).

#### 6.5 fnos 0x05–0x08: Seek / Standby / Stop / Pause

- **Seek request:** `+0 u32 m_lbn` (RPC:785-788). The others carry no data.
- Each handler performs the operation and waits for completion before returning:
  - `sceCdSync(4)`: ~30 s timeout (0x41EB0000 ticks).
  - `sceCdSync(6)`: the sync timeout, default 15 s.
- IOP details:
  - Seek applies the DVD layer-1 remap (MAN:7238-7252).
  - Standby on PS2 media is a 1-sector read of LSN 16; otherwise it is ncmd 0x02 (MAN:6226-6265).
  - Stop is ncmd 0x03; Pause is ncmd 0x04 (MAN:6267-6275).

#### 6.6 fno 0x0C: sceCdApplyNCmd

- **Request**, 20 bytes (RPC:797-803; EE struct RPC:48-53): `+0 u8 cmdNum` (the EE writes a u16), `+2 u16 inBuffSize` (0..16), `+4 u8 inBuff[16]`.
- **IOP:** `m_retres = sceCdApplyNCmd(...)`. It retries forever until the command is issued, so it returns 1, then calls `sceCdSync(2)` (FSV:2799-2806; MAN:6079-6088).
- **Reply:** the EE asks for 16 bytes; only `+0 m_retres` is meaningful.

#### 6.7 fno 0x0D: sceCdReadIOPMem

- **Request**, 24 bytes (RPC:805-813; EN:479-488): `+0 lbn, +4 sectors, +8 m_buf (IOP address), +12 mode, +16 unused` (ps2sdk puts `_rd_intr_data` there), `+20 m_eedest` (EE `&curReadPos`).
- **IOP** (FSV:427-475): starts `sceCdRE(lbn, sectors, buf, mode)`, which is async and applies the DVD remap internally. While it is busy, it DMAs `sceCdGetReadPos()` to `m_eedest` every ~8 ms.
- On a command failure it sets error READCFR. The data lands only in IOP RAM.
- **EE completion:** generic end function.

#### 6.8 fno 0x0E: sceCdNCmdDiskReady

- No request.
- **Reply** `+0`: **6** if any of the following holds, otherwise **2** (FSV:2848-2879):
  - `(status & 0xC0) != 0x40`
  - an IOP multi-sector read is in progress (`sceCdSC(0xFFFFFFFD)`)
  - the media mode does not match the disc (`sceCdSC(0xFFFFFFF4)`, G-mmode)
  - the disc type is still DETCT* (1..4)

#### 6.9 fno 0x0F: sceCdReadChain

- **Request**, 788 bytes (RPC:815-820; EN:533-579):
  - `+0 sceCdRChain m_readChain[65]`: 12 bytes each. The list ends at the first entry with any field = 0xFFFFFFFF; at most 64 entries.
  - `+780 sceCdRMode m_mode`
  - `+784 u32 m_eedest` (EE `&curReadPos`)
- **IOP** (FSV:1313-1411), per entry:
  - `buffer & 1`: `sceCdRE` into IOP memory, then `sceCdSync(0)`.
  - Otherwise: EE read via `cdvdfsv_chreadee`, which DMAs directly with **no head/tail handling** (so the EE buffer must be aligned) and applies the DVD remap.
  - After each entry, `readpos += sectors*secsize` (2048/2328/2340) and readpos is DMA'd to `m_eedest`.
  - Stops on error or on sceCdBreak (`m_break_cdvdfsv_readchain`).
- The IOP export `sceCdReadChain` is just a stub that returns 0 (MAN:7038-7044).

---

### 7. Completion, sync and callbacks (EE side)

- **No RPC exists for sceCdSync or sceCdCheckCmd.**
- **EE `sceCdSync(mode)`** (EN:772-791): mode 0 spins while `cbSema || sceSifCheckStatRpc(&clientNCmd)`. Mode 1 returns 1 if that is true, else 0.
- **`sceSifCheckStatRpc`** is true while the RPC packet is outstanding. The packet is freed in the RPC-end handler *after* the end function has run (SIFR:291-311, 677-691).
- **Before every NOWAIT N-cmd**, the EE sets `cbSema = 1` and `CdCallbackNum = fno` (e.g. EN:215-216).
- **End function `_CdGenericCallbackFunction(&CdCallbackNum)`** (EL:386-405), running in interrupt context:
  1. Sets `CdCallbackNum = fno` and calls `iSignalSema(nCmdSemaId)`.
  2. If a callback thread exists and `sceCdCallbackFunc` is set: `iSignalSema(callbackSemaId)`. `cbSema` stays 1 until the thread has run the user callback (EL:363-383).
  3. Otherwise: `cbSema = 0`, `CdCallbackNum = 0`.
- **The callback thread** exists only if `sceCdInitEeCB(prio, stack, size)` was called (EL:320-340). It waits on a **local EE semaphore**.
- **`sceCdCallback(func)`** (EL:342-353) only stores the pointer. It returns the old pointer, or 0 if `sceCdSync(1)` is busy.
- **There is no extra SID and no IOP→EE message for callbacks.**
- The reason passed by ps2sdk-EE is the **N-cmd fno**: 1 read, 2 CDDA, 5 seek, 6 standby, 7 stop, 8 pause, 13 IOPMem, 15 chain. That differs from the `SCECdFunc*` numbering (COM:266-275) that the IOP-side `sceCdCallback` uses (MAN:4110-4128, where internal 9 and 14 are reported as 1). See G-cb.
- **EE `sceCdGetReadPos`** (EN:595-603) returns uncached `curReadPos` only while `CdCallbackNum == 1`.
- **IOP `sceCdCheckCmd`** (export 21) returns `m_wait_flag`: 1 = idle, 0 = a command is executing (MAN:6090-6093).
- **HLE consequence:** for a NOWAIT N-cmd, do the work, write every EE-side result (data, 144-byte packet, readpos), and only then deliver the RPC-end. That ordering is what makes the EE end function run correctly.

---

### 8. SID 0x80000593: S-commands

- All S-commands are blocking on the EE.
- The EE serialises them with `sCmdSemaId` and refuses a call while the previous S-RPC is busy (ES:452-484).
- Many IOP handlers wait for cdvdman's S-cmd event flag and retry up to 3 times while the result is 0 (e.g. FSV:2458-2463).
- An unknown fno gives `m_retres = 0`; the rest of the reply buffer is stale (FSV:3210-3213).
- Reply structs: RPC:364-748. Only the first "recv" bytes go back.

| fno | EE API (ES line) | send | recv | request | reply | IOP semantics | build |
|---|---|---|---|---|---|---|---|
| 0x01 | sceCdReadClock (118) | 0 | 16 | – | `+0 retres, +4 sceCdCLOCK` | mechacon S-cmd 0x08. Sets `pad=0`, `month&=0x7F`. On failure or `stat≠0`, returns the cached clock, or a default (month 3, day 4, 05:06:07, year 0) (MAN:7934-7960) | base |
| 0x02 | sceCdWriteClock | 8 | 16 | sceCdCLOCK | `+0, +4 clock` | | OSD |
| 0x03 | sceCdGetDiskType (217) | 0 | 4 | – | `+0` disc type | register 0x1F40200F (MAN:2293-2307) | base |
| 0x04 | sceCdGetError (237) | 0 | 4 | – | `+0` last error (u8) | MAN:6154-6161. EE returns −1 if the RPC fails | base |
| 0x05 | sceCdTrayReq (257) | 4 | 8 | `+0 int param` | `+0 retres, +4 traychk` | Check(2): `traychk = 1` if media changed since the last check (**the first check after boot returns 1**), retres 1. Open/Close: S-cmd 0x06; ok iff the result byte is 0 (MAN:8004-8043) | base |
| 0x06 | sceCdRI (782) | 0 | 16 | – | `+0 retres, +4 status, +8 u8[8]` | S-cmd 0x12, i.Link ID (MAN:7268-7277) | base |
| 0x07–0x0A | WI / ReadNVM / WriteNVM / DecSet | | | | | | OSD |
| 0x0B | sceCdApplySCmd (281) | 20 | 16 | `+0 u8 cmd, +2 u16 len, +4 u8[16]` | 16 raw result bytes, **no retres** | IOP `sceCdApplySCmd(...,out)`, retrying for up to ~5 s; its return value is discarded (FSV:2790-2797, MAN:5405-5439). EE returns 1 if the RPC succeeded (ES:301) | base |
| 0x0C | sceCdStatus (305) | 0 | 4 | – | `+0` status | register 0x1F40200A with fix-ups; −1 while a power-off is pending (MAN:2347-2391) | base |
| 0x0D–0x14 | SetHDMode / Open / Close / ReadConfig / WriteConfig / Read & WriteConsoleID / MV | | | | | | OSD |
| 0x15 | sceCdCtrlADout (888) | 4 | 8 | `+0 int mode` | `+0 retres, +4 status` | S-cmd 0x14 (MAN:7592-7601) | base |
| 0x16 | sceCdBreak (327) | 0 | 4 | – | `+0` = **1 always** | IOP sceCdBreak: aborts the current N-cmd and sets last error 0x01 ABRT (MAN:5480-5554, FSV:352-360) | base |
| 0x17–0x19 | ReadSUBQ / ForbidDVDP / AutoAdjustCtrl | | | | | | OSD |
| 0x1A | sceCdRM (909) | 0 | 24 | – | `+0 retres, +4 status, +8 char[16]` | model name | base |
| 0x1B–0x20 | WM / ForbidRead / SpinCtrlEE / BootCertify / CancelPOffRdy / BlueLedCtrl | | | | | | OSD |
| 0x21 | sceCdPowerOff (389) | 0 | 8 | – | `+0 retres, +4 result` | S-cmd 0x0F. Older models close the tray first. Afterwards sceCdStatus returns −1 for 5 s (MAN:7560-7590) | base |
| 0x22 | sceCdMmode (410) | 4 | 4 | `+0 int media` (+4 `char m_char4`) | `+0` = 1 | Executed only if `len==4 \|\| m_char4==0`, otherwise the reply is stale (FSV:1937-1943). Sets mmode (MAN:7506-7510) | base |
| 0x23 | sceCdChangeThreadPriority (431) | 4 | 4 | `+0 int prio` | `+0`: **0 on success**, −403 if outside 9..123 | re-prioritises the cdvdfsv threads (FSV:315-332) | base |
| 0x24 | (sceCdReadGUID) | 0 | 16 | – | `+0 retres, +8 u64` | | DNAS |
| 0x25 | (sceCdSetTimeout) | 8 | 4 | `+0 int param` (1 = read/sync ms, 2 = stream ms), `+4 int ms` | `+0` 1/0 | fails while busy. Defaults 15000 / 5000 ms (MAN:264-265, 2066-2083) | base |
| 0x26 | (sceCdReadModelID) | 0 | 8 | – | `+0, +4 u32` | | DNAS |
| 0x27 | (sceCdReadDvdDualInfo) | 0 | 12 | – | `+0 retres, +4 int on_dual, +8 u32 layer1_start` | G-dual (MAN:4517-4599, FSV:2839-2846) | base |
| 0x28+ | wake-up time, PS1 boot param, fan, … | | | | | | OSD / XOSD |

Fno numbers: ES:38-74 and FSV:2994-3209. API names in parentheses are not implemented in ps2sdk-EE; their sizes come from the RPC structs.

---

### 9. Streaming: fno 0x09 to cdvdstm

- cdvdfsv forwards the raw request: `devctl("cdrom_stm0:", 0x4396, req, len, &g_crr.m_retres, 4)`. If devctl fails, retres is 0 (FSV:2931-2934).
- This requires cdvdstm (IRX "cdvd_st_driver" v2.2, STM:18).

**Request** = the first 20 bytes of `cdrom_stm_devctl_t` (IOC:61-69; EN:716-722):

| off | field | meaning |
|---|---|---|
| 0 | u32 m_posszarg1 | lbn (Start/Seek/SeekF); `bufmax` = total ring **sectors** (Init) |
| 4 | u32 m_posszarg2 | sectors to read (Read); `bankmax` (Init) |
| 8 | u32 m_buffer | EE destination (Read); **IOP** ring-buffer address (Init) |
| 12 | u32 m_cmdid | 1 Start, 2 Read, 3 Stop, 4 Seek, 5 Init, 6 Stat, 7 Pause, 8 Resume, 9 SeekF (EN:36-46) |
| 16 | sceCdRMode m_rmode | only used by Start; ps2sdk sends a zeroed `dummyMode` otherwise |
| 20 | u32 m_error | not sent |

The ring holds `bankmax` banks of `bufmax/bankmax` 2048-byte sectors in IOP RAM. Handler: `ee_stream_handler_normal`, STM:750-1019.

| cmd | EE API | effect | retres |
|---|---|---|---|
| 5 Init | `sceCdStInit(bufmax, bankmax, iopbuf)` | sets the ring geometry; `bankmax` must be > 0 (STM:816-832) | 1 |
| 1 Start | `sceCdStStart(lbn, mode)` | stores the mode, resets the ring, sets `lsn = lbn`, sets the stream flag to 2, and starts the first read with `sceCdNop` (STM:853-920) | 1 / 0 |
| 2 Read | `sceCdStRead` | copies up to N sectors from the ring to EE `m_buffer` by synchronous SIF-DMA and frees consumed banks (STM:921-1003) | low 16 bits = sectors copied; high 16 bits = error: the read error code, or 0x111 if the stream is not running and nothing was copied (STM:1004-1017) |
| 3 Stop | `sceCdStStop` | break, clear the ring, clear the error | 1 |
| 4 Seek | `sceCdStSeek(lbn)` | restart at `lbn` with the mode unchanged | 1 |
| 6 Stat | `sceCdStStat` | sectors buffered, counted in whole banks (STM:796-815) | n |
| 7 Pause | `sceCdStPause` | stream flag 0, wait for idle | 1 |
| 8 Resume | `sceCdStResume` | stream flag 2, restart reads | 1 / 0 |
| 9 SeekF | `cdrom0:` lseek on a stream fd | if streaming: set lsn, clear the ring, enter state 2 | 1 / 0 |

- In state 2 (after SeekF), only commands 3 and 9 work; all others return 0 (STM:770-774).
- Background refill (STM:1021-1179) reads one bank per pass with `sceCdRE`, so the DVD remap is applied. On a read error it retries at `lsn ± 16k`.
- **EE `sceCdStRead`** (EN:615-657):
  - STMNBLK: one call. Returns the low 16 bits; `*error` = the high 16 bits.
  - STMBLK: loops until all sectors are read, or an error occurs with zero progress.
  - Stream sectors are always 2048 bytes (EN:625, STM:860).
- **CDDA stream** (fno 0x0A → devctl 0x4398, STM:1181-1488): same commands, with 2352/2368/2448-byte sectors chosen by datapattern. ps2sdk-EE sends byte counts and an EE "init buffer", which disagree with the IOP code (EN:746-758). Low priority.
- **IOP-side `sceCdSt*`** (MAN:2536-2661) use devctl 0x4393 with the same command ids. `sceCdStRead` uses devctl 0x4394 and loops in blocking mode (STM:176-213, 618-656). The IOP stream uses stream flag 1 and the EE stream uses flag 2; the two are mutually exclusive.

---

### 10. IOP libraries

#### cdvdman

- Module IRX_ID **"cdvd_driver" v2.38** (MAN:25).
- Export library **"cdvdman" v1.1** (MANX:2). It also registers the iomanX device "cdrom" (MAN:263).
- The export order in MANX was verified mechanically against the `DECLARE_IMPORT` numbers in MANH:226-336. These numbers must match Sony's ROM cdvdman, because homebrew imports from rom0 with them.

| # | function | C signature (source) |
|---|---|---|
| 0 | _start | `int _start(int ac, char **av)` (MAN:366) |
| 1, 3 | _retonly | |
| 2 | cdvdman_termcall | `void cdvdman_termcall(int with_stop)` (MAN:494) |
| 4 | sceCdInit | `int sceCdInit(int mode)` (COM:636) |
| 5 | sceCdStandby | `int sceCdStandby(void)` (COM:421) |
| 6 | sceCdRead | `int sceCdRead(u32 lbn, u32 sectors, void *buffer, sceCdRMode *mode)` (COM:389). Async; returns 1 if started, 0 if busy or the fio/fsv lock is held (MAN:6858-6875) |
| 7 | sceCdSeek | `int sceCdSeek(u32 lbn)` (COM:414) |
| 8 | sceCdGetError | `int sceCdGetError(void)` (COM:491) |
| 9 | sceCdGetToc | `int sceCdGetToc(u8 *toc)` (COM:398) |
| 10 | sceCdSearchFile | `int sceCdSearchFile(sceCdlFILE *file, const char *name)` (COM:651) |
| 11 | sceCdSync | `int sceCdSync(int mode)` (COM:643) |
| 12 | sceCdGetDiskType | `int sceCdGetDiskType(void)` (COM:485) |
| 13 | sceCdDiskReady | `int sceCdDiskReady(int mode)` (COM:658) |
| 14 | sceCdTrayReq | `int sceCdTrayReq(int param, u32 *traychk)` (COM:499) |
| 15 | sceCdStop | `int sceCdStop(void)` (COM:428) |
| 16 | sceCdPosToInt | `u32 sceCdPosToInt(sceCdlLOCCD *p)` (COM:664) |
| 17 | sceCdIntToPos | `sceCdlLOCCD *sceCdIntToPos(u32 i, sceCdlLOCCD *p)` (COM:661) |
| 21 | sceCdCheckCmd | `int sceCdCheckCmd(void)` (MANH:142) |
| 22 | sceCdRI | `int sceCdRI(u8 *buffer, u32 *status)` (COM:807) |
| 24 | sceCdReadClock | `int sceCdReadClock(sceCdCLOCK *clock)` (COM:469) |
| 28 | sceCdStatus | `int sceCdStatus(void)` (COM:505) |
| 29 | sceCdApplySCmd | `int sceCdApplySCmd(u8 cmdNum, const void *inBuff, u16 inBuffSize, void *outBuff)` (COM:567) |
| 37 | sceCdCallback | `sceCdCBFunc sceCdCallback(sceCdCBFunc function)` (COM:683). Called from interrupt with `SCECdFunc*`; returns 0 if busy (MAN:3854-3867) |
| 38 | sceCdPause | `int sceCdPause(void)` (COM:435) |
| 39 | sceCdBreak | `int sceCdBreak(void)` (COM:511) |
| 40 | sceCdReadCDDA | `int sceCdReadCDDA(u32 lbn, u32 sectors, void *buffer, sceCdRMode *mode)` (COM:391) |
| 44 | sceCdGetReadPos | `u32 sceCdGetReadPos(void)`: bytes transferred so far (COM:667, MAN:6385-6409) |
| 45 | sceCdCtrlADout | `int sceCdCtrlADout(int mode, u32 *status)` (COM:841) |
| 46 | sceCdNop | `int sceCdNop(void)` (MANH:143) |
| 47 | sceGetFsvRbuf | `void *sceGetFsvRbuf(void)`: 42128-byte IOP scratch buffer that cdvdfsv uses for TOC and raw sectors (MANH:144, MAN:362, 394-397) |
| 48 / 49 | sceCdstm0Cb / sceCdstm1Cb | `int sceCdstm0Cb(void (*p)(int val))` (MANH:145-146) |
| 50 | sceCdSC | `int sceCdSC(int code, int *param)` (MANH:147) |
| 51 | sceCdRC | `int sceCdRC(sceCdCLOCK *clock)`: raw clock, no month masking (MANH:148-151) |
| 54 | sceCdApplyNCmd | `int sceCdApplyNCmd(u8 cmdNum, const void *inBuff, u16 inBuffSize)` (COM:457) |
| 56 | sceCdStInit | `int sceCdStInit(u32 bufmax, u32 bankmax, void *buffer)` (COM:609) |
| 57 | sceCdStRead | `int sceCdStRead(u32 sectors, u32 *buffer, u32 mode, u32 *error)` (COM:587) |
| 58 | sceCdStSeek | `int sceCdStSeek(u32 lbn)` (COM:600) |
| 59 | sceCdStStart | `int sceCdStStart(u32 lbn, sceCdRMode *mode)` (COM:577) |
| 60 | sceCdStStat | `int sceCdStStat(void)` (COM:615) |
| 61 | sceCdStStop | `int sceCdStStop(void)` (COM:593) |
| 62 | sceCdRead0 | `int sceCdRead0(u32 lsn, u32 sectors, void *buffer, sceCdRMode *mode, int csec, void *callback)`: raw, takes a **drive** LSN (MANH:152) |
| 63 | sceCdRV | `int sceCdRV(u32 lsn, u32 sectors, void *buf, sceCdRMode *mode, int arg5, void *cb)`: raw 2064-byte DVD sectors (MANH:159, MAN:7127-7177) |
| 64 | sceCdRM | `int sceCdRM(char *buffer, u32 *status)` (COM:873) |
| 66 | sceCdReadChain | `int sceCdReadChain(sceCdRChain *tag, sceCdRMode *mode)`: **stub, returns 0** (MAN:7038-7044) |
| 67 / 68 | sceCdStPause / sceCdStResume | `int (void)` (COM:621, 627) |
| 74 | sceCdPowerOff | `int sceCdPowerOff(u32 *status)` (COM:539) |
| 75 | sceCdMmode | `int sceCdMmode(int media)` (COM:548) |
| 77 | sceCdStSeekF | `int sceCdStSeekF(unsigned int lsn)` (COM:913) |
| 78 | sceCdPOffCallback | `void *sceCdPOffCallback(void (*func)(void *userdata), void *userdata)` (COM:922) |
| 79 / 80 / 82 | sceCdReadDiskID / sceCdReadGUID / sceCdReadModelID | DNAS build only; `_retzero` otherwise (MANX:82-94) |
| 81 | sceCdSetTimeout | `int sceCdSetTimeout(int param, int timeout)` (COM:931) |
| 83 | sceCdReadDvdDualInfo | `int sceCdReadDvdDualInfo(int *on_dual, unsigned int *layer1_start)` (COM:948) |
| 84 | sceCdLayerSearchFile | `int sceCdLayerSearchFile(sceCdlFILE *fp, const char *path, int layer)` (COM:958) |
| 112 | sceCdApplySCmd2 | `int sceCdApplySCmd2(u8 cmdNum, const void *inBuff, unsigned long inBuffSize, void *outBuff)` (MANH:169) |
| 114 | sceCdRE | `int sceCdRE(unsigned int lsn, unsigned int sectors, void *buf, sceCdRMode *mode)`: sceCdRead without the fio/fsv lock check (COM:976, MAN:6853-6856) |
| 179 | sceCdDoesUniqueKeyExist | DNAS build only |

All other slots 0..190 are `_retzero` in the default build. That includes the MANH names 19 GetToc2, 20 ReadDVDV, 23 WI, 25 WriteClock, 26/27 NVM, 30–36, 41–43, 52, 53, 55, 65, 69–73, 76 ReadFull, 90 Status2 and 109/110 wake-up (MANH:240-336).

**IOP `sceCdSync(mode)`** (MAN:2085-2181) returns 0 when done; modes 1 and 17 return 1 while busy.

| mode | behaviour |
|---|---|
| 0 | block until idle and no multi-sector read |
| 1 | poll |
| 3 / 6 | block, with the sync timeout (15 s) followed by sceCdBreak |
| 4 | block, with a ~30 s timeout |
| 5 | block until command idle (ignores the multi-sector read flag) |
| 16 / 17 | block / poll, also waiting for EE-RPC-busy and streaming |
| 32 | internal per-chunk wait |

**cdvdman exports that cdvdfsv imports** (FSVI:77-115), by export number:
- 4 Init, 5 Standby, 7 Seek, 8 GetError, 9 GetToc, 10 SearchFile
- 11 Sync, 12 GetDiskType, 13 DiskReady, 14 TrayReq, 15 Stop, 16 PosToInt
- 22 RI, 24 ReadClock, 28 Status, 29 ApplySCmd
- 38 Pause, 39 Break, 40 ReadCDDA
- 44 GetReadPos, 45 CtrlADout, 47 sceGetFsvRbuf, 50 sceCdSC, 54 ApplyNCmd
- 62 Read0, 63 RV, 64 RM
- 74 PowerOff, 75 Mmode
- 79 ReadDiskID, 80 ReadGUID, 81 SetTimeout, 82 ReadModelID, 83 ReadDvdDualInfo, 84 LayerSearchFile
- 114 RE, 179 DoesUniqueKeyExist

It does **not** import sceCdRead (#6). It also imports iomanX `devctl` for streaming (FSVI:73-75).

**`sceCdSC` codes used by cdvdfsv** (MAN:4601-4765):

| code | purpose |
|---|---|
| `0xFFFFFFF2` | get `&internal struct` |
| `0xFFFFFFE7` | S-cmd event-flag id |
| `0xFFFFFFF7` | cdvdman version |
| `0xFFFFFFF6` | EE-RPC-busy fno (takes/releases the fio/fsv lock) |
| `0xFFFFFFF1` | sync timeout in ms |
| `0xFFFFFFEE` | record a read timeout |
| `0xFFFFFFFE` | set last error |
| `0xFFFFFFEA` | `DvdDual_infochk` |
| `0xFFFFFFE9` | `sceCdLsnDualChg(lsn)`: *returns* the new LSN; does not write `*param` |
| `0xFFFFFFFC` | cd36key |
| `0xFFFFFFFD` | multi-sector read flag |
| `0xFFFFFFF4` | media mode OK |
| `0xFFFFFFF5` | interrupt event-flag id |
| `0xFFFFFFF8` | spin control |
| `0xFFFFFFE8` | not handled; returns 0 |

#### cdvdfsv and cdvdstm

- **cdvdfsv:** IRX "cdvd_ee_driver" **v2.38** (FSV:17). Library "cdvdfsv" v1.1 (FSVX):
  - 0 `_start`; 1–3 `_retonly`
  - 4 `int *cdvdfsv_dummyentry(int)`: returns `&verbose` if arg == 128
  - 5 `int sceCdChangeThreadPriority(int priority)`
- **cdvdstm:** IRX "cdvd_st_driver" v2.2. Library "cdvdstm" v1.1:
  - 0 `_start`; 1 `_retonly`; 2 `cdvdstm_termcall`; 3 `_retonly`; 4 `cdvdstm_dummyentry` (STM exports.tab)

---

### 11. Constants (COM)

| enum | values |
|---|---|
| Disc type (109-140) | NODISC 0x00, DETCT 0x01, DETCTCD 0x02, DETCTDVDS 0x03, DETCTDVDD 0x04, UNKNOWN 0x05, PSCD 0x10, PSCDDA 0x11, **PS2CD 0x12**, PS2CDDA 0x13, **PS2DVD 0x14**, DVDVR 0xFC, CDDA 0xFD, DVDV 0xFE, IllegalMedia 0xFF, GDTFUNCFAIL −1 |
| Drive status (171-186) | Stop 0x00, ShellOpen 0x01, Spin 0x02, Read 0x06, Pause 0x0A, Seek 0x12, Emg 0x20. Bitwise: 1 = open, 2 = spin, 4 = read, 8 = pause, 0x10 = seek, 0x20 = error |
| Error (69-107) | FAIL −1, NO 0x00, ABRT 0x01, CMD 0x10, OPENS 0x11, NODISC 0x12, NORDY 0x13, CUD 0x14, IPI 0x20, ILI 0x21, PRM 0x22, READ 0x30, TRMOPN 0x31, EOM 0x32, SFRMTNG 0x38, READCF 0xFD, READCFR 0xFE |
| Sector type (25-35) | S2048 0 / S2328 1 / S2340 2. CDDA: S2352 0 / S2368 1 / S2448 2 |
| Spin (37-62) | Max = Stm = DvdDL0 = 0, Nom 1, X1 2, X2 3, X4 4, X12 5, Nm2 10, 1p6 11, Mx 20 |
| Mmode (64-67) | Cd 1, Dvd 2. Internal 0xFF = any (MAN:4696) |
| Tray (161-168) | Open 0, Close 1, Check 2 |
| Init (280-287) | INIT 0, INoD 1, EXIT 5 |
| Ready / interrupt (142-158) | SCECdComplete 2, SCECdNotReady 6 |
| Callback reason (266-275, IOP numbering) | Read 1, ReadCDDA 2, GetToc 3, Seek 4, Standby 5, Stop 6, Pause 7, Break 8 |
| Stream mode (363-368) | STMNBLK 0, STMBLK 1 |
| Limits | CdlMAXTOC 100 (264); CdlMAXFILE 64, CdlMAXDIR 128, CdlMAXLEVEL 8 (290-295); SCE_CdSTREAM open flag 0x40000000 (23) |

`SCECdRMode` field meanings are in §2. There are no other `sceCdMmode` values.

---

### 12. Gotchas and ambiguities

**G-search: sceCdSearchFile path rules.**
- The path must start with `\` (MAN:2689).
- The IOP copies at most 1023 characters (MAN:2200-2204); the EE packet holds 255 (RPC:101, EL:207).
- At most 8 levels (MAN:2696, 2710).
- **Directories** come from the ISO L-path-table: its first 2048 bytes only, at most 128 directories. The location is taken from PVD byte 140 on CD and is **hard-coded to sector 257 on DVD** (MAN:2855, 2861, 2864, 2870-2875).
- Directories are matched by (parent number, exact `strcmp`) (MAN:2900-2912). The root name is "" (path-table root entry; the leading `\`).
- **Files** are searched only in the **first 2048-byte sector** of the directory, and only among its first 64 records (MAN:2924, 2932-2935).
- File names are compared with `strncmp(record, request, 12)` (MAN:2781-2784). So matching is **case-sensitive**, the `;1` suffix is **required** (e.g. `\SYSTEM.CNF;1`), and for names longer than 12 characters only the first 12 count.
- No case folding is done, and no `;1` is added automatically. (The `cdrom0:` iomanX open path does append `;1`; MAN:721-732, 1266.)
- A concurrent search makes the call return 0 immediately (MAN:2196-2199).
- The PVD must contain "CD001" (MAN:2843). Results are cached until the media changes (MAN:2671-2688).
- **Decompilation slip:** the component-split loop (MAN:2696-2709) advances one character per iteration and its inner loop has no NUL check. As written it cannot parse `\A\B;1`. Implement the intended split instead: components separated by `\`, directories walked from the root, last component = file.

**G-date.** `sceCdlFILE.date[0]` is never written by this cdvdman; the ISO flags go to the int after the struct. The comment at COM:216 ("0=iso file flags") may reflect a different Sony version. Unverified.

**G-dual: dual-layer DVD.**
- **Layer-1 start:** `L1 = (TOC[21]<<16 | TOC[22]<<8 | TOC[23]) − 0x30000 + 1` (MAN:4359-4362, 4383).
- **Dual flag:** dual iff `TOC[14] & 0x60`. `m_opo_or_para = (TOC[14] & 0x10) ? 2 : 1` (MAN:4384). The DVD convention that bit 4 = OTP is not stated in the source.
- **`sceCdReadDvdDualInfo`:** returns `on_dual = (m_opo_or_para != 0)` and `layer1_start = L1`. A non-DVD disc gives `(0, 0)` with retres 1 (MAN:4517-4541).
- **Logical → drive LSN:** `sceCdLsnDualChg` subtracts **16** when the disc is dual-layer and `lsn ≥ L1` (MAN:4503-4507). It is applied exactly once on every DVD read/seek path:
  - EE fno 1 and chain reads (FSV:1022, 1254)
  - IOP `sceCdRE`/`sceCdRead` (MAN:6765)
  - `sceCdSeek` (MAN:7249)
  - streaming, through `sceCdRE` (cdvdstm discards its own `sceCdSC(0xFFFFFFE9)` results, STM:344, 907)
  - `sceCdRead0` and `sceCdRV` take drive LSNs.
- **`sceCdLayerSearchFile(layer=1)`** on PS2DVD uses `base = L1`. The PVD is read at logical `base+16`, which is drive `L1`; the path table at `base+257`; directories at `base+extent`. The returned `lsn = base + extent` (MAN:2815-2825, 2864, 2924, 2737). For CDs the layer argument is effectively ignored (`base = 0`).
- **Derived (not stated in source)** for a flat image where image sector = drive LSN: report `L1` = the image index of the layer-1 PVD. A layer-1 file with extent `e` is then at image sector `L1 + e − 16`.
- The sector-ID check adds `L1` for layer-1 sectors when `m_opo_or_para == 1` (FSV:519-525, MAN:6696). This only matters for raw-sector emulation.
- The "Master Disc" two-disc dual-layer emulation (MAN:4411-4501, 4542-4598) is a dev feature.

**G-mmode.**
- mmode starts at 0 (MAN:293). The boot-mode-4 record sets 0xFF (values 0/1/2) or CD (value 3) (MAN:4689-4704).
- Reads are refused unless mmode matches the disc: CD types need 1, PS2DVD needs 2, or mmode is 0xFF (MAN:6603-6627, 7097-7102, 7140-7142). fno 0x0E returns 6 on a mismatch, so EE `sceCdRead` returns 0.
- Games call `sceCdMmode` after `sceCdInit`.

**G-unaligned / version.**
- cdvdfsv ≤ v1.04 used 16-byte head/tail buffers (16-byte alignment, 48-byte packet).
- ps2sdk-EE picks the layout from the version that sceCdInit reports, or probes with fno 2 (EN:153, 831-866).
- Sony EE libs are presumably built for one layout (inferred). Prefer reporting 0x0226.

**G-toc.** ps2sdk-EE's copy size looks inverted relative to `m_isdvd` (EN:330 vs FSV:1841-1851). The IOP always DMAs 0x810 bytes.

**G-cdda.** In `cdvdfsv_rpc5_02_readcdda`, the guard `error_code_tmp != NO && != EOM && != SFRMTNG` (FSV:1659, 1668, 1737) is always false, because the variable only ever holds those values. As written, no data is transferred; this is almost certainly a decompilation slip. The intended behaviour is "proceed unless EOM/SFRMTNG". Also, S2448 is treated as 2352 on the IOP (FSV:1618-1622), while the EE assumes 2448 (EN:287-288).

**G-cb.** The callback reason seen by EE callbacks is the EE library's own number (ps2sdk: N-cmd fno), not the IOP's `SCECdFunc*` value. The EE library is linked into the game, so HLE needs nothing here beyond delivering RPC completion.

**G-prio.** S-cmd 0x23 replies 0 on success (FSV:331), while COM:550-557 documents "1 on success".

**G-mmode-pkt.** S-cmd 0x22 with a request longer than 4 bytes and a nonzero byte at +4 does nothing and returns a stale reply (FSV:1939).

**G-tray.** The first `SCECdTrayCheck` after boot reports "changed" (`chflags` start at 1, MAN:319, 6115-6123).

**G-busy.**
- IOP `sceCdRead` (export 6) returns 0 while an EE N-cmd, a search, or `cdrom0:` I/O holds the fio/fsv lock (MAN:6858-6875).
- `cdvdman_read` also refuses while the drive is not ready or a multi-sector read is already running (MAN:6784-6801).
- EE calls return 0 instead of queueing.

**G-errors.**
- Read errors only reach the EE through S-cmd 0x04.
- EOM is rewritten to NO in the interrupt handler (MAN:3925-3928).
- Timeouts are counted in IOP ticks: `0x9000` per ms (36.864 MHz; FSV:440, MAN:2104).

**G-zero.** fno 1 with 0 sectors and a misaligned `paddr` gives a negative `bsize` (FSV:1055-1057). Special-case it.

**G-applyscmd.** `inBuffSize` is not clamped to 16 on the IOP (FSV:2795-2796, MAN:4977-4980).

**ps2sdk-EE-only quirks** (relevant only when testing with ps2sdk-built homebrew):
- `sceCdReadKey` does not `SignalSema` on success (EN:883-891).
- `sceCdStStat`, `sceCdStPause` and `sceCdStResume` zero `streamStatus`, so later `sceCdStRead` calls return 0 (EN:682-704, 623-624).
- The search packet is always 0x124.

### 13. HLE notes (recommendations, not sourced)

1. **Servers:** register 0x592, 0x593, 0x595, 0x597, 0x59A and 0x59C. Answer unknown fnos with retres 0, as the real server does.
2. **sceCdRead (fno 1):** the simplest correct approach is layout-independent:
   - Write all `total` bytes directly to EE `paddr`.
   - DMA a 144-byte packet with `b1len = b2len = 0` and `dst = 0`. Lengths sit at the same offsets in both layouts, so the EE copies nothing.
   - Set readpos = total, then fire the RPC-end.
3. **Defaults:** treat mmode as 0xFF by default. Disc type 0x14 for DVD images, 0x12 for CD (0x13 with CDDA). Status 0x0A when idle with a disc, 0x06 while reading, 0x00 after Stop.
4. **Raw CD reads:** 2340-byte reads from 2048-byte ISO images need a synthesized header (BCD MSF of lsn+150, mode 2), an 8-byte subheader, the data, and padding.
5. **EE address fields** (`m_eedest`, `m_paddr`, …) are EE virtual pointers exactly as the EE code had them. Map them the way the EE DMAC would see them, i.e. physical, typically `addr & 0x1FFFFFFF`.

---

## Appendix B: memory card protocol (MCSERV / XMCSERV, MCMAN / XMCMAN)

Extracted from the ps2sdk sources (AFL 2.0) by a research agent, with the same conventions as Appendix A. The runtime implements this in `runtime/src/iop/memcard.cpp`; `docs/runtime.md` (Memory cards) lists what is left out. One correction from testing: `mcOpen` with newlib's `O_RDONLY` (0) cannot read, on hardware and in the HLE alike, because the flags are the IOP's.

Extracted from the ps2sdk sources (AFL 2.0). Only interface facts are recorded here: numbers, layouts and semantics.
All citations are `ABBR:line`, with paths relative to the ps2sdk root:

| Abbr | Path | Abbr | Path |
|---|---|---|---|
| LMC | ee/rpc/memorycard/src/libmc.c | SRV | iop/memorycard/mcserv/src/mcserv.c |
| LMH | ee/rpc/memorycard/include/libmc.h | SRVH | iop/memorycard/mcserv/src/mcserv.h |
| EX | ee/rpc/memorycard/samples/mc_example.c | SEXP | iop/memorycard/mcserv/src/exports.tab |
| CMN | common/include/libmc-common.h | MAN | iop/memorycard/mcman/src/main.c |
| IOC | common/include/io_common.h | FIO | iop/memorycard/mcman/src/ps2mc_fio.c |
| ESIF | ee/kernel/src/sifrpc.c | SIO | iop/memorycard/mcman/src/mcsio2.c |
| ISIF | iop/system/sifcmd/src/sifrpc.c | DEV | iop/memorycard/mcman/src/mcdev.c |
| KER | ee/kernel/include/kernel.h | MANI | iop/memorycard/mcman/src/mcman-internal.h |
| | | MANH / MEXP | iop/memorycard/mcman/include/mcman.h / mcman/src/exports.tab |

Tags: **[D]** means derived by tracing code paths rather than stated directly. **[?]** means Sony's original modules may differ, or the fact is unverified.

---

### 1. Transport, binding and protocol selection

| Fact | Value | Cite |
|---|---|---|
| RPC server ID (SIO2 memory cards) | `0x80000400` | SRV:36, LMC:354 |
| RPC server ID (xfromserv, DEV9 external flash, arcade only) | `0x80000480` | SRV:32, LMC:357 |
| Server receive buffer | 2048 bytes | SRV:25, SRV:122 |
| Reply buffer | always `&rpc_stat`, a `mcRpcStat_t {s32 result; u32 mcserv_version; u32 mcman_version}` (12 B) | SRV:26, SRV:253, CMN:144-149 |
| Reply bytes copied to the EE | `rsize` chosen by the EE: **4** for every call, **12** for XMC init (0xFE) | LMC:222-223, 389-390, 399, 453; ISIF:554, 562 |
| Bind | `sceSifBindRpc(cd, 0x80000400, 0)` in a loop until `cd->server != NULL`. If nothing is registered, the loop spins forever | LMC:367-380 |
| `mcInit(type)` argument | ignored; the protocol is auto-detected (MC_TYPE_MC=0 / MC_TYPE_XMC=1 kept only for compatibility) | LMC:848-855, LMH:196-199 |

#### Detection sequence in ps2sdk libmc (all calls blocking, mode 0)
1. It primes a stale result. It calls fno **0x0A** (XMC flush) and then fno **0x7A** (MC flush), each with `fd=0xFFFFFFFF` and rsize 4. The real server answers -5 (`sceMcResDeniedPermit`, invalid fd). (LMC:388-390, MAN:696-697)
2. It calls fno **0xFE** with rsize 12. The server does not touch `rpc_stat.result` for an unknown fno (SRV:241-245), so an old MCSERV still returns the stale -5.
   - If the SIF call fails, `err = ret-100` (LMC:399-406).
   - If `result == -5`, the call was unhandled and libmc falls back to MC (LMC:409-412, 440-445).
   - Otherwise libmc requires `mcserv_version >= 0x205`, or it fails with -120, and `mcman_version >= 0x206`, or it fails with -121. On success `mcInit` returns `result` (LMC:416-438, 482).
3. Fallback: fno **0x70** with `desc.offset = -217` and rsize 4. If `result == -5`, init fails with -122. Otherwise `mcInit` returns `result` (LMC:450-471).

#### What the server answers to init
- For 0x70 and 0xFE, `result = McRetOnly(fd) = 0` (SRV:140-145, 404-422; MAN:3232-3238).
- An XMCSERV build also fills `mcserv_version = 0x208` (MODVER) and `mcman_version = 0x20b`, the mcman IRX version (SRV:266-272, SRVH:31, MAN:20, MANI:69).
- An old-MCSERV build sets `McSetPS1CardFlag(offset == -217)` (SRV:410-419).

| EE sends | Real XMCSERV | Real old MCSERV |
|---|---|---|
| 0x0A, fd=-1 | -5 | stale (unknown fno) |
| 0x7A, fd=-1 | -5 | -5 |
| 0xFE | `{0, 0x208, 0x20b}` | stale -5, so the client falls back |
| 0x70, offset=-217 | 0 | 0 |

- **HLE recommendation [D]:** serve both fno sets from one server, as XMCSERV does (SRV:138-246). Answer 0xFE with `{0, ≥0x205, ≥0x206}`.
- ps2sdk comments say SDK 3.0.3 and 3.1.0 ship MCSERV 2.10 and MCMAN 2.30 (SRV:13-16, MAN:15-19). Reporting `0x210` / `0x230` should therefore pass a Sony minimum check.
- [?] Sony's thresholds are not in ps2sdk. ps2sdk's -101/-120/-121 values look like Sony's sceMcIni* codes.

#### Completion model (needed by your SIF layer)
- Every API call except init uses `SIF_RPC_M_NOWAIT` (LMC:216-226). Only one call can be in flight at a time. While a call is pending, the next API call returns the pending command number instead of sending (LMC:197-200).
- **End functions** run on the EE inside the SIF `RPC_END` handler, before the call is marked done (ESIF:291-302).
  - Functions: GetInfo → `mcGetInfoApdxOld/New`; Read → `mcReadFixAlignOld/New`; ChDir → `mcStoreDir`; ReadPage → `libmc_ReadAlignFunction` (LMC:512, 563, 628, 752).
  - The EE sends a call without an end function with `rmode=0` (ESIF:195-197). The IOP then completes it by DMAing the reply and overwriting the EE call packet with `rpc_id=0, rec_id=0`. `sceSifCheckStatRpc` then returns 0 (ISIF:558-582, ESIF:677-691).
  - With `rmode=1` the IOP sends `SIF_CMD_RPC_END` and the reply instead (ISIF:552-557).
- **Critical [D]:** the EE never sends the `type`, `free` and `format` destination pointers to the IOP. Only the EE end function writes them, from the endParam the server DMA'd (LMC:246-279, 485-513). If your SIF emulation never calls `cd->end_function(cd->end_param)`, `mcGetInfo` outputs are never written. The same applies to unaligned read bytes, unless you use the shortcut in §3.4.
- The end function reads the endParam through `UNCACHED_SEG(addr) = addr | 0x20000000` (LMC:509-510, 561; KER:36-37). The request, however, carries the cached address (LMC:504, 558). Your memory map must alias 0x2xxxxxxx to RAM.
- Every EE address in a request (`buffer`, `param`, `mcT`, `curdir`) is the raw EE pointer. Treat it as physical RAM, with the segment bits masked [D].
- `mcSync(mode, *cmd, *result)` (LMC:789-829):
  - Returns -1 if nothing is pending. Otherwise it polls `sceSifCheckStatRpc`; mode 0 busy-waits.
  - Returns 0 while the call is still running. Returns 1 when done, and sets `*result` to the 4-byte reply (`rpc_stat.result`).
  - ps2sdk has no `mcCheck`. `mcSync(MC_NOWAIT=1, …)` is the poll (LMH:39-40, 448-455). `*cmd` is computed EE-side; see §6.

---

### 2. Function numbers (fno)

Source: the table at LMC:53-95 and the server dispatch at SRV:138-246. Both tables are indexed by the same `MC_RPCCMD_*` index. Indices 14-16 are overloaded: in MC they are erase/readpage/writepage, in XMC they are getentspace/chgprio/checkblock (LMC:29-51). libmc gates the protocol-specific calls: they return -1 without any RPC on the wrong protocol (LMC:189-201).

| Operation (API) | MCSERV fno | XMCSERV fno | Param struct | EE end fn | Notes |
|---|---|---|---|---|---|
| init (`mcInit`) | 0x70 | 0xFE | desc | – | blocking; reply 4 B (MC) or 12 B (XMC) |
| getinfo (`mcGetInfo`) | 0x78 | 0x01 | desc | yes | flag fields and endParam size differ (§3.3) |
| open (`mcOpen`) | 0x71 | 0x02 | name | – | |
| mkdir (`mcMkDir`) | 0x71 | 0x02 | name | – | open with flags `0x40` (LMC:605-613) |
| close | 0x72 | 0x03 | desc | – | |
| seek | 0x75 | 0x04 | desc | – | |
| read | 0x73 | 0x05 | desc | yes | 16-B vs 64-B alignment; endParam 64 vs 192 B |
| write | 0x74 | 0x06 | desc | – | |
| flush | 0x7A | 0x0A | desc | – | |
| chdir (`mcChdir`) | 0x7B | 0x0C | name | yes | |
| getdir (`mcGetDir`) | 0x76 | 0x0D | name | – | |
| setfileinfo | 0x7C | 0x0E | name | – | |
| rename (`mcRename`) | – | 0x0E | name | – | setfileinfo with `flags=0x10`; XMC only (LMC:711-726) |
| delete | 0x79 | 0x0F | name | – | |
| format | 0x77 | 0x10 | desc | – | |
| unformat | 0x80 | 0x11 | desc | – | |
| getentspace | – | 0x12 | name | – | XMC only (LMC:699-709) |
| changethreadpriority | – | 0x14 | desc | – | XMC only. **The ps2sdk server has no case for 0x14**, so it returns a stale result (LMC:92, 777-787; SRV:138-246) |
| checkblock | – | 0x33 | desc | – | no EE API; server `McCheckBlock(port,slot,offset)` (SRV:237-239, 1194-1201) |
| eraseblock | 0x7D | – | desc | – | MC only, raw (LMC:728-738) |
| readpage | 0x7E | – | desc | yes | MC only, raw (LMC:740-753) |
| writepage | 0x7F | – | desc | – | MC only, raw (LMC:755-775) |

- The XMCSERV build handles **both** columns (SRV:140-239). 0x73 and 0x78 keep the old 16-B/64-B-endParam semantics; 0x05 and 0x01 use the new ones.
- [D] The XMC fnos equal the `MC_FUNC_*` numbers in LMH:66-88: getinfo 1, open 2, close 3, seek 4, read 5, write 6, flush 0x0A, chdir 0x0C, getdir 0x0D, setinfo 0x0E, delete 0x0F, format 0x10, unformat 0x11, getent 0x12, chgprio 0x14. `MC_FUNC_MK_DIR` (0x0B) and `MC_FUNC_RENAME` (0x13) have no fno in ps2sdk.
- [?] Handling 0x0B and 0x13 defensively costs nothing. Sony's libmc also has sceMcGetSlotMax, sceMcReadFast and sceMcWriteFast, which ps2sdk lacks. Another HLE (Play!) is believed to service XMC fnos 0x16, 0x17 and 0x1B for these. This is unverified here, so log any unknown fno.

---

### 3. Request and reply layouts

#### 3.1 `desc` param: `mcDescParam_t`, 48 B (CMN:83-94)
| Off | Size | Field |
|---|---|---|
| 0 | 4 | `fd` |
| 4 | 4 | `port` |
| 8 | 4 | `slot` |
| 12 | 4 | `size` |
| 16 | 4 | `offset` |
| 20 | 4 | `origin` |
| 24 | 4 | `buffer` (EE addr) |
| 28 | 4 | `param` (EE addr) |
| 32 | 16 | `data[16]` (inline bytes) |

#### 3.2 `name` param: 1044 B (LMC:97-109, SRVH:96-108)
| Off | Size | Field |
|---|---|---|
| 0 | 4 | `port` |
| 4 | 4 | `slot` |
| 8 | 4 | `flags` |
| 12 | 4 | `maxent` |
| 16 | 4 | `mcT` (EE addr of `sceMcTblGetDir`) or `curdir` (EE addr of a 1024-B buffer) |
| 20 | 1024 | `name`, NUL-terminated, `strlcpy`'d, so at most 1023 chars (LMC:523) |

The reply for every fno is the 4-byte `result`. The only exception is 0xFE, which returns 12 B (§1).

#### 3.3 Per-operation field usage

| Op (MC/XMC) | Request fields used | Server writes into EE memory | `result` |
|---|---|---|---|
| init 70/FE | desc: `fd` (passed to McRetOnly), `offset` (-217 magic, old MCSERV only) | – | 0, plus versions at +4/+8 on 0xFE |
| getinfo **78** | desc: `port`, `slot`, `size`=want type, `offset`=want free, `origin`=want format (ignored by server), `param` | DMA a 64-B `mcEndParam_t` to `param`: `+0 type` (only if size>0), `+4 free` (0 unless want-free and r≥-1) (SRV:866-911) | `McDetectCard` r, or the McGetFreeClusters error |
| getinfo **01** | desc: `port`, `slot`, `size`=want **format**, `offset`=want free, `origin`=want **type**, `param` | DMA a 192-B `mcEndParam2_t`: `+0 type`, `+4 free`, `+144 formatted` (0/1). If r<-1, free=formatted=0 (SRV:915-971) | `McDetectCard2` r, or the free error |
| open 71/02 | name: `port`, `slot`, `flags`, `name` | – | fd 0..2; **0** for mkdir; or error |
| close 72/03 | desc: `fd` | – | 0 / err |
| seek 75/04 | desc: `fd`, `offset`, `origin` | – | new position |
| read 73/05 | desc: `fd`, `size`, `buffer`, `param` | §3.4 | bytes read / err |
| write 74/06 | desc: `fd`, `size`, `origin` (inline count), `buffer`, `data` | – (server pulls data, §3.5) | `origin+size` / err |
| flush 7A/0A | desc: `fd` | – | 0 / err |
| chdir 7B/0C | name: `port`, `slot`, `curdir`, `name`=new dir (`""` means just query) | DMA **1024 B** to `curdir`: the NUL-terminated absolute cwd, e.g. `"/"` or `"/BESLES-12345"`. Written **always**, even on error (SRV:819-843; FIO:1026-1115) | 0 / err |
| getdir 76/0D | name: `port`, `slot`, `flags`(=mode), `maxent`, `mcT`, `name` | DMA n×64-B `sceMcTblGetDir` to `mcT[0..n)` (§3.6) | n / err |
| setinfo 7C/0E | name: `port`, `slot`, `flags`, `mcT`, `name`=target | – (server pulls 64 B from `mcT`, SRV:1187) | 0 / err (§3.7) |
| delete 79/0F | name: `port`, `slot`, `flags` (libmc sends 0; non-zero means *undelete*), `name` | – | 0 / err |
| format 77/10 | desc: `port`, `slot` | – | 0 / err |
| unformat 80/11 | desc: `port`, `slot` | – | 0 / err |
| getentspace –/12 | name: `port`, `slot`, `name`=dir | – | free dir-entry slots (§6) |
| chgprio –/14 | desc; ps2sdk libmc does not send the level (LMC:779-786) | – | unhandled in ps2sdk |
| checkblock –/33 | desc: `port`, `slot`, `offset`=block | – | McCheckBlock |
| erase/readpage/writepage 7D/7E/7F | desc: `offset`=block, `origin`=mode (-1 = no fill); `fd`=page, `buffer`, `param`/`data` | raw 512-B pages (SRV:1007-1177) | not meaningful for a host-dir backend |

The MC/XMC getinfo flag-field swap is at LMC:492-503.

#### 3.4 Read (0x73 old / 0x05 new) and the endParam mechanism
Let `A = 16` for 0x73 and `A = 64` for 0x05. `buf` is the EE `buffer` field.

| Step | Rule | Cite |
|---|---|---|
| head | `size1 = size` if `size <= A`; else `size1 = (A - (buf % A)) % A`. These bytes go into `eP.src1`, and `dest1 = buf` | SRV:477-480, 605-608 |
| tail | `size2 = (size - size1) % A`. These bytes go into `eP.src2`, and `dest2 = buf + size1 + mid` | SRV:482-486, 610-614 |
| middle | `mid = size - size1 - size2`, read in chunks of ≤8 KB and DMA'd **directly** to `buf+size1` (A-aligned) | SRV:512-547, 640-689 |
| endParam | **always** DMA'd to `param`, even on error (then sizes = 0): 64 B `mcEndParam_t` (0x73) or 192 B `mcEndParam2_t` (0x05) | SRV:488-495, 563-574, 705-716 |
| short read (EOF) | head short: `size1=got`, `size2=0`, stop. Middle short (old): the partial chunk is DMA'd and `size2=0`. Middle short (new): the `got & 63` tail moves into src2/dest2. | SRV:501-505, 541-545, 654-666 |
| result | total bytes read (head+mid+tail), or the first negative McRead error | SRV:576-579 |

EE end function: copies `size1` bytes `src1→dest1` and `size2` bytes `src2→dest2` (LMC:284-311).

| Struct | Layout | Cite |
|---|---|---|
| `mcEndParam_t` (64 B) | `+0 s32 size1/type`, `+4 s32 size2/free`, `+8 dest1`, `+12 dest2`, `+16 src1[16]`, `+32 src2[16]`, `+48 unused[16]` | CMN:99-116 |
| `mcEndParam2_t` (192 B) | `+0 size1/type`, `+4 size2/free`, `+8 dest1`, `+12 dest2`, `+16 src1[64]`, `+80 src2[64]`, `+144 s32 formatted` (+144..191 union of 48 B) | CMN:121-142 |

- **[D] HLE shortcut:** you can write the whole range straight into EE RAM and DMA an endParam with `size1 = size2 = 0`. That is safe with the ps2sdk end function.
- [?] Confirm that the game's (Sony) end function behaves the same way. Either way, **always** write the endParam. The EE buffer is static and reused, so stale `size*`/`dest*` from an earlier call would otherwise be replayed.

#### 3.5 Write (0x74 / 0x06)
| Item | Rule | Cite |
|---|---|---|
| Client split, `size < 17` | `size=0`, `origin=size`, `buffer=0`, all bytes inline in `data[]` | LMC:574-579 |
| Client split, `size ≥ 17` | `origin = (16 - (buf & 15)) & 15` head bytes in `data[]`; `size -= origin`; `buffer = buf + origin` (16-aligned) | LMC:580-589 |
| Server | `McWrite(data, origin)` first. If that is short, return. Then fetch `size` bytes from EE `buffer` (≤8 KB per `sceSifGetOtherData`) and write | SRV:735-763 |
| Result | `origin + size`. On a short write: `r + written` (r≥0) or `r` (r<0) | SRV:753-765 |

#### 3.6 GetDir (0x76 / 0x0D) and `sceMcTblGetDir`

Server loop (SRV:769-816):
- It decrements `maxent` and iterates `maxent` times. Each pass calls `McGetDir(port, slot, name, flags & 0xFFFF, 1, tmp)`. The first pass uses the EE flags; later passes use flags 1.
- Each found entry is DMA'd (64 B) to `mcT`, and `mcT` advances.
- If `r == 0` (no more entries), it stops.
- If `r < 0`, it returns `r`, but entries already DMA'd stay in EE memory.
- If `maxent ≤ 0`, it returns 0 **without** touching the enumeration state.

`mode` / `flags` semantics (FIO:1118-1254):
- **0 starts a new listing.**
  - `name` is split at the last `/`. The left part is the directory (`""` means cwd, `"/"` means root). The right part is a wildcard pattern (FIO:1130-1156).
  - The directory must exist and be a directory, otherwise -4 (FIO:1158-1167).
- **Non-zero continues** from the previous cursor. If there is no valid cursor, it returns -4 (FIO:1177-1180).
- The cursor (cluster, index, pattern pointer) is **one global state** for all ports and slots (FIO:14-17, MAN:29). The pattern pointer points into the *current* request buffer (FIO:1156).
- Pattern: `*` matches any run and `?` matches one char (MAN:359-414). Quirk: `?` also matches end-of-string (MAN:391-396).
- **Root listing starts at entry index 2**, so the root's `.` and `..` are never returned (FIO:1172-1175).
- In a subdirectory, entries 0 and 1 are returned as `"."` and `".."` if they match the pattern (FIO:1202-1232).
  - `"."` carries the attributes and times of **the directory's own entry in its parent**.
  - `".."` carries those of the **parent directory's own entry**. For a root-level dir that is the root `.` entry: 0x8427.
- Skipped entries: non-existing ones (`!(mode & 0x8000)`), and hidden ones (0x2000) only when PS1CardFlag==0. ps2sdk's default flag is 1, so hidden entries are listed (FIO:1193-1196, MAN:34).

`sceMcTblGetDir`, 64 B (LMH:139-148 with `aligned(64)` on the EE; MANH:24-33). Zero-filled, then (FIO:1200-1244):

| Off | Size | Field | Filled with |
|---|---|---|---|
| 0 | 8 | `_Create` (`sceMcStDateTime`) | entry creation time |
| 8 | 8 | `_Modify` | entry modification time |
| 16 | 4 | `FileSizeByte` | file length in bytes; **0 for directories** (FIO:1243-1244) |
| 20 | 2 | `AttrFile` | entry mode (§4.3) |
| 22 | 2 | `Reserve1` | `McFsEntry.unused` (normally 0) (FIO:1238) |
| 24 | 4 | `Reserve2` | 0 from getdir. It is an *input* for setinfo flag 0x08 |
| 28 | 4 | `PdaAplNo` | 0 for PS2 |
| 32 | 32 | `EntryName` | `strncpy(...,32)`, so a 32-char name has **no NUL** (FIO:1234) |

`sceMcStDateTime`, 8 B (CMN:22-31):

| Off | Field |
|---|---|
| +0 | `u8 Resv2` (0) |
| +1 | `u8 Sec` |
| +2 | `u8 Min` |
| +3 | `u8 Hour` |
| +4 | `u8 Day` (1-31) |
| +5 | `u8 Month` (1-12) |
| +6 | `u16 Year` (full year) |

Times are **JST**, taken from the CDVD RTC. If the RTC read fails, the time is 2000-03-04 00:00:00 (MAN:1090-1125).

#### 3.7 SetFileInfo / Rename flags (server side; FIO:358-470)
EE constants: `sceMcFileInfoCreate=1`, `Modify=2`, `Attr=4` (CMN:212-215). `mcRename` sends 0x10 with the new name in `EntryName` (LMC:719-722). The target is resolved first; if it is not found, see §6 #12.

| Flag bit | Effect | Source field |
|---|---|---|
| 0x01 | set creation time | `_Create` (FIO:458-459) |
| 0x02 | set modification time | `_Modify` (FIO:461-462) |
| 0x04 | `mode = (mode & 0x8030) \| (AttrFile & ~0x8030)`, so Exists, File and Subdir are kept. If PS1CardFlag==0, only the bits in `0x180F` change | `AttrFile` (FIO:444-449) |
| 0x08 | entry `attr` (opaque u32) = `Reserve2` | `Reserve2` (FIO:441-442) |
| 0x10 | **rename** to `EntryName` (truncated to 31 chars). Fails with -4 when: the new name is `"."`, `".."`, empty or contains `/`; the target is a `.`/`..` entry; or the new name already exists | FIO:371-421, 464-467 |
| 0x200 | mode bits within `0x380F` (Hidden, PS1, PDAExec, RWXC) taken from AttrFile | FIO:451-456 |
| 0xFEED | ps2sdk-only hack: raw `mode = AttrFile`, then treated as 0x03 | FIO:429-439 |

On success the server flushes the cache (MAN:978-984).

---

### 4. Codes, types, flags, attributes

#### 4.1 Return codes (CMN:157-170)
| Code | Name | Where mcman produces it |
|---|---|---|
| 0 | `sceMcResSucceed` | |
| -1 | `sceMcResChangedCard` | getinfo: a new *formatted* card was detected. Old MCMAN: the first op after a swap. Also low-level I/O failures |
| -2 | `sceMcResNoFormat` | an unformatted card |
| -3 | `sceMcResFullDevice` | no free cluster (write, create, dir growth) (FIO:550-561, 878-880) |
| -4 | `sceMcResNoEntry` | not found; empty name; mkdir on an existing name; rename target exists; delete of `.`, `..` or root (FIO:666-667, 716-726, 1288-1289; MAN:2204-2207) |
| -5 | `sceMcResDeniedPermit` | bad or closed fd; R/W permission missing; file already open for write; delete/recreate of an open file; dir without R+X while traversing (MAN:641-646, 2209-2223, 1863-1865; FIO:748-749, 780-795) |
| -6 | `sceMcResNotEmpty` | delete of a non-empty dir (FIO:1291-1303) |
| -7 | `sceMcResUpLimitHandle` | all 3 handles in use (FIO:669-677) |
| -8 | `sceMcResFailReplace` | bad-block replacement |
| -11 / -12 / -13 | `FailResetAuth` / `FailDetect` / `FailDetect2` | card probing (SIO:864-867, 931-935, 995-999, 1009) |
| -51 | `sceMcResDeniedPS1Permit` | a PS1 card while PS1CardFlag==0 (MAN:476, 551-552) |
| -90 | `sceMcResFailAuth` | card authentication |
| **≤ -10** | "card could not be detected" | documented contract (EX:75, EX:107) |
| -31, -40…-49 | internal FS/format errors | FIO:616, 75, 119, 132, 157, 169, 202, 224, 242 |

#### 4.2 GetInfo semantics
| Item | Value | Cite |
|---|---|---|
| result **0** | same card as the last probe: the card answered "not changed" and `cardform != 0` | SIO:914-928 |
| result **-1** | full re-probe found a **formatted** card. This happens on the first GetInfo after module start, power-on or a swap | SIO:1002-1006 |
| result **-2** | full re-probe found an **unformatted** card. Later calls return **0**, because `cardform = -1 ≠ 0` | SIO:1007-1014, SIO:914-928, MAN:1360 |
| result ≤ -10 | no card. ps2sdk typically yields -11 (nothing answers). Afterwards `type = 0` | SIO:931-935, MAN:602-607, EX:75 |
| `type` | `McGetMcType`: 0 none, 1 PS1, 2 PS2, 3 PDA/PocketStation | MAN:252-256, CMN:172-176, LMH:42-45 |
| `free` | `McGetFreeClusters`. PS2: number of free FAT entries, i.e. free **1 KB clusters** (cluster size 1024). 0 if unformatted or full | MAN:275-305, MANI:110 |
| `format` (XMC) | `McGetFormat() > 0 ? 1 : 0` (`MC_FORMATTED`=1) | SRV:947-951, LMH:47-48 |
| `format` (MC) | the server does not send it; the EE infers `type != 0 && result != -2`. **After the first call, an unformatted card therefore reads as formatted under MCSERV** | LMC:259-260, LMH:222 |
| full card [D] | allocatable clusters = `1000*(clusters_per_card/1000)+1` = **8001** on an 8 MB card. Root uses 1, so a fresh card reports **free = 8000** | MAN:1407-1433, FIO:171-175, MAN:1544-1554 |

#### 4.3 Attribute bits (`AttrFile` / on-card mode) (CMN:196-210, LMH:50-63)
| Bit | Name | Notes |
|---|---|---|
| 0x0001 | `sceMcFileAttrReadable` | open-for-read needs it only if PS1CardFlag==0 (FIO:751-756) |
| 0x0002 | `…Writeable` | open-for-write needs it (FIO:748-749) |
| 0x0004 | `…Executable` | path traversal needs R and X on every dir (MAN:1863-1865) |
| 0x0008 | `…DupProhibit` | copy-protect (browser only) |
| 0x0010 | `…File` | |
| 0x0020 | `…Subdir` | |
| 0x0040 | `sceMcFileCreateDir` | open flag only |
| 0x0080 | `…Closed` | set when a file is closed or flushed after writing; cleared while it is written (FIO:618-624, 533-543; MAN:662-679) |
| 0x0200 | `sceMcFileCreateFile` | = O_CREAT; open flag only |
| 0x0400 | `sceMcFile0400` | always set on created entries |
| 0x0800 | `…PDAExec` | |
| 0x1000 | `…PS1` | |
| 0x2000 | `…Hidden` | |
| 0x8000 | `…Exists` | the entry slot is in use |

| Typical mode | Value | Cite |
|---|---|---|
| directory | **0x8427** (R W X Subdir 0x400 Exists) | FIO:953-955 |
| `.` entry | 0x8427 | MAN:1528-1529 |
| non-root `..` entry | 0x8427 | MAN:1562-1563 |
| root `..` | 0xA426 (Hidden, no R) | MAN:1550-1551 |
| file just created | **0x8417** | FIO:989-991 |
| file after write + close | **0x8497** (+0x08 if copy-protected) | FIO:618-624 |
| PS1 card entries | 0x9417 | iop/memorycard/mcman/src/ps1mc_fio.c:630 |

#### 4.4 Open flags (values = Sony `SCE_*` = `FIO_O_*`, IOC:29-38)
| Value | Std name | mcman meaning | Cite |
|---|---|---|---|
| 0x0001 | O_RDONLY | read | FIO:728 |
| 0x0002 | O_WRONLY | write | FIO:729 |
| 0x0003 | O_RDWR | read and write | |
| 0x0040 | – | **mkdir** (`sceMcFileCreateDir`). Returns 0 on success, -4 if the name exists | FIO:722-723, 951-987 |
| 0x0200 | O_CREAT | create; **implies write**. An **existing file is deleted and recreated empty** (truncate) | FIO:661-662, 797-806 |
| 0x0400 | O_TRUNC | **ignored** (no truncation without O_CREAT) | [D] FIO:647-1023 |
| 0x0100 / 0x0010 / 0x8000 | APPEND / NBLOCK / NOWAIT | ignored | [D] |
| 0x0800 | O_EXCL | **not** exclusive: it is copied into the new entry as PDAExec | FIO:955, 991 |
| 0x1000 | – | new entry gets the PS1 attribute | FIO:955, 991 |
| 0x2000 | – | new entry gets Hidden. Masked off if PS1CardFlag==0 | MAN:619-620, FIO:948-955 |

Other open semantics:
- An empty name returns -4 (FIO:666-667).
- Opening a directory returns an fd that can only be closed or dread: rdflag/wrflag are 0, so read on it returns -5 (FIO:764-778).
- An existing file opened without CREAT gets `rdflag = want_R && mode&R` and `wrflag = want_W && mode&W` (FIO:807-825).
- Seek origin: 0 SET, 1 CUR (also the default), 2 END. A negative result clamps to 0. Seeking past EOF is allowed (MAN:770-783, IOC:43-45).
- Read at or past EOF returns 0 (FIO:481-522). A write extends the file (FIO:584-590).
- A read or write error closes that handle. An error below -9 invalidates all handles on that card (MAN:811-817, 847-853).

#### 4.5 What real listings look like [D, from §3.6 + §4.3]
`mcGetDir(0,0,"/*",0,N,t)`: no `.` or `..`.

| # | EntryName | AttrFile | FileSizeByte |
|---|---|---|---|
| 0 | BESLES-12345 | 0x8427 | 0 |
| 1 | BASLUS-20000 | 0x8427 | 0 |

`mcGetDir(0,0,"/BESLES-12345/*",0,N,t)`:

| # | EntryName | AttrFile | FileSizeByte |
|---|---|---|---|
| 0 | `.` | mode of `/BESLES-12345` itself, e.g. 0x8427 | 0 |
| 1 | `..` | mode of the root `.` entry: 0x8427 | 0 |
| 2 | icon.sys | 0x8497 | 964 |
| 3 | (icon, data files…) | 0x8497 | size |

- Order is the physical slot order: creation order, with deleted slots reused.
- `mcGetDir(0,0,"/BESLES-12345",0,1,t)` lists **root** with the pattern `BESLES-12345`. Games use it as an existence and attribute probe.

---

### 5. mcman IOP export table (for IOP modules that import it directly)
- Library name is **`mcman`**: version **1.1** for MCMAN and **2.3** for XMCMAN (MEXP:2-8, MANH:83-87). xfromman exports `xfromman` 2.3.
- IRX module name `mcman_cex`, version 2.11 (MANI:61, MAN:20).
- MCSERV exports library `mcserv` 1.1 with only `_start`/`_dummy` (SEXP:14-23), so there is no callable API.
- Direct calls are synchronous and use IOP pointers; `McGetDir` writes `maxent` entries itself (MAN:880-899).

Entries (MEXP:9-88, signatures MANH:35-78, import numbers MANH:89-128):

| # | MCMAN 1.1 | XMCMAN 2.3 | Signature |
|---|---|---|---|
| 0 | `_start` | same | module entry |
| 1-3 | `_dummy` | same | reserved |
| 4 | `_start` (placeholder) [?] | same | |
| 5 | McDetectCard | McDetectCard | `int (int port, int slot)`. XMCMAN: *light* probe (never -1). MCMAN: full probe (MAN:450-457) |
| 6 | McOpen | | `int (int port, int slot, const char *name, int flags)` |
| 7 | McClose | | `int (int fd)` |
| 8 | McRead | | `int (int fd, void *buf, int len)` |
| 9 | McWrite | | `int (int fd, void *buf, int len)` |
| 10 | McSeek | | `int (int fd, int offset, int origin)` |
| 11 | McFormat | | `int (int port, int slot)` |
| 12 | McGetDir | | `int (int port, int slot, const char *dirname, int flags, int maxent, sceMcTblGetDir *info)` |
| 13 | McDelete | | `int (int port, int slot, const char *name, int flags)` |
| 14 | McFlush | | `int (int fd)` |
| 15 | McChDir | | `int (int port, int slot, const char *newdir, char *curdir)` |
| 16 | McSetFileInfo | | `int (int port, int slot, const char *name, sceMcTblGetDir *info, int flags)` |
| 17 | McEraseBlock `(int port, int block, void **pagebuf, void *eccbuf)` | McEraseBlock2 `(int port, int slot, int block, void **pagebuf, void *eccbuf)` | MANH:47, 60 |
| 18 | McReadPage | | `int (int port, int slot, int page, void *buf)` |
| 19 | McWritePage | | `int (int port, int slot, int page, void *pagebuf, void *eccbuf)` |
| 20 | McDataChecksum | | `void (void *buf, void *ecc)` |
| 21 | `_dummy` | McDetectCard2 | `int (int port, int slot)`, full probe (the one GetInfo uses) |
| 22 | `_dummy` | McGetFormat | `int (int port, int slot)` |
| 23 | `_dummy` | McGetEntSpace | `int (int port, int slot, const char *dirname)` |
| 24 | `_dummy` | McReplaceBadBlock | `int (void)` |
| 25 | `_dummy` | McCloseAll | `int (void)` |
| 26-28 | `_dummy` | `_dummy` | [?] |
| 29 | McReadPS1PDACard | | `int (int port, int slot, int page, void *buf)` |
| 30 | McWritePS1PDACard | | `int (int port, int slot, int page, void *buf)` |
| 31-35 | `_dummy` | `_dummy` | [?] |
| 36 | McUnformat | | `int (int port, int slot)` |
| 37 | McRetOnly | | `int (int fd)`, returns 0 |
| 38 | McGetFreeClusters | | `int (int port, int slot)` |
| 39 | McGetMcType | | `int (int port, int slot)` |
| 40 | McSetPS1CardFlag | | `void (int flag)` |
| 41 | `_dummy` | `_dummy` | |
| 42 | `_dummy` | McGetModuleInfo | `struct irx_id *(void)` |
| 43 | `_dummy` | McGetCardSpec | `int (int port, int slot, s16 *pagesize, u16 *blocksize, int *cardsize, u8 *flags)` |
| 44 | `_dummy` | McGetFATentry | `int (int port, int slot, int fat_index, int *fat_entry)` |
| 45 | `_dummy` | McCheckBlock | `int (int port, int slot, int block)` |
| 46 | `_dummy` | McSetFATentry | `int (int port, int slot, int fat_index, int fat_entry)` |
| 47 | `_dummy` | McReadDirEntry | `int (int port, int slot, int cluster, int fsindex, McFsEntry **pfse)` |
| 48 | `_dummy` | Mc1stCacheEntSetWrFlagOff | `void (void)` |
| 49 | `_dummy` | McCreateDirentry | `int (int port, int slot, int parent_cluster, int num_entries, int cluster, const sceMcStDateTime *ctime)` |
| 50 | `_dummy` | McReadCluster | `int (int port, int slot, int cluster, McCacheEntry **pmce)` |
| 51 | `_dummy` | McFlushCache | `int (int port, int slot)` |
| 52 | `_dummy` | McSetDirEntryState | `int (int port, int slot, int cluster, int fsindex, int flags)` |
| 53 | `_dummy` | `_dummy` | |

Games load these ROM module names: `rom0:SIO2MAN` / `rom0:MCMAN` / `rom0:MCSERV` (old) or `rom0:XSIO2MAN` / `rom0:XMCMAN` / `rom0:XMCSERV` (EX:240-274).

---

### 6. Gotchas

1. **The first GetInfo returns -1.**
   - The first GetInfo per (port, slot) after module start, or after an insertion, does a full probe. It returns -1 for a formatted card and -2 for an unformatted one; later calls return 0 (SIO:902-1015). Games use -1 as the "card inserted, rescan" event.
   - With old MCMAN, file ops also use the full probe. The first op after a swap therefore fails with -1 if GetInfo was not called first (MAN:450-457, 615-617).
   - With XMCMAN, file ops before any GetInfo fail with ≤ -10, because the light probe sees `cardform==0` and returns -13 (SIO:869-898, MAN:460-532).
   - HLE: keep a per-(port, slot) "seen" flag, return -1 once, and be lenient on early file ops [D].
2. **A re-probe resets the cwd to `/`.** The devinfo struct is memset and only the first 0x150 bytes are reloaded from the superblock (MAN:1352, 1376-1378). The cwd is stored per (port, slot) (MAN:1821-1822; FIO:1047-1048). Format also resets it (FIO:238).
3. **Paths.**
   - A leading `/` means absolute from root. Anything else is relative to the cwd set by chdir (MAN:1814-1823).
   - `.` and `..` are resolved; `..` at root stays at root (MAN:1924-1997). A trailing `/` is accepted (MAN:1892-1904).
   - Every traversed directory needs R and X, otherwise -5 (MAN:1863-1865).
   - Not found: with intermediate components still left → internal 2; at the last component → internal 1. Callers map both to -4 (MAN:1882-1890; FIO:716-726).
4. **Lengths.**
   - The on-card name field is 32 B (CMN:65). Create copies up to 32 chars, with no NUL when exactly 32 (FIO:939). Rename caps names at 31 (FIO:465-466).
   - Paths go up to 1023 chars (LMC:523; FIO:1134-1135). The chdir result buffer is 1024 B (SRV:832).
   - ps2sdk does **not** validate characters; `mcman_checkpath` is commented out (FIO:664-665, 1035-1036, 1265-1266). [?] Sony's original likely rejects `?`, `*` and control chars with -4.
5. **GetDir.** The mode is 0 for first and non-zero to continue, there is one global cursor, root skips `.`/`..`, and `flags` is masked with 0xFFFF (SRV:786, MAN:889, FIO:1128). [?] Sony's libmc may use the upper 16 bits. `maxent` is the capacity of the caller's array; the result is the count written. Details in §3.6.
6. **Handles.**
   - At most **3** are open in total across both ports; the fds are 0..2 (MANI:146; FIO:669-677).
   - Only one writer per file; a second open-for-write returns -5 (FIO:780-795).
   - Deleting or recreating an open file returns -5 (MAN:2209-2223).
7. **Create and mkdir.**
   - O_CREAT on an existing file truncates it (recreates it empty); O_TRUNC alone does nothing.
   - mkdir returns **0** (not an fd), or -4 if the name exists.
   - Creating an entry updates the parent's `_Modify` (FIO:931).
8. **Always DMA the endParam for read and getinfo**, even on errors (SRV:488-495, 563-574, 893-905). Also always write the 1024-B chdir buffer (SRV:828-842). The EE end function runs unconditionally on the reused static buffer.
9. **Unknown fno.** The real server leaves the result stale (SRV:241-245); that is load-bearing for ps2sdk's version probe (§1). For anything else, log it and return an error.
10. **Delete.** It fails with -6 on a non-empty dir (any existing entry at index ≥ 2), and with -4 for `.`, `..` or root (FIO:1288-1303; MAN:2204-2207). `flags≠0` means undelete (FIO:1274-1281).
11. **GetEntSpace** returns `(dir.length & 1) + #non-existing slots` (MAN:1783-1792). Here `length` counts all slots, including `.` and `..`, at 2 entries per 1 KB cluster. For a host-backed dir with no holes, return `(n_entries_incl_dot_dotdot & 1)`. Non-PS2 cards return 0 (MAN:867-869).
12. **SetFileInfo on a missing target** returns **+1/+2** in ps2sdk, because `mcman_cachedirentry`'s positive code is passed through (FIO:367-369; MAN:960-987). [?] Sony likely returns -4, and returning -4 is the safer HLE choice.
13. **ChDir to a file** is accepted by ps2sdk, which has no directory check (FIO:1040-1048). [?] -4 is the safer HLE choice.
14. **Free-space accounting for a host-dir backend [D].**
    - Each directory uses `ceil(entries/2)` clusters, where entries include `.` and `..` (FIO:849-907, 956).
    - Each file uses `ceil(size/1024)` clusters; zero-length files use none (`cluster = -1`, FIO:993).
    - `free = 8001 - Σdirs - Σfiles` for an 8 MB card, so an empty card reports 8000.
    - Running out of clusters gives -3.
15. **Timestamps are JST** (MAN:1090-1125). Convert host times to UTC+9.
    - [?] The non-IOP (host-build) path does `rawtime += -9*3600`, which looks sign-inverted (MAN:1130-1131).
    - `.` and `..` report the self and parent entries' times (FIO:1202-1241).
16. **Hidden and PS1CardFlag.**
    - ps2sdk defaults PS1CardFlag to 1 (MAN:34). XMCSERV never changes it; old MCSERV sets it to `offset==-217` (SRV:410-419).
    - With flag 0: hidden entries vanish from getdir and lookups, PS1 cards return -51, and the setinfo attribute masks shrink (FIO:1195-1196, MAN:2046-2049, MAN:476, FIO:446-447).
    - [?] LMH:62 says hidden entries are "hidden to games". Sony's default may be 0.

---

### 7. Where ps2sdk may differ from Sony's modules
| Item | Detail | Cite |
|---|---|---|
| Base versions | mcserv is reversed from BOOTROM XMCSERV 2.8 and mcman from XMCMAN 2.9/2.10. SDK 3.x modules (MCSERV 2.10, MCMAN 2.30) contain unported changes | SRV:13-17, MAN:15-20 |
| 0x14 chgprio | sent by libmc, not handled by the ps2sdk server; the level is not sent | LMC:777-787, SRV:138-246 |
| Extra Sony fnos | [?] slot-max / read-fast / write-fast (reportedly 0x16/0x17/0x1B), plus possibly 0x0B and 0x13 | §2 |
| Name validation | disabled in ps2sdk | FIO:664-665 |
| 0xFEED setinfo | ps2sdk-only | FIO:429-439 |
| `-217` init magic / PS1CardFlag default | ps2sdk convention | SRV:410-419, LMC:450, MAN:34 |
| MAX_FDHANDLES=3, export slots 4/26-28/31-35/41/53 | [?] as reversed; the `_dummy` slots may be real functions in Sony's build | MANI:146, MEXP:9-88 |
| ps2sdk EE libmc quirks (not the game's Sony libmc) | (a) `mcSync *cmd` returns the internal `MC_RPCCMD` index, not `MC_FUNC_*`; they differ from flush onward, e.g. flush gives 7 instead of 0x0A. (b) `mcMkDir` sets cmd only on *failure*, so it reports OPEN. (c) `mcStoreDir` NUL-terminates the source instead of the destination, and does not check for NULL. (d) the MCSERV format inference noted in §4.2. Sony's `sceMcSync` presumably reports `sceMcFunc*` = LMH:66-88 | LMC:230, 609-612, 316-326, 814-815 |
| MCSERV getinfo | [?] the sample comment claims `*format` is always 0, but the code infers it | EX:77-78 vs LMC:259-260 |

---

### Appendix: the `mc` ioman device (if the game uses `mc0:` through fio instead of libmc)
- Device `"mc"`. Unit maps to `port = unit & 1` and `slot = (unit>>1) & 3` (DEV:97-119, 216-228).
- Every op first calls `McDetectCard2` (full probe) and proceeds if the result is ≥ -1 (format: ≥ -2) (DEV:320-325, 398-401).
- open passes the fio flags straight to McOpen (DEV:322). mkdir is `McOpen(0x40)` (DEV:442). remove and rmdir are `McDelete(..,0)` (DEV:417, 459). dopen is `McOpen(..,0)` (DEV:476).
- dread returns **every** existing slot, including `.` and `..` in root, with their raw modes. It sets `stat.mode` bits R1 W2 X4 C8 F0x10 D0x20 (+PS1 0x1000, PDA 0x800), `attr`, `size`, and `ctime`/`mtime` as raw `sceMcStDateTime` (FIO:255-311; MANI:173-177; IOC:47-63).
- chstat calls McSetFileInfo with ATTR→0x008, MODE→0x200, CT→0x001, MT→0x002 (DEV:529-576).
- Errno mapping (DEV:130-163; EFORMAT=47 at CMN:152-154):

| mc code | errno |
|---|---|
| -8 | -EIO |
| -7 | -EMFILE |
| -6 | -EEXIST |
| -5 | -EACCES |
| -4 | -ENOENT |
| -3 | -ENOSPC |
| -2 | -EFORMAT |
| any other negative (-1, ≤ -9) | -ENXIO |
