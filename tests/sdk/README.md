# ps2sdk test programs

Programs built with the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain
and [ps2sdk](https://github.com/ps2dev/ps2sdk), so the runtime is tested
against compiled code and the real EE libraries instead of hand-written
assembly. Each one is recompiled at build time and run twice (recompiled and
`--interpret`), and its output is compared with `<name>.expected`.

| Test | What it covers |
|---|---|
| `iop_rpc` | `rpctest.irx` loaded through LOADFILE: module arguments, threads, semaphores, event flags, message boxes, alarms, vblank waits, IOP memory, and its RPC server called in wait and no-wait mode |
| `iop_sd` | `sdtest.irx` driving libsd: registers, voice transfers and their interrupts, a looping block transfer, batches |
| `pad`, `padx` | libpad and libpadx against PADMAN with input from `pad.script`: ports, modes, pressure mode, buttons and sticks |
| `vu` | VU0 micro programs (`vu/vu0.vsm`) started with `VCALLMS`/`VCALLMSR` after a VIF0 upload, a VU1 program (`vu/vu1.vsm`) on a VIF1 double buffer drawing with `XGKICK`, VIF1 `UNPACK` formats and modes; the frame is checked against `vu.frame.sha256` |
| `cdvd` | libcdvd on the disc built from `disc/`: disc type, tray, file search, sector reads, streaming, the clock, `cdrom0:` |
| `mc` | libmc (XMCSERV) on an empty card directory |

## Rebuilding

The tests use the binaries checked in under `bin/`, so the toolchain is only
needed after changing a source file:

```sh
export PS2DEV=/path/to/ps2dev PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
make -C tests/sdk
```

Binaries are stripped of debug information but keep their symbols, which the
recompiler uses. Rebuilding with a different toolchain or ps2sdk version
changes the binaries but should not change the expected output.

## License

The sources here are part of this repository. The binaries in `bin/` are
linked with ps2sdk, which is licensed under the Academic Free License 2.0;
see the ps2sdk repository for its terms.
