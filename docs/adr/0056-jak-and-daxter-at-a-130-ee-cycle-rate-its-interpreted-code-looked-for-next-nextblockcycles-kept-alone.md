# ADR 0056: Jak and Daxter at a 130% EE cycle rate, its interpreted code looked for next, and nextBlockCycles kept alone

- Date: 2026-11-23
- Status: accepted (the maintainer's answers, below).

## Context

[ADR 0055](0055-a-session-writes-down-the-pace-it-kept-and-the-notice-shows-for-any-new-code.md)'s
work, done the same day ([docs/app-log.md](../app-log.md), "The pace a session
kept, and the notice for any new code", and "The 64-bit read of
nextBlockCycles"), put five questions to the maintainer, each with a
recommendation:

1. **Jak and Daxter's boss fight.** In the maintainer's session, from minute 39½
   on, the game itself drew about 30 pictures a second. Its "NTSC Mode" patch
   shows it at 60 frames a second.
   - Recommended: PCSX2's EE cycle rate at 130% in their Jak and Daxter folder,
     with the folder brought up to date so that its next session writes its
     pace down, and the boss fight played again.
   - Otherwise: 30 there as now, or the game at 50 without "NTSC Mode".
2. **The notice in Jak and Daxter.** Part of the code the game loads from its
   data stays interpreted however often its folder compiles. So the notice for
   any new code would show about once a minute there, saying that closing the
   game compiles it.
   - Recommended: find out why, next.
   - Otherwise: show the notice only for code the folder has not compiled from,
     or leave it.
3. **PCSX2 v2.9.189's 64-bit read at the 32-bit `nextBlockCycles`** (under
   VUSyncHack or FullVU0SyncHack).
   - Recommended: PS2Native keeps the field alone, which is what PCSX2 computed
     until v2.9.174.
   - Otherwise: follow the read, as
     [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)
     followed the count register's carry.
   - Telling PCSX2 is the maintainer's
     ([ADR 0010](0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).
4. **Commit and push.**
5. **The maintainer's other earlier game folders.**
   - Recommended: left with 0.3's programs until 2 is settled.
   - Otherwise: brought up to date now.

## Decision

The maintainer's answers: "1. Ok 2. Yes 3. Agree 4. Commit and push."

1. **Jak and Daxter at a 130% EE cycle rate.** The maintainer's Jak and Daxter
   folder on Windows is brought up to date with these programs. Its game's
   settings file sets PCSX2's EE cycle rate to 130% (`EmuCore/Speedhacks`,
   `EECycleRate = 1`), a setting PS2Native does not fix: time is counted as
   PCSX2's recompiler counts it at every rate (step G17). The maintainer plays
   the boss fight again, and its `pace.txt` says whether the game holds 60
   there.
2. **Why Jak and Daxter's loaded code stays interpreted is the next step.**
   "Yes" is read as the recommendation: find out why part of the code the game
   loads from its data stays interpreted after its folder compiles it. The
   notice for any new code (ADR 0055) stays as decided meanwhile.
3. **PS2Native keeps `nextBlockCycles` alone** ("Agree").
   - `Cop2Prologue` subtracts the field, zero-extended. PCSX2 v2.9.189's
     recompiler instead reads 64 bits there, taking in the low half of
     `VU0.Mem`.
   - So for the games PCSX2's database gives VUSyncHack or FullVU0SyncHack (28
     entries, none of the maintainer's), a run may differ from stock PCSX2
     v2.9.189 before a COP2 instruction. Stock PCSX2's own runs differ there,
     depending on where the system put its memory.
   - The difference is listed with its cause
     ([ADR 0005](0005-never-further-from-the-console-than-pcsx2.md); the
     execution model's "What is not reproduced").
   - Telling PCSX2 is the maintainer's to do or not; the session writes
     nothing for it.
4. **Committed and pushed.**
5. **Not answered.** The maintainer's other earlier game folders keep their
   programs, as recommended, until 2 is settled.

## Consequences

- **Jak and Daxter in the maintainer's folder** plays with 30% more EE cycles a
  frame than the console has, as PCSX2 counts them. The computer does that much
  more of the EE's work; the session run again kept at least 2.7 times the
  console's pace. A session played so writes its rate down, and runs again with
  it.
- **The notice in that folder.** Until 2 is settled, a Jak and Daxter session
  there shows the notice about once a minute.
- **VUSyncHack and FullVU0SyncHack.** A same-as-stock comparison of a game with
  either fix may differ from stock PCSX2 v2.9.189 at its COP2 instructions. No
  test or session has such a game.
