# Phase 3 report: the first real games

| | |
|---|---|
| Phase | 3, the first real game ([ADR 0009](adr/0009-phase-3-first-game-killzone.md)) |
| Approved | 2026-11-11 |
| Ended | 2026-11-16, at the maintainer's word ([ADR 0027](adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md)) |
| This report | 2026-11-16, at the maintainer's word: asked what to work on next, the session recommended closing Phase 3 with a report and going on to Phase 4; the maintainer: "Yes" |
| Games | *Killzone* (`SCES-52004`); *007 - Agent Under Fire* (`SLES-50539`) |
| Platform | Windows 11 x64 only; Linux has never been built or run ([ADR 0008](adr/0008-linux-run-deferred.md)) |
| Where the detail is | [game-log.md](game-log.md), steps G0 to G18; ADRs 0009 to 0026 |

The brief asks of Phase 3: select one game the maintainer provides, attempt
genuine native recompilation, implement what is missing through reusable
solutions, compare the results with PCSX2, document compatibility, visual
issues and performance, and record limitations and regressions, avoiding
game-specific hardcoding. This report says where each of those stands, and
what Phase 4 has to decide first. Every number in it is from a run recorded
in the game log, on the date given there.

## What was done

**Every processor's code is compiled ahead of time.** The game's EE code
(G2, G3), the BIOS's EE code (G8, from the maintainer's dump, never in the
repository), the vector units' micro programs (G11) and the I/O processor's
code (G12) are each translated to C++ by PS2Native's own recompiler and
built into the host. None of PCSX2's recompilers for PS2 code runs in a
compiled game any more: the stand-ins [ADR 0003](adr/0003-no-jit-in-generated-games.md)
allowed for a time are gone. Code a build has not seen is interpreted once,
reported, captured and compiled by the next build. After its rebuild
nothing of the code the runs have met is interpreted, but the BIOS's 48
instructions at boot and a few where compiled code hands the rest of a block
over on purpose, each with its cause ("G8", "G18").

**Time is counted exactly as PCSX2's recompiler counts it**
([ADR 0011](adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md)):
the runtime keeps the record of blocks PCSX2's EE, VU and IOP recompilers
would hold, so a compiled run is the stock run, sync point by sync point,
and is checked as such.

**Reusable solutions, nothing written for one game.** What each game needed
was built as a general mechanism and tested on programs of PS2Native's own:
games made of several executables that load one another (G18, a test
program loading two programs of its own), PCSX2's patches and the EE
overclock a patch asks for (G15 to G17, held to stock PCSX2 at five rates),
sessions in a window written down and run again (G13), the compiled code
for blocks that check themselves (G18). The only per-game inputs are the
build's captures (in the git-ignored build tree), controller scripts
(`tests/games`) and PCSX2's own patch files.

**A game can be played**: a window at the console's pace with sound, the
keyboard and a controller (G4), shown by default on Vulkan at three times the
console's resolution in a picture of 1920 by 1080 (G14), with the game's
widescreen and frame-rate patches where PCSX2 has them (G15, G16).

## Compatibility

| Game | How far | By whom |
|---|---|---|
| Killzone | Boot, logos, intro film, menus, the first level: 17,500 frames by script (menus, three and a half minutes of play, being killed, Retry) | Script; the maintainer into the first level (2026-11-12, "Everything looked good"; 2026-11-14, 2 minutes 48 seconds) |
| Agent Under Fire | Language selection, films, menus, missions 1 to 5, the driving missions (`DRIVING.ELF`) among them | A script into the first mission (9,000 frames); the maintainer in three sessions (14, 37 and 23 minutes, 2026-11-16), the last two from a save on the memory card |

Not run yet: Killzone beyond the first level, its multiplayer side and its
four `.REL` modules; Agent Under Fire beyond the fifth mission, and its
missions in the other five languages, which are executables of their own
and are interpreted until a build is given them.

## Against stock PCSX2

| Run | Sync points, all the same in place and time | Registers | Pictures, sound |
|---|---:|---|---|
| Killzone, the level script (17,500 frames) | 93,221,937 | the EE's, COP0's, VU0's, VU1's and the IOP's the same throughout, but the FPU's sign of a zero (below) | the same picture at 1,750 frames, the sound hardware the same at 350 moments |
| Killzone, with both patches and the 130% overclock | the same in place and time | the FPU's sign of a zero aside | (G17) |
| Agent Under Fire, the script (9,000 frames) | 49,981,658 (50,091,671 with the widescreen patch) | every one the same | the same picture at 180 frames |
| Agent Under Fire, the first session run again (42,322 frames) | 223,104,024 | VU0's FBRST from about frame 40,690 on (below), every other the same | the same at 170 frames and moments |
| Agent Under Fire, the second session run again (105,784 frames) | 507,570,237 | VU0's from sync point 247,873,421 on, at the place of the first session's FBRST difference | the same at 106 frames and moments |

Each compiled run is also the run with everything interpreted, at every
event test, where that has been run (Killzone's first 4,200 frames, 47.7
million event tests; Agent Under Fire's script, 56.9 million).

**The two register differences are on purpose**, instructions where
PS2Native follows the console and stock PCSX2's recompiler does not
([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)): the
sign of a zero that `max.s`, `min.s`, `abs.s` and `neg.s` leave in an FPU
register, and `ctc2` to VU0's FBRST asking for a reset, which clears the
reset unit's half of the register on the console (the ps2autotests
recording `cpu/vu0_macro/transfer`) and not under PCSX2's recompiler. In
neither game did either change where the run went or when.

**On the console's own recordings** (ps2autotests): on no line of their
output is stock PCSX2 right and PS2Native wrong, the criterion of
ADR 0005, tested by `never-further` on all 55 programs. Measured on
2026-11-11: PS2Native matches the console on 40 programs, stock PCSX2 on 39
with its recompilers and 38 with its interpreters. Four programs are listed
as not the stock run, each with its cause
(`tests/same-as-stock-known-differences.cmake`): three where the console is
followed, one where stock PCSX2's recompiler crashes. 533 tests, all
passing (2026-11-16).

## Visual and audio issues

- **Killzone:** none known. The maintainer found nothing wrong in the
  window; with the widescreen patch the game draws itself wide.
- **Agent Under Fire:** artifacts in the HUD, the second mission most;
  stock PCSX2 shows them too, and PCSX2 has no patch for them. They are left
  to PCSX2 (ADR 0026: "Ok let's leave them"); a setting of PCSX2's own that
  removed them would be put to the maintainer. With the widescreen patch the
  HUD stretches with the picture, as in PCSX2. In the fifth mission "a couple
  hiccups": not looked into. The session met 4 blocks of micro code no
  build had compiled, and the renderer compiles a shader the first time an
  effect is drawn; which it was is not known.
- **Not compared:** the hardware renderer's pictures with the software
  renderer's, either with a console's, or a run's pictures with a console's.
  The comparisons with stock PCSX2 use the same renderer on both sides.

## Performance

All against stock PCSX2 timed beside it on the same computer, on the
processor's fast cores; stock PCSX2's time is 1.

| Run | PS2Native | Notes |
|---|---:|---|
| Killzone, the level run | 0.97 (play 0.95) | with profile-guided optimization trained on another run of the level (G12) |
| Killzone, with both patches | 1.00 (play 0.98) | and with the 130% overclock, the same (G16, G17) |
| Agent Under Fire, the script | 1.19 (play 1.12) | without profile-guided optimization (G18) |
| Agent Under Fire, the first session | 1.13 | (G18, "The maintainer's first session") |
| Agent Under Fire, the second session | 1.26 | of it the fourth mission 1.8 at the median, the rest 1.12 |

In the window, frames a second where the console shows 50: Killzone 49.7 to
50.0 (G14, G16); Agent Under Fire's script 49.96 and its third session
49.99; its first session 49.58, the explosion in its second mission slowed
down while its micro code was interpreted the first time; its second
session 48.05, its fourth mission slowed down by a wait loop at 7.6 times
stock PCSX2's time, put right the same day ("G18", "Compiled code checks
such a block itself").

Where the time goes, and why compiled code is not faster still: the code is
compiled before the game runs, so what PCSX2's recompiler learns at run time
and builds into its machine code (which page of memory the game writes to,
where its blocks begin and end) PS2Native's code finds out by asking as it
runs. More than a third of Killzone's compiled code's time was reaching
memory through the core's address table, where PCSX2's recompiler uses one
instruction and a fault ("G6"). Profile-guided optimization took 5 to 7
percent off Killzone's play ("G10"); it has not been trained on Agent Under
Fire.

## Regressions

None open. Defects found on the way and put right, each in the game log:
time counted by a rule about the program alone, which put Killzone's intro
film four frames late ("The timing difference"; the record of blocks of
ADR 0011 since); a cycle-rate formula called at every block's end, 1.22
times stock PCSX2's time ("G17"); a session run again without the memory
card it was played with ("G17"); Agent Under Fire's fourth mission slowed
down ("G18").

## Limitations

- **Windows only.** Linux is required ([ADR 0001](adr/0001-platform-priorities.md))
  and has never been built or run; the window is not written for it.
- **A BIOS image and the disc image are needed** at run time.
- **Time is counted as PCSX2 `v2.9.170`'s recompiler counts it**, two of its
  defects included ([ADR 0010](adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md),
  [ADR 0013](adr/0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)).
  Moving to another PCSX2 means making the comparisons again.
- **New code is interpreted until the next build.** Each part of a game
  played for the first time brings some (each of the maintainer's sessions
  so far did), and a build needs what the runs captured.
- **A build holds one game's executables.** Going from one game to the other
  is a rebuild.
- **Agent Under Fire:** only the English missions are compiled; no
  profile-guided optimization; the fourth mission takes 1.8 times stock
  PCSX2's time, the compiled code still calling the runtime twice each time
  round a wait loop whose page is not write-protected, where PCSX2's code
  compares a few words inside the block (ADR 0026).
- **What the providers stop at:** a few forms of IOP and VU code neither game
  has, each named when met (game log, "Open").
- **What a longer run could meet:** PCSX2's recompilers empty their caches
  when full, which PS2Native cannot know. It has not happened in any run
  compared with stock PCSX2, where the runs would have parted.
- **The EE overclock a patch asks for is read from the words of its note**
  ("G17").
- **No application yet:** no wizard, no packaging; everything runs from the
  development tree, built with Visual Studio.

## What Phase 4 has to decide first

The brief's Phase 4: the compilation wizard, game recognition and settings,
the compilation pipeline, packaged native applications, diagnostics, more
platforms. What Phase 3 leaves for it to decide:

1. **How a user's computer compiles a game.** Today it takes Visual Studio's
   compiler and the development tree. Either the wizard brings a compiler of
   its own (clang and lld), or a game's code is compiled into a module that a
   prebuilt runtime loads (the assessment, risk 10).
2. **What a packaged game holds and needs**: the BIOS's compiled code, made
   on the user's computer from their own dump, as a file of its own beside
   the game ([ADR 0007](adr/0007-bios-code-translated-ahead-of-time.md)); the
   disc image; where its patches come from ([ADR 0025](adr/0025-how-a-game-is-shown-and-its-patches.md)
   leaves that to Phase 4); the window's defaults, which the wizard starts
   from (G14).
3. **New code on a user's computer.** In development a run captures it and
   the next build compiles it. A packaged game needs a way to do that, or to
   arrive with it.
4. **Speed without a profile.** Profile-guided optimization needs a training
   run of the game. Without it Agent Under Fire takes 1.12 to 1.19 times
   stock PCSX2's time; Killzone's play took 1.05 at the end of G10, the last
   time it was measured without one. The brief's bar is parity.
5. **Linux**, required and never run.
