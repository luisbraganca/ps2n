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
(with their reason, below), "2. Yes, write an issue as a guide I'll write it
myself.", "3. Yes", "Stop."

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

It is put to the maintainer, with a measurement to come: what the comparison
with stock PCSX2 is to be, and what the website says of it.

## The session's choices, for the maintainer to decide otherwise

- **How the patch is carried** (the folder it is in, how the copy is made
  and kept, which builds are without it) is the session's to build, and is
  listed here once it is.
