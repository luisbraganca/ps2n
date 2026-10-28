# Config reference

A game is described by one TOML file. Only `[input] elf` is required:

```toml
[input]
elf = "SLUS_123.45"
```

`ps2-uncompiler analyze game.elf --write-config game.toml` writes a starter
config with the common keys commented in, and lists the functions it already
bound to runtime stubs.

General rules:

- Relative paths resolve against the directory of the config file.
- Unknown keys and wrong types are errors that name the file and line
  (`game.toml:5: unknown key 'skp' in [functions]`), so a typo cannot silently
  disable an option.
- **Addresses** are integers (`0x00123456`) or strings (`"0x00123456"`).
- **Function references** (in `bind`, `skip`, `override`) are either a name
  (from the ELF symbols or a function map) or an address string starting with
  `0x`. An address that the analysis did not already treat as a function start
  becomes one. A name that matches nothing is a warning in `report.txt`, not
  an error, so one config can serve builds that differ slightly.

## `[input]`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `elf` | path | required | The EE executable (`SLUS_123.45`, `SCES_...`, a homebrew `.elf`). IRX modules are rejected: they are IOP code (see `ps2-uncompiler irx`). |
| `ghidra_map` | path | none | A function map, usually written by [tools/ghidra/ExportPs2ucFunctions.java](../tools/ghidra/ExportPs2ucFunctions.java). |
| `map_files` | array of paths | `[]` | More function maps, same format. |

### Function maps

A function map is a CSV file with a header row. Columns are matched by name,
case-insensitively, so several existing exports work unchanged:

| Column | Accepted header names |
|---|---|
| start (required) | `start`, `address`, `addr`, `location`, `entry`, `entry point` |
| name | `name`, `function`, `symbol`, `function name` |
| end (exclusive) | `end` |
| size in bytes | `size`, `function size`, `length`, `bytes` |
| type | `type` (rows whose type is not `Function` are skipped) |

Addresses are hexadecimal with or without `0x`, and may carry a Ghidra space
prefix (`ram:00100230`). Lines starting with `#` are ignored and fields may be
quoted. This covers the bundled Ghidra script, PS2Recomp's exporter, and
Ghidra's own *Functions* and *Symbol Table* window exports.

## `[output]`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `dir` | path | `out/<elf name>` | Where the generated files go. |
| `name` | string | ELF name without extension (`SLUS_123.45` gives `SLUS_123`) | Name of the executable and of the CMake project. |
| `mode` | `"multi"` or `"single"` | `"multi"` | One source file, or several `recomp_funcs_NNN.cpp`. |
| `files` | integer 0-4096 | `0` | Number of `recomp_funcs_NNN.cpp` files in multi mode. `0` picks one file per ~16,000 instructions. Fix the count when a build system must know the file list before the recompiler runs (`ps2uc_add_recompiled_game` does this). |
| `comments` | bool | `true` | Disassembly comment on every generated line. |
| `project` | bool | `true` | Write a `CMakeLists.txt` that builds the output against the runtime. Turn off when you add the sources to your own build. |
| `runtime_dir` | path | the tree the recompiler was built from | ps2-uncompiler source tree the generated `CMakeLists.txt` uses (also settable later as the `PS2UC_ROOT` cache variable). |
| `extra_sources` | array of paths | `[]` | Your own C++ files to compile into the game, typically the functions named in `[functions] override`. |

Generated files:

| File | Contents |
|---|---|
| `functions.h` | A declaration for every guest function, and for your override functions. |
| `recomp_funcs_NNN.cpp` | The translated functions. |
| `recomp_table.cpp` | The function table (address ranges, names, which functions the config replaced), resume points, patches, and the ELF's CRC-32. |
| `recomp_main.cpp` | `main()`, which calls `ps2::run()`. |
| `CMakeLists.txt` | The project, if `project = true`. |
| `report.txt` | What the analysis found: function sources, bindings, unresolved indirect jumps, unknown instructions, warnings, and the full function list. |

Files are rewritten only when their content changes, so recompiling after a
config edit rebuilds only what changed.

## `[analysis]`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `symbols` | bool | `true` | Use ELF symbols as function starts and sizes. Set to `false` if a game's symbols are misleading; names still work as function references in the config. |
| `jal_targets` | bool | `true` | Every `jal` target is a function start. |
| `data_pointers` | bool | `true` | Words in data sections that point into code, and `lui`+`addiu`/`ori` pairs that build a code address (callbacks, thread entries). A pointer that looks like a function start (after a `jr $ra` and its delay slot, at a stack-frame setup, or at the start of a section) becomes one; any other pointer becomes a label, which is how `.rodata` switch tables are found. |
| `prologues` | bool | `false` | Treat `addiu $sp, $sp, -N` right after a `jr $ra` (and its delay slot and any zero padding) as a function start. Finds functions that are only called indirectly; check `report.txt` after enabling it on a new game. |
| `jump_tables` | bool | `true` | Resolve `jr` through switch tables into a C++ `switch`. Unresolved `jr` still works, through the dispatcher. |
| `preempt_loops` | bool | `true` | Every loop back-edge is a safepoint where interrupts, timers, vsync and thread preemption happen. Turning it off speeds up tight loops, but code that spins on memory (a vsync flag, a DMA status) will hang. |
| `split_gaps` | bool | `true` | Code between known functions that nothing references becomes a function of its own instead of being left out. |
| `entries` | array of addresses | `[]` | Extra function starts. Inside a function whose size is known (from the config, a map or a symbol) they become mid-function entry points instead of splitting it. |
| `entries_files` | array of paths | `[]` | Files of extra entries, in the format the runtime writes to `ps2uc_missing.toml` (any `0x...` number outside a `#` comment counts). A file that does not exist yet is a warning, so you can list it before the first run. |

### `[[analysis.functions]]`

Explicit functions, the strongest source:

```toml
[[analysis.functions]]
name = "DrawHud"          # optional; default sub_XXXXXXXX
start = 0x00123400
size = 0x1F0              # or: end = 0x001235F0 (exclusive); both optional
```

### How starts and boundaries are decided

Function starts come from several sources, strongest first: config, function
maps, ELF symbols, the entry point, `entries`, JAL targets, data pointers,
code pointers, prologues, unclaimed code. When two sources name the same
address, the stronger one supplies the name and size. A function with a known
size extends to that size; weaker starts inside it become entry points into
it, and a stronger start inside it ends it early. A function without a known
size runs to the next start.

## `[stubs]`: replace functions with runtime implementations

| Key | Type | Default | Meaning |
|---|---|---|---|
| `auto` | bool | `true` | Functions whose name matches a runtime stub (`printf`, `memcpy`, `strlen`, ...) are replaced by it automatically. The generic handlers (`ret0`, `ret1`, `retm1`, `nop`) are never bound automatically. |
| `bind` | table or array | none | Explicit bindings, see below. |
| `exclude` | array of names | `[]` | Names that `auto` must leave alone, for example a game's `memcpy` with different semantics. |

`bind` takes either form:

```toml
bind = { "0x00123456" = "ret0", "sceCdInit" = "ret1" }   # target = handler
bind = ["ret0@0x00123456", "ret1@sceCdInit", "printf"]   # handler@target; a bare name binds the function of that name
```

A handler that does not exist is an error. `ps2-uncompiler stubs` lists the
handlers:

| Handler | Replaces |
|---|---|
| `ret0`, `ret1`, `retm1` | anything: returns 0, 1 or -1 in `$v0` |
| `nop` | anything: returns without touching `$v0` |
| `printf`, `puts`, `putchar`, `scePrintf` | output to the host's stdout |
| `sprintf`, `snprintf` | formatting into guest memory |
| `memcpy`, `memmove`, `memset`, `memcmp` | memory functions |
| `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strcat`, `strchr`, `strrchr` | string functions |
| `sceSifLoadModule`, `SifLoadModule` | IOP module loading, straight to the HLE IOP |

Replacing C library functions is optional (the recompiled originals work);
it is useful for console output and as a speed-up.

## `[functions]`: skip or override

| Key | Type | Meaning |
|---|---|---|
| `skip` | array of references | Not recompiled. A call logs `called skipped function NAME` (counted in the exit summary) and returns 0 in `$v0`. |
| `override` | table or array | The function is replaced by your C++ function, same two forms as `bind` (`{ target = "symbol" }` or `["symbol@target"]`). |

An override is a plain C++ function with no parameters that works on the
guest CPU state. Arguments are in `$a0`-`$a3` and `$t0`-`$t3` (EE ABI), the
result goes to `$v0`. The generated wrapper has already set `cpu.pc` to the
return address, so the function simply returns:

```cpp
// game_overrides.cpp, listed in [output] extra_sources
#include "ps2/runtime.h"

using namespace ps2;
using namespace ps2::regs;

void my_rand() { setU32(V0, 4); }   // deterministic, for testing

void my_draw_hud() {
    const uint32_t player = ru32(A0);                // first argument: a guest pointer
    const uint32_t health = read32(player + 0x40);   // guest memory (write32 to store)
    if (health == 0) callGuest(0x00141000);          // guest code, with optional arguments
    setU32(V0, health);
}
```

```toml
[output]
extra_sources = ["game_overrides.cpp"]

[functions]
override = { "rand" = "my_rand", "0x00140000" = "my_draw_hud" }
```

The recompiler declares these functions in `functions.h`; you provide the
definitions. If more than one setting applies to the same function, the
order of precedence is `override`, then `skip`, then `bind`, then `auto`.

Stubs, skips and overrides also apply when the game runs with `--interpret`,
so that mode remains usable for telling recompiler bugs from runtime gaps.

## `[[patches]]`: replace instruction words

```toml
[[patches]]
address = 0x00100abc
value = 0x00000000      # nop
expect = 0x0c040000     # the original word: jal 0x00100000
```

Patches are applied before analysis, so a patched branch or call changes the
control flow the recompiler sees. They are also written into guest RAM at
startup, so the interpreter, and code that reads its own instructions, see
the same words. If `expect` is given and the ELF holds a different word, the
recompile stops with `patch at 0x00100abc expects 0x0c040000 but the ELF has
...: this config was written for a different build`. Always give `expect`.

## Command-line overrides

`ps2-uncompiler recompile` options take precedence over the config file:

| Option | Overrides |
|---|---|
| `--elf PATH` | `[input] elf` |
| `--out DIR` | `[output] dir` |
| `--name NAME` | `[output] name` |
| `--files N` | `[output] files` (and selects multi mode) |
| `--single` | `[output] mode = "single"` |
| `--no-project` | `[output] project = false` |
| `--no-comments` | `[output] comments = false` |

Paths given on the command line are relative to the current directory, not
to the config file.

## A complete example

```toml
[input]
elf = "SLUS_123.45"
ghidra_map = "ghidra/functions.csv"

[output]
dir = "out/game"
name = "game"
mode = "multi"
extra_sources = ["src/overrides.cpp"]

[analysis]
entries_files = ["ps2uc_missing.toml"]                  # written by the last run, in the directory it ran in

[[analysis.functions]]
name = "VblankHandler"
start = 0x0010f200
size = 0x80

[stubs]
auto = true
exclude = ["memcpy"]                                    # this game's memcpy is not the standard one
bind = { "sceMcInit" = "ret0" }

[functions]
skip = ["sceIpuInit", "MoviePlay"]                      # IPU video is not emulated
override = { "0x00140000" = "game_draw_hud" }

[[patches]]
address = 0x0012ab40
value = 0x00000000
expect = 0x0c04b2c0                                     # skip a copy-protection check
```
