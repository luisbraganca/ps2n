# ADR 0024: Further into the game before Phase 3 ends, as step G13

| | |
|---|---|
| Status | Accepted; in progress |
| Date | 2026-11-14 |
| Decided by | Maintainer, on the session's recommendation |

## Context

On 2026-11-14 the IOP's code was compiled ahead of time (step G12,
[ADR 0023](0023-the-iops-code-compiled-as-step-g12.md)). With it, nothing
that Killzone runs as far as the scripts reach is carried out by one of
PCSX2's recompilers for PS2 code: the end state of
[ADR 0003](0003-no-jit-in-generated-games.md), for the first 17,500
frames. Those are the start, the menus and three and a half minutes of the
first level.

The session's report on G12 put the decision about what comes next. As it
was put:

"**What comes next?** I recommend going further into the game before
closing Phase 3. Everything so far rests on five minutes of one level, and
the rest will bring code no build has seen on all four processors. How
that goes is what the Phase 4 application has to be designed around. You
would play on in the window, each session writes down what it interpreted,
and I rebuild and compare. The alternatives are closing Phase 3 now and
deciding on Phase 4, or taking the speed leads first (the vector units'
program search at about 5 s of play, the IOP's blocks at 4.6 s)."

## Decision

The maintainer's word: "Ok"

How it was read:

- **G13 is a step of Phase 3: further into the game.** The maintainer
  plays on in the window, beyond what the scripts reach. Each session
  writes down what it had to interpret: of the game's EE code, of the
  micro code, of the IOP's code. A session of the project puts that into
  the captures, makes the build again, checks it again and reports.
- **Phase 3 does not end yet.** Phase 4 still needs its own approval.
- **The speed leads wait.** They stay in the game log's "Open".

## What the session proposes within that

The maintainer's word names the direction, not the step's criterion. What
follows is the session's proposal, and the maintainer may decide any of it
otherwise.

- **What is reported for each part of the game played:** how much code no
  build had seen, on which processor (the EE, the vector units, the IOP),
  and how often it ran interpreted.
- **After each rebuild** every test passes, and the scripted runs are
  still the stock run and the interpreted run, as after every step so far.
- **What goes wrong in play is listed with its cause:** a stop at
  something a provider does not carry out, a picture or a sound that is
  wrong, a slowdown. It is then put right by a change that is not for this
  game alone, or left with its reason (the brief: reusable solutions, no
  game-specific hardcoding unless unavoidable).
- **How far is far enough is the maintainer's to say.** The step has no
  end of its own.
- **What cannot be compared.** A session in the window cannot be held to
  the stock run sync point by sync point: its input and its time are live.
  The scripted runs stay the comparison. Whether the scripts are taken
  further as well, or a session's input is recorded so that it can be run
  again, is a question for when the first sessions are in.

## Consequences

- The game log has the step in its two tables, the command for a session
  that keeps what it had to interpret ("How to run it"), and what each
  session brought ("G13").
- The step's first move is the maintainer's: a session in the window.
  The first was on 2026-11-14, started for them at their word ("Can you
  run it for me please?"); the game log has what it brought ("G13").
- The window check of G12 showed what to expect of the IOP: a session
  there has a memory card, which the scripted runs do not, and it
  interpreted 349 blocks, of the memory card's modules above all, that no
  script reaches.

## Sessions recorded, to be run again (2026-11-14)

After the first session, the session's report put the question this record
had left for then. As it was put:

"**Record sessions so they can be replayed?** I recommend yes. The window
would save your controller input frame by frame, plus copies of the memory
card and the clock at the start. I could then replay the session without a
window and compare it with stock PCSX2 at every sync point, as I do for the
scripted runs. A replay can drift from what you saw on screen, but both
sides of the comparison get exactly the same input. It means a change to
the host. Without it, the code you reach is compiled but never compared."

The maintainer's word: "1. Yes"

How it was read:

- **A session in the window writes down what it takes to run it again**:
  the controller's input frame by frame, in the form of the scripts the
  scripted runs take (`-pad`); the memory card as it was when the session
  began; the time the console's clock started at.
- **A session written down is run again without a window**, as a scripted
  run is, and held to the stock run sync point by sync point and to the
  interpreted run, after what it brought has been compiled. That becomes
  part of G13's check after each session.
- **A replay is the session as the machine took it in at frame
  boundaries.** Where it parts from what the maintainer saw (the hardware
  renderer's picture against the software renderer's, input that came
  between two frames), it is still one run, which both sides of a
  comparison get alike.

Built on 2026-11-14 (game log, "G13", "Sessions written down, and run
again"). To be written down, the console's clock in a window is now set by
the host, to this computer's time of day, where it had been left to the
core, which takes the time of day itself.

## The patches a session was played with (2026-11-15)

With G17 done (ADR 0025), the session's report put this as it was put:
"A recorded session doesn't note which patches it was played with, so
replaying it needs -Patches widescreen,fps added by hand (forgetting it
gives a replay that isn't the session). Should sessions note their
patches, so a replay picks them up by itself?", with "Yes, as a small next
step" recommended ("The session writes down its patches and overclock, and
a replay applies them unless told otherwise. Checked with a replay as
today.") against "Not now". The maintainer's answer: "Yes, as a small next
step".

How it was read:

- **A session writes down the patches it was played with** (`patches
  widescreen,fps`, or `none`) and, where it was given one, PCSX2's EE cycle
  rate (`ee-cycle-rate 0`), beside its clock.
- **A run of it again applies them**, unless it is given its own
  (`-patches`, `-ee-cycle-rate`; `game.ps1 -Patches`, `-EeCycleRate`).
- **The overclock a patch's note asks for is not written down as a rate.**
  In the session the note set it when the game's executable started; a rate
  given to a run is set from the machine's start, the BIOS's code included,
  so written down as one it would make another run. The patches written
  down ask for it again, at the same moment.
- **A session written down before** has no patches line, and is run again
  with none unless it is given them.
