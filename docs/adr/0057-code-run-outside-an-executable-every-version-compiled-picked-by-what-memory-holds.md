# ADR 0057: Code run outside an executable: what ran captured, every version compiled, picked by what memory holds

- Date: 2026-11-23
- Status: accepted (the maintainer's answer, below); built and measured the
  same day.

## Context

At the maintainer's word ([ADR 0056](0056-jak-and-daxter-at-a-130-ee-cycle-rate-its-interpreted-code-looked-for-next-nextblockcycles-kept-alone.md),
"2. Yes"), the session looked for why part of the code Jak and Daxter loads from
its data stays interpreted after its folder compiles it
([docs/app-log.md](../app-log.md), "Why Jak and Daxter's loaded code stays
interpreted").

**The cause.** A folder's compile puts all the code its game ran outside its
executables into one image, holding one version of code at each address.
Where captured versions disagree, the first is kept.

**Jak and Daxter.** The game loads different code into the same memory as it
goes. In the maintainer's folder, 83,085 addresses had captured versions that
disagreed. Every version but the first:

- runs interpreted;
- is captured again at the end of each session;
- makes the folder compile after nearly every session, for nothing.

It is at most 0.2% of the EE's work. Under ADR 0055 it is also a notice about
once a minute that says the code will be compiled, which it never is.

**The other games.** None of the maintainer's other games has such conflicts.
Gran Turismo 3 runs 158,869 instructions of its code outside its executable,
loaded once.

**Why not compile every captured version.** A version captured where a
compiled function's memory held other words covers that function's whole
range, most of it memory around the code that ran, which changes from one
capture to the next. Compiling every captured version would never settle.

The session recommended:

- **Capture only the code that ran.**
- **Compile each different version of it.**
- **Have the runtime pick the one memory holds,** as it does for the IOP's
  code and the vector units' micro code
  ([ADR 0023](0023-the-iops-code-compiled-as-step-g12.md),
  [ADR 0022](0022-vu-micro-programs-compiled-as-step-g11.md)).

So that what a session met runs compiled the next time, the captures stop
growing, and the notice and the compile after a session come only for code
that is new. A larger change: the recompiler, the runtime, a new module
version, and every game's folder compiling once more. To be built and
measured on Jak and Daxter first, with Gran Turismo 3 checked to come out
unchanged.

## Decision

The maintainer's answer: "Yes implement the recommendation".

The code a game runs outside its executables is captured as the code that ran,
every different version of it at an address is compiled, and the runtime runs
the version memory holds. It is measured on Jak and Daxter first, and Gran
Turismo 3 is checked to come out unchanged, before it goes into a folder of the
maintainer's or a release. How it is built, and the session's choices within
it, are added here as they are made, for the maintainer to decide otherwise.

## How it is built (2026-11-23)

[docs/app-log.md](../app-log.md), "Code run outside an executable, every
version compiled", has every detail and number. The session's choices, for
the maintainer to decide otherwise:

- **Captured as blocks.** A block run outside the executable, the BIOS's
  files and the executables a program loads itself, where no compiled code
  ran it, is captured as a `[[block]]`: every version of it, over the words
  the runtime compares before compiled code runs it.
  - Earlier captures' `[[code]]` pieces outside the executable go into the
    first image as before, neither trimmed nor sorted into further images:
    where one disagrees with another, what runs there is captured as blocks
    from now on.
- **Further images by first fit.** The first image is what it was. A block
  that disagrees with it goes into the first further image it agrees with,
  its end a boundary there as well as its start. The first image lists them
  (`ImageInfo::layers`, module version 6); in a folder's compile, each is a
  file of its own.
- **The runtime picks.** It puts all of them in use, identifies a block
  against the others where the asking image's code is not memory's, keeps
  which one holds it, and passes the block on to it. The same is done where a
  BIOS image's code is not memory's, for the program's own version of code
  it patched into a place of the BIOS's. That was found with Gran Turismo 3,
  whose patched program loader had been interpreted and captured in every
  session.
- **The notice counts what is new.** The notice over the picture (ADR 0055)
  counts what a run records for the next compile, not every instruction
  interpreted. Known code that compiled code cannot carry out exactly as
  PCSX2's would is interpreted every time without being new, and no compile
  takes it in. Counted as instructions it would have shown the notice:
  - in Jak and Daxter, in 435 of a 44-minute session's half seconds;
  - in Gran Turismo 3, through its program loader;
  - in one of the maintainer's Agent Under Fire folders, through the BIOS's
    kernel, which that folder never captured.
- **Tests.** `tests/programs/overlay.s` runs two routines in turns at one
  address outside its executable, with its capture; every rebuilt test
  program must interpret nothing outside its executable.

## Measured (2026-11-23)

- **Jak and Daxter.** The maintainer's 44-minute session was run again from a
  copy of their folder brought up to date, as played.
  - With the blocks its first run recorded compiled, the run recorded nothing
    new and interpreted 48,071 EE cycles, against 580,691,693. No five-second
    stretch had EE code interpreted, where 427 of 534 had before.
  - The other versions came to 205 further images and 17 MB of C++. The
    game's module grew from 36.5 to 44.0 MB.
- **Gran Turismo 3.** Its code compiled outside the executable is the same,
  its C++ differing only by the empty table of further images. Its program
  loader's patched code runs compiled now.
- **Killzone and Agent Under Fire.** Run again with the new programs, each
  recorded nothing.
- **Every run** was its session at every fingerprint.
- **Tests.** Every test passes, on Windows and on Linux.

## Consequences

- **Module version.** The module version changes. Every game's folder
  compiles its game once more when it is brought up to date.
- **Jak and Daxter.** In a folder with these programs, the first session
  records the versions it meets as blocks. After the compile that follows, a
  session that goes where earlier ones went runs compiled there. Its notice,
  and its compile after a session, come only for code met for the first time.
- **Status.** The maintainer's answers to what was measured are
  [ADR 0058](0058-every-version-compiled-committed-the-maintainers-folders-brought-up-to-date-0-4-after-their-play.md):
  committed, their folders brought up to date, 0.4 after their play.
