# Config reference

A game is described by one TOML file. Only `[input] elf` is required:

```toml
[input]
elf = "SLUS_123.45"
```

`ps2n analyze game.elf --write-config game.toml` writes a starter
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
| `elf` | path | required | The EE executable (`SLUS_123.45`, `SCES_...`, a homebrew `.elf`). IRX modules are rejected: they are IOP code (see `ps2n irx`). |
| `ghidra_map` | path | none | A function map, usually written by [tools/ghidra/ExportPs2nFunctions.java](../tools/ghidra/ExportPs2nFunctions.java). |
| `map_files` | array of paths | `[]` | More function maps, same format. |
| `programs` | array of paths | `[]` | The game's other executables, which it starts with `LoadExecPS2`: each an ELF, or a config file (`.toml`) for that ELF. See [Games with several executables](#games-with-several-executables). |

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

### Games with several executables

Many games are more than one ELF: a small boot program, the one `SYSTEM.CNF`
names, starts the game proper with `LoadExecPS2`, and further executables
(game modes, a movie player) start each other the same way. List them in the
boot program's config and they are recompiled into the same executable:

```toml
[input]
elf = "SLES_505.39"                                     # the boot program
programs = ["ACTION.UK", "driving.toml", "MOVIE.ELF"]
```

An entry is an ELF, recompiled with the default settings, or a config file
for that ELF when it needs settings of its own: its `[analysis]` (its own
`entries_files`, for example), `[stubs]`, `[functions]` and `[[patches]]`
apply, and its `[output] extra_sources` are built too. The rest of its
`[output]` comes from the boot program's config, and `[pnach]` is read from
the boot program's config alone. Each program's code goes to a
folder of the output named after its ELF file (`ACTION_UK/`).

At run time `LoadExecPS2` loads the file the game names, from the disc or
`host:`, and runs the code recompiled from that very file, recognised by its
CRC-32. A program the game loads by itself and jumps into is recognised too,
once its instructions are all found in RAM. An executable that is not listed,
or whose file changed since it was recompiled, runs on the interpreter
instead, with a warning. See [runtime.md](runtime.md#several-executables) for
what carries over.

## `[output]`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `dir` | path | `out/<elf name>` | Where the generated files go. |
| `name` | string | ELF name without extension (`SLUS_123.45` gives `SLUS_123`) | Name of the executable and of the CMake project. |
| `mode` | `"multi"` or `"single"` | `"multi"` | One source file, or several `recomp_funcs_NNN.cpp`. |
| `files` | integer 0-4096 | `0` | Number of `recomp_funcs_NNN.cpp` files in multi mode. `0` picks one file per ~16,000 instructions. Fix the count when a build system must know the file list before the recompiler runs (`ps2n_add_recompiled_game` does this). |
| `comments` | bool | `true` | Disassembly comment on every generated line. |
| `project` | bool | `true` | Write a `CMakeLists.txt` that builds the output against the runtime. Turn off when you add the sources to your own build. |
| `runtime_dir` | path | the tree the recompiler was built from | PS2Native source tree the generated `CMakeLists.txt` uses (also settable later as the `PS2N_ROOT` cache variable). |
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
| `entries_files` | array of paths | `[]` | Files of extra entries, in the format the runtime writes to `ps2n_missing.toml` (any `0x...` number outside a `#` comment counts). A file that does not exist yet is a warning, so you can list it before the first run. |

### `[[analysis.functions]]`

Explicit functions, the strongest source:

```toml
[[analysis.functions]]
name = "DrawHud"          # optional; default sub_XXXXXXXX
start = 0x00123400
size = 0x1F0              # or: end = 0x001235F0 (exclusive); both optional
```

### Where the code is

The code is what the ELF's executable sections hold, or its executable
segments when it has no section headers. Metrowerks CodeWarrior links code
and data into one writable, executable section (`main`), so its headers do
not say where the code ends. The symbols do: when every function symbol in
such a section lies below every object symbol, the code ends at the first
object, and the rest is data, scanned for code pointers like any other data.
Without symbols, or with objects among the functions, the whole section is
treated as code: data in it shows up in `report.txt` as unknown instructions
and functions that run off the end of the code.

### How starts and boundaries are decided

Function starts come from several sources, strongest first: config, function
maps, ELF symbols, the entry point, `entries`, JAL targets, data pointers,
code pointers, prologues, unclaimed code. When two sources name the same
address, the stronger one supplies the name and size. A function with a known
size extends to that size; weaker starts inside it become entry points into
it, and a stronger start inside it ends it early. A function without a known
size runs to the next start.

## `[vu]`: recompile micro code

| Key | Type | Default | Meaning |
|---|---|---|---|
| `code_files` | array of paths | `[]` | Files of VU micro code, in the format the runtime writes to `ps2n_missing.vu.toml`. `ps2n recompile` turns them into `recomp_vu.cpp`, for every executable of the game (only the boot program's config is read). |

Games upload micro code at run time, so it comes from a run. Every program
the VU interpreter runs is recorded, once however often and wherever it ran,
and at exit the runtime writes `ps2n_missing.vu.toml` next to the
`--missing-out` file. Each `[[image]]` in it is what a unit's micro memory
held when programs were first seen, with `entries` (where they started) and
`targets` (where recompiled code had to hand a program over to the
interpreter, as addresses in that image). Keep a copy of the file, list it
here, and recompile. After the next run, merge what the new file adds: images
with the same contents are one image, with all their entries and targets, and
a program two images hold is compiled once.

A program is told by its code, not its address: the pairs it reaches from its
start (its branches are relative, its calls come back to the pairs after
them) and where they sit from the start. Games often upload a program
wherever micro memory has room (in a play session, Agent Under Fire's
driving mission put its 23 VU1 programs at 400 addresses), so a recompiled
program runs wherever micro memory holds the words it was compiled from: the
runtime finds it by its first pairs, and it adds where it sits to every
address it reads or writes (TPC, the links of BAL and JALR, JR's returns).
Each block checks its own words where it runs, once after every change to
micro memory. It runs to its end, or hands the program over to the
interpreter, with the pipeline as it stands, in these cases:
- a branch or E bit in a delay slot;
- a JR or JALR to an address it was not told about (the image's `targets`);
- micro memory that no longer holds its code.

`ps2n vu <file>... -o <out.cpp>` writes the C++ alone, to look at.

```toml
[vu]
code_files = ["vu-code.toml"]   # a copy of ps2n_missing.vu.toml, merged after later runs
```

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

A handler that does not exist is an error. `ps2n stubs` lists the
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

## `[pnach]`: PCSX2's patch files

PCSX2's per-game `.pnach` files can patch a recompiled game because it keeps
every guest address. Support is partial: the widescreen groups used by the two
games in [games.md](games.md) work, while no-interlacing patches and some
patch operations do not (see the limitations below). The files are read from
wherever the config says: PCSX2 bundles them in `resources/patches.zip` (copy
the game's file out) and keeps the user's own in its `patches/` folder. ps2n
does not ship any.

```toml
[pnach]
files = ["patches/SLES-50539_992B46DC.pnach"]
groups = ["Widescreen 16:9"]
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `files` | array of paths | (required) | The patch files. PCSX2 names them `<SERIAL>_<CRC>.pnach`, with the CRC of the executable it loaded them for: the exclusive or of the file's 32-bit words. A name with another CRC than the boot program's gets a warning. |
| `groups` | array of strings | `[]` | The `[group]`s to apply, by their exact names, as PCSX2's per-game Patches page enables them (`Widescreen 16:9` is what its Apply Widescreen Patches setting enables). The lines before a file's first group always apply, as in PCSX2. |

The lines are applied when the game is recompiled: each executable's image
gets what PCSX2 would write to memory while it runs, once at its entry point
(places 0, 2 and 3) and as at every vsync (places 1 and 2), and the words
they change become `[[patches]]` of that executable, with the ELF's words as
`expect`. A condition (extended codes `D` and `E`) tests the image as the
lines left it. A file in the boot program's config applies to every
executable of the game, with the entry-point lines for the boot program
alone: PCSX2 keeps the boot executable's file while a game loads its other
executables by itself, as Agent Under Fire does. A group's `gsaspectratio`
(any `N:D`) goes with the boot program to the runtime, whose window shows
that shape under Auto ([runtime.md](runtime.md#running-a-recompiled-game)).

A line that writes to memory no executable of the game initialises (BSS, the
heap) cannot be applied to an image: the game fills that memory at run time.
Such writes go with the boot program to the runtime, which makes them as
PCSX2 does while the boot executable runs: the entry-point ones when it loads
the executable, the others at the start of every vblank, before the game
hears of it. Code: Veronica X's widescreen patch is all of this kind (its
camera matrix and its films' display size).

The recompile prints what each executable got:

```
pnach: SLES-50539_992B46DC.pnach [Widescreen 16:9]: SLES_505.39 0 words (6 conditions on memory it does not initialise), MOVIE.ELF 0 words (6 conditions on memory it does not initialise), ACTION.UK 1 word, DRIVING.ELF 4 words
  the patches ask for a 16:9 picture (gsaspectratio)
pnach: SLUS-20184_24036809.pnach [Widescreen 16:9]: SLUS_201.84 0 words, 3 writes to memory the game fills at run time, made by the runtime
```

What is read as PCSX2 reads it: `patch=place,cpu,address,type,data` with
the types `byte`, `short`, `word`, `double`, `extended`, `beshort`,
`beword`, `bedouble` and `bytes`, keys and names matched with their case,
`//` comments, and of the extended codes the writes (`0`, `1`, `2`), the
serial write (`4`) and the conditions (`D`, `E`, on a halfword or a byte),
where a false test skips the extended lines after it. Lines PCSX2 skips as
malformed, codes it ignores (`8`, `9`, `A`, `B`, `C`, `F`) and
`gsinterlacemode` (the runtime's deinterlacing is chosen with
`--deinterlace`) are warnings. Not covered, and an error when a chosen group
has them:

- the increment, copy, pointer and bitwise codes (`3`, `5`, `6`, `7`), which
  act on values the game keeps changing;
- `dpatch` (a patch placed by matching code) and patches to the IOP;
- conditions on memory no executable initialises: PCSX2 tests them every
  vsync, while ps2n counts them as false when it recompiles and the runtime
  makes no tests. A write to such memory under a condition that held for an
  image is not applied either, with a warning, nor is one that only another
  executable than the boot one makes (the runtime writes while the boot
  executable runs);
- a game that starts its executables with `LoadExecPS2`, for which PCSX2
  switches to the file named for each: `[pnach]` is read from the boot
  program's config only.

## Command-line overrides

`ps2n recompile` options take precedence over the config file:

| Option | Overrides |
|---|---|
| `--elf PATH` | `[input] elf` |
| `--program PATH` | adds to `[input] programs` (repeatable) |
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
entries_files = ["ps2n_missing.toml"]                   # written by the last run, in the directory it ran in

[[analysis.functions]]
name = "VblankHandler"
start = 0x0010f200
size = 0x80

[stubs]
auto = true
exclude = ["memcpy"]                                    # this game's memcpy is not the standard one
bind = { "sceMcInit" = "ret0" }

[functions]
skip = ["MoviePlay"]                                    # go straight past the intro movies
override = { "0x00140000" = "game_draw_hud" }

[[patches]]
address = 0x0012ab40
value = 0x00000000
expect = 0x0c04b2c0                                     # skip a copy-protection check
```
