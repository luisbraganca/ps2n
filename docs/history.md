# How PS2Native got here

A record of what was done and decided, phase by phase, as each session wrote
it into `CLAUDE.md` on the day. It was moved here on 2026-11-27
([ADR 0061](adr/0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md)),
word for word, and put in the order things happened.

**It says what was true when it was written.** "Is next", "under way" and "so
far" in it speak of their own day. Where things stand now is at the top of
[`CLAUDE.md`](../CLAUDE.md). The detail of every step is in the logs
([poc-log.md](poc-log.md), [game-log.md](game-log.md),
[app-log.md](app-log.md)), and every decision in [adr/](adr/README.md).

A step adds a short paragraph at the end of its phase here: what was asked,
what was done, what was measured, and where the log has the rest.

## Phase 1: the references examined


| Project | Revision |
|---|---|
| PCSX2 | tag `v2.9.170`, commit `a886f8ef` |
| AnyPS5 | `3c9276b7` |
| PS2Recomp | `e5636981` |
| PS2 Web Recomp | `76554ba4` |
| ps2autotests | `97469ffb` |

They were cloned into a temporary directory for that session and are not part
of this repository.


## Phase 2: the proof of concept

Phase 2 (proof of concept) is done on Windows; its Linux step (P5) is
deferred by the maintainer. Its steps and criteria are in section 14 of the
assessment, and [docs/poc-log.md](poc-log.md) records what was done and
measured. Its last section lists the questions that are open, those waiting
for the maintainer first. Record every decision the maintainer makes as an
architecture decision record under `docs/adr/`.

## The first game's files

The game is the maintainer's own copy, used where it is: the disc image and
the files extracted from it are in `C:\ps2\killzone`. Read them, never copy
them, and keep everything generated from them (C++, captures, pictures) in
the git-ignored build tree.

## Phase 3: the first real game

What follows is Phase 3's history. [docs/game-log.md](game-log.md) has its steps, their pass
criteria and what has been measured; its status table at the top says where
things stand. On 2026-11-12 every step was done as far as the game's first
level, the maintainer had played it, and the compiled game took 1.35 times
stock PCSX2's time. The maintainer then chose a speed step inside the phase
(G6, ADR 0015), which ended on 2026-11-12 with the compiled game at 1.21 to
1.23 times stock PCSX2's time. The next step, G7 (ADR 0016), was done on
2026-11-12: the game's VU0 instructions have semantics of PS2Native's own,
which the self-test holds to PCSX2's recompiler, and the level run is still
the stock run, at 1.21 times its time. G9 (ADR 0018), done on 2026-11-12
in a worktree of its own, has compiled code carry out the VU0 instructions
itself where their block lets it: 1.19 times stock PCSX2's time. G8 (ADR
0017), done on 2026-11-12 and rebased onto G9, compiles the BIOS's EE code
ahead of time, from the dump the build is given: 51 of its instructions are
still interpreted, at boot, and the level run takes 1.18 times stock
PCSX2's time. G10 (ADR 0019), performance as the maintainer's research lays
it out, is under way, in the worktree `..\ps2n-vu0` (branch `perf`) and in
the main tree beside it (ADR 0021): profile-guided optimization of the
generated code, loops that have reached a fixed point skipped, the COP2
instructions carried out by compiled code, calls through a register made
by compiled code itself, and the way a load or store seldom takes made
short (the game's code 14% smaller). All of it together, with
profile-guided optimization, takes the level run to 1.04 times stock
PCSX2's time (play alone 1.04), still the stock run. On 2026-11-13 a third
session, in the main tree, measured the website's two speed charts again
(the first 4,200 frames 1.09 times stock PCSX2's time with nothing drawn,
from 1.35) and made `lq` and `sq` loads and stores on the locals and a block
start's call of the runtime one of one argument: the level run the same,
1.04, the host 12 MB smaller. The same day, at the maintainer's word, the
FPU's arithmetic: its registers held in SSE registers, and the bits its
additions keep past the guard bit looked up instead of branched to. With
profile-guided optimization the level run takes 1.01 times stock PCSX2's
time, its play as long as stock PCSX2's, still the stock run; there the
maintainer ended G10. Then, at the maintainer's word, step G11 (ADR 0022):
the vector units' micro programs compiled ahead of time, begun with a
measurement (`ps2n-host -vu-report`). In play VU1's micro memory is
rewritten about 40 times a frame, with 208 distinct pieces of code written
at 5,405 places, which PCSX2's VU recompiler makes 6,088 programs of; the
vector units took about a third of play's CPU-thread time. Then, by the
maintainer's choice, VU1's registers held to the stock run at every sync
point, and PS2Native's own VU interpreter (`runtime/vu_interp.h`) before
the translator. On 2026-11-13 the interpreter gave microVU's results on
16,000 random micro programs (`selftest/tables-and-semantics`), which took
following microVU's register allocation, VI and XMM, as well as its
instructions; and with both VUs on it the level run is the stock run at
every one of its 93,221,937 sync points, VU0's and VU1's registers
included. On 2026-11-14 the translator: what microVU's second pass decides
for a block is made once, as a list of steps (the block's plan,
`core/include/ps2n/vu_plan.h`), the same for the same code wherever the
game writes it, and each plan is compiled ahead of time as one C++
function (`ps2n-recomp vu`), which the host finds by a fingerprint of the
plan; blocks go on to each other as microVU's do, and the program at a
start or a jump is not looked for where it cannot have changed. Over the
level run VU1's 42,173 blocks in 6,088 programs have 589 plans, and after
one run and one rebuild every block run is compiled code's: nothing of the
game's micro code is interpreted, and nothing of it runs on PCSX2's VU
recompiler. 20,000 random micro programs give microVU's results, 1,500 of
them on compiled blocks alone; every test passes (468), PS2Native's
configurations on its own VU provider; the level run is the stock run at
every sync point, VU0's and VU1's registers included, and takes 0.97 times
stock PCSX2's time, its play 0.95. G11's pass criterion is met. Then, at
the maintainer's word on 2026-11-14 ("Let's work on the I/O processor"),
step G12 (ADR 0023): the IOP's code, the BIOS's modules and the game's,
compiled ahead of time, begun with a measurement (`ps2n-host -iop-report`).
Over the level run the IOP runs about 10,600 small blocks of code, nearly
all of them formed in the first 250 frames; 85% of its cycles pass in
stretches that end where its kernel's idle loop is, and the host spends
16% of the run in its provider, most of it around the IOP's code and not
in it. The IOP's place and registers are held to the stock run at every
sync point, as the EE's and the VUs' are. PS2Native's IOP provider
(`runtime/iop_provider.h`) keeps the record of blocks PCSX2's IOP
recompiler keeps, and carries a block out by its interpreter or by the
function a build compiled for it (`ps2n-recomp iop`), which it finds by a
fingerprint of the block's instructions wherever the IOP's kernel loaded
them. After one run and one rebuild every one of the level run's 911
million block runs of IOP code is compiled code's (8,023 blocks): nothing
of the IOP's code is interpreted, and nothing of it runs on PCSX2's IOP
recompiler. 4,000 random IOP programs give that recompiler's results,
1,500 of them on compiled blocks alone; every test passes (469),
PS2Native's configurations on its own IOP provider; the level run is the
stock run at every sync point, the IOP's place and registers included,
once both runs start with the console's clock at the same time
(`ps2n-host -clock`), and takes what it took with PCSX2's IOP recompiler
under it: 0.97 times stock PCSX2's time, its play 0.95. G12's pass
criterion is met: in the run into the first level no recompiler of
PCSX2's for PS2 code carries anything out, and none of ADR 0003's
stand-ins is in use. The next step, at the maintainer's word the same day
(ADR 0024), is G13: further into the game before Phase 3 ends, the
maintainer playing on in the window and what each session had to
interpret compiled into the next build. The first session, the same
evening (2 minutes 48 seconds, into the first level, on PCSX2's hardware
renderer), interpreted none of the game's EE code, 26 blocks of VU1's
micro code and 791 blocks of the IOP's, 772 of them in the memory card's
modules (the scripted runs have no memory card). They are compiled into
the build since; every test passes, and the scripted runs are still the
stock run and the interpreted run. At the maintainer's word the same
night: sessions are recorded, to be run again and compared as the
scripted runs are (ADR 0024), and the steps after G13 are G14 to G16
(ADR 0025): the window's defaults (Vulkan, a picture of 1920 by 1080, the
game drawn at three times the console's resolution), then PCSX2's
patches, with a game's widescreen and frame-rate patches applied by
default when there are any. G14 was done that night: the window's
defaults are PCSX2's Vulkan renderer, three times the console's
resolution and a picture of 1920 by 1080, full screen where a window that
size does not fit, the maintainer's choice. G15 the next day: the game's
widescreen patch applied by default, taken from the installed PCSX2's
archive; Killzone in 16:9, nothing of its code interpreted, and a run with
the patch is the stock run with it, the FPU's sign of a zero aside, and
the interpreted run. G16 the same day: the game's frame-rate patch applied
by default with it (the group of its patch file whose name says FPS, none
where several do), at first without the EE overclock its note asks for.
In the first level Killzone shows 41 new pictures a second where it showed
25; the code the patch changes is compiled as a second version of its
function, nothing interpreted after one rebuild; a run with both patches
is the stock run with them, the FPU's sign of a zero aside, and the
interpreted run, and takes 1.00 times stock PCSX2's time with the same
patches, its play 0.98 (without them 0.975 and 0.95, as before); in the
window the console's pace, as stock PCSX2 keeps it. G17 the same day, at
the maintainer's word: the EE overclock a patch asks for in its note
applied with it (130% for Killzone, with which stock PCSX2 shows 46 new
pictures a second in play where 41), and time counted as PCSX2's
recompiler counts it at that setting, at every rate it has (every test
program held to stock PCSX2 at five of them; 519 tests, all passing). The
level run with both patches and the overclock is the stock run with them,
the FPU's sign of a zero aside, and the interpreted run, nothing of it
interpreted after one rebuild, and takes 1.00 times stock PCSX2's time
with the same setting, its play 0.98, a block's cost read from a table
made when the setting is read (calling PCSX2's formula at every block's
end took 1.22); in the window the console's pace. A session run again now
gets a new memory card where it began without one, as the window did. G18
the same evening, at the maintainer's word ("let me test using agent under
fire"; then "Build multi-program support", ADR 0026): a second game,
*Agent Under Fire*, prepared for the maintainer to play, and games made of
several executables. Its boot executable loads three others itself, over
the same addresses, which PCSX2 never sees: each is now compiled ahead of
time, and the runtime tells from memory which is loaded (a test program
does the same; 533 tests, all passing). Over a script into its first
mission nothing of it is interpreted; the run is the stock run at every
sync point, every register included, and the interpreted run, and takes
1.19 times stock PCSX2's time without profile-guided optimization; in the
window the console's pace. The build tree holds its EE code in place of
Killzone's. On 2026-11-16 the maintainer played its first two missions:
HUD artifacts that stock PCSX2 shows too are left to PCSX2 (ADR 0026);
what the session had to interpret is compiled in since, and the session
run again is the stock run in place and time at every sync point (VU0's
FBRST aside, where PS2Native follows the console) and takes 1.13 times its
time; the explosion that slowed the session down, its micro code
interpreted then, takes 0.46 of the console's time now. In the second
session, the fourth mission slowed down: it waits in a small loop whose
page is not write-protected, and compiled code asked the runtime about its
blocks at every execution, 7.6 times stock PCSX2's time. At the
maintainer's word ("Fix it"; "we really need to fix this") the image's
table names such a block by a record of the runtime's, and compiled code
has its words compared at every execution as PCSX2's code does
(`checkedAnswer`): the mission takes 0.27 of the console's time at the
median, from 1.13, 1.8 times stock PCSX2's, and the session run again is
the stock run in place and time at every sync point. The third session
(the fifth mission) kept the console's pace in the window. G13 goes on as
the maintainer plays further. Asked what next, the session recommended
closing Phase 3 and going on to Phase 4; at the maintainer's "Yes" it wrote
the Phase 3 report ([docs/phase3-report.md](phase3-report.md)):
compatibility, the comparisons with stock PCSX2, visual issues,
performance, regressions, limitations, and what Phase 4 has to decide
first. At the maintainer's word ("1. Yes"), Phase 3 ended there and
Phase 4 began (ADR 0027).

## Phase 4: the user application

Phase 3 (the first real game: Killzone, then a second) ran from
2026-11-11 to 2026-11-16; its report is
[docs/phase3-report.md](phase3-report.md). Phase 4 (the user
application) was approved on 2026-11-16 (ADR 0027), and the maintainer
decided its design the same day ([docs/phase4-proposal.md](phase4-proposal.md),
ADR 0028): the wizard brings its own `clang` and `lld`, a game's code is
compiled on the user's computer into a module that a prebuilt runtime
loads, a packaged game is a folder, the wizard is Qt 6, Windows first. Its
steps are P4.1 to P4.7, each with its pass criterion
([docs/app-log.md](app-log.md)). The first, P4.1, was measured the
same day: with the compiled code in a module of its own that the host
loads (`build.ps1 -Module on`), every test passes and Agent Under Fire
takes the time it takes with the code built into the host. The maintainer
chose a loader of PS2Native's own, which would let compiled code reach the
core's variables without the import table, if it made the game faster, and
the import table otherwise. The code such a loader would give measured
slower on 2026-11-17 (ADR 0029), so a module reaches the runtime through
its import table; P4.1 is done. P4.2 was done the same day: the generated
code is compiled against PS2Native's own description of what it reaches of
PCSX2's core (`ps2n/pcsx2_layout.h`, compared with PCSX2's own when the
runtime is built and when the host starts), with no system header and no
standard library, into four modules (the programs' EE code, the BIOS's,
the micro code, the IOP's code) linked with the host's import library and
nothing else; every test passes in all three build trees, Agent Under
Fire's script is the stock run at every sync point from them, the
maintainer's sessions replay at every fingerprint, and they take what
P4.1's module took. P4.3, the toolchain the wizard brings, was done the
same day: LLVM's own 22.1.3 at the maintainer's choice (ADR 0030), a folder
of 254 MB (`scripts\windows\kit.ps1`) whose one command compiles Agent
Under Fire from its disc image; on the maintainer's Windows 10 laptop,
without Visual Studio, it compiled the game in about 8 minutes into code
the same byte for byte as this computer's, and the game played there for 15
minutes within 3% of the console's pace, a run this computer replays at
every fingerprint. That run found what a session record lacked (the patches
as applied, the BIOS's ROM1 and ROM2), put right. P4.4, the packaged game,
was done the same day, with Microsoft's C++ runtime in the folder at the
maintainer's choice (ADR 0031): a game is a folder that plays it
(`make-game.ps1 -Title`), started from a launcher named after the game,
which starts the runtime beside it with the folder's settings, without a
console; Killzone and Agent Under Fire played from their folders at the
console's pace, nothing of their code interpreted, and the development
host's sessions run again from them (the maintainer's four of Agent Under
Fire among them) are the sessions at every fingerprint, and the other way
round. Killzone's code needed a module to
have its own stack probe (`__chkstk`, `runtime/module_entry.cpp`), which a
test of the module tree holds. P4.5, the wizard, is under way: at the
maintainer's word it goes through all of PCSX2's settings that apply to a
game (ADR 0032), PCSX2's own per-game pages compiled unchanged into
`ps2n-wizard` (`wizard/`), what PS2Native carries out in PCSX2's place shown
but fixed; a game's settings are a file in its folder that the runtime
reads (`ps2n-host -packaged`); the five steps make a game from its disc
image into a folder that plays it, `PS2Native.exe` at the root of the
folder `kit.ps1` makes (Agent Under Fire in 336 seconds, every default
accepted, played from its folder at the console's pace). On 2026-11-18
the maintainer made a game the project had never run with it, *Gran
Turismo 3* (`SCES-50294`), without a command line: its boot executable is
a loader, the game's code is loaded from its data, so the first session
interpreted it and was slow (47.6 frames a second of 60); the folder made
again from what the session wrote down runs that session in a fifth of the
console's time, nothing interpreted, the session at every fingerprint, and
the maintainer played it from it at the console's pace (59.69 of 60). A
session in a folder now writes the BIOS's capture too, and the aspect
ratio is 16:9 by default (ADR 0032). P4.5 is done, the maintainer's run
counted for its criterion (ADR 0033); what was left of it is a step of its
own, P4.8, the folder for others, done before the folder goes to anyone but
the maintainer. P4.6, new code in play, met its criterion the same day: at
the maintainer's choice every game's folder carries its own toolchain (ADR
0034), and when a session ends its launcher starts `ps2n-remake`, which
compiles what the session met, only what changed, in the background, with
an icon and a notification, for the next start; in a fresh folder of Gran
Turismo 3 everything its first session interpreted was compiled at the
next start. At the maintainer's word, having tried it (ADR 0035), a
notification says at once that it compiles and another when it is done,
and a notice over the picture says when the game runs code met for the
first time. The maintainer then played a fourth game made with the
wizard, *Jak and Daxter* (`SCES-50361`), from its folder: 39 minutes and
then 23 at the console's pace, 0.17% and then 0.04% of the EE's cycles
interpreted, what each session met compiled about a minute after it. P4.8,
the folder for others, is next, before P4.7, at the maintainer's word, and
was begun the same day (ADR 0036): a disc in any format PCSX2 plays, read
through PCSX2's own readers by the runtime (`ps2n-host -disc-executables`,
`-disc-extract`); the executables compiled chosen in the wizard's first
step; PCSX2's archive of patches fetched at the user's word where PCSX2 is
not installed, or given; a fixed setting written into a game's settings
file refused by the runtime; and the licence of everything in the folder
(`licences\`, Qt's made from Qt's own SBOMs). On this computer the wizard
made a fifth game the project had never run, *Silent Hill 3*
(`SLES-51434`), from the maintainer's `.chd`, as on a computer without
PCSX2, its patches fetched, in 155 seconds, once the recompiler cut into
pieces a function it had made of a megabyte of the game's data (its
executable one segment, data with code). The maintainer ran the criterion
the same evening on their Windows 10 computer, which has no PCSX2: Silent
Hill 3 made there with the wizard from its `.chd`, its patches fetched at
the button, four sessions played ("Everything went flawlessly"); P4.8 is
done (ADR 0037). With it, at the maintainer's word, a game does not play
while it compiles: started while its own recompile compiles, its launcher
waits for the compile, in a window that says so, and then plays with it,
which the maintainer confirmed the next day. P4.7, Linux, the step left,
is next, at the maintainer's word ("4. Ok", 2026-11-19), built and tested
first on a Linux inside this computer, WSL 2 with Ubuntu 22.04 (ADR 0038).
It was begun the same day: PS2Native builds on Linux with LLVM's 22.1.3 and
PCSX2's own dependencies, and every test passes there (534), the compiled
code in the host and in shared objects of its own; the game's window is an
X11 one, the notice over its picture; the launcher, the wizard and the
recompile work there (`wizard/platform.h`), and `scripts/linux/kit.sh` puts
the Linux folder together, with the libraries a desktop Linux may not have
taken from Ubuntu 22.04 into it. With it, inside that Linux, Silent Hill 3
was made from the maintainer's `.chd` as without PCSX2 (123 s), played from
its folder by its own launcher at the console's pace, what its first
session met compiled in the background and taken at the next start, and the
launcher waited for that compile when started during it. The maintainer's
run on their own Linux machine is next. Found with it:
the notice over a game covering the screen on Windows showed only at a
session's start, which disallowing exclusive full screen put right (ADR
0039). Then, at the maintainer's word (ADR 0040), a game no longer compiles
in the background: what a session met is compiled at once in a window of
its own, in front, with Later, which closing it is too; and the game never
starts with anything left to compile: before it starts, it compiles what is
left in the same window, Later there leaving it unstarted. Checked on
Windows and inside the Linux. The Linux laptop not being the maintainer's
to use, their own run inside this computer's WSL counts for P4.7 (ADR
0041), on OpenGL there (the RTX through Mesa's D3D12 driver: Silent Hill 3
at the console's pace), Linux then described as tested under WSL 2; a
desktop shortcut starts the Linux `PS2Native` for it. The maintainer made
Jak and Daxter with it and played five sessions, what each met compiled in
its window, the later ones on compiled code alone at about 90% of the
console's pace inside WSL; WSL's OpenGL leaves parts of characters
transparent, which PCSX2's software renderer does not. At their words ("I
guess it's good"; "Let's create releases on Github") P4.7 is done, and with
it every step of Phase 4; releases go on GitHub, the first 0.1 (ADR 0042),
the version the file `version.txt`, which the maintainer published on
2026-11-19. On 2026-11-20, at the maintainer's word, a speed step (ADR
0043), measured in the configuration users get, a game's folder (copies in
`build\measure`, never the maintainer's own): compiled games took 1.07 to
1.24 times stock PCSX2's time on the maintainer's sessions run again, and
take 1.06 to 1.19 after the changes kept (compiled code's accesses to
devices, the parallel instructions in SSE); with the EE's and the IOP's
interpreters made faster, Gran Turismo 3's first session takes 77.5 s
where 108.8 s, and 6% less again
where other games' folders give a new game the BIOS's kernel they met (the
wizard, `BiosCapturesBeside`). A profile of another session gave Jak and
Daxter 7.5% more; at the maintainer's word a game's folder learns from its
user's own play (ADR 0044): the order of its functions, as its sessions ran
them, is in since 2026-11-21 (about 0.4%, measured with care; a first link
by hand had seemed to give 2.0%); a profile sampled while the game plays,
tried the same day, gave nothing that shows (clang could place a quarter of
it: Windows' line tables have no discriminators); at the maintainer's word
(ADR 0045) a folder takes its profile by counting instead, which gave the
7.5%: after a session of at least 3 minutes, once per game, on a run again
of its first 3 minutes without a window, never keeping the game from
starting, measured on a second game (Gran Turismo 3) before it goes into a
folder. Measured and built the same day, counted at clang's IR level
(`-fprofile-generate`, 8.4% where the front end's gave 6.7%): through a
folder's own path, after a session of the maintainer's, Jak and Daxter's
round took 174 s and the game 8.0% less time, Gran Turismo 3's 95 s and
3.6%; the same path runs on Linux under WSL 2, every test passing there and
on Windows. The docs/app-log.md section "The speed step" has every number.
Then, at the maintainer's word ("Speed: match PCSX2", ADR 0046), compiled
games as users get them (a folder with its own counts) were measured against
stock PCSX2: 1.06 to 1.11 times its time (Silent Hill 3's start-up 1.25).
Kept since: what PCSX2's recompiler decides of a COP2 instruction decided by
the recompiler where every block compiled code can run it in agrees, the
runtime comparing before a block runs (`core/include/ps2n/rec_cop2.h`; Jak
and Daxter 8.2% less without counts); and a micro program's run entered at
less cost (Jak and Daxter 1.5 to 2%). Tried and taken out: accesses to
devices asking only where they may fault, and an IOP block's way out at less
cost. As users get them now: Jak and Daxter 1.04, Gran Turismo 3 1.06, Agent
Under Fire 1.08, Killzone 1.05 (not counted). A folder's compile now counts
its toolchain in what it compiled from, so a folder brought up to date
compiles itself again. The maintainer's answers the same day ("1. Yes",
"2. Yes", "3. Ok"): committed and pushed, the new programs into their game
folders, and next the runtime itself (PS2Native's runtime and PCSX2's core,
34% to 50% of a run's processor thread) built with a profile of play,
measured first, against stock PCSX2 built without one. Measured that night:
PCSX2's own releases are built with link-time optimization, which the build
here had left out; stock PCSX2 built so takes 1.5 to 4% less time, and against
it compiled games as users have them take 1.07 to 1.11 times its time. With
the runtime built with link-time optimization and a profile of play (the
functions no training run ran left out of it) they take 0.99 to 1.00 times
it, a game the profile never saw nearly as little, and a game's first session
11% less than today. The folders were brought up to date the same evening.
Every test passes with such a runtime on Windows and on Linux but three
comparisons of one console-recorded program, disabled there for a defect of
PCSX2's found with it (its interpreted masked VIF unpack reads its mask tables
128 bytes low). The build's options for it are in, off by default
(`build.ps1 -RuntimeLto`, `-RuntimePgo`). The maintainer's answers on
2026-11-22 (ADR 0047: "1. Yes", "2. Yes", "3. Yes", "4. Yes, write the text,
I'll change it", "5. Yes", "6. Yes", "7. Ok do it"): stock PCSX2 is timed built
as its releases are, without a profile, and the website's chart is corrected to
it; a release's runtime is built with link-time optimization and the profile of
play, and the tree the folder is made from is built so; the maintainer's game
folders get that runtime; the maintainer tells PCSX2 of the defect themselves
(the session wrote no text for it, ADR 0010); what was measured is committed
and pushed; a folder counts its profile after a session of 3 minutes of the
game's own time (9,000 frames at 50 frames a second); and 0.2 is next, with a
way to bring the game folders of an earlier version up to date. Built the
same day: the runtime writes a session's frames a second down and a folder
counts by them (a measurement folder of Killzone counted for the first time;
the maintainer's own counts after its next session of 3 minutes, once it has
the new programs); the profile is
made by the repository's own scripts (`runtime-profile.ps1`, `.sh`), on Linux
too, where every session of the maintainer's written down on Windows runs
again as the session; and, measured again as 0.2 has it, compiled games take
0.98 (Jak and Daxter), 1.00 (Gran Turismo 3), 0.99 (Agent Under Fire) and 0.92
(Killzone, whose counts fit its timed session better than a user's would)
times the time of stock PCSX2 as released, and a first session 10 to 15% less
than on 2026-11-20. Timed on Linux for the first time, under WSL 2, where a
run's time repeats to about 1%: Jak and Daxter 1.05 times stock PCSX2's time
as 0.2 has it (1.23 as 0.1 had it), not at PCSX2's time there, why not known
yet. The wizard brings a game's folder made with an earlier
PS2Native up to date (ADR 0048, "Games made earlier..." in its first step): the
folder is given this version's programs and compiles its game again before it
next starts; tried on a folder made by 0.1 as released, on Windows and on
Linux. The version is 0.2 (`version.txt`). The maintainer's answers the same
day (ADR 0049: "1. Yes", "2. Yes, prepare the release", "3. Yes", "4. Do all
seven", "5. After"): committed and pushed; 0.2 released from that commit, both
downloads made from it and the release a draft on GitHub that the maintainer
reads and publishes; a folder brought up to date with another toolchain counts
its profile once more after a session; the session brings the maintainer's own
seven game folders up to date; and why a compiled game takes more than stock
PCSX2's time on Linux is looked for after 0.2 is out. docs/app-log.md,
"Compiled games against PCSX2, as users get them" and "0.2: a game's folder
brought up to date". Done the same day: both
downloads made from the commit `4b45373`, every test passing in the trees
they were made from, each unpacked and tried (its wizard made a game, and a
session of the maintainer's run again from that folder was the session at
every fingerprint); the release a draft on GitHub, which the session
published the same day at the maintainer's word ("1. Yes, publish"; the tag
`v0.2`), the website's downloads then moved to 0.2 ("2. Yes do it"); and the
maintainer's seven folders have 0.2's programs, each compiled again and the
start of one of its sessions run again from it the session. Then Linux's
speed, looked for the same day (docs/app-log.md, "Linux's speed"): the
runtime's profile works on Linux (`ps2n-host -profile`), and with it most of
what a run takes more inside WSL 2 than on Windows turned out to be PCSX2's
own, in stock PCSX2 as in the compiled game: its core wakes its VU thread,
which is not in use, at every STROW and STCOL command of VIF1 (2.4 million
times in Jak and Daxter's session), and a wake costs six times as much inside
WSL 2 as on Windows; without those wakes (a trial on a copy of PCSX2's source,
nothing of it in) both runs take 28% less there, and Jak and Daxter 9% less
on Windows, the other three games the same. Whether PS2Native carries such a
change to PCSX2's code, and whether PCSX2 is told, was put to the maintainer,
who answered the same day (ADR 0050: "1. Yes", "2. Yes, write an issue as a
guide I'll write it myself.", "3. Yes", "Stop."): PS2Native's builds leave
those wakes out, its first change to PCSX2's code, a patch applied to a copy
of PCSX2's source when the runtime is built, the submodule as it is and stock
PCSX2 timed as released; the maintainer tells PCSX2 themselves (the session
wrote no text for it, ADR 0010); committed and pushed; and the last 3% of
Linux's distance is left for now. The change was built the same day
(`patches/pcsx2`, applied to copies of the two files it changes when a tree
is configured, `cmake/ps2n-pcsx2-patches.cmake`): every test passes with it
on Windows and on Linux, and Jak and Daxter takes 9% less time with it on
Windows and 27% less under WSL 2, the other three games the same. Found
writing the answers down: PCSX2 switches its VU thread on by default on a
computer with three cores or more, and every run of `ps2n-host`, stock
PCSX2's among them, has had it off, so "stock PCSX2's time" in every
comparison here is PCSX2's recompilers with VU1 on the CPU thread. Timed the
same day with its VU thread on (`ps2n-host -pcsx2-vu-thread`), stock PCSX2
takes 40% less time on Jak and Daxter's session, 24% less on Killzone's, 12%
less on Gran Turismo 3's and 2% less on Agent Under Fire's (whose runs so
left their session: runs with the thread on do not all repeat). The compiled
games, with the change at 0.90 to 1.00 times the time of stock PCSX2 with VU1
on the CPU thread, are at 1.02 to 1.50 times the time of stock PCSX2 as it is
by default (under WSL 2, Jak and Daxter 0.75 and 1.59). What the website
says of the comparison, and whether VU1 is next carried out on a thread of
its own (PS2Native's VU provider cannot simply be put on PCSX2's VU thread),
was put to the maintainer, whose answers the same day (ADR 0051: "1. Yes",
"2. Yes", "3. Yes", "4. Yes", "5. Wait for that step", "6. Leave it as it
is") were: the change committed and pushed; the public comparison corrected
(the website's first chart has stock PCSX2 with its VU thread on as a third
bar, and the page, the README and 0.2's notes on GitHub say that both sides
of every ratio ran with VU1 on the main processor thread, and what a compiled
game takes against stock PCSX2 as it is by default: 1.02 to 1.65 times its
time as 0.2 has it); the maintainer's seven game folders given the runtime
with the change; no release with the change alone; and the next speed step
VU1 carried out on a thread of its own, on PCSX2's own VU thread, as PCSX2
does by default, measured against stock PCSX2 with its VU thread on. A speed
claim against stock PCSX2 says which of the two it is against.
Of the compiled game's own distance to stock PCSX2 on Linux, 1.05 times its
time, two parts were found and put right (1.03 now): the 128-bit
instructions' lanes, which Linux's calling convention kept the compiler from
making host SIMD of (`Lanes` in `ops_mmi.h`), and micro code the measurement's
Linux folder had not compiled; the rest, in the game's and the IOP's compiled
code, was not found (tried and taken out: the runtime's functions given
Windows's calling convention on Linux, and the code on large pages).
The VU thread step was built and measured the same day (ADR 0051, "The step,
built"; docs/app-log.md, "VU1 on a thread of its own"), and is put to the
maintainer: it is not committed, in no folder of theirs and in no release.
Where PCSX2's VU thread is on, PS2Native's VU provider carries VU1 out on it,
doing there what PCSX2's VU recompiler does differently there. A game played
in a window has the thread as PCSX2 would give it (on where the computer has
three processor cores or more, the user's to switch on PCSX2's own page in
the wizard, PCSX2's game database over both), decided once when its disc is
read; every other run has VU1 on the CPU thread, whatever a game's settings
file says. Every test passes: 543 in the development trees and 541 and 540 in
the release's, on Windows and on Linux, where two tests (the self-tests on
blocks compiled ahead of time) ran for the first time: every count of Linux's
tests before had them skipped, which no report had said. Measured as a
release would have it (the runtime's profile of play made again with the
thread, each folder's own counts taken with these programs), on the
maintainer's sessions against stock PCSX2 with its VU thread on: Jak and
Daxter 0.96 times its time, Gran Turismo 3 1.01, Agent Under Fire 0.98,
Killzone 0.95, where 1.50, 1.13, 1.02 and 1.24 without the thread; under WSL
2, Jak and Daxter 0.99 (with 0.2's profile there). The last picture and sound
of the runs are the same with the thread and without, but Agent Under Fire's:
its run with the thread on does not repeat, on stock PCSX2 as compiled, so a
session of such a game played with the thread cannot be run again as itself.
Gran Turismo 3's 1.4% is on the main thread's EE and IOP side, which
PS2Native's faster micro code made up for while VU1 shared that thread. A
game's first session takes 19% and 32% less time with the thread (Gran
Turismo 3, Jak and Daxter). The maintainer's answers the same day (ADR 0052:
"1. Yes", "2. Yes", "3. Yes", "Do whatever needs doing from your side (from
your "Not done" category)"): the step committed and pushed; their seven game
folders given the release's programs; 0.3 prepared with it, a draft on GitHub
they publish; and what the session had not done, done (the Linux runtime's
profile of play made again with the thread, a game in a window on Linux with
the thread). The version is 0.3 (`version.txt`). Done the same day: committed
and pushed (`d10604d`); the runtime's profile of play made again on both
systems from that commit; every test passing in the trees the downloads are
made from (541 and 540, none skipped); both downloads made, each unpacked and
tried (its wizard made Jak and Daxter, a session run again whole from that
folder the session at every fingerprint; a folder made by 0.2's own download
brought up to date and compiled again); the release's runtime timed again
(Jak and Daxter 0.97, Gran Turismo 3 1.00, Agent Under Fire 0.95, Killzone
0.94 times the time of stock PCSX2 with its VU thread on; under WSL 2 0.99);
Jak and Daxter played in a window inside WSL with the thread, its session run
again the session at every fingerprint, with the thread and without; the
maintainer's seven folders given 0.3's programs, each compiled again, a
session's start run again the session, their memory cards unchanged; and 0.3
a draft pre-release on GitHub, which the maintainer published the same day at
their word ("1. Publish"; the tag `v0.3` on `d10604d`), the website's
downloads and the README then moved to it ("2. Yes"). docs/app-log.md, "0.3
(ADR 0052)".

## Phase 5: maintenance

Phase 5, Maintenance, began at the maintainer's "Do 2"
(2026-11-22, ADR 0053) with PCSX2's newest version tried, v2.9.189, in a git
worktree and on a branch of their own (`..\ps2n-pcsx2-189`, `pcsx2-2.9.189`;
the submodule moved there only, PCSX2's newer Windows dependency package
there only). There PS2Native follows what PCSX2 changed of what it follows:
the count register's addition 32 bits wide (PCSX2's correction of the
maintainer's issue 15186, so the carry ADR 0013 mirrored is gone),
`nextBlockCycles` 32 bits again, and the IOP recompiler's JR and JALR keeping
their target (the maintainer's issue 15192: the pair PS2Native refused is
carried out). Every test passes on it as on v2.9.170 on both systems, a
game's folder is made and brought up to date as before, and the maintainer's
sessions run again on it are the sessions at every fingerprint, stock
PCSX2's runs and the compiled games' (docs/app-log.md, "PCSX2 2.9.189
tried"). At the maintainer's answers (ADR 0054: "1. Yes", "2. Yes", "3. I
will, after. Make sure they're all ready") PS2Native moved to it on
2026-11-23: the submodule at v2.9.189, the branch's ten changes in the main
tree, PCSX2's Windows dependency package of 2026-11-21 in `deps` (the earlier
one beside it as `deps\windows-x64-2.9.170`), the places that name the version
PS2Native follows saying v2.9.189, every test passing in the main trees as in
the branch's; committed and pushed; inside WSL the same. The maintainer's three
games PS2Native had never run (Resident Evil – Code: Veronica X, Secret Agent
Clank, SSX Tricky, the first discs from the USA it has run) were made ready
for them to try the same morning: each made by the wizard of their PS2Native
folder with every default; on a copy, each played as its launcher plays it,
compiled what its first session met and played again on compiled code, went
from its first screens into its menus, and over its first 1,800 frames is
the stock run at every sync point (Secret Agent Clank in place and time, VU0's
FBRST aside, where PS2Native follows the console as for Agent Under Fire).
Found on the way: the trees built after the dependency package was swapped
under them mixed objects compiled against both packages, and the wizard so
built crashed on every new game; built again from nothing, it runs clean
(docs/app-log.md, "The tree a release is made from, built again whole").
The same day, after a session of Jak and Daxter that lagged in its boss fight
with no notice shown, at the maintainer's word (ADR 0055: "Yes record. Yes
the notice should show for any new code."): a session in the window writes
down the pace it kept (`pace.txt`, every 300 frames: the frames a second, the
longest frames, what the game itself drew a second, how much was
interpreted; any run with `-pace`), and the notice over the picture shows for
any code met for the first time. Their session run again with it was the
session at every fingerprint and showed the lag as the game's own: from
minute 39½ the game drew about 30 pictures a second in bursts, the emulation
never behind and nothing interpreted that weighed, the game shown at 60 frames
a second by its "NTSC Mode" patch. Found with it: the move to v2.9.189 had
changed what a module and the runtime share (`Mem` and what follows it eight
bytes nearer) but not the module version, so a folder's modules compiled by
0.3 crashed on the new runtime; the version is 5 since. microVU's macro mode
reads 64 bits at the 32-bit `nextBlockCycles`, taking in the low half of `Mem`
where PCSX2's VUSyncHack or FullVU0SyncHack game fix is on (none of the
maintainer's games), so that its comparison depends on where the system put
PCSX2's memory. And part of the code Jak and Daxter loads from its data stays
interpreted however often its folder compiles (in 80% of the session's
stretches run again), so the notice for any new code would show about once a
minute there (docs/app-log.md, "The pace a session kept, and the notice for
any new code").

The maintainer's answers the same day (ADR 0056: "1. Ok 2. Yes 3. Agree 4.
Commit and push."):

- their Jak and Daxter folder plays the game at a 130% EE cycle rate, brought
  up to date to write its pace down;
- PS2Native keeps `nextBlockCycles` alone;
- committed and pushed;
- next, why part of Jak and Daxter's loaded code stays interpreted after its
  folder compiles it.

Found the same day: a folder's compile kept one version of the code its game
ran outside its executable at each address, the first captured, and Jak and
Daxter loads other code into memory it ran code from before (83,085 such
addresses in the maintainer's folder).

At the maintainer's word (ADR 0057: "Yes implement the recommendation"),
every version is compiled:

- **Captured:** each block run there, as a block (`[[block]]`), every version
  of it.
- **Compiled:** the versions that disagree with the first image go into
  further images, which the first lists (`ImageInfo::layers`, module version
  6).
- **Run:** the runtime runs a block with whichever image holds what memory
  holds. Where a BIOS image's code is not memory's, the game's images are
  tried too, which made Gran Turismo 3's patched program loader run compiled.
- **The notice** counts what a run records for the next compile, not every
  instruction interpreted.

Measured the same evening (docs/app-log.md, "Code run outside an executable,
every version compiled"):

- **Jak and Daxter:** the maintainer's 44-minute session, run again on a copy
  of their folder with the blocks its first run recorded compiled, recorded
  nothing new and interpreted 48,071 EE cycles where 580,691,693. Its notice
  found new code in none of its half seconds; it would have found some in 435
  counting interpreted instructions.
- **Gran Turismo 3, Killzone and Agent Under Fire:** each run again recorded
  nothing. Gran Turismo 3's C++ differs only by the empty table of further
  images.
- **Every run** was its session at every fingerprint.
- **Tests:** every test passes, 556 and 554 on Windows, 556 and 553 on Linux.

The maintainer's answers the same day (ADR 0058: "1. Yes", "2. Yes", "3.
Yes"):

- committed and pushed;
- their game folders brought up to date with these programs, their PS2Native
  folders made again from the committed tree;
- 0.4 prepared once they have played a session or two with the updated
  folders.

Done the same evening:

- committed and pushed (`2cc8125`);
- their PS2Native folders made again from it;
- their nine game folders on Windows and the one inside WSL brought up to
  date, each compiling its game again;
- where a session can be run again as itself, its start run again with the
  new programs: the session at every fingerprint;
- memory cards unchanged.

On 2026-11-25 the maintainer, having played: "I did some play tests,
everything looks smooth. Let's release 0.4." (ADR 0059). 0.4 is made as 0.3
was. The version is 0.4 (`version.txt`).

Done the same day:

- committed and pushed (`a243df4`);
- the training folders brought up to date, and the runtime's profile of play
  made again on both systems;
- stock PCSX2's trees built again at v2.9.189, as its releases are, Windows'
  from nothing;
- every test passing in the trees the downloads are made from (554 on Windows;
  553 on Linux, and 556 in its development tree), none skipped;
- both downloads made and tried: the session at every fingerprint from a folder
  each wizard made, a folder of 0.3's download brought up to date, the pace
  record's first run on Linux;
- their PS2Native folders made again, their game folders unchanged;
- timed against stock PCSX2 v2.9.189 with its VU thread on: Jak and Daxter
  0.93, Gran Turismo 3 1.00, Agent Under Fire 0.97, Killzone 0.94; under WSL 2,
  Jak and Daxter 0.96;
- a draft pre-release on GitHub for the maintainer to publish.

At the maintainer's answers ("1. Yes, publish yourself", "2. Yes do that", "3.
Yes please"), 0.4 was published the same day (12:42 UTC, the tag `v0.4`), the
website and the README moved to it, and their game folders were given its
programs, each compiling its game again.

The same afternoon, at the maintainer's words (ADR 0060: an overlay opened
with Esc to quit, edit the controls and choose the graphics card, "the only
acceptable changeable settings post-compilation", with "PS2 style sounds"),
a menu over the game, built and checked; at their answers ("1. Yes", "2. Yes",
"3. No") committed and pushed (`e616401`), given to their game folders (the
nine on Windows and the one inside WSL, each compiled again, a session's start
run again the session), opened by Esc only (not by a controller); in no
release yet:

- **Esc** opens it over a game played in its window; the machine is held
  between two frames meanwhile, so a session with the menu opened in it runs
  again as itself (checked: every fingerprint).
- **Its pages:** Resume; Controls (each input's key and controller input,
  kept in the game's data); Graphics card (taken at once, kept in the game's
  settings file); Reboot and Quit, which ask first, as the window's close
  button does (the next day, at the maintainer's word; at their answers
  committed and pushed, `224d896`, and given to their game folders).
- **Its sounds,** PS2Native's own in the PS2's manner, played beside the
  game's.
- docs/app-log.md, "A menu over the game (ADR 0060)".

On 2026-11-27, asked what was next, the maintainer chose "Maintenance tools"
of four options, and, for PCSX2's newest version, v2.9.198, "Follow them in a
branch" (ADR 0061). Done the same day:

- **The release's scripts in the repository** (`scripts\windows`,
  `scripts/linux`, each described in `docs/development.md`), where they had
  been copied from one release to the next in the build tree: the
  maintainer's game folders brought up to date and checked, compiled games
  timed against stock PCSX2, a release made and tried, the clone inside WSL
  brought to the tree. Each was tried on copies and in rehearsals.
- **A newer PCSX2 tried by one command** (`try-pcsx2.ps1`, `try-pcsx2.sh`): in
  a worktree and on a branch of its own, built and tested, the measurement
  folders' sessions run again, a report written. Tried first on v2.9.189, the
  version followed, where nothing may differ, and nothing did.
- **`CLAUDE.md` cut** from 23,888 words to 2,627; what left it was moved word
  for word into this record, `docs/adr/README.md` and `docs/development.md`.
- **v2.9.198 tried** in its worktree: six of PCSX2's changes touch what
  PS2Native follows, and PCSX2's own code needed a missing include to compile
  on Linux here. With PS2Native following them, every test passed on both
  systems, and the four timed games' sessions ran again as themselves, the
  compiled games' EE cycles the same on both versions.

At the maintainer's answers ("1. Yes", "2. Yes", "3. Yes", "4. Yes"):
committed and pushed (`b542370`); the rule that `build\` is never deleted or
cleaned whole kept; the worktree of Phase 3 removed; and PS2Native moved to
v2.9.198 (ADR 0062), carrying the include as its second change to PCSX2's
code, one that stock PCSX2's trees are built with too. The module version is
7. At their next answers ("1. Yes", "2. Yes"): committed and pushed
(`d50ef76`), and their game folders given its programs, the nine on Windows
and the one inside WSL, each compiling its game again; where a session could
be run again, its start was the session at every fingerprint compared, and
their memory cards are unchanged. docs/app-log.md, "Maintenance tools (ADR
0061)".

The same night the maintainer wrote "Handle release 0.5" (ADR 0063). 0.5 is
made as 0.4 was, by the release's scripts in the repository for the first
time. The version is 0.5 (`version.txt`).
