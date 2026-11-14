# ADR 0024: Further into the game before Phase 3 ends, as step G13

| | |
|---|---|
| Status | Accepted; not begun |
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

- The game log has the step in its two tables, as not begun, and the
  command for a session that keeps what it had to interpret ("How to run
  it").
- The step's first move is the maintainer's: a session in the window.
- The window check of G12 showed what to expect of the IOP: a session
  there has a memory card, which the scripted runs do not, and it
  interpreted 349 blocks, of the memory card's modules above all, that no
  script reaches.
