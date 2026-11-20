# ADR 0043: A speed step: compiled code against PCSX2's recompilers, and faster interpreters

- Date: 2026-11-20
- Status: accepted (the maintainer's words and choices, below).

## Context

Phase 4's steps were all done (ADR 0041), and 0.1 was released (ADR 0042),
when the maintainer asked:

1. "How come our compiled games are running slower than PCSX2's interpreted
   (or block compiled) games??? Compiled should always be faster. We need to
   check why and fix it."
2. "How come when our games are running in interpreted mode (that is, ps2n
   found new code that wasn't compiled yet mid-gameplay) they're far slower
   than when running in PCSX2? Shouldn't both interpreted modes be
   similar???"

What the session laid out before anything was changed:

- **PCSX2 does not interpret with its default settings.** Its EE, IOP and VU
  recompilers compile each block of code the first time it runs, at run time
  (a JIT), so a game on PCSX2 never runs interpreted, new code included. Its
  interpreters (the configuration `stock-interp`) took 8 to 23 times as long
  as compiled code on the six loops of `bench.s` (proof-of-concept log).
  PS2Native interprets code no compile has seen until the next compile,
  because no compiler of PS2 code ships in a game (ADR 0003): question 2
  compares PS2Native's interpreter with PCSX2's compiler. PS2Native's EE
  interpreter was 1.4 to 1.7 times slower than PCSX2's interpreter on
  2026-11-10 and had not been measured since.
- **Ahead-of-time code is not faster by itself** (the brief, section 9). It
  has to count time exactly as PCSX2's recompiler does (ADR 0011), to find
  out at run time whether memory still holds the code it was compiled from,
  and to reach memory through the core's address table; PCSX2's recompiler
  learns all of that when it compiles, and builds it into its code. The
  numbers on the site and in the logs came from the development build with
  profile-guided optimization (Killzone's play at 0.95 times stock PCSX2's
  time); the code a game's folder compiles has no profile (Agent Under Fire
  1.07 to 1.19), and three of the five games had never been timed against
  stock PCSX2.

Asked what showed compiled games to be slower, the maintainer answered
"Numbers in logs/site". Asked how first sessions should get faster, between
faster interpreters within today's rules and also compiling what a session
meets while it plays and loading it into the running game (which would undo
ADR 0037 and ADR 0040), the maintainer chose "Faster interpreters
(Recommended)".

## Decision

1. A speed step of its own (the step ADR 0028 left for later): first measure,
   in the configuration users get (a game's folder, its modules compiled
   without a profile), each of the five games against stock PCSX2 on a
   session of the maintainer's run again, the site's two charts, and the
   first sessions of Gran Turismo 3 and Jak and Daxter against stock PCSX2's
   recompilers and its interpreters; then make the costs the profiles show
   smaller, one change at a time, each measured and kept only if it gains.
2. Every change keeps the run the stock run at every sync point and the
   interpreted run at every event test, and every recorded session the
   session at every fingerprint: time is counted as before (ADR 0011).
3. First sessions get faster within today's rules: PS2Native's EE, IOP and VU
   interpreters made at least as fast as PCSX2's interpreters, and the BIOS's
   kernel compiled for a new game's first session. Code a session meets is
   still compiled after the session or before the next start (ADR 0040),
   never while the game plays.
4. A profile of the user's own play given to the compiler, should a gap
   remain, is put to the maintainer first, with what it costs.

The measurements and the changes are in [docs/app-log.md](../app-log.md),
"The speed step".
