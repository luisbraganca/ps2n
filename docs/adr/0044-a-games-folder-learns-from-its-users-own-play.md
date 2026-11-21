# ADR 0044: A game's folder learns from its user's own play: the order of its functions first, then a sampled profile

- Date: 2026-11-20
- Status: accepted (the maintainer's words, below).

## Context

The speed step of [ADR 0043](0043-a-speed-step-compiled-code-against-pcsx2-and-faster-interpreters.md)
left compiled games at 1.06 to 1.19 times stock PCSX2's time in the
configuration users get, a game's folder, whose modules are compiled
without a profile. Its decision 4 put a profile of the user's own play to
the maintainer first, with what it costs. Measured on Jak and Daxter, the
profile taken from one of the maintainer's sessions and the time from
another ([docs/app-log.md](../app-log.md), "The speed step"):

- **The module's functions placed in the order the other session ran
  them** (`lld-link /ORDER`, from the list `ps2n-host -functions-ran`
  writes): 2.0% less time. What it costs: a list written when a session
  ends, and the game's module linked again, in seconds.
- **Profile-guided optimization from what the other session ran** (clang's
  `-fprofile-instr-generate`, then `-fprofile-instr-use`): 7.5% less time,
  Jak and Daxter at 1.11 times stock PCSX2's time. The counting made that
  session take 2.7 times as long, which no one could play: a folder would
  need a profile sampled while the game plays as it always does, which is
  not built yet and whose gain is not measured; and the window a folder
  compiles in after a session (ADR 0040) would compile the game's modules
  again with it, about a minute for Jak and Daxter on the development
  computer, longer on a laptop.

The session put it to the maintainer as: "Should a game's folder learn from
your own play to compile its code better? ... I recommend yes, ordering
first, then building and measuring the sampled profile before it ships."

## Decision

The maintainer's answer, on 2026-11-20: "1. Yes." Read as the
recommendation accepted, in its order:

1. First the order of a game's functions: when a session ends, the runtime
   writes which of the game's compiled functions ran, and the folder's
   compile after the session (`ps2n-remake`, ADR 0040) links the game's
   module with its functions in the order its sessions ran them.
2. Then a profile sampled while the game plays, given to the compiler when
   the folder compiles the game's modules: built, and measured, before it
   goes into a game's folder, with how much of the 7.5% it keeps and what it
   adds to the compile after a session.

The session's choices, for the maintainer to decide otherwise: each is
measured on the maintainer's sessions in the configuration users get
before it goes into a game's folder; neither changes how time is counted
(ADR 0011) or what a run does, every session run again still the session at
every fingerprint; and what a folder learns from its user's play stays in
that folder.

## Measured since

2026-11-21: the order, built into a game's folder, measured again with
four rounds each on the same objects, gives Jak and Daxter about 0.4%, not
the 2.0% above, whose two rounds without the order had run slow
([docs/app-log.md](../app-log.md), "The order of a folder's functions,
built (2026-11-21)"). It costs a relink after a session that ran new
functions, and stays. The profile's 7.5% is the larger part of what a
folder can learn.

2026-11-21: the profile sampled while the game plays, built as a
measurement (`ps2n-host -sample-profile`) and tried on Jak and Daxter, gives
0.2%, within the noise: clang could place about a quarter of its records,
and Windows' debugging format has no discriminators to place the rest by
(docs/app-log.md, "A profile sampled while the game plays, tried
(2026-11-21)"). It does not go into a folder. Counting after the session,
on a run again of it with modules compiled to count, which gave the 7.5%,
was put to the maintainer as a decision of its own, and takes this part's
place ([ADR 0045](0045-a-games-folder-counts-its-profile-on-a-run-again-of-a-session-after-play.md)).
