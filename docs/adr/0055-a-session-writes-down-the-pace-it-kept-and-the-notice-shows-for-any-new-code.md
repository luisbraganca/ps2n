# ADR 0055: A session writes down the pace it kept, and the notice shows for any new code

- Date: 2026-11-23
- Status: accepted (the maintainer's answers, below).

## Context

After playing Jak and Daxter from their folder (0.3's programs) for 44
minutes, the maintainer wrote: "Again, it seems on Jak and Daxter I reached
uncompiled code (it was interpreting) and the banner never showed up. Maybe
because I was on fullscreen?"

The session's own report (`last-run.log` in the folder) said the notice over
the picture ([ADR 0035](0035-a-game-says-when-it-runs-new-code-and-when-it-compiles-it.md))
had not been shown because its line had never been crossed: code met for the
first time came to at most 0.37% of the EE's cycles in any half second, where
the notice asks for 1% of a processor's work (the session's choice in ADR
0035), and the window showed 59.90 frames a second where the console shows
59.93. Asked whether the notice should show for any new code, the maintainer
answered: "It sure got close, the game was laggy as hell, specially during
boss fight."

The session ran that session again whole, from a copy of the folder, as it was
played (VU1 on PCSX2's VU thread, Vulkan at three times the console's
resolution), with the time noted every 1,800 frames: it was the session at
every one of its 3,205 fingerprints, and the slowest 30 seconds of it took 8.7
seconds, 0.29 of the console's time. So the emulation never fell behind, and
the lag was something a run again does not show: a short hitch, a fraction of
a second (PCSX2's renderer compiling what a new effect needs, the first time
it is seen), or the frames' pacing on the screen. Put to the maintainer: write
down the frame rate and the longest frame every few seconds of each session,
to find the next laggy moment and its cause (recommended), and the notice for
any new code (recommended).

## Decision

The maintainer's answers: "Yes record. Yes the notice should show for any new
code." Then, while it was being built: "Just an fyi, the boss fight was laggy
even when playing complied. So maybe that was the reason". How it was read:
the boss fight lagged on compiled code too, so code met for the first time
was not what made it lag; the game itself may draw fewer pictures there, which
the frames a second PCSX2 shows do not tell apart. So the record says what the
game drew too, and a session run again can say it for a session played before
the record existed.

1. **A session in the window writes down the pace it kept,** in `pace.txt`
   beside its `input.txt`: every 300 frames (5 seconds at 60 frames a second),
   the frame, the seconds since the first, the frames a second over the
   stretch, its longest frame on the CPU thread and the longest frame PCSX2
   showed (its GS thread's, as PCSX2's own performance figures measure them
   each half second), in milliseconds, what the game itself drew a second at
   the console's pace (new pictures, as PCSX2 counts them), and the part of
   the EE's cycles, of the IOP's blocks and of the vector units' blocks an
   interpreter carried out in it. The run's report says the worst of it: how
   many stretches fell under 99% of the console's frames a second, the
   slowest, the longest frames and the fewest pictures the game drew, with
   where they are. Any run of `ps2n-host` writes the same where `-pace <file>`
   says (`game.ps1 -Pace`); without a window a run goes as fast as the
   computer goes, so there only what the game drew and the interpreted parts
   mean what they say.
2. **The notice shows for any code met for the first time,** whenever in the
   last half second the EE, the IOP or a vector unit ran some, however little;
   the BIOS's instructions at boot aside, which are interpreted on purpose and
   no compile takes in. As before, for five seconds, at most once a minute, the
   same words. It replaces ADR 0035's 1%.

The session's choices, for the maintainer to decide otherwise: the stretch of
300 frames; the longest frame shown taken from PCSX2's performance figures
(their maximum over each half second, read every 30 frames); what the game
drew taken from the same figures (its new pictures to each frame shown, over
each half second, averaged over the stretch and put at the console's frames a
second, so that a run without a window says it too); the file in the
session's folder, so that it goes with the session it describes; the report's
line for "behind" at 99%; `-pace` for any run.

## Done the same day

[docs/app-log.md](../app-log.md), "The pace a session kept, and the notice
for any new code", has every number.

- **Built as decided.** The column of what the game drew is included, `-pace`
  works for any run, and others can read `pace.txt` while the session writes
  it.
- **Tests.** Every test passes: 543, and 541 with the same three disabled, on
  Windows; 543 and 540 on Linux; none skipped.
- **The maintainer's session, run again with `-pace`.** It ran whole, from a
  copy of their folder brought up to date, and was the session at every one of
  its 3,205 fingerprints.
  - The game itself drew 60 pictures a second through its 38th minute.
  - From minute 39½ it drew about 30, in bursts of 15 to 30 seconds. The
    emulation was never behind, and nothing interpreted weighed.
  - The game's settings enable PCSX2's "NTSC Mode" patch, which shows this
    European game at 60 frames a second. A picture that takes longer than a
    sixtieth of a second of the console's time is shown for two frames.
  - That is the lag: the game's own, in the console's time as PCSX2 counts it.
- **In a window** (copies, out of sight, muted): the pace was written down
  beside each session, and the notice showed in a new game's first session
  and in Jak and Daxter's.
- **Found with it, and put to the maintainer:**
  - The move to v2.9.189 should have changed the module version
    ([ADR 0054](0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md)).
    It is 5 since.
  - PCSX2 reads 64 bits at `nextBlockCycles` under two game fixes, which none
    of the maintainer's games has. PS2Native keeps the field alone.
  - Part of the code Jak and Daxter loads from its data stays interpreted
    however often its folder compiles: in 80% of the session's stretches run
    again. So the notice for any new code would show about once a minute
    through most of a session of it.
- **The three games the maintainer has not played yet** were brought up to
  date with these programs, each compiling its game again.
- **The maintainer's answers** to what was put to them are
  [ADR 0056](0056-jak-and-daxter-at-a-130-ee-cycle-rate-its-interpreted-code-looked-for-next-nextblockcycles-kept-alone.md).
- **What the notice counts of the EE**, since
  [ADR 0057](0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md)
  the same evening, is the code a run records for the next compile, not every
  instruction interpreted. Known code that compiled code cannot carry out
  exactly is interpreted every time without being new; counted, it would have
  shown the notice in games that meet nothing new.

## Consequences

- The next session that lags can be located to within a few seconds, and
  told apart: the emulation falling behind (fewer frames a second), a hitch
  of the CPU thread, a hitch only in what PCSX2 shows, or code met for the
  first time.
- In a long first session the notice comes back about once a minute as long
  as the game keeps meeting new code, even where that costs nothing that
  shows.
- A folder gets both with the programs of the next release, or when it is
  brought up to date.
- A session played before has no `pace.txt`. Run again with `-pace`, it still
  says what the game drew, as the boss fight's session did.
