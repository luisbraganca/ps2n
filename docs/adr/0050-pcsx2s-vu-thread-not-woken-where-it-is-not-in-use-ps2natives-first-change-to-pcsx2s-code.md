# ADR 0050: PCSX2's VU thread is not woken where it is not in use: PS2Native's first change to PCSX2's code

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below).

## Context

Looking for Linux's speed
([ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md),
point 5; [docs/app-log.md](../app-log.md), "Linux's speed") found a cost that
is PCSX2's own, in stock PCSX2 as in a compiled game:

- **PCSX2's core wakes its VU thread at every STROW and STCOL command of
  VIF1, whether that thread is in use or not.** PCSX2 can carry VU1 out on a
  thread of its own (its "MTVU" setting). PS2Native has that setting fixed
  off: VU1's micro code is compiled code's, on the CPU thread. The thread
  exists all the same, and each of those commands wakes it to copy 16 bytes.
- **What it costs,** on Jak and Daxter's session, which sends about 290 of
  those commands a frame: 28% of the run under WSL 2, where waking a thread
  costs six times what it costs on Windows, and 9% on Windows, for stock
  PCSX2 and for the compiled game alike. The other three games timed send
  few of them and do not change. Measured in a trial on a copy of PCSX2's
  source; every run stayed its session at every fingerprint.
- **Of the compiled game's own distance to stock PCSX2 on Linux,** 1.05 times
  its time as 0.2 has it, two parts were found and put right (1.03), and the
  rest, 3%, was not found.

A change to PCSX2's code is the maintainer's to order
([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)), so it
was put to them.

## Decision

The session put four decisions to the maintainer, with its recommendation for
each: whether PS2Native's builds leave those wakes out; whether PCSX2 is
told; whether the day's work is committed and pushed; and whether the last 3%
on Linux is looked for further or left. The maintainer's answers: "1. Yes"
(the answer went on to give their reason, which point 1 has in the session's
words), "2. Yes, write an issue as a guide I'll write it myself.", "3. Yes",
"Stop."

1. **PS2Native's builds leave those wakes out.** It is the first change
   PS2Native carries to PCSX2's code. The submodule stays as it is, never
   edited: the change is a patch in this repository, applied to a copy of
   PCSX2's source when the runtime is built. Stock PCSX2 is still timed as it
   is released, without the change. It is carried at once: it changes no
   result of a run, only what a run costs, and the maintainer chose not to
   wait for PCSX2's answer on it. (ADR 0010 has the correction of the VU0
   status flag carried only after 30 days without a reply. Whether the
   corrections of the other defects told to PCSX2 are carried as this one is
   was not asked, and is not decided.)
2. **The maintainer tells PCSX2 themselves,** in an issue they write. Asked
   for one written as a guide, the session wrote none, as ADR 0010 has it and
   as on 2026-11-14 and earlier this day: PCSX2's rules for AI agents forbid
   issue texts an AI generated, and its team asked for none. It gave what an
   issue needs instead (below).
3. **Committed and pushed.**
4. **The last 3% on Linux is left for now.** (This answer came without its
   number. The fourth question was whether to keep looking or to stop here.)

## What the maintainer was given for PCSX2

- **Where.** `pcsx2/Vif_Codes.cpp`, `vifCode_STCol` and `vifCode_STRow`
  (lines 646 and 667 of `v2.9.170`), and `pcsx2/Vif.cpp`, lines 375 to 406,
  the writes of the row and column registers. Ten calls of
  `VU_Thread::WriteCol` and `WriteRow` (`pcsx2/MTVU.cpp`), which put a
  command into the thread's queue and end in `KickStart()`. None of the ten
  is under `THREAD_VU1`, as every other use of the thread in those two files
  is.
- **How it is seen.** With the VU thread off, in a game that sends many of
  those commands (Jak and Daxter), a profile of the CPU thread has the
  system's function that wakes a thread (`ZwReleaseSemaphore` on Windows,
  `sem_post` on Linux) under `vifCode_STRow`.
- **What limits it.** It was measured on PS2Native's build of PCSX2's core,
  not on a release of PCSX2. PCSX2's `master`, read on 2026-11-22, has the
  same lines, and none of its issues or pull requests found mentions them.
  It may be deliberate: the thread's copy of the row and column kept current,
  so that the setting can be switched on while a game runs. It costs only
  where the thread is off, which is not PCSX2's default on most computers
  (next section). And an AI session found it, which PCSX2's rules ask a
  contributor to say.

## Found while this was written: what "stock PCSX2" has had switched off

- **PCSX2 switches its VU thread on by default** on a computer with three
  processor cores or more, on Windows and on Linux
  (`VMManager::SetHardwareDependentDefaultSettings`, `pcsx2/VMManager.cpp`).
  With it on, the thread is in use, and those commands are its work. So the
  cost above is met where the thread is off: in PS2Native, always; in stock
  PCSX2, on a computer of two cores or where its user switched it off. The
  session had not said so when it put the decisions. It changes nothing of
  the first, PS2Native not using the thread; it limits what the matter is
  worth to PCSX2.
- **Every run of `ps2n-host` has had that thread off, stock PCSX2's among
  them** (`host/main.cpp`: "VU1 stays on the CPU thread so that runs
  repeat"). So "stock PCSX2's time", wherever this project's logs, its
  website and its releases' notes compare with it, is the time of PCSX2's
  recompilers with VU1 carried out on the CPU thread, as PS2Native carries
  it out. How a compiled game stands to stock PCSX2 with its VU thread on,
  as most of its users have it, has not been measured.

It was measured the same day (below), and is put to the maintainer with the
numbers: what the comparison with stock PCSX2 is to be, and what the website
says of it.

## How the change is carried

Built the same day ([docs/app-log.md](../app-log.md), "PS2Native's first
change to PCSX2's code").
[ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md) had said
that a first patch would change the decision "the machine is unmodified
PCSX2" ([ADR 0002](0002-static-cpus-on-pcsx2-core.md)) and be recorded with
how it is applied and how it is dropped again. This is that record: the core
is PCSX2 as it was released, but for the changes of `patches/pcsx2`. The
session's choices, for the maintainer to decide otherwise:

- **Where a change is.** `patches/pcsx2`, one patch to a change, numbered:
  `0001-the-vu-thread-is-not-woken-where-it-is-not-in-use.patch`. A patch
  says at its top what it changes, why, and when it was made, and the
  folder's README lists them.
- **How it is applied.** When a build tree is configured
  (`cmake/ps2n-pcsx2-patches.cmake`): each file a patch changes is copied
  into the tree, the patches are applied to the copies by git, and PCSX2's
  own targets are given the copies to compile in the place of the files they
  came from. The submodule is not touched, and nothing else of PCSX2's build
  changes. A patch that does not apply, or that changes a file no target
  compiles, stops the configuration and says which. The assessment had it so
  ("a patch queue ... applied at configure time").
- **What it does not carry: a change to a header.** The files beside a header
  include it by name from their own folder, and would not see a copy. A patch
  that changes one stops the configuration; such a change would need PCSX2's
  source copied whole.
- **Which trees are without it.** One, the tree stock PCSX2 is timed on
  (`build.ps1 -Pcsx2Patches off`, `build.sh --pcsx2-patches off`; the tree
  remembers it). Every other tree has the changes, the trees the tests run in
  among them: there the tests' "stock" configurations run PCSX2 with them.
  This change alters no result of a run, and every test passes with it. A
  change that would alter one goes to the maintainer for that as well.
- **How a runtime says which it is.** The second line of `ps2n-host -help`
  names the changes PCSX2's core in it was built with, or says that it is as
  PCSX2 has it. A release's `licences` folder carries the patches
  (`PCSX2-changes`), and its `README.txt` says that PCSX2's core in it is
  changed, as the GPL asks of a changed version.
- **How it is dropped again.** Its file and its line in the README are
  deleted, when the PCSX2 pinned here no longer needs it or the maintainer
  says so; a tree then builds PCSX2 as it is. Should the pin move to a PCSX2
  that has changed those lines, the patch no longer applies, and the
  configuration stops there.
- **Stock PCSX2 can be timed with its VU thread on,** for the measurement
  owed above: `ps2n-host -pcsx2-vu-thread` (`game.ps1 -Config stock-jit
  -Pcsx2VuThread`). Every other run keeps VU1 on the CPU thread.

## Measured (2026-11-22)

Each game's timed session, three rounds in turns, the folders as 0.2 has
them; every test passes with the change on Windows (532 and 534) and on Linux
under WSL 2 (531 and 534). [docs/app-log.md](../app-log.md), "PS2Native's
first change to PCSX2's code", has every round.

- **What the change gives.** Jak and Daxter takes 9.2% less time on Windows
  (50.23 s where 55.31 s) and 27.5% less under WSL 2 (57.34 s where 79.13 s),
  each run still its session at every fingerprint. Gran Turismo 3, Agent
  Under Fire and Killzone do not change.
- **What stock PCSX2 takes with its VU thread on.** On Windows 40% less time
  on Jak and Daxter's session (33.49 s where 55.90 s), 24% less on
  Killzone's, 12% less on Gran Turismo 3's and 2% less on Agent Under Fire's;
  under WSL 2, 36.13 s where 76.01 s on Jak and Daxter's. Agent Under Fire's
  runs so left their session at frame 1,900, each another way: runs with the
  thread on do not all repeat, which is why every run here has VU1 on the CPU
  thread.
- **PS2Native's VU provider cannot simply be put on PCSX2's VU thread:** it
  follows PCSX2's VU recompiler as that behaves with the thread off.
  Carrying VU1 out on a thread of its own is a step of its own.

Where that leaves a compiled game, with the change, as a multiple of stock
PCSX2's time:

| | Stock PCSX2 with VU1 on the CPU thread | Stock PCSX2 with its VU thread on |
|---|---:|---:|
| Jak and Daxter, Windows | 0.90 | 1.50 |
| Gran Turismo 3, Windows | 0.99 | 1.13 |
| Agent Under Fire, Windows | 1.00 | 1.02 |
| Killzone, Windows | 0.93 | 1.22 |
| Jak and Daxter, Linux under WSL 2 | 0.75 | 1.59 |

The first column is the comparison every log, the website and 0.2's notes
have made: ADR 0046's goal was met in it. The second is stock PCSX2 as it is
by default where there are three cores or more. Both are without a frame
limiter: at the console's pace each game plays at full speed on this computer
either way.

## Put to the maintainer

With the numbers above: whether the change and what was built for it are
committed and pushed; whether the website, the README and 0.2's notes say
which stock PCSX2 they compare with, and what it takes with its VU thread on;
whether the next speed step is VU1 carried out on a thread of its own, held
to stock PCSX2 with its VU thread on; and whether the maintainer's own game
folders get the runtime with the change now, and a release is made with it
now or with that step. Answered the same day:
[ADR 0051](0051-the-comparison-with-stock-pcsx2-said-as-it-is-vu1-on-a-thread-of-its-own-next.md).
