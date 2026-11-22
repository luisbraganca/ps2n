# CLAUDE.md

PS2Native (`ps2n`) converts PlayStation 2 games into native applications by
ahead-of-time static recompilation. This repository is a reboot of
`../ps2-uncompiler`, which stays untouched as an archive.

## Read first

- [docs/brief.md](docs/brief.md): the maintainer's brief, verbatim. It is
  authoritative. Its section 11 (AI development rules) applies to every
  session. If an implementation idea conflicts with it, say so; do not bend
  the goals.
- [docs/phase1-feasibility.md](docs/phase1-feasibility.md): the Phase 1
  assessment, with source references into PCSX2, the old project and the
  reference projects.
- [docs/execution-model.md](docs/execution-model.md): how compiled code, the
  fallback interpreter and the PCSX2 core run a program together. Read it
  before touching `runtime/` or `recompiler/`: how time is counted (exactly
  as PCSX2's EE recompiler counts it, by the blocks it would have formed) is
  a contract between the two. Its last two sections are the same for the
  vector units' micro programs and for the I/O processor's code.
- [docs/phase3-report.md](docs/phase3-report.md): what Phase 3 did, how it
  compares with stock PCSX2, what it leaves, and what Phase 4 has to decide
  first.
- [docs/phase4-proposal.md](docs/phase4-proposal.md): Phase 4's design as
  the maintainer decided it (ADR 0028), and its steps, P4.1 to P4.7, with
  their pass criteria; a step P4.8, the folder for others, was added by
  ADR 0033 ([docs/app-log.md](docs/app-log.md) has all eight).

## Status

**Phase 3 (the first real game: Killzone, then a second) ran from
2026-11-11 to 2026-11-16; its report is
[docs/phase3-report.md](docs/phase3-report.md). Phase 4 (the user
application) was approved on 2026-11-16 (ADR 0027), and the maintainer
decided its design the same day ([docs/phase4-proposal.md](docs/phase4-proposal.md),
ADR 0028): the wizard brings its own `clang` and `lld`, a game's code is
compiled on the user's computer into a module that a prebuilt runtime
loads, a packaged game is a folder, the wizard is Qt 6, Windows first. Its
steps are P4.1 to P4.7, each with its pass criterion
([docs/app-log.md](docs/app-log.md)). The first, P4.1, was measured the
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
PCSX2's time on Linux is looked for after 0.2 is out. Done the same day: both
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
the thread). The version is 0.3 (`version.txt`).
docs/app-log.md,
"Compiled games against PCSX2, as users get them" and "0.2: a game's folder
brought up to date".**
What follows is Phase 3's history. [docs/game-log.md](docs/game-log.md) has its steps, their pass
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
the Phase 3 report ([docs/phase3-report.md](docs/phase3-report.md)):
compatibility, the comparisons with stock PCSX2, visual issues,
performance, regressions, limitations, and what Phase 4 has to decide
first. At the maintainer's word ("1. Yes"), Phase 3 ended there and
Phase 4 began (ADR 0027).

Phase 2 (proof of concept) is done on Windows; its Linux step (P5) is
deferred by the maintainer. Its steps and criteria are in section 14 of the
assessment, and [docs/poc-log.md](docs/poc-log.md) records what was done and
measured. Its last section lists the questions that are open, those waiting
for the maintainer first. Record every decision the maintainer makes as an
architecture decision record under `docs/adr/`.

The game is the maintainer's own copy, used where it is: the disc image and
the files extracted from it are in `C:\ps2\killzone`. Read them, never copy
them, and keep everything generated from them (C++, captures, pictures) in
the git-ignored build tree.

Decisions:

- [ADR 0001](docs/adr/0001-platform-priorities.md): Windows x64 and Linux x64
  are required; macOS, Android and iOS are optional. Build and test nothing
  for an optional platform unless asked.
- [ADR 0002](docs/adr/0002-static-cpus-on-pcsx2-core.md): the game's code is
  translated ahead of time and runs on PCSX2's core as the machine. A BIOS is
  required.
- [ADR 0003](docs/adr/0003-no-jit-in-generated-games.md): no recompiler for
  PS2 code ships in a generated game. PCSX2's VU and IOP recompilers are
  temporary, labeled stand-ins; its EE recompiler is never one. Unseen code
  is interpreted once, reported, captured and rebuilt.
- [ADR 0004](docs/adr/0004-proof-of-concept-environment.md): PCSX2 `v2.9.170`
  as a submodule; the `SCPH-70004` BIOS read in place; Linux is built and
  tested by the maintainer on their own machine, so never report Linux as
  working without their run.
- [ADR 0005](docs/adr/0005-never-further-from-the-console-than-pcsx2.md):
  what counts as correct. On no line of a console-recorded test may stock
  PCSX2 be right and PS2Native wrong, and every remaining difference is
  listed with its cause. Instruction semantics follow PCSX2's recompiler,
  and the console where a recording shows the recompiler wrong.
- [ADR 0006](docs/adr/0006-no-jit-rule-covers-ps2-code-only.md): the no-JIT
  rule covers PS2 program code only. PCSX2's VIF unpack recompiler and its
  software renderer's code generator stay.
- [ADR 0007](docs/adr/0007-bios-code-translated-ahead-of-time.md): the
  BIOS's EE code is to be translated ahead of time, on the user's machine
  from their own dump, as a file of its own beside the game. A BIOS image is
  still required at run time. Begun on 2026-11-12 as step G8 (ADR 0017).
- [ADR 0008](docs/adr/0008-linux-run-deferred.md): the first Linux run is
  deferred until the maintainer chooses. Linux is still required.
- [ADR 0009](docs/adr/0009-phase-3-first-game-killzone.md): Phase 3 is
  approved; the first game is Killzone (`SCES-52004`).
- [ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md):
  the fix for PCSX2's VU0 status-flag bug is offered to PCSX2 by the
  maintainer, as an issue with the correction they suggest; PS2Native
  carries it in its patch queue only if that stalls (30 days after the
  issue is opened with no reply from a PCSX2 maintainer). The issue's text
  and the correction are the maintainer's to write, not a session's. The
  two issues, PCSX2's numbers 15185 and 15186, were opened on 2026-11-11.
  15186 was answered within the hour: the defect and its correction were
  confirmed there, and PCSX2 corrected it that night (in `v2.9.173`).
  15185 had no reply on 2026-11-14: from 2026-12-11, look by reading only
  whether it has one, and tell the maintainer. PCSX2's team asked there
  that no LLM-generated issues be sent.
- [ADR 0011](docs/adr/0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md):
  time is counted exactly as PCSX2's EE recompiler counts it. A run on
  PS2Native is the stock run, sync point for sync point; a difference is a
  defect to find, unless it comes from an instruction that deliberately
  follows the console (ADR 0005) and is listed with its cause.
- [ADR 0012](docs/adr/0012-ps2native-branding-not-pcsx2s.md): what PS2Native
  builds carries PS2Native's branding (`assets/branding`) and never PCSX2's
  logo; PS2Native's logo is not put on PCSX2's interface either. PCSX2's
  name stays wherever it is attribution: logs, license texts, copyright
  headers, documentation. A finished game shows nothing of what the core can
  draw over the picture (messages, performance figures, indicators): that is
  switched off by settings, never restyled by patching PCSX2. The host's
  window does all of this (`ps2n-host -window`).
- [ADR 0013](docs/adr/0013-pcsx2s-count-register-carry-mirrored-until-fixed.md):
  a defect of PCSX2's recompiler (reading the count register carries into
  `EntryHi`) is offered upstream the same way, as an issue of its own, and
  reproduced by PS2Native until the pinned PCSX2 has the correction, as
  long as it costs nothing in speed and changes nothing on screen. (Had the
  issue been ignored for 30 days, the correction would have been carried
  locally; it was confirmed instead.) `tests/programs/countcarry.s` shows
  it. PCSX2 corrected it on 2026-11-12, first in `v2.9.173`; PS2Native
  still pins `v2.9.170`, and moving the pin is a decision of its own.
- [ADR 0014](docs/adr/0014-the-website-is-served-from-docs.md): the project
  has a website, one page that GitHub Pages serves from `docs/` as it is
  (`docs/.nojekyll`). The documentation stays where it is; the site is
  `docs/index.html`, `docs/404.html` and `docs/site/`. The page says only
  what the logs say, with dates, and shows nothing from a game or a BIOS.
- [ADR 0015](docs/adr/0015-a-speed-step-before-phase-3-ends.md): a speed
  step inside Phase 3 (G6) before the phase ends. The generated code is
  made faster one change at a time, each measured on the level run and
  with the profile; the compiled game stays the stock run. What comes
  after the two changes named is decided by the profile and reported; a
  change of another kind (to how time is counted, to what is compared with
  stock PCSX2) is put to the maintainer first.
- [ADR 0016](docs/adr/0016-vu0-instructions-with-semantics-of-ps2natives-own.md):
  the speed step ended where it stood on 2026-11-12. The next step, G7:
  the VU0 instructions in EE code carried out by semantics of PS2Native's
  own, following PCSX2's recompiler (microVU in macro mode) as ADR 0005
  has it, in place of PCSX2's interpreter functions. What the recompiler
  itself hands to a function of the core (`VCALLMS`, `VCALLMSR`) stays
  that function; when VU0 is brought up to date, and which flags reach the
  registers, stay as the recompiler decides them for the block. Done on
  2026-11-12 (`runtime/include/ps2n/ops_vu0.h`).
- [ADR 0017](docs/adr/0017-bios-ee-code-compiled-as-step-g8.md): the next
  step, G8, inside Phase 3: the BIOS's EE code compiled ahead of time, as
  ADR 0007 has it, beginning with the four pieces that run under fast boot
  (the reset code, the kernel, `EELOAD`, `EENULL`). During development the
  translation is linked into `ps2n-host` from the build tree, made from the
  dump the build is given and never in the repository; a file of its own
  beside a packaged game is Phase 4's. Done on 2026-11-12: four images,
  and the code the game copies into kernel memory compiled with the game
  (`game_outside`).
- [ADR 0018](docs/adr/0018-vu0-instructions-carried-out-by-compiled-code.md):
  step G9: compiled code carries out a VU0 instruction itself, inline, where
  the runtime's record of its block says nothing is done about VU0 first
  (`PS2N_VU0`); elsewhere the runtime as before. Made in a worktree of its
  own (`..\ps2n-vu0`, branch `vu0-direct`) while another session worked on
  G8 in the main tree; the two sessions agree before timing anything, and
  whichever step is committed second is rebased onto the first. Done on
  2026-11-12.
- [ADR 0019](docs/adr/0019-performance-as-the-research-lays-it-out.md):
  step G10, inside Phase 3: performance as the maintainer's research lays
  it out. Gameplay measured first (startup apart from play, nothing drawn
  and the hardware renderer, profiled; `ps2n-host -mark`, `-profile-from`),
  then the expensive paths one change at a time, each measured against
  stock PCSX2 timed in turn, profile-guided and link-time optimization
  evaluated. What would change how time is counted, or what is compared
  with stock PCSX2, goes to the maintainer first. Under way: profile-guided
  optimization (`build.ps1 -Pgo`), loops that have reached a fixed point
  (`PS2N_BLOCK_END_FIXED`), the COP2 transfers made by compiled code
  (`PS2N_COP2_MOVE`, no gain that shows), the COP2 instructions that come
  first in their block (`PS2N_COP2_ASKS`, `PS2N_COP2_CYCLES`), the rest of
  them (`PS2N_VCALLMS`, `CFC2` and `CTC2` as moves; no gain that shows).
  Link-time
  optimization of PS2Native's own code (`build.ps1 -Lto thin`) was
  evaluated: no gain, not used. On 2026-11-13 a third session, given the
  website's two charts ("We need to improve these numbers"), measured them
  again and took on their slowest parts: `lq` and `sq` on the locals
  (`PS2N_LQ`, `PS2N_SQ`) and a block start asking the runtime in one
  argument (`askAbout`, the game's code 6% smaller), both kept and both
  within the noise over the level run; registers kept changed across the
  end of a block, tried and taken out. Then, at the maintainer's word, the
  FPU's registers in SSE registers (`val::Fpr`) and the masks of its
  additions' guard bit from a table (`kGuardMasks`), the self-test holding
  all 511 exponent differences to PCSX2's recompiler: play as long as
  stock PCSX2's, with profile-guided optimization. Ended there by the
  maintainer on 2026-11-13 (ADR 0022).
- [ADR 0020](docs/adr/0020-no-claude-references-in-commit-messages.md):
  no commit message references Claude: no `Co-Authored-By` line naming
  Claude, no `Claude-Session` line. The history was rewritten on
  2026-11-12 to remove them (messages only; files, authors and dates
  unchanged) and force-pushed. Mentions of the file `CLAUDE.md` are fine.
- [ADR 0021](docs/adr/0021-two-sessions-on-step-g10.md): two sessions on
  G10 at once, the maintainer's choice: the second in the main tree, the
  first in its worktree, each with its own changes, saying "go" and "done"
  around every timing. The second's: calls through a register made by
  compiled code (`callSites`, `PS2N_CALL_DYNAMIC`), `ps2n-host -code-size`,
  the way a load or store seldom takes made short (one runtime call, no
  registers put back): play 1.18 to 1.14 times stock PCSX2's time without
  profile-guided optimization; the FPU's settings read once per function
  tried and taken out.
- [ADR 0022](docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md):
  step G11, inside Phase 3: the micro programs the game runs on VU0 and VU1
  compiled ahead of time by PS2Native's own translator, in the place of
  PCSX2's VU recompiler (microVU), the stand-in of ADR 0003, following it
  as ADR 0005 has it; next in ADR 0003's order of work. Begun with a
  measurement, the host watching the core's two VU providers
  (`ps2n-host -vu-report`). With it the maintainer chose: G10 ended; VU1's
  registers held to the stock run at every sync point, as the EE's and
  VU0's are; PS2Native's own VU interpreter before the translator, as the
  reference and what runs code a build has not compiled. Both done on
  2026-11-13: VU1's registers in the sync trace, and the interpreter
  (`runtime/vu_interp.h`, `ps2n-host -vu-ps2n`), which follows microVU's
  analysis (`core/include/ps2n/vu_mvu.h`), its record of programs and
  blocks, its instructions, and its allocation of host registers, where
  that shows in results (the high half of VI registers; which of two NaNs
  an AVX addition or multiplication gives). It refuses what it does not
  follow (`QuirkOf`, and the T and M bits on a branch), none of it in
  Killzone's code. The translator, at the maintainer's word on 2026-11-14
  ("maybe the translator?"), done that day: a block's plan
  (`ps2n/vu_plan.h`, the steps' semantics in `ps2n/vu_ops.h`), compiled
  micro code found by its plan's key (`ps2n-recomp vu`,
  `ps2n/vu_recomp.h`), blocks no build has compiled interpreted and
  captured (`ps2n-host -vu-capture`, `build.ps1 -VuCapture`), PS2Native's
  configurations on PS2Native's VU provider. What the session decided
  within the step is listed there, for the maintainer to decide otherwise.
- [ADR 0023](docs/adr/0023-the-iops-code-compiled-as-step-g12.md):
  step G12, inside Phase 3, at the maintainer's word on 2026-11-14 ("Let's
  work on the I/O processor"): the IOP's code, the BIOS's modules and the
  game's, compiled ahead of time by PS2Native's own translator, in the
  place of PCSX2's IOP recompiler, the last stand-in of ADR 0003,
  following it as ADR 0005 has it. Begun with a measurement
  (`ps2n-host -iop-report`). What the session decided within the step is
  listed there, for the maintainer to decide otherwise: the IOP's place and
  registers held to the stock run at every sync point (on the strength of
  the answer for VU1's in ADR 0022); PS2Native's own IOP provider
  (`runtime/iop_provider.h`), which keeps the record of blocks PCSX2's IOP
  recompiler keeps and interprets them; IOP code compiled block by block,
  each as that recompiler formed it, found by a fingerprint of its
  instructions wherever it is loaded (`ps2n-recomp iop`,
  `ps2n/iop_recomp.h`); blocks no build has compiled interpreted and
  captured (`ps2n-host -iop-capture`, `build.ps1 -IopCapture`); what it
  does not carry out refused by name; PS2Native's configurations on
  PS2Native's IOP provider; and every run that is not played in a window
  started with the console's clock at one fixed time (`ps2n-host -clock`),
  since the first comparison with the IOP in it showed two runs reading
  different times of day. The self-test found a defect of PCSX2's IOP
  recompiler on the way (a jump through a register that goes to address
  0): the provider stops at that form. On 2026-11-14 the maintainer chose
  to tell PCSX2 themselves, in an issue of their own, and asked for a
  draft to go by; the session wrote none, as ADR 0010 has it, and gave
  where the defect is and how it is seen (the ADR's section on it). The
  maintainer opened it the same day: PCSX2's issue 15192.
- [ADR 0024](docs/adr/0024-further-into-the-game-before-phase-3-ends.md):
  the next step, G13, inside Phase 3, at the maintainer's word on
  2026-11-14 ("Ok", to the session's recommendation): further into the
  game before the phase ends. The maintainer plays on in the window,
  beyond what the scripts reach; each session writes down what it had to
  interpret (the game's EE code, the micro code, the IOP's code); a
  session puts that into the captures, makes the build again, checks it
  again and reports what new code each part of the game brought, and what
  went wrong. The step's criterion is the session's proposal, and how far
  is far enough is the maintainer's to say. Begun on 2026-11-14 with a
  first session of the maintainer's (`game.ps1 -Play` with `-Capture`,
  `-VuCapture` and `-IopCapture`; the game log's "How to run it", and
  "G13" for what it brought). The maintainer's answer after it ("1.
  Yes"): a session records what it takes to run it again (its
  controller's input frame by frame, its memory card and its clock at the
  start), and a session is run again without a window and compared. On
  2026-11-15 ("Yes, as a small next step"): it records the patches it was
  played with too, which a run of it again applies.
- [ADR 0025](docs/adr/0025-how-a-game-is-shown-and-its-patches.md): how
  a game is shown by default, at the maintainer's word on 2026-11-14
  ("I want the defaults to be: 1920x1080, Vulkan, Automatically apply
  Widescreen/FPS patches if available"; "let's split into several
  steps"): G14, the window's defaults (PCSX2's Vulkan renderer, a picture
  of 1920 by 1080, the game drawn at three times the console's
  resolution, each changeable); G15, PCSX2's patches available to the
  host and a game's widescreen patch applied by default, where the
  patches come from put to the maintainer first (a new dependency); G16,
  a game's frame-rate patch likewise, its speed measured. The comparisons
  keep the software renderer. Phase 4's wizard starts from these
  defaults. The patches come from the installed PCSX2's archive, read
  where it is (`C:\Program Files\PCSX2\resources\patches.zip`, 2.8.2; the
  maintainer's answer on 2026-11-15, "The installed PCSX2"): the
  repository it is made from declares no license, and where a packaged
  game gets its patches is Phase 4's. A frame-rate patch is the group of
  the game's patch file whose name says FPS (none where several do). At
  the maintainer's word on 2026-11-15 ("Sure apply recommended PCSX2
  patches"), step G17: where a patch applied asks in its note for PCSX2's
  EE overclock (`EECycleRate`), the host applies it (the percentage the
  note names, 130% where it names none; `-ee-cycle-rate` given keeps its
  own), and PS2Native counts time as PCSX2's recompiler counts it with that
  setting; a comparison gives stock PCSX2 the same setting.
- [ADR 0026](docs/adr/0026-a-second-game-and-games-of-several-executables.md):
  step G18, at the maintainer's word on 2026-11-15 ("let me test using
  agent under fire ... Prepare it and do some initial testing"; then
  "Build multi-program support"): a second game, *007 - Agent Under Fire*
  (`SLES-50539`), the maintainer's copy in `C:\ps2\agent-under-fire`, used
  as Killzone's is (read where it is, never copied, everything generated
  in the build tree), prepared for the maintainer to play; and games made
  of several executables. Its boot executable loads three others itself
  (`SifLoadElf`, not the BIOS's loader), over the same addresses, which
  PCSX2 never sees: each is compiled as an image of its own (`ps2n-recomp
  compile --loaded-by`, `build.ps1 -GameMore`), which the runtime puts in
  use when memory holds its code, and what a run interprets in it is
  captured under its CRC. `ACTION.UK` is the English missions'; another
  language's is interpreted. After the maintainer's first session
  (2026-11-16, "Ok let's leave them"): artifacts of PCSX2's renderer that
  stock PCSX2 shows too are left to PCSX2; a setting of PCSX2's own that
  removes them is put to the maintainer when one is found.
- [ADR 0027](docs/adr/0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md):
  Phase 3 ends on 2026-11-16 with its report
  ([docs/phase3-report.md](docs/phase3-report.md)), and G13 with it.
  Phase 4, the user application, is approved, and begins with a design
  proposal the maintainer decides on before any code is written, first of
  all how a user's computer compiles a game.
- [ADR 0028](docs/adr/0028-phase-4s-design-a-shipped-compiler-and-game-modules.md):
  Phase 4's design, at the maintainer's word on 2026-11-16, every
  recommendation of [docs/phase4-proposal.md](docs/phase4-proposal.md)
  accepted. A game is compiled on the user's own computer by a `clang` and
  `lld` the wizard brings; its generated code is freestanding (no PCSX2
  header, no standard library, the machine state in PS2Native's own
  structures checked against PCSX2's) and compiled into a module that a
  prebuilt runtime (PS2Native's runtime and PCSX2's core) loads, the BIOS's
  code a module of its own. A packaged game is a folder, the disc and BIOS
  images read in place. New code a player meets is compiled by the game in
  the background, outside its process, for its next start, the wizard's
  recompile first. The wizard is Qt 6. Patches come from an installed
  PCSX2's archive or its release fetched at the user's word, never shipped.
  No profile is trained for the first version (1.1 to 1.2 times stock
  PCSX2's time), speed later. Windows first, the design portable. The
  steps P4.1 to P4.7 in the proposal's order, beginning with what the
  module costs.
- [ADR 0029](docs/adr/0029-a-games-module-reaches-the-runtime-through-its-import-table.md):
  a game's module reaches the runtime through its import table, as an
  ordinary DLL does; no loader of PS2Native's own. The maintainer's rule
  on 2026-11-16 ("If the custom loader makes the game faster, I'd go with
  the custom loader. If not, your recommendation"; "If it doesn't affect
  game speed, only compile speed, I'd also go with your recommendation"),
  applied to a measurement of 2026-11-17: the same generated code reaching
  the core directly, as such a loader would have it, took longer than
  through the import table (Agent Under Fire's second session 1 against
  0.977, its fourth mission 1 against 0.954), why not known yet.
  `build.ps1 -Module imports` measures it.
- [ADR 0030](docs/adr/0030-the-wizards-compiler-is-llvms-own-release.md):
  the `clang` and `lld-link` the wizard ships are LLVM's own release, 22.1.3
  (the version the development builds use), from its Windows x64 archive
  downloaded once into `deps`, of which only what a module needs is kept;
  not Visual Studio's copy. P4.3's criterion is checked on another computer
  of the maintainer's, without Visual Studio, with a folder and one command
  the session prepares. The maintainer's choices on 2026-11-17, both the
  session's recommendations.
- [ADR 0031](docs/adr/0031-microsofts-c-runtime-travels-in-the-folder.md):
  Microsoft's C++ runtime (`msvcp140.dll`, `vcruntime140.dll`,
  `vcruntime140_1.dll`), which the runtime and PCSX2's libraries need,
  travels in the wizard's folder and in every game folder, from Visual
  Studio's redistributable files; nothing is installed for it, and newer
  copies come with PS2Native's updates. The maintainer's choice on
  2026-11-17, the session's recommendation.
- [ADR 0032](docs/adr/0032-the-wizard-goes-through-pcsx2s-settings.md):
  the wizard goes through every setting of PCSX2's that applies to a game,
  at PCSX2's defaults but the window's (ADR 0025: the patches with their
  overclock, Vulkan, 1920 by 1080 at three times the console's
  resolution); those that change how the processors are emulated shown
  but fixed at what PS2Native follows, each changeable once checked
  against PCSX2; the core's on-screen display not offered (ADR 0012);
  PCSX2's own per-game settings pages inside the wizard if they build into
  it unchanged, PS2Native's own otherwise; PCSX2's program settings,
  hotkeys and RetroAchievements left out. The maintainer's words on
  2026-11-17 ("the wizard should go through all the PCSX2 settings"), and
  four recommendations accepted. On 2026-11-18, at the maintainer's word
  ("this setting on the wizard should always be as widescreen (16:9)"), the
  aspect ratio's default is 16:9 for every game, not PCSX2's "Auto
  4:3/3:2" (`ps2n::game::SetDefaults`).
- [ADR 0033](docs/adr/0033-the-wizards-step-ends-what-the-folder-needs-for-others-is-a-step-of-its-own.md):
  P4.5 is done: the maintainer's run of 2026-11-18 (Gran Turismo 3, made
  without a command line, three settings changed) counts for its
  criterion. What was left of it is a step of its own, P4.8, the folder for
  others, done before the folder `kit.ps1` makes goes to anyone but the
  maintainer: the licences of everything in it, disc images other than
  `.iso`, PCSX2's patches where PCSX2 is not installed, a fixed setting
  edited by hand refused, a choice of the executables compiled. P4.6 is
  next, beginning with the game's code a session finds outside its
  executables compiled in parts. The maintainer's word: "Agree", to the
  session's recommendation.
- [ADR 0034](docs/adr/0034-a-game-compiles-new-code-with-its-own-toolchain-and-says-so.md):
  how a game compiles new code itself (P4.6), the maintainer's two choices
  on 2026-11-18: every game's folder carries its own copy of the toolchain
  (about 200 MB more per game), so it compiles wherever it is, against the
  session's recommendation (PS2Native's folder's); and it says so with an
  icon in Windows' notification area while it compiles and a notification
  when the next start will have it, nothing when a session met nothing new.
  The rest as the session laid it out: the compile starts when the window
  closes, at low priority, only what changed, the new modules taken at the
  next start.
- [ADR 0035](docs/adr/0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md):
  at the maintainer's word on 2026-11-18, having tried it ("The compilation
  notification was really delayed... It should show right after closing
  the game showing that is compiling, than a different one saying it's
  compiled. Also when the game is running on interpreted code that can be
  compiled, there should show an overlayed notification saying so"): a
  game's own recompile shows one notification as soon as it finds the
  session met new code (it merges only the sessions it has not merged,
  `captures\merged.txt`) and a different one when it is done; and while a
  game in its folder runs interpreted code a compile would compile, a
  notice of PS2Native's own shows over its picture, in a window of
  PS2Native's laid over the game's (`host/window.cpp`, `ShowNotice`), never
  PCSX2's on-screen display, which ADR 0012 keeps off. The session's
  choices, for the maintainer to decide otherwise: when over the last half
  second at least 1% of the EE's, the IOP's or the VUs' work was
  interpreted; five seconds; at most once a minute; top left; the brand's
  colours.
- [ADR 0036](docs/adr/0036-the-folder-for-others-how-p4-8-is-done.md):
  P4.8, the folder for others, is next, before P4.7 (the maintainer's
  "Yep", 2026-11-18), its `.chd` the maintainer's Silent Hill 3 ("I do,
  there's a chd file for silent hill 3"). The session's choices, for the
  maintainer to decide otherwise: a disc read through PCSX2's own readers
  by the runtime (`ps2n-host -disc-executables`, `-disc-extract`), what is
  looked for on it PS2Native's (`ps2n/iso9660.h`, free of exceptions), a
  `.cue` standing for the image it names; PCSX2's archive of patches
  fetched at a button from the latest release of PCSX2's patches project
  (the file PCSX2's own builds take), or a file given, kept in the folder's
  `patches\`; a fixed setting in a game's settings file refused by the
  runtime, naming it; every executable ticked, the boot one always; the
  licences written by `kit.ps1`, Qt's from Qt's SBOMs and source, with
  where each source is; and, found making Silent Hill 3 (one segment, data
  with code, its last function a megabyte), a function cut into pieces of
  about 16,384 instructions, which changes no other game's analysis.
- [ADR 0037](docs/adr/0037-p4-8-done-a-game-does-not-play-while-it-compiles.md):
  P4.8 is done: the maintainer's run of 2026-11-18 on their Windows 10
  computer, which has no PCSX2, counts for its criterion ("Everything went
  flawlessly"). And, in their words, "running the game while compilation
  is running in the background shouldn't be allowed". Started while the
  folder's recompile compiles, the launcher waits for it in a window that
  says so (Cancel leaves the game unstarted, the compile going on), then
  plays with what it compiled: the session's choice, which the maintainer
  made theirs on 2026-11-19 ("2. Yes"; with it "4. Ok", P4.7 next). The
  session's choices, for the maintainer to decide otherwise: compiling is
  while `ps2n-remake` holds
  `work\remake.lock`, which it now lets go of as soon as its modules are
  ready, or for 20 s after one is started, before it takes it; the window
  shows after a second and a half.
- [ADR 0038](docs/adr/0038-p4-7-built-and-tested-on-a-linux-inside-the-development-computer-first.md):
  P4.7 is built and tested first on a Linux inside the development
  computer, WSL 2 with Ubuntu 22.04 (PCSX2 makes its own Linux releases on
  it), the packages PCSX2's Linux build needs installed in it by the
  session: the maintainer's "1. Sure." on 2026-11-19. ADR 0004 stands:
  Linux is reported as working only after the maintainer's run on their own
  machine. That machine is a laptop with Linux Mint and, as the maintainer
  thinks, NVIDIA's GTX 1660; P4.7's criterion, the session's proposal, is
  accepted ("3. Yes"): there, a game made with the wizard from its disc
  image plays from its folder at the console's pace, and what a session
  meets for the first time is compiled in the background for its next
  start.
- [ADR 0039](docs/adr/0039-the-notice-over-a-full-screen-game-exclusive-full-screen-disallowed.md):
  the notice over the picture (ADR 0035) reached the screen over a game
  covering it only at a session's start: PCSX2's default lets the graphics
  driver take such a game over (Vulkan's full-screen exclusive), which
  NVIDIA's does once it has run a moment, and from then on nothing Windows
  lays over it shows. At the maintainer's "Fix it" (having watched both),
  exclusive full screen is disallowed by default on Windows
  (`ExclusiveFullscreenControl = 0`, `ps2n::game::SetDefaults` and every
  window run), a game's settings file may allow it again; the game folders
  on this computer got the line in their settings files; and the runtime's
  report says how near the notice's line a run came and how often it was
  shown.
- [ADR 0040](docs/adr/0040-a-game-compiles-in-a-window-of-its-own-and-never-starts-with-anything-left-to-compile.md):
  at the maintainer's word on 2026-11-19 ("Compiling shouldn't be done on
  the background, but yes on a foreground modal/window ... the user is able
  to click 'Later' or close the modal/window (which is the same thing) ...
  next time the game runs, it will first compile pending compilations before
  actually running"; "It shouldn't be possible to run the game without
  compiling pending compilations first"), what a session met is compiled at
  once, at normal priority, in a window of its own, in front (how far it has
  got, Later); Later stops it, what was compiled kept; the game never starts
  with anything left to compile: before it starts, `ps2n-remake
  --before-play` compiles what is left in the same window, and Later there,
  or a compile that fails, leaves the game unstarted. It replaces ADR 0034's
  background compile, ADR 0035's icon and notifications (its notice over the
  picture stays) and ADR 0037's wait. The session's choices are listed there,
  for the maintainer to decide otherwise.
- [ADR 0041](docs/adr/0041-p4-7s-run-inside-wsl-on-the-development-computer.md):
  P4.7's criterion is met by the maintainer's own run inside the Ubuntu
  22.04 of WSL 2 on the development computer, the Linux laptop not being
  theirs to use (their "Yes", 2026-11-19): a game made with the Linux
  wizard from its disc image plays from its folder at the console's pace,
  and what a session meets is compiled for its next start. There the
  renderer is OpenGL (Mesa's D3D12 driver on the RTX 4070 SUPER; Vulkan
  inside WSL is the CPU's), the keyboard plays, and a desktop shortcut
  starts the Linux `PS2Native` through `wslg.exe`. Until a Linux machine of
  its own runs PS2Native, Linux is described everywhere as tested under WSL
  2 (Ubuntu 22.04) on the development computer. The maintainer's run, Jak
  and Daxter made with the Linux wizard and played for five sessions, about
  90% of the console's pace inside WSL, counted ("I guess it's good").
- [ADR 0042](docs/adr/0042-releases-on-github-beginning-with-0-1.md):
  PS2Native is released on GitHub, beginning with 0.1 (tag `v0.1`), at the
  maintainer's word ("Let's create releases on Github"): both folders as
  downloads (`PS2Native-<version>-windows-x64.zip` from `kit.ps1 -Out`,
  `PS2Native-<version>-linux-x64.tar.xz` from `kit.sh --archive`), made from
  the tagged, committed and pushed tree, with their licences and nothing of
  a game, a BIOS or PCSX2's patches; 0.1 a pre-release (five games, two
  Windows computers, Linux under WSL only); the version the file
  `version.txt`, shown by the wizard's title, the runtime's `-help` and the
  licences. GitHub
  CLI (`gh`, installed with winget at the maintainer's choice) creates a
  release as a draft, published at the maintainer's word. The maintainer
  published 0.1 on 2026-11-19.
- [ADR 0043](docs/adr/0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md):
  a speed step, at the maintainer's word on 2026-11-20 ("How come our
  compiled games are running slower than PCSX2's ...? We need to check why
  and fix it"; and why first sessions are so much slower than on PCSX2):
  first measured in the configuration users get (a game's folder, its
  modules without a profile) against stock PCSX2 on the maintainer's own
  sessions run again, then the costs the profiles show made smaller one
  change at a time, each kept only if it gains, time counted as before.
  First sessions get faster within today's rules ("Faster interpreters
  (Recommended)"): PS2Native's interpreters, and the BIOS's kernel compiled
  for a new game's first session; a profile of the user's own play given to
  the compiler is put to the maintainer first, with its cost. What was
  measured and changed: [docs/app-log.md](docs/app-log.md), "The speed
  step".
- [ADR 0044](docs/adr/0044-a-games-folder-learns-from-its-users-own-play.md):
  a game's folder learns from its user's own play, at the maintainer's word
  on 2026-11-20 ("1. Yes.", to the session's recommendation): first the
  order of the game's functions, as its sessions ran them, given to the
  linker when the folder compiles after a session; then a profile sampled
  while the game plays, given to the compiler (the counted profile gave
  7.5%), built and measured before it goes into a folder. Each measured on
  the maintainer's sessions first; what a folder learns stays in it. The
  order was built on 2026-11-21, on Windows and Linux (docs/app-log.md, "The
  order of a folder's functions, built"): about 0.4% on Jak and Daxter,
  where a first link by hand had seemed to give 2.0%. The sampled profile,
  tried the same day, gave 0.2% and goes into no folder (ADR 0045).
- [ADR 0045](docs/adr/0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md):
  a game's folder takes its profile by counting, not by sampling, at the
  maintainer's word on 2026-11-21 ("1. Yes", to the session's
  recommendation): the sampled profile gave 0.2%, the counted one 7.5% on
  Jak and Daxter. After a session of at least 3 minutes, once per game, the
  folder's compile compiles the modules to count, runs the session's first
  3 minutes again with them without a window, and compiles them again with
  the counts; Later puts it off to the next session's end and never keeps
  the game from starting (unlike new code, ADR 0040). Built and measured on
  a second game (Gran Turismo 3, the session's choice) before it goes into
  a folder. It replaces ADR 0044's second part. Built on 2026-11-21 at
  clang's IR level (the session's choice, listed in the ADR with the others):
  Jak and Daxter 8.0% less time after its folder's own round, Gran Turismo 3
  3.6%.
- [ADR 0046](docs/adr/0046-compiled-games-at-least-as-fast-as-stock-pcsx2-as-users-get-them.md):
  at the maintainer's word on 2026-11-21 ("Speed: match PCSX2", asked what
  to work on next), compiled games are to take no more than stock PCSX2's
  time in the configuration users get, a folder with the profile it counts
  itself: first measured so, each game counted on one session of the
  maintainer's and timed on another; then the costs the profiles show, one
  change at a time, each kept only if it gains, time counted as before. The
  session's expected order: the IOP's block entry and exit, the COP2
  information decided when the code is generated (checked against the
  runtime's record before a block runs it), the VU provider's entry and exit
  of a micro program, accesses to devices, the 128-bit instructions, block
  starts and ends, the IOP's jump to the next block. The same day, from 1.06
  to 1.11 times stock PCSX2's time to 1.04 to 1.08, the goal not reached,
  the maintainer's answers ("1. Yes", "2. Yes", "3. Ok"):
  committed and pushed; the new programs into their game folders; next the
  runtime itself built with a profile of play, measured first, against stock
  PCSX2 built without one. What becomes of a folder's counts after an update
  was put to them on 2026-11-22 (ADR 0048) and answered (ADR 0049).
- [ADR 0047](docs/adr/0047-the-runtime-built-as-pcsx2s-releases-are-and-with-a-profile-of-play.md):
  the maintainer's answers of 2026-11-22 to what that measurement found
  ("1. Yes", "2. Yes", "3. Yes", "4. Yes, write the text, I'll change it",
  "5. Yes", "6. Yes", "7. Ok do it"). Stock PCSX2 is timed built as its own
  releases are, with link-time optimization, which the build here had left
  out, and without a profile (built so it takes 1.5 to 4% less time; against
  it compiled games took 1.07 to 1.11 times its time, not 1.04 to 1.08), and
  the website's chart is corrected. A release's runtime is built with
  link-time optimization and a profile of play (0.99 to 1.00 times that stock
  PCSX2's time), on each system from training runs made there, the profile
  kept in the build tree; the maintainer's game folders get it. The maintainer
  tells PCSX2 of the defect found with it themselves; asked for the text, the
  session wrote none (ADR 0010) and gave where it is and how it is seen; the
  maintainer opened it the same day, PCSX2's issue 15223. A
  folder counts its profile after 3 minutes of the game's own time (9,000
  frames at 50 frames a second, 10,800 at 60: it changes ADR 0045's number).
  0.2 is next, with a way to bring the game folders of an earlier version up
  to date. The session's choices, for the maintainer to decide otherwise: the
  tree the folder is made from is built so, the development tree as before;
  stock PCSX2 is timed on a runtime of a tree of its own.
- [ADR 0048](docs/adr/0048-a-games-folder-of-an-earlier-version-is-brought-up-to-date-by-the-wizard.md):
  how a game's folder made with an earlier PS2Native is brought up to date
  (the maintainer's "7. Ok do it", ADR 0047; the rest the session's choices,
  for the maintainer to decide otherwise). The wizard does it, at the user's
  word: "Games made earlier..." in its first step lists the games of its own
  `games` folder and of any folder the user adds, with how each one's programs
  stand to this PS2Native's, and gives those ticked this version's runtime,
  launcher and toolchain, nothing else of the folder touched. The game then
  compiles itself again before it next starts, by its own recompile, whose
  window says why. `programs.txt` in a folder says which PS2Native's its
  programs are (0.1's folders do not say, and are earlier ones; a later
  version's folder is left as it is); nothing is changed while the game runs
  or compiles. The version is 0.2 from this work on. Put to the maintainer
  with a measurement (ADR 0046's point 6), and answered "3. Yes" (ADR 0049):
  a folder brought up to date with another toolchain counts its profile once
  more after a session.
- [ADR 0049](docs/adr/0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md):
  the maintainer's answers of 2026-11-22 to what was built for 0.2 ("1. Yes",
  "2. Yes, prepare the release", "3. Yes", "4. Do all seven", "5. After").
  Committed and pushed. 0.2 is released from that commit as ADR 0042 has a
  release made: both downloads from the committed and pushed tree, the runtime
  built with link-time optimization and the profile of play, the release a
  draft on GitHub with its notes, which the maintainer reads and publishes
  (asked again once it was made, "1. Yes, publish", "2. Yes do it": the
  session published it the same day, and the website's downloads moved to
  0.2). A
  folder brought up to date with another toolchain counts its profile once
  more after a session. The session brings the maintainer's own game folders
  up to date, the six on Windows and the one inside WSL. Why a compiled game
  takes more than stock PCSX2's time on Linux (Jak and Daxter 1.05 under WSL
  2) is looked for after 0.2 is out. The session's choices, for the maintainer
  to decide otherwise: 0.2 is a pre-release, as 0.1 is; the profile of play is
  the one made on 2026-11-22, the runtime's source unchanged since; the
  maintainer's folders get the release's own programs, the session has each
  compile its game and runs the start of a session again, and what each counts
  waits for a session of the maintainer's; the website's downloads stay 0.1's
  until 0.2 is published.
- [ADR 0050](docs/adr/0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md):
  the maintainer's answers of 2026-11-22 to what looking for Linux's speed
  found ("1. Yes", "2. Yes, write an issue as a guide I'll write it myself.",
  "3. Yes", "Stop."). PS2Native's builds leave out PCSX2's wakes of its VU
  thread where that thread is not in use (at every STROW and STCOL command of
  VIF1; PS2Native has the thread fixed off): the first change PS2Native
  carries to PCSX2's code, a patch in this repository applied to a copy of
  PCSX2's source when the runtime is built, the submodule never edited, stock
  PCSX2 timed as released, without it; carried at once, since it changes no
  result of a run. The maintainer tells PCSX2 themselves; asked for an issue
  written as a guide, the session wrote none (ADR 0010) and gave where it is,
  how it is seen and what limits it. Committed and pushed. The last 3% of the
  compiled game's distance to stock PCSX2 on Linux is left for now. How the
  change is carried is the session's choice, listed there (it changes ADR
  0002's "the machine is unmodified PCSX2", as ADR 0010 said a first patch
  would): `patches/pcsx2`, applied by git to copies of the files a patch
  changes when a tree is configured, PCSX2's targets compiling the copies;
  compiled sources only, not headers; every tree but the one stock PCSX2 is
  timed on, so the tests' "stock" configurations run PCSX2 with it; a
  runtime's `-help` and a release's `licences` say so; dropped by deleting the
  patch. Built and measured the same day: Jak and Daxter 9.2% less time on
  Windows and 27.5% less under WSL 2, every test passing. Found writing it
  down, measured, and put to the maintainer: PCSX2 switches its VU thread on
  by default on a computer with three cores or more, and every run of
  `ps2n-host` has had it off, stock PCSX2's among them; with it on
  (`ps2n-host -pcsx2-vu-thread`) stock PCSX2 takes 40% less time on Jak and
  Daxter's session, 24%, 12% and 2% less on the other three games', and the
  compiled games are at 1.02 to 1.50 times its time, where 0.90 to 1.00
  against stock PCSX2 with VU1 on the CPU thread.
- [ADR 0051](docs/adr/0051-the-comparison-with-stock-pcsx2-said-as-it-is-vu1-on-a-thread-of-its-own-next.md):
  the maintainer's answers of 2026-11-22 to what ADR 0050 measured ("1. Yes",
  "2. Yes", "3. Yes", "4. Yes", "5. Wait for that step", "6. Leave it as it
  is"). ADR 0050's change is committed and pushed. The public comparison is
  corrected: the website, the README and 0.2's notes on GitHub (edited by the
  session at that word, two passages marked as corrections, nothing else of
  the release touched) say that both sides ran with VU1 on the main processor
  thread, and give what stock PCSX2 takes with its VU thread on; from now on
  a speed claim against stock PCSX2 says which of the two it is against. The
  next speed step is VU1 carried out on a thread of its own, as PCSX2 does by
  default, measured against stock PCSX2 with its VU thread on. The
  maintainer's seven game folders get the runtime with ADR 0050's change. No
  release with that change alone: the next comes with the VU thread step.
  ADR 0050's record of its first answer stays as it is. The step as the
  session proposes to take it, for the maintainer to decide otherwise:
  PS2Native's VU provider on PCSX2's own VU thread, with PCSX2's machinery
  for it, following PCSX2's VU recompiler as it behaves with the thread on;
  on by default as in PCSX2 (three cores or more, off where PCSX2's game
  database says so), the user's to switch on PCSX2's own settings page; every
  comparison that holds VU1's registers to a stock run keeps VU1 on the main
  thread; a run with the thread on is held to the maintainer's sessions being
  their sessions at every fingerprint wherever stock PCSX2's with its VU
  thread on are; its criterion, no more than stock PCSX2's time with its VU
  thread on, every test passing as before; measured on one game first and
  put to the maintainer before it goes into a folder or a release. Built and
  measured the same day (the ADR's "The step, built"; docs/app-log.md, "VU1
  on a thread of its own"), and put to the maintainer, in no folder of theirs
  and no release: the provider on PCSX2's VU thread where that thread is on,
  held to PCSX2's VU recompiler by a sixth mode of the self-test and nine
  tests more. The session's choices within it, for the maintainer to decide
  otherwise: a game played has the thread as PCSX2 would give it, the user's
  to switch on PCSX2's own page in the wizard; every run without a window
  has VU1 on the main thread, whatever a game's settings file says; a session
  written down says when it was played with the thread; the runtime's
  profile of play is trained with the thread on; and the thread's setting is
  decided once for a run, when the game's disc is read, PCSX2's game database
  heard then (ADR 0050's change holds only if the thread is never switched
  on while a game runs; the note at its top says what is true now). Found
  with it: Agent Under Fire's run with the thread on does not repeat (on
  stock PCSX2 as compiled), so a session of such a game played with the
  thread cannot be run again as itself; and two of Linux's tests had never
  run there, skipped for want of a capture and counted as passing, which
  `scripts/linux/vu-selftest.sh` and `iop-selftest.sh` put right.
- [ADR 0052](docs/adr/0052-vu1-on-its-own-thread-committed-the-maintainers-folders-get-it-and-0-3-is-prepared-with-it.md):
  the maintainer's answers of 2026-11-22 to what ADR 0051's step measured
  ("1. Yes", "2. Yes", "3. Yes", "Do whatever needs doing from your side
  (from your "Not done" category)"). The step is committed and pushed. The
  maintainer's seven game folders get the release's programs, made from
  0.3's tree, each compiling its game again. 0.3 is prepared with it as ADR
  0042 and ADR 0049 have a release made (the profile of play made again on
  each system from the tree released, both downloads tried, a draft on
  GitHub the maintainer publishes); the session had recommended doing it
  after the maintainer had played with the step, and their "Yes" with "Do
  whatever needs doing" is read as now, the draft leaving them free to play
  first. What the session had not done is done: the Linux runtime's profile
  of play made again with the thread, and a game in a window on Linux with
  the thread, inside WSL, its window on the maintainer's screen meanwhile.
  The session's choices, for the maintainer to decide otherwise: the Windows
  profile made again too (the runtime's source changed after the one
  measured with); the release's runtime timed again before the notes quote
  a number; 0.3 a pre-release, its notes saying which stock PCSX2 each speed
  claim is against. The version is 0.3 from this commit.

## Layout, build and test

```
third_party/pcsx2         PCSX2 v2.9.170, a pinned submodule. Never edit it; what PS2Native changes of PCSX2's code is in patches/pcsx2
patches/pcsx2             PS2Native's changes to PCSX2's code (ADR 0050), one patch to a change, each decided by the
                          maintainer and saying at its top what it changes and why; its README lists them (one so far:
                          PCSX2's VU thread is not woken where it is not in use)
third_party/ps2autotests  hardware-verified PS2 test programs, a pinned submodule
cmake/ps2n-inject.cmake   adds PS2Native's targets to PCSX2's own CMake project without touching it; and
                          ps2n-pcsx2-patches.cmake, which applies patches/pcsx2 to copies of the files they change, in the
                          build tree, and gives PCSX2's targets the copies to compile
CMakeLists.txt, core/     build-time tools, a project of their own with no PCSX2 dependency: the
                          instruction decoder, disassembler, assembler, ELF/IRX/SNR2 readers, the reader of a
                          BIOS image's directory (romdir) and of a disc image's files (iso9660); for the
                          vector units, the first pass of PCSX2's VU
                          recompiler (ps2n/vu_mvu.h) and a block of micro code as a list of steps, its plan
                          (ps2n/vu_plan.h; the steps themselves, which compiled micro code needs, in
                          ps2n/vu_step.h and vu_steps.inc); for the IOP, its instructions as PCSX2's IOP
                          recompiler treats them and what a block of them is (ps2n/r3000a.h); all used by the
                          runtime too, and vu_step.h and r3000a.h by compiled code as well
recompiler/               ps2n-recomp, in that same project: analysis and C++ generation for EE executables
                          and for the BIOS's EE code (bios.cpp), and C++ generation for the blocks of micro
                          code (vu_codegen.cpp) and of IOP code (iop_codegen.cpp) that runs captured
runtime/                  what PS2Native adds to the core at run time: the EE provider (dispatcher,
                          fallback interpreter), the record of the blocks PCSX2's recompiler would have formed
                          (rec_blocks), instruction semantics, the contract for generated code; the VU
                          provider (vu_interp), which follows PCSX2's VU recompiler: its record of programs
                          and blocks, the planner, the interpreter of plans; the steps' semantics
                          (ps2n/vu_ops.h) and the contract for compiled micro code (ps2n/vu_recomp.h); the
                          IOP provider (iop_provider), which follows PCSX2's IOP recompiler: its record of
                          blocks, the interpreter; the IOP's instructions carried out (ps2n/iop_ops.h) and the
                          contract for compiled IOP code (ps2n/iop_recomp.h); what a module of compiled code
                          and the host share (game_module.h, module_entry.cpp, ps2n/module.h); what compiled
                          code reaches of PCSX2's core, PCSX2's own in the runtime and PS2Native's description
                          of it in a module (ps2n/pcsx2.h, ps2n/pcsx2_layout.h), and the description compared
                          with PCSX2's when the runtime is built and when the host starts (pcsx2_check.cpp)
host/                     ps2n-host: console frontend for the PCSX2 core, the test bench (the self-test of the
                          instruction semantics, of the VU provider and of the IOP provider; the watches on the
                          core's providers: sync_trace, vu_report, iop_report); with -window, where a game
                          is played (window.cpp: a Win32 window on Windows, an X11 one on Linux; on Linux
                          shm_names.cpp gives PCSX2's one shared memory name for every process the process's
                          number); a game's launcher on Windows (launcher.cpp:
                          the program a game's folder is started from, which starts the runtime beside it,
                          never with anything left to compile: ADR 0040);
                          and a disc image's files read through PCSX2's own readers of every format it plays
                          (disc.cpp: -disc-executables, -disc-extract; step P4.8, ADR 0036)
wizard/                   ps2n-wizard, the application a game is made with (Phase 4, step P4.5; ADR 0032):
                          its five steps (wizard_window.cpp), the recompile (pipeline.cpp), the game's folder
                          (package.cpp), PCSX2's archive of patches fetched or given where PCSX2 is not installed
                          (patches.cpp; step P4.8), the games made with an earlier PS2Native brought up to date
                          (update_window.cpp; ADR 0048); and ps2n-remake (remake_main.cpp; step P4.6, ADR 0034), the same
                          recompile in a small window of its own, with which a game's folder compiles what its
                          sessions met for the first time itself, after a session and before the game starts (ADR
                          0040); a game's launcher on Linux (launcher_main.cpp, Qt's
                          windows; step P4.7), and what differs between the two systems for the wizard, the
                          recompile and a game's folder (platform.h: the files' names, the toolchain's compiler and
                          linker and what they are given); PCSX2's own settings pages, compiled from the submodule unchanged, in
                          a window of its own (settings_window.cpp: PCSX2's SettingsWindow class with the pages a
                          game has), with what they call of the rest of PCSX2's frontend (stand_ins.cpp,
                          pcsx2-qt/MainWindow.h, found before PCSX2's) and of the core (host_interface.cpp)
tests/unit/               unit tests of the build-time tools
tests/module/             what a module defines for itself, tested (stack_probe.cpp: its __chkstk)
tests/disc/               a small disc of our own in three formats, for the runtime's reading of a disc (disc/formats)
tests/                    CTest registration for programs booted on the core, the driver script, the recorded
                          baselines; recompiles the test programs into the build tree and links them into the host
tests/programs/           our own test programs, in assembly for core's assembler: selfmod (rewrites its code), bench,
                          timeout (a delay loop that PCSX2 does not run), countcarry (a defect of PCSX2's
                          recompiler that PS2Native reproduces), loader (loads the two programs in loader/ itself,
                          over the same addresses, as a game of several executables does)
tests/sdk/                C test programs built with ps2sdk (prebuilt in bin/), their sources, what stock PCSX2 prints
tests/games/              controller scripts for runs of a game (button presses we wrote; nothing from a game)
scripts/windows, linux    build and test entry points
assets/                   branding/ (the logo, the icons, the colours; its README says which to use where) and
                          ps2n.rc (the icon of the Windows executables)
docs/                     brief, assessment, decisions (adr/), proof-of-concept log, game log, Phase 3 report, Phase 4
                          proposal and log (app-log.md); and the website,
                          which GitHub Pages serves from this folder: index.html, 404.html, .nojekyll, site/
                          (site/README.md says what belongs to it and where each of its numbers comes from)
version.txt               PS2Native's version (0.3), which the build gives the programs as ps2n_version.h (ADR 0042;
                          not VERSION: the root is on the include path, and Windows would find it for <version>)
build/, deps/             build trees and downloaded dependency packages; git-ignored
```

- Build: `scripts\windows\build.ps1` (add `-Reconfigure` after changing any
  CMake file). It enters the Visual Studio developer shell by itself.
  `-Pcsx2Source <dir> -BuildDir build\<name>` builds against another PCSX2
  source tree in a build tree of its own (and `test.ps1 -BuildDir` tests
  it): to try a newer PCSX2, or a change someone made to PCSX2
  (`build.sh --pcsx2-source` on Linux). A change of the session's own to
  PCSX2 is tried so, on a copy of the submodule in the build tree, never in
  the submodule (`build\measure\vif-trial.ps1` is one such trial). A change
  the maintainer has decided PS2Native carries (ADR 0050) is a patch in
  `patches\pcsx2`: when a tree is configured, each file a patch changes is
  copied into it (`ps2n-pcsx2\src`), the patches are applied to the copies,
  and PCSX2's targets compile the copies in the place of the files they came
  from (`cmake/ps2n-pcsx2-patches.cmake`; a patch that does not apply, or
  changes a file no target compiles, a header among them, stops the
  configuration). Every tree has them but the one stock PCSX2 is timed on:
  `build.ps1 -Pcsx2Patches off` (`build.sh --pcsx2-patches off`) builds PCSX2
  as it is, and the build tree remembers it. `ps2n-host -help` says in its
  second line which a runtime has, and a release's `licences` folder carries
  the patches. So in every other tree the tests' "stock" configurations run
  PCSX2 with PS2Native's changes: a change that would alter a result of a
  run is not one to carry this way without the maintainer's word on that
  too.
- Test: `scripts\windows\test.ps1 -Bios <BIOS image>`, optionally
  `-Filter <regex>`. The BIOS is the maintainer's `SCPH-70004` dump in
  `Documents\PCSX2\bios`. Tests are skipped, not passed, without one.
- Speed: `scripts\windows\bench.ps1 -Bios <BIOS image> -Repeat 3` runs
  `tests/programs/bench.s` under each configuration and prints the times.
  A run that is timed is kept on the processor's fast cores (`ps2n-host
  -fast-cores`; `bench.ps1` and `game.ps1` pass it): left to Windows, the
  same run takes up to twice as long the next time. `game.ps1 -TimeProfile`
  (`ps2n-host -profile`) writes where the CPU thread's time went, by kind
  of code and by function, into `profile.txt` among the run's files; for
  the compiled code also by kind of generated line, its FPU's lines by
  instruction, and its hottest
  instructions by function and offset, to read beside the function's
  disassembly (`llvm-objdump --disassemble-symbols`, in the Visual Studio
  install's `VC\Tools\Llvm\x64\bin`). On Linux `ps2n-host -profile` (through
  `tests/guest_test.cmake -DPROFILE=1`) says it by kind of code and by
  function, without lines, a sample for every millisecond the thread ran,
  and names what called each of the system's functions, with where in the
  host's file the call returns to (`llvm-symbolizer --inlines -e` the host
  says what was compiled into there); to see what the two systems' compilers
  make of one generated file, compile it for both on Linux (`clang
  --driver-mode=cl --target=x86_64-pc-windows-msvc` with a Windows folder's
  options; docs/app-log.md, "Linux's speed"). Over the level run, two builds of
  the same code, or the same build an hour apart, can differ by a second:
  a change worth less than that does not show, and a ratio needs a stock
  run timed beside it. `docs/game-log.md`, "G5" and "G6", has what was
  measured. `game.ps1 -Marks 6600 -ProfileFrom 6600` notes the time at
  chosen frames and profiles from one on: the level run's startup apart
  from its play ("G10"). Profile-guided optimization of the generated code:
  `build.ps1 -Pgo generate`, a run with `LLVM_PROFILE_FILE` set (the level
  is trained on `tests/games/killzone/level1-train.pad`, not the timed
  script), `llvm-profdata merge` (in the Visual Studio install's
  `VC\Tools\Llvm\x64\bin`), then `build.ps1 -Pgo use -PgoData <file>`; the
  counts stay in the build tree, and are made again after the generated
  code changes ("G10", "Profile-guided optimization"). `build.ps1 -Lto
  thin` links PS2Native's own code (the generated code, the runtime) with
  link-time optimization, PCSX2's code compiled as before; it gave nothing
  over the level run, and `-Lto off` is how the build is used ("G10",
  "Link-time optimization"). What the vector units run: `game.ps1
  -VuReport` (`ps2n-host -vu-report`) writes `vu-report.txt` among the
  run's files (what was written into each VU's micro memory, where, with
  how many contents, where programs were started, how long each ran), with
  the counts frame by frame and the contents written beside it, which are
  the game's and stay in the build tree ("G11"). What the IOP runs:
  `game.ps1 -IopReport` (`ps2n-host -iop-report`) writes `iop-report.txt`
  (how much IOP time passed and how long the host took for it, how often
  the IOP's memory was written, which modules its kernel listed where and
  when, and, under PCSX2's IOP recompiler, how many blocks it formed in
  each), with the counts frame by frame beside it ("G12"). How much host code
  each kind of generated line became: `ps2n-host -code-size <file>` (from
  the host's own debugging information; no machine, about 20 s). To try a
  change to a header on one generated file, compile that file alone with
  the flags `build\windows-clang\build.ninja` gives it (without `/Yu` and
  `/Fp`) and an include directory searched first, into a scratch folder
  outside the build, and read `llvm-size -A` of the object. `game.ps1 -CallSites off` times one
  build with calls through a register sent to the runtime's dispatcher.
  A game's folder as users get it (the speed step, ADR 0043): copies made
  with `make-game.ps1` into `build\measure\<kit>\games` (`-LineTables`
  compiles the modules with line tables, for `-TimeProfile` to name their
  functions and lines; `-ExtraFlags` and `-ExtraLink` try other compiler and
  linker options; all three for measurements only), a session of the
  maintainer's run again from a copy with `game.ps1 -Runtime
  <copy>\ps2n-host.exe -Replay <session> -Marks 1`, in turns with
  `-Config stock-jit`; `build\measure` keeps the scripts (`replay.ps1`,
  `variant.ps1`, `flags.ps1`, `relink.ps1`, the batches) and
  `results.txt`. Batches run on a desktop of their own
  (`build\measure\hidden.ps1`), one at a time. `game.ps1 -FunctionsRan`
  (`ps2n-host -functions-ran`) writes the compiled functions a run ran, for
  `lld-link /ORDER`; a module made to count what of its code runs (compiled
  with `-fprofile-generate`, linked with LLVM's profile runtime and
  `runtime/module_counts.cpp`, as a game's folder makes it: ADR 0045) has
  its counts written by the host at the run's end (`ps2n-host -module-counts
  <dir>`, with `-modules <dir>` to load such modules from elsewhere than
  beside the host). `game.ps1 -SampleProfile`
  (`ps2n-host -sample-profile`) writes the profiler's samples of the modules
  (made with `-LineTables`) as a sampled profile for clang's
  `-fprofile-sample-use`: tried on 2026-11-21, it gave nothing that shows
  (docs/app-log.md, "A profile sampled while the game plays, tried").
  Clang's `-mllvm -sample-profile-check-record-coverage=100` says how much
  of a sampled profile a file's functions took.
- The runtime itself (ADR 0046; docs/app-log.md, "The runtime itself"):
  `build.ps1 -RuntimeLto on` builds `ps2n-host`, PCSX2's core and what
  PS2Native adds to it, with link-time optimization, as PCSX2's own releases
  are built (`build.ps1` otherwise leaves it out); `-RuntimePgo generate`
  builds it to count what it runs, in a tree without the wizard (a run then
  writes its counts where `LLVM_PROFILE_FILE` says), `llvm-profdata merge
  -sparse` puts the runs' counts together without the functions no run ran,
  and `-RuntimePgo use -RuntimePgoData <file>` builds it with them
  (`build.sh --runtime-lto`, `--runtime-pgo`, `--runtime-pgo-data`). The
  build tree remembers them; `off` takes them out. The modules of compiled
  code, the launcher and the wizard's own code are built as before
  (`ps2n_not_the_runtime`, `cmake/ps2n-inject.cmake`). The counts come from
  playing games and stay in the build tree. At the maintainer's word (ADR
  0047) a release's runtime is built with both, so the tree the folder is
  made from is: `build.ps1 -BuildDir build\windows-clang-module -RuntimeLto on
  -RuntimePgo use -RuntimePgoData build\pgo\runtime-windows.profdata`; the
  development tree, `build\windows-clang`, stays without either. The profile
  is made again for a release, from the tree released, by
  `scripts\windows\runtime-profile.ps1 -Bios <BIOS image>`
  (`scripts/linux/runtime-profile.sh`): the runtime built to count in
  `build\windows-clang-count`, the training runs of `build\pgo\training.txt`
  (one to a line: a name, the disc image, a game's folder whose modules the
  run takes, a session to run again or `-`, the frames, and `vu-thread` for
  a sixth part where the run is to have VU1 on PCSX2's VU thread, as a game
  is played where that thread is on; a session that is timed against stock
  PCSX2 never among them) each through `game.ps1
  -Modules <game folder>`, and the counts merged sparse; about 10 minutes on
  Windows, 18 on Linux under WSL 2, where each game's folder has to be one
  made on Linux. The build gives the compiler a copy of the profile named
  after what it holds, so a profile made again under the same name is
  compiled with (what was compiled with two profiles does not link). `kit.ps1`
  and `kit.sh` say how the tree's runtime was built, and warn when it is not a
  release's. Stock PCSX2 is timed built as its releases are, on the runtime of
  a tree of its own with link-time optimization, no profile and none of
  PS2Native's changes to PCSX2's code (`build.ps1 -BuildDir
  build\windows-clang-stock -Module on -RuntimeLto on -RuntimePgo off
  -Pcsx2Patches off`; built with link-time optimization it takes 1.5 to 4%
  less time than built as the trees are by default), never on the runtime
  with the profile. Every run of `ps2n-host` has VU1 on the CPU thread, stock
  PCSX2's too, so that runs repeat; PCSX2's own default where there are three
  cores or more is its VU thread ("MTVU"), which `game.ps1 -Config stock-jit
  -Pcsx2VuThread` (`ps2n-host -pcsx2-vu-thread`, `-DPCSX2_VU_THREAD=1`
  through `tests/guest_test.cmake`) switches on, for timing stock PCSX2 as
  its users mostly have it (ADR 0050). With a runtime built with
  link-time optimization the three comparisons of two runs of
  `dma/vif/stcycl` are disabled, for a defect of PCSX2's that makes its runs
  differ there (`tests/same-as-stock-known-differences.cmake`); they run in
  the development tree. `build\measure\rpgo.ps1` is the trial that measured
  both, `build\measure\batch26.ps1` the measurement as 0.2 has it.
- The compiled code in modules of its own (Phase 4, steps P4.1 and P4.2):
  `scripts\windows\build.ps1 -BuildDir build\windows-clang-module -Module on`
  with the main tree's game settings builds everything the host would hold
  compiled into four modules beside `ps2n-host.exe`, one for each kind of
  code (`ps2n-game.dll`, the programs' EE code; `ps2n-bios.dll`;
  `ps2n-vu.dll`, the micro code; `ps2n-iop.dll`), which the host loads when
  it starts (`runtime/game_module.h`); what crosses between them is
  `runtime/include/ps2n/module.h`. A module is compiled with PS2Native's own
  headers and nothing else, no system header, no standard library, nothing
  of PCSX2's (`ps2n/pcsx2.h` gives it `ps2n/pcsx2_layout.h` in their place),
  and linked with the host's import library and nothing else; what the
  compiler asks of a C runtime it defines itself (`_fltused`, and
  `__chkstk`, which a function whose locals take more than a page calls:
  `runtime/module_entry.cpp`). So a header
  the generated code includes takes PS2Native's own headers, `<stdint.h>`,
  `<stddef.h>` and the SSE intrinsics' headers, and nothing of PCSX2's but
  through `ps2n/pcsx2.h`; a field of PCSX2's that compiled code comes to use
  goes into `pcsx2_layout.h` and its check, `runtime/pcsx2_check.cpp`. `test.ps1` and
  `game.ps1` take the same `-BuildDir`. Without `-Module on` the code is
  built into the host as before. `-Module imports` (in a tree of its own,
  `build\windows-clang-imports`) compiles the code as for a module and
  links it into the host all the same: what reaching the core through the
  import table costs, alone. What it costs is in
  [docs/app-log.md](docs/app-log.md), "P4.1" and "P4.2".
- What a computer with nothing else installed compiles and plays a game
  from (Phase 4, steps P4.3 and P4.4): `scripts\windows\kit.ps1` puts it
  together in `build\kit` from the module tree, LLVM's own release (ADR
  0030; fetched into `deps\llvm-22.1.3` when not there, its SHA-256
  checked) and Visual Studio's redistributable C++ runtime (ADR 0031); its
  one command, `make-game.ps1 -Image <disc image> -Bios <BIOS image>
  [-Title <name>] [-More <names on the disc>] [-Captures <folder>] [-Play]`
  (`scripts\windows\kit\`), makes the game into a folder of its own there,
  `games\<name>`, that plays it: `<name>.exe`, the launcher
  (`host/launcher.cpp`), starts the runtime beside it with `settings.txt`
  (`ps2n-host -settings`, one option to a line, `%GAME%` the folder; the
  runtime started with no options reads it by itself), which writes each
  session down in the folder's `sessions` (`-sessions`) and names the
  window (`-title`); `last-run.log` has what the runtime said, and the
  launcher says why in a message box when it could not play. It takes the
  game's executables from the disc image, in any format PCSX2 plays, through
  the runtime (`ps2n-host -disc-extract`, PCSX2's own readers; a `.cue`
  stands for the image it names; step P4.8), and the
  next time it runs compiles what the folder's sessions had to interpret
  too. `game.ps1 -Runtime <folder>\ps2n-host.exe` runs a packaged game as
  it is, a session again among them. Run it from the folder with nothing
  but Windows on `PATH` to check that it needs nothing else; what it makes
  stays in the build tree, as everything made from a game does. The
  wizard (P4.5) does the same in five steps: built with `build.ps1
  -BuildDir build\windows-clang-module -Wizard on` (Qt from PCSX2's
  dependency package), it goes into the folder's `runtime` with Qt and its
  plugins (`runtime\wizard-files.txt` lists what a game's folder leaves
  out), and `PS2Native.exe` at the folder's root starts it. It finds a
  disc's executables itself (`ps2n-host -disc-executables`), each compiled
  unless the user leaves it out, writes the game's
  own settings file and patch file into its folder's data, and makes the
  folder with `-packaged`: the runtime then takes the game's settings from
  that file, over the defaults the wizard showed, which are the runtime's
  too (`host/game_settings.h`, `ps2n::game::SetDefaults`), and refuses to
  start when the file sets what PS2Native fixes (`kFixedSections`,
  `kFixedKeys`, the list the wizard keeps out of it; step P4.8). Where
  PCSX2 is not installed, its settings step offers to fetch PCSX2's archive
  of patches, or to take one the user gives, kept in the folder's
  `patches\` (ADR 0036). `kit.ps1` writes `licences\`, the licence of
  everything the folder carries and where its source is
  (`licences\README.txt`; Qt's notice made from the SBOMs in PCSX2's
  dependency package, with the texts of Qt's source, fetched into
  `deps\qt-<version>-licences`); the folder for others is made with
  `-Out` into a folder of its own, from a tree committed and pushed. A game's
  folder, whichever made it, also has a copy of the toolchain, Qt and
  `ps2n-remake.exe`, and `make.txt`, how its modules are made (step P4.6,
  ADR 0034): when a session ends, its launcher starts `ps2n-remake`, which
  merges what the sessions met into the folder's captures and, when that is
  more than the modules were compiled from (`work\compiled-from.txt`),
  compiles only what changed (objects newer than their sources stay) into
  `work\next-<time>`, in a window of its own in front, with how far it has
  got and Later, which closing it is too (ADR 0040; `work\remake.log` says
  what each run did); the launcher puts those modules in place at the next
  start. A session also writes the compiled functions it ran
  (`functions-ran.txt`), which the compile adds to the folder's
  (`captures\functions-ran.txt`) and links the game's and the BIOS's
  modules in that order (ADR 0044: `/ORDER` on Windows,
  `--symbol-ordering-file` on Linux), a session that ran new ones having
  the modules linked again. Once for a game, after a session of at least
  3 minutes of the game's own time (9,000 frames where the console shows the
  game at 50 frames a second, 10,800 at 60: the runtime writes a session's
  `frames-a-second` down when it ends, ADR 0047; a session written down before
  it did is taken for one at 60), `ps2n-remake` then compiles the game for how its
  user plays it, in the same window (ADR 0045): the modules compiled to count
  (`work\count`), the session's first 3 minutes run again with them by the
  folder's runtime without a window (`-modules`, `-module-counts`,
  `-progress`; the session's memory cards, NVRAM and settings copied into
  `work\count\run`), the counts merged by the toolchain's `llvm-profdata`,
  and the modules compiled again with them (`work\objects-profiled`, then the
  folder's objects), the counts kept in `captures\profile.profdata`, which
  every compile after takes. Later puts it off to another session's end; a
  game started meanwhile stops it (`work\stop-profile`); when it fails,
  `work\count\failed.txt` says why and it is not tried again. A game's folder
  made with an earlier PS2Native is brought up to date by the wizard (ADR
  0048; `wizard/update_window.cpp`, `BringUpToDate` in `wizard/package.cpp`):
  "Games made earlier..." in its first step lists the games of the folder's
  own `games` and of any folder added, and gives those ticked this
  PS2Native's runtime, launcher and toolchain; `programs.txt` in a game's
  folder says which PS2Native's its programs are (the version, and a
  fingerprint of the runtime and toolchain they were copied from; a folder
  `make-game.ps1` made says nothing, and is listed as an earlier one's). The
  folder's own recompile then compiles the game again before its next start
  (the toolchain being another), saying why (`work\brought-up-to-date.txt`),
  and, where the folder had its counts, counts once more after a session
  (`work\count-again.txt`). For checks, `ps2n-wizard --update <folder>` does
  it without a window, a line written for each game, and `--updates <folder>
  --shots <folder> [--auto]` opens the window and saves it as a picture. The game never starts with anything left to compile: before the
  runtime, the launcher runs `ps2n-remake --before-play`, which compiles
  what is left in the same window (and waits for a compile already
  running), and starts the game only when it ends with 0; Later there
  leaves the game unstarted. For checks,
  `ps2n-wizard --kit build\kit --image <iso> --bios <bin> --shots <folder>
  --auto [--until <step>] [--without-pcsx2 [--fetch-patches]]` fills the
  first steps in, saves each step (the settings, each category) as a
  picture, and presses Next itself (`--without-pcsx2`: as on a computer
  without PCSX2; `--fetch-patches`: the archive fetched unasked); run from
  the build tree it needs `deps\windows-x64\deps\bin` on `PATH` and
  `QT_PLUGIN_PATH` set to `deps\windows-x64\deps\plugins`.
- The game: `scripts\windows\build.ps1 -Game <its boot executable>` compiles
  it into the host (the build tree remembers it; `-GameCapture <file>` adds
  what runs reported). A game made of several executables, whose boot
  executable loads the others itself without the BIOS's loader (Agent Under
  Fire's), gets them with `-GameMore '<path>|<path>'`: each is compiled as an
  image the runtime puts in use while memory holds its code (ADR 0026; the
  execution model's "Executables a program loads itself"). A build holds
  one game's EE code: the build tree now holds Agent Under Fire's, with the
  micro code and IOP code of both games; Killzone comes back with `-Game
  <its executable> -GameMore none -GameCapture build\game\killzone\capture.toml`
  and its profile (`-Pgo use -PgoData build\pgo\game.profdata`). `scripts\windows\game.ps1 -Image <disc image> -Config
  <configuration> -Frames <n>` runs it, with `-Pad` for a controller script
  (`tests/games/<game>/*.pad`), `-FrameEvery` for pictures along the way and
  `-Capture` to collect what had to be interpreted. `docs/game-log.md`, "How
  to run it", has the commands. A run's files are in
  `build\windows-clang\ps2n\tests\game\<name>\<configuration>`.
  `scripts\windows\sheets.ps1 -Name <name> -Config <configuration>` puts the
  pictures a run saved on contact sheets, sixty to an image: the way to look
  a whole run over, and to find out what a controller script has to press
  when (look at a sheet with the Read tool).
- The BIOS's EE code (step G8): `scripts\windows\build.ps1 -Bios <BIOS
  image> -BiosCapture build\bios\capture.toml` compiles it into the host as
  four images, from the maintainer's dump read in place, into
  `build\windows-clang\ps2n\generated\bios`; the build tree remembers both.
  Every test run with compiled code writes what it had to interpret of the
  BIOS to `bios-capture.toml` among its files (`ps2n-host -bios-capture`);
  `ps2n-recomp capture --out build\bios\capture.toml build\bios\capture.toml
  <those files>` puts them into the BIOS's capture for the next build.
  Without -Bios the BIOS is interpreted. Nothing generated from the BIOS
  may leave the build tree (ADR 0007). `ps2n-recomp bios-info <BIOS image>`
  says what the analysis finds in it.
- The vector units' micro code (step G11): in PS2Native's configurations
  both VUs run on PS2Native's VU provider (`ps2n-host -vu-ps2n both`;
  `game.ps1 -VuPs2n none` puts PCSX2's VU recompiler back under them; the
  stock run of a comparison never has PS2Native's). It carries a block of
  micro code out by the function a build compiled for it, found by a
  fingerprint of what the block does (its plan's key), and by its
  interpreter otherwise.
  `game.ps1 -VuCapture build\game\<name>\vu-capture.toml` adds the blocks a
  run had to interpret to that file (`ps2n-host -vu-capture`, `ps2n-recomp
  vu-capture`), and `scripts\windows\build.ps1 -VuCapture <file>` compiles
  them into the host, into `build\windows-clang\ps2n\generated\vu` (the
  build tree remembers the file; `-VuCapture none` forgets it). After a run
  the host says, for each VU, how many different blocks of micro code ran,
  how many of them compiled and how many interpreted, and how often: for
  the level run "interpreted" must be zero. `game.ps1 -VuStatic off`
  (`ps2n-host -vu-static off`) has every block interpreted, the reference
  for the compiled ones. `scripts\windows\vu-selftest.ps1 -Bios <BIOS
  image>` captures the self-test's first random programs and builds them
  in (`build.ps1 -VuSelftestCapture`), for the test `selftest/vu-compiled`:
  run it again whenever that test says blocks had to be interpreted, which
  is what any change to how blocks are planned does. On Linux it is
  `scripts/linux/vu-selftest.sh --build-dir <tree>` (`PS2N_BIOS` set), once
  for each tree: a tree it was never run for skips the test, and ctest counts
  a skipped test among those that did not fail, so look for "Skipped" in a
  test run's output before saying that every test passed. So does a game's
  capture go stale: run the game with `-VuCapture` again and rebuild. The
  captures are derived from the game and stay in the build tree. The
  execution model's "The vector units' micro programs" says how it works.
- The IOP's code (step G12): in PS2Native's configurations the IOP runs on
  PS2Native's IOP provider (`ps2n-host -iop-ps2n`; `game.ps1 -IopPs2n off`
  puts PCSX2's IOP recompiler back under them; the stock run of a
  comparison never has PS2Native's). It carries a block of IOP code out by
  the function a build compiled for it, found by a fingerprint of the
  block's instructions wherever they are loaded, and by its interpreter
  otherwise. `game.ps1 -IopCapture build\game\<name>\iop-capture.toml`
  adds the blocks a run had to interpret to that file (`ps2n-host
  -iop-capture`, `ps2n-recomp iop-capture`), and
  `scripts\windows\build.ps1 -IopCapture <file>` compiles them into the
  host, into `build\windows-clang\ps2n\generated\iop` (the build tree
  remembers the file; `-IopCapture none` forgets it). After a run the host
  says how many different blocks the IOP ran, how many of them compiled
  and how many interpreted, and how often, and how many blocks PCSX2's IOP
  recompiler holds: for the level run "interpreted" and that must be
  zero. `game.ps1 -IopStatic off` (`ps2n-host -iop-static off`) has every
  block interpreted, the reference for the compiled ones.
  `scripts\windows\iop-selftest.ps1 -Bios <BIOS image>` captures the
  blocks of the self-test's random IOP programs and builds them in
  (`build.ps1 -IopSelftestCapture`), for the test `selftest/iop-compiled`:
  run it again whenever that test says blocks had to be interpreted, which
  is what a change to the programs the self-test makes does (on Linux,
  `scripts/linux/iop-selftest.sh --build-dir <tree>`, as for the VUs). The
  captures are derived from the BIOS and the game and stay in the build tree. What
  the provider does not carry out it stops at, with an error that names it
  (`r3000a::shape`, `Refuse` in `runtime/iop_provider.cpp`). The execution
  model's "The I/O processor's code" says how it works.
- Playing the game: `scripts\windows\game.ps1 -Image <disc image> -Play
  -Name <name>` opens it on PCSX2's Vulkan renderer at three times the
  console's resolution, with a picture of 1920 by 1080: full screen on a
  screen that size, in a window on a larger one (Alt+Enter or F11
  switches; ADR 0025, step G14; `-Renderer`, `-Upscale` and `-WindowSize`
  change them), with the game's widescreen and frame-rate patches where
  the installed PCSX2's archive has them, its films kept at 4:3, and the EE
  overclock the frame-rate patch asks for (steps G15 to G17; `-Patches
  none` leaves both out, `-Patches widescreen` the frame-rate one,
  `-EeCycleRate 0` the overclock; the game's patch file is copied into the
  run's data folder), at the
  console's speed, with sound, the keyboard and the first controller
  (`ps2n-host -help` has the keys), until the window is closed; `-Config
  stock-jit` puts stock PCSX2 in the same window. In a window VU1 is carried
  out on PCSX2's own VU thread where the settings have it, as in PCSX2 (ADR
  0051: its default where the computer has three processor cores or more, a
  game's own setting over it, PCSX2's game database over both); every run
  without a window has VU1 on the CPU thread, so that it repeats, whatever a
  game's own settings file says (`OnSettingsLoaded`, `host/main.cpp`), unless
  `game.ps1 -Pcsx2VuThread` (`ps2n-host -pcsx2-vu-thread on`) asks for the
  thread. The setting is decided once for a run, when the game's disc is
  read, PCSX2's game database heard then, and nothing switches the thread on
  while a game runs: `patches/pcsx2`'s first change holds only so (a run's
  report says how many of VU1's runs were on the thread: all of them or
  none). A session written down says when it was played with the thread
  on (`vu-thread on`): a run of it again need not be it then (Agent Under
  Fire's is not, with the thread or without: with the thread on its run does
  not repeat). To check what a played game gets without a window on the
  maintainer's screen, a game's window runs on the measurement's hidden
  desktop, muted (`build\measure\vut-04.ps1`). `game.ps1 -Patches
  widescreen,fps` gives a comparison's two runs the patches and the
  overclock their note asks for. A session
  in the window takes `-Pad`, `-Frames` and `-Marks` as a scripted run
  does: the game log's "How to run it" has the session that measures the
  pace in play. What a game keeps between sessions (memory card, the
  console's settings) is in `build\game\<name>\play`, and so is what the
  last session had to interpret of the game (`capture.toml`; `-Capture
  <file>` adds it to the file `build.ps1 -GameCapture` takes), of its
  micro code (`vu-capture.toml`; `-VuCapture <file>` likewise) and of the
  IOP's code (`iop-capture.toml`; `-IopCapture <file>`). Every session is
  written down to be run again, in `build\game\<name>\play\sessions\<time>`
  (its controller frame by frame, the clock it started at, its memory
  cards and NVRAM as they were, a new card where a slot had none, the
  patches it was played with; `ps2n-host -record`), and `game.ps1 -Replay
  <that folder>` runs it again without a window, in any configuration,
  `same-as-stock` included, with those patches (a session written down
  before they were is given them: `-Patches`); each run says whether it is
  the session at every fingerprint of its registers (ADR 0024, step G13). In a
  window the console's clock starts at this computer's time of day; every
  other run starts it at one fixed time (`ps2n-host -clock fixed`), or at
  the replayed session's, so that two runs are the same machine. Windows
  only so far.
  A session that checks the window does it with the sound off, because the
  sound comes out of the maintainer's speakers:
  `scripts\windows\window-check.ps1 -Image <disc image> -Name <name> -Steps
  '5 shot a.png', '8 key K', '14 close'` starts a muted session in a window
  of 1280 by 720 (`-WindowSize ''` for the game's own default), has the
  host write the picture on display at each `shot` and photographs the
  window beside it (`a-window.png`: a picture Vulkan presents comes out
  white in a photograph on this computer), presses keys in it and closes
  it (look at the pictures with the Read tool).
- Tests are named `<suite>/<configuration>/<program>`. Suites: `ps2autotests`
  (console recordings), `programs` (our own, `tests/programs/*.s`, built with
  `ps2n-recomp assemble`) and `sdk` (C programs built with ps2sdk, binaries in
  `tests/sdk/bin`). Configurations: `stock-jit`, `stock-interp`
  (PCSX2 as it ships), `ps2n-ee` (our providers, the EE's, the IOP's and
  the VUs', interpreting), `ps2n-static` (our providers, the program
  compiled ahead of time; a test program's micro code is interpreted, no
  build having captured it, and its IOP code runs compiled where the host
  has the block, as it has the BIOS's modules' with a game's capture built
  in), `lockstep` (the compiled run compared with the interpreted one at
  every event test, the IOP's state in the fingerprint), `same-as-stock`
  (ADR 0011: the compiled run compared with a stock run at every sync
  point: place, cycle count, the IOP's PC and cycle count, and the
  registers, VU0's, VU1's and the IOP's among them), for the `programs`
  and `sdk` suites `same-as-stock-ee130`, `-ee180`, `-ee300`, `-ee75` and
  `-ee50` (the same comparison with PCSX2's EE cycle rate at that setting
  on both sides, step G17), `never-further`
  (ADR 0005: the compiled run's output against the stock run's, line by
  line; CTest runs the two it compares first), and for the console-recorded
  VU programs `ps2n-static-microvu` (compiled EE code with PCSX2's VU and
  IOP recompilers under it, as before G11: tells a fault of the VU provider
  or of the IOP provider from one of the EE's) and `ps2n-static-vu-thread`
  (PS2Native's providers with VU1 carried out on PCSX2's own VU thread, ADR
  0051: held to the recordings as `ps2n-static` is; the picture a test
  program's VU1 draws has it too, `frames/software/ps2n-static-vu-thread/vu`,
  beside `stock-jit-vu-thread`, PCSX2's VU recompiler on that thread). Every
  other configuration has VU1 on the CPU thread: a run with the thread on is
  not compared with another sync point by sync point. Also
  `selftest/tables-and-semantics`: our instruction semantics
  against PCSX2's interpreter and recompiler, and PS2Native's VU provider
  against PCSX2's VU recompiler on random micro programs
  (`host/selftest_vu.cpp`: 2,000 per VU in each of five settings, the fifth
  with encodings PCSX2's VU recompiler knows nothing of, and for VU1 a sixth,
  as on PCSX2's VU thread, where what a program's end tells the main thread
  is compared with the rest; a program that
  differs is made as small as it still differs in and printed with the
  registers it uses); and `selftest/vu-compiled`, the first 150 of those
  programs per VU and setting with every block compiled ahead of time
  (skipped without the self-test's capture in the build, failed when a
  block had to be interpreted). The same self-test holds PS2Native's IOP
  provider to PCSX2's IOP recompiler on 4,000 random IOP programs
  (`host/selftest_iop.cpp`: registers, memory and cycle counts after every
  stretch of time, and the addresses at which each holds a block; a
  program that differs is made as small as it still differs in and
  printed), and `selftest/iop-compiled` runs the first 1,500 of them with
  every block compiled ahead of time (skipped and failed likewise). The
  module tree has one test more, `selftest/module-stack-probe`: the stack
  probe a module defines for itself (`__chkstk`, `runtime/module_entry.cpp`,
  which no test program's code calls), on functions whose locals take up to
  fifty pages, on a thread whose stack Windows commits a page at a time.
- A failing test's files are in `build\windows-clang\ps2n\tests\<test name>`:
  `core.log` (PCSX2's log), `guest.txt`, `actual.txt`.
- A `lockstep` failure names the event test where the runs part, a
  `same-as-stock` failure the sync point. The execution model's "Comparing
  two runs" says how to go from there (`ps2n-host -sync`, `-sync-against`,
  `-sync-detail`; they work under stock PCSX2 too).
- Generated C++ is in `build\windows-clang\ps2n\generated`. To look at what
  the recompiler makes of one program without building:
  `build\tools-windows\bin\ps2n-recomp.exe info <elf>`. To read code at an
  address, of an executable or of a raw image such as a BIOS:
  `ps2n-recomp.exe disasm <file> --at <address> [--count n] [--base address]`.
- A test that differs from the console recording goes into
  `tests/ps2autotests-known-differences.cmake` only after its cause is known;
  one whose run differs from the stock run, into
  `tests/same-as-stock-known-differences.cmake`, likewise.
- After a run, `ps2n-host` reports what the interpreter did. For a game whose
  image is complete, "instructions inside the executable's code" must be
  zero; when it is not, the line "compiled code handed over to the
  interpreter" names the addresses. "The BIOS interpreted" says, piece by
  piece, what of the BIOS was; with the BIOS compiled, that is 48
  instructions at boot (blocks counted double) and one `di`/`eret` at
  `8000517C`. "VU0's blocks of micro code" and "VU1's" say how many
  different ones ran, compiled and interpreted; a line of its own says how
  many instructions of encodings PCSX2's VU recompiler knows nothing of
  the blocks held (carried out as it carries them out: not at all), and
  another names what the VU provider does not carry out, where the run
  stopped at it. "PS2Native's IOP provider" and "the IOP's blocks" say the
  same for the IOP: its stretches of time, the blocks begun, formed and
  thrown away, how many different blocks ran compiled and interpreted, how
  many blocks PCSX2's IOP recompiler holds (none, when it carried out
  nothing), and what the provider stopped at, if anything.
- Linux (step P4.7): `scripts/linux/build.sh` (its options are `build.ps1`'s)
  and `test.sh` (`PS2N_BIOS=<BIOS image>`, `--build-dir` as `test.ps1
  -BuildDir`, half the processors unless `--jobs`) are run in WSL 2 with Ubuntu 22.04 on this computer (ADR
  0038), in a clone of the repository inside it, never on `/mnt/c` (Git for
  Windows gives the working tree CRLF line endings, which PCSX2's shell
  scripts do not run with). It needs CMake 3.25 or later (Qt 6.12), LLVM's
  22.1.3 in `deps/llvm-22.1.3-linux`, and builds PCSX2's dependencies with
  PCSX2's own script the first time. `scripts/linux/kit.sh` puts the Linux
  folder together in `build/kit-linux` from the module tree built with
  `--module on --wizard on` (`--archive`: a `.tar.xz` of it as well, which
  keeps who may run a program): `kit.ps1`'s counterpart, with the libraries
  a desktop Linux may not have taken from Ubuntu 22.04 into it (all but
  those AppImages leave to the system, as PCSX2's own Linux release), each
  finding the others beside it (`$ORIGIN`), and their licences
  (`licences/Ubuntu`); `patchelf` does the run paths. Linux is reported as working only after
  the maintainer's run on their own machine (ADR 0004); their run inside
  this computer's WSL counts for P4.7, Linux then described as tested under
  WSL 2 (ADR 0041). Inside WSL, Vulkan is the CPU's (llvmpipe) and OpenGL
  the RTX through Mesa's D3D12 driver: a game played there takes OpenGL. A game the
  maintainer may be playing on this computer keeps its pace: Linux builds and
  tests on half the processors, Windows ones at below-normal priority.
- Never run stock `pcsx2-gsrunner` here: it writes into the maintainer's own
  `Documents\PCSX2`.
- The website: `scripts\windows\site.ps1 -Open` serves `docs` on this
  machine as GitHub Pages serves it. It has no build step. The page repeats
  numbers and states from `docs/game-log.md`, `docs/poc-log.md` and the
  status above: when one of those changes, change `docs/index.html` and the
  date at its foot too (`docs/site/README.md` lists what comes from where).
  It goes live when `main` is pushed. The repository's `README.md` repeats
  the fewest of them (the version to download and its two files, the games
  tried, where each system stands, the number of tests): a release, or a
  change of one of those, changes it with the page.

## Standing rules

- Phases are gated: stop at the end of each and ask for approval.
- Never substitute emulation, interpretation or a run-time JIT for static
  recompilation without a documented, approved fallback.
- Investigate root causes before changing code. After repeated failed
  attempts, stop and reassess instead of trying variations.
- Never invent test or benchmark results, and never skip a failing test
  silently. A performance or compatibility claim needs a reproducible
  measurement. A speed claim against stock PCSX2 says which stock PCSX2:
  with VU1 on the main processor thread, as every comparison of two runs has
  it, or with its VU thread on, PCSX2's default where there are three cores
  or more, which is much faster on some games (ADR 0051).
- Do not commit unless asked. Do not push to GitHub or delete repositories
  without explicit authorization. Ask before installing software.
- Commit messages never reference Claude: leave out the `Co-Authored-By`
  and `Claude-Session` lines a session adds by default
  ([ADR 0020](docs/adr/0020-no-claude-references-in-commit-messages.md)).
- No BIOS images, game data, or code generated from a game or from a BIOS in
  the repository.
- PCSX2 (GPL-3.0+) may be reused with attribution. AnyPS5 is GPL-2.0-only:
  read it for ideas, never copy its code.
- PCSX2's `AGENTS.md` forbids AI-generated contributions and any automated
  posting to its GitHub. Never open issues or pull requests there, never
  comment there, and never write what is posted there (code, commit
  messages, descriptions, issue texts, replies). The maintainer may
  contribute a fix themselves; a session may explain and point, no more
  ([ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).
  A commit message here never names one of PCSX2's issues or pull requests
  by its address or as `PCSX2/pcsx2#` and a number: GitHub would post a
  reference there.

## Development machine

Windows 11 x64 (i5-14600KF, 64 GB). Visual Studio Build Tools 2026 provides
clang-cl, CMake and Ninja, but only inside a developer shell; the command that
enters one is in `../ps2-uncompiler/CLAUDE.md` under "Build and test". The
Vulkan SDK is in `C:\VulkanSDK`. Stock PCSX2 is installed in
`C:\Program Files\PCSX2`, with its data (including the maintainer's BIOS
dumps) in `Documents\PCSX2`. Game workspaces from the old project are under
`C:\ps2\`. Use the BIOS dumps only after the maintainer says which.

## References examined in Phase 1

| Project | Revision |
|---|---|
| PCSX2 | tag `v2.9.170`, commit `a886f8ef` |
| AnyPS5 | `3c9276b7` |
| PS2Recomp | `e5636981` |
| PS2 Web Recomp | `76554ba4` |
| ps2autotests | `97469ffb` |

They were cloned into a temporary directory for that session and are not part
of this repository.
