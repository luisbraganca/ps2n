# ADR 0063: 0.5 is released

- Date: 2026-11-27
- Status: accepted (the maintainer's words, below).

## Context

0.4 was published on 2026-11-25 ([ADR 0059](0059-0-4-is-released.md)).
The maintainer's game folders hold `d50ef76`'s programs. Since 0.4,
PS2Native has:

- **A menu over the game,** opened with Esc: Resume, Controls, Graphics card,
  Reboot and Quit, the last two asking first, as the window's close button
  now does; with sounds of its own
  ([ADR 0060](0060-a-menu-over-the-game-opened-with-esc-quit-controls-and-the-graphics-card.md)).
- **Moved to PCSX2 v2.9.198,** with a second change of PS2Native's to PCSX2's
  code: a missing include, without which PCSX2's source does not compile on
  Linux here
  ([ADR 0062](0062-ps2native-moves-to-pcsx2-2-9-198-carrying-a-missing-include.md)).
  The module version is now 7.
- **Its release, the updates of the maintainer's folders and its timings as
  scripts in the repository**
  ([ADR 0061](0061-maintenance-tools-a-newer-pcsx2-tried-by-one-command-the-releases-scripts-in-the-repository-claude-md-cut.md)).
  They have been rehearsed, and make a release for the first time here.

## Decision

The maintainer's words, on 2026-11-27: "Handle release 0.5".

0.5 is made as 0.4 was, by the scripts of ADR 0061, in the order
docs/development.md gives ("A release"):

- **The version** is 0.5 (`version.txt`), committed and pushed.
- **The runtime's profile of play** is made again on each system from the
  tree released. The last one was made at v2.9.189, by ADR 0061's rehearsal,
  and PCSX2's code in the runtime has changed since. The folders it is trained
  on are brought up to date with that tree first, since the module version
  changed.
- **Both downloads** are made from the committed and pushed tree, its runtime
  built with link-time optimization and that profile, every test passing in
  the trees they are made from. Each is unpacked and tried:
  - its runtime reads a disc;
  - its wizard makes a game, and a session is run again from that folder;
  - a folder made by 0.4's own download is brought up to date and compiles
    again.
- **The release's runtime is timed** against stock PCSX2 v2.9.198 built as
  its releases are, before the notes quote a number. Each speed claim says
  which stock PCSX2 it compares with.
- **A draft pre-release on GitHub.**

The session's choices, for the maintainer to decide otherwise:

- **"Handle" is read as far as the draft,** as for 0.4. Publishing it, the
  website and their game folders are put to them then: each is outward-facing
  or theirs.
- **0.5 is a pre-release,** as 0.1 to 0.4 are.
- **Their game folders keep `d50ef76`'s programs** unless they say otherwise.
  The release's differ from those in the version they say and in the
  runtime's profile of play.
- **The website's downloads and the README** move to 0.5 only after it is
  published.
