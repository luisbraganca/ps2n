# ADR 0053: PS2Native tried on PCSX2's newest version, 2.9.189

- Date: 2026-11-22
- Status: accepted (the maintainer's answer, below). Whether the submodule
  moves was the maintainer's decision once the results were in: it moved
  ([ADR 0054](0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md)).

## Context

0.3 was published the same day
([ADR 0052](0052-vu1-on-its-own-thread-committed-the-maintainers-folders-get-it-and-0-3-is-prepared-with-it.md)).
Asked "What should we work on next?", the session gave four options and
recommended the first (the maintainer's three games PS2Native has never run).
It said that options 1 to 3 belong to the brief's Phase 5, Maintenance, which
starts only at the maintainer's word. The second option, as it was put:

> **Test PS2Native against the newest PCSX2.** PS2Native is pinned to PCSX2
> 2.9.170, and PCSX2 put out 2.9.189 yesterday.
> - **Why:** PCSX2 fixed the count-register bug you reported in 2.9.173.
>   PS2Native copies that bug until it moves to a newer PCSX2.
> - **What I'd do:** build against 2.9.189 in a separate build folder, run
>   every test, and play your recorded sessions back. You'd then decide
>   whether to move.
> - **Risk:** if PCSX2 changed how it counts time, your recorded sessions may
>   no longer play back exactly as they were played.

## Decision

The maintainer's answer: "Do 2".

How it was read: Phase 5 begins, with this check as its first work (the
brief's "Automate upstream synchronization checks", done once by hand first);
the other two options are not taken up for now. PS2Native is built and tested
on PCSX2 v2.9.189 (`5977c525`, 2026-11-21), the newest when the maintainer was
asked, and the maintainer's sessions are played back on it. The submodule
moves only at the maintainer's word, after the results.

The session's choices, for the maintainer to decide otherwise:

- **A worktree and a branch of their own.** The work is in a git worktree
  beside the repository (`..\ps2n-pcsx2-189`), on a branch of its own
  (`pcsx2-2.9.189`), where the submodule is moved to v2.9.189. The main tree,
  0.3's, and its build trees stay as they are, to compare with. Nothing of the
  branch is committed or pushed until the maintainer decides.
- **PCSX2's newer dependency package, in that worktree only.** PCSX2 2.9.189
  needs DirectX-Headers 1.620.1 or later on Windows; the package in `deps` has
  1.619.5. The worktree has PCSX2's rolling package of 2026-11-21 (165.7 MB,
  SHA-256 `c74f3608e7aba8f10aea7b71e829667de2d282ab1d98ca4e8ba04749697a4da6`,
  the digest GitHub gives for it). Its Qt is the same 6.12.0. PCSX2's Linux
  dependencies did not change between the two versions.
- **What PCSX2 changed of what PS2Native follows, followed in the branch.**
  Of PCSX2's 39 commits between the two versions, most are its renderers',
  its interface's and its game database's. Four reach what PS2Native follows:
  - The count register is brought up to date by a 32-bit addition
    (v2.9.173; PCSX2's correction of the maintainer's issue 15186). PS2Native
    no longer reproduces the carry into EntryHi, as
    [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)
    foresaw. `tests/programs/countcarry.s` prints 0 under every configuration
    now, and its known difference under PCSX2's interpreter is gone.
  - `VURegs::nextBlockCycles` is 32 bits again (v2.9.174). PS2Native's
    description of PCSX2's structures follows it (`ps2n/pcsx2_layout.h`).
    Where microVU's macro mode still reads 64 bits there (the field and the
    padding after it, which nothing writes), PS2Native reads the field
    zero-extended, which is what that read gives.
    *Corrected on 2026-11-23:* there is no padding after the field. In
    v2.9.170 it was 64 bits, after four bytes of padding; at 32 bits it
    follows `clipflag` directly, and the four bytes after it are the low half
    of `Mem`, the address of VU0's memory. That read gives the field and that
    half, so where PCSX2's VUSyncHack or FullVU0SyncHack game fix is on,
    PS2Native's zero-extended field is not what it gives. And what follows
    the field is eight bytes nearer, which the module version should have
    said ([ADR 0054](0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md);
    docs/app-log.md, "The 64-bit read of nextBlockCycles").
  - The IOP recompiler keeps a JR's or JALR's target across a delay slot that
    gives up every host register (v2.9.183; PCSX2's pull request 15198, which
    closed the maintainer's issue 15192 of
    [ADR 0023](0023-the-iops-code-compiled-as-step-g12.md)). PS2Native carries
    such a pair out where it refused it, and the IOP self-test's random
    programs include such pairs now. Its capture is made again for them.
  - microVU's XGKICK under PCSX2's "XGKick Sync" game fix keeps 64 bits of a
    cycle count (v2.9.174). PS2Native's VU provider refuses that game fix, so
    nothing follows from it.
  - The rest reaches nothing PS2Native follows: PCSX2's VU1 interpreter (PS2Native
    follows microVU), the renderers, the game database (none of the five games'
    entries changed), and a new graphics setting on PCSX2's own settings page,
    which the wizard shows as that page has it.
- **No timing.** It was not asked for; it is offered with the results.

## Results

Done the same night ([docs/app-log.md](../app-log.md), "PCSX2 2.9.189
tried", has every number):

- **Every test passes on v2.9.189 as on v2.9.170, on both systems,** none
  skipped: 543 of 543 in the development trees, 541 of 544 on Windows and 540
  of 543 on Linux in the trees a release is made from (the three comparisons
  of `dma/vif/stcycl` disabled there, as in 0.3's: PCSX2's issue 15223 is not
  corrected in v2.9.189). PS2Native's own change to PCSX2 applies as it is.
- **A game's folder is made and brought up to date as before:** the wizard
  of a folder made from the branch's tree made Jak and Daxter from its disc
  image, and copies of 0.3's five measurement folders were brought up to date
  by it and compiled again before they played.
- **The maintainer's sessions run again on v2.9.189 are the sessions at every
  fingerprint,** stock PCSX2's runs and the compiled games', with VU1 on the
  CPU thread and on PCSX2's VU thread (Agent Under Fire's without it), the
  compiled games' EE cycles the same as on v2.9.170 to the cycle: Jak and
  Daxter, Gran Turismo 3, Agent Under Fire, Killzone and Silent Hill 3 on
  Windows, Jak and Daxter on Linux.
- **Nothing was timed.**

Put to the maintainer on 2026-11-23: whether PS2Native moves to v2.9.189.
