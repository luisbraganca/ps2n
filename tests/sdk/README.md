# ps2sdk test programs

C programs built with the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain
and [ps2sdk](https://github.com/ps2dev/ps2sdk), brought over from the old
project (`../ps2-uncompiler/tests/sdk`). They test what hand-written assembly
does not: compiled code linked with the real EE libraries, talking to the
IOP's modules through SIF RPC, on the BIOS kernel.

| Program | What it covers | In the test suite |
|---|---|---|
| `iop_rpc` | `rpctest.irx` loaded from `host0:` through LOADFILE: module arguments, threads, semaphores, event flags, message boxes, alarms, vblank waits, IOP memory, and its RPC server called in wait and no-wait mode | yes |
| `pad`, `padx` | libpad and libpadx against the BIOS's PADMAN and XPADMAN: ports, modes, pressure mode. No buttons are pressed: the host has no way to press them yet | yes |
| `mc` | libmc against the BIOS's XMCSERV. The host has no memory card in either slot, so every call reports that; a test of the errors, not of a card | yes |
| `vu` | VU0 micro programs started with `VCALLMS`/`VCALLMSR` after a VIF0 upload, a VU1 program on a VIF1 double buffer drawing with `XGKICK`, VIF1 `UNPACK` formats and modes. Only what it prints is compared; the frame it draws is step P3 | yes |
| `cdvd` | libcdvd on a disc built from `disc/` | not yet: nothing builds the disc image |
| `iop_sd` | `sdtest.irx` driving libsd | no: on a real BIOS `sdtest.irx` does not load, because nothing loads `rom0:LIBSD` first. The old project's runtime faked that module. Needs the program changed and rebuilt |

Their expected outputs (`<name>.expected`) are what stock PCSX2 `v2.9.170`
printed with its recompilers and the `SCPH-70004` BIOS: the whole output of
the machine, the BIOS's own lines included, up to the program's last line.
The old project's expected outputs are not used: they recorded what its own
high-level runtime printed.

## Rebuilding

The tests use the binaries in `bin/`, so the toolchain is only needed after
changing a source file. It is not installed on the development machine.

```sh
export PS2DEV=/path/to/ps2dev PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
make -C tests/sdk
```

Rebuilding with another toolchain or ps2sdk version changes the binaries, and
with them addresses the expected outputs may show.

## License

The sources here are part of this repository (GPL-3.0-or-later). The binaries
in `bin/` are linked with ps2sdk, which is licensed under the Academic Free
License 2.0; see the ps2sdk repository for its terms.
