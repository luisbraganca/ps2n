# ADR 0028: Phase 4's design: a shipped compiler, and a game's code in a module

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-16 |
| Decided by | Maintainer, on the session's recommendations |

## Context

Phase 4 begins with a design proposal that the maintainer decides on before
any code is written ([ADR 0027](0027-phase-3-ends-phase-4-begins-with-a-design-proposal.md)).
The session wrote it, [phase4-proposal.md](../phase4-proposal.md), from what
was measured on Agent Under Fire's build of 2026-11-16. What weighs most in
it: a game's compiled code is a translation of the game's code and of the
BIOS's, so no one may distribute it, and it has to be compiled on the user's
own computer. Today that takes Visual Studio's build tools, which PS2Native
may not ship, because generated code includes PCSX2's headers and
Microsoft's standard library. Compiling Agent Under Fire's code took 1,950 s
of compile time, just over 3 minutes on this computer's 20 threads.

The session put the proposal's eight decisions to the maintainer, each with
one recommendation:

1. "How a user's computer compiles a game. Recommended: ship LLVM's own
   compiler and linker (clang and lld, about 200 MB, freely
   redistributable). We'd make the generated code depend on nothing but our
   own headers and compile each game into a module that a prebuilt runtime
   loads. The user installs nothing. The other way is to have the user
   install a compiler."
2. "What a packaged game is. Recommended: a folder holding a launcher named
   after the game, its compiled modules and its fixed settings. The disc
   image and the BIOS are read where they are. The other way is a single
   executable."
3. "New code a player meets. Recommended: the game recompiles itself in the
   background and uses the new code from its next start. The first step
   toward that is the wizard recompiling when asked."
4. "The wizard's interface. Recommended: Qt 6, PCSX2's own toolkit, already
   in its dependency package. The other way is a command line first."
5. "Patches. Recommended: use an installed PCSX2's patch archive, or fetch
   PCSX2's release of it when the user says so. The archive's repository
   declares no licence, so we shouldn't ship it ourselves."
6. "Speed. Recommended: accept 1.1 to 1.2 times stock PCSX2's time for the
   first version, since both games keep the console's pace, and make the
   generated code faster later. The other way is training a profile in the
   wizard, at twice the compile time."
7. "Platforms. Recommended: Windows first, with the design kept portable,
   and Linux when you choose to run it."
8. "Order of work. Recommended: as in the proposal's table, starting with
   P4.1, a prototype that measures what loading game code as a separate
   module costs in speed, before anything else depends on it."

The maintainer: "1. Sure 2. Yes 3. Ok 4. Yep perfect 5. Agree 6. Yes, we'll
try to optimize in the future. 7. Yes 8. Sure".

## Decision

Every recommendation is accepted. As it was read:

1. **A game is compiled on the user's own computer, by a `clang` and an
   `lld` that the wizard brings** (LLVM's, under Apache-2.0 with the LLVM
   exception). Generated code is made freestanding: the contract it
   compiles against includes neither PCSX2's headers nor a standard
   library, and describes the machine state it touches in structures of
   PS2Native's own, which the runtime checks against PCSX2's where the
   runtime is built. A game's code is compiled into a module, a DLL on
   Windows and a shared object on Linux, which a prebuilt runtime loads:
   PS2Native's runtime and PCSX2's core, built once by the project and
   brought with the wizard. The BIOS's code is compiled from the user's own
   dump into a module of its own, beside the game's
   ([ADR 0007](0007-bios-code-translated-ahead-of-time.md)). The user
   installs nothing.
2. **A packaged game is a folder**: the runtime as a launcher named after
   the game, with its icon; the game's module and the BIOS's; the settings,
   fixed when it was built; what the runtime needs of PCSX2's resources;
   and where the disc image and the BIOS image are, both read in place.
3. **Code a player meets for the first time is compiled by the game
   itself**: what a session had to interpret is written down, as today; the
   game has its module compiled again in the background, with the toolchain
   the wizard brought, uses it from its next start, and says so. The wizard
   compiling a game again when the user asks comes first. Read with
   [ADR 0003](0003-no-jit-in-generated-games.md): the toolchain runs as a
   process of its own, outside the game, and nothing it compiles runs before
   the game's next start, so no code is translated while the game runs.
   What ADR 0003 says of code not seen at build time holds (interpreted the
   first time, reported, captured, included by the next build), the next
   build being made on the user's computer.
4. **The wizard is a Qt 6 application**, as PCSX2's own interface is, with
   Qt from PCSX2's dependency package. It recognizes a game by PCSX2's own
   code and databases (its game list, `GameIndex.yaml`,
   `RedumpDatabase.yaml`): title, serial, region, CRC and PCSX2's
   compatibility rating. Its configuration starts from the window's
   defaults (G14 to G17, [ADR 0025](0025-how-a-game-is-shown-and-its-patches.md)),
   every one acceptable as it is. Like the window, it carries PS2Native's
   branding and not PCSX2's ([ADR 0012](0012-ps2native-branding-not-pcsx2s.md)).
5. **A game's patches come from an installed PCSX2's archive**, read where
   it is, or, where there is none, from PCSX2's own release of it, which the
   wizard fetches only when the user says so. PS2Native does not ship the
   archive. A packaged game keeps the one patch file it uses.
6. **The first version trains no profile.** Without profile-guided
   optimization a game takes 1.1 to 1.2 times stock PCSX2's time, as Agent
   Under Fire does (1.12 to 1.19), and both games keep the console's pace in
   the window. "We'll try to optimize in the future": making the generated
   code faster is a step of its own, after these, when the maintainer
   chooses.
7. **Windows first, the design kept portable**: nothing tied to one
   system's loader, and on Linux the modules are shared objects. Linux when
   the maintainer chooses to run it ([ADR 0008](0008-linux-run-deferred.md));
   macOS stays optional ([ADR 0001](0001-platform-priorities.md)).
8. **The order of work is the proposal's**, each step with its pass
   criterion there: P4.1 the module's cost, P4.2 freestanding generated
   code, P4.3 the shipped toolchain, P4.4 the packaged game, P4.5 the
   wizard, P4.6 new code in play, P4.7 Linux. P4.1 comes first: a
   prototype in which the test programs and Agent Under Fire are built as a
   module the runtime loads, every test passing and the game's time
   measured against today's build. What the module's boundary costs is put
   to the maintainer before P4.2 is built on it.

## What does not change

- How time is counted ([ADR 0011](0011-time-is-counted-as-pcsx2s-recompiler-counts-it.md))
  and what is compared with stock PCSX2
  ([ADR 0005](0005-never-further-from-the-console-than-pcsx2.md)).
  Development builds keep every comparison working throughout the phase:
  the test suite, `same-as-stock`, `lockstep`.
- What may be distributed: the wizard, the prebuilt runtime, `clang` and
  `lld`, and Qt's libraries, each under its licence. What may not, and is
  made only on the user's computer from their own copies: a game's compiled
  code and the BIOS's. No BIOS image, disc image or patch archive is ever
  shipped. The wizard says so when it packages a game (the assessment,
  section 11), and the licensing obligations of PS2Native and of a packaged
  game are written down with it (the brief, section 6).
