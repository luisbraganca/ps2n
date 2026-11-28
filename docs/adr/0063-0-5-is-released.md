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

## Done the same night

docs/app-log.md, "0.5 (ADR 0063)", has the detail.

- **The commit:** `bbe4e56`, committed and pushed at 23:26 UTC. Its records of
  the maintainer's folder update (ADR 0062) went in with it.
- **The training folders** were brought up to date with that tree's programs and
  compiled, seven on each system.
- **The runtime's profile of play** was made again from them on Windows (746 s)
  and on Linux (905 s), the counting trees built at v2.9.198 within those. Every
  training run of a session was the session at every fingerprint compared.
- **Every test passes in the trees the downloads are made from,** none skipped:
  - Windows: 554;
  - Linux: 553, and 556 in its development tree;
  - the three comparisons of `dma/vif/stcycl`, disabled with this runtime, did
    not run.
- **The downloads:**
  - `PS2Native-0.5-windows-x64.zip`, 115,355,565 bytes;
  - `PS2Native-0.5-linux-x64.tar.xz`, 144,399,968 bytes;
  - their licences name 0.5, the commit, and PCSX2 v2.9.198 with PS2Native's
    two changes.
- **Each download, unpacked and tried:**
  - its runtime read a `.chd`;
  - its wizard made Jak and Daxter from its disc image, and then found that
    folder up to date;
  - a session of the maintainer's, run again whole from that folder, was the
    session at every one of its 167 fingerprints, the same machine on both
    systems (41,554,654,904 EE cycles, as with 0.4's downloads).
- **The update path:** a folder made by 0.4's own download was brought up to date
  by 0.5's wizard and compiled again. On both systems the start of a session run
  again from it was the session at every fingerprint compared, and the wizard,
  asked again, found it up to date.
- **The release's runtime, timed** on the maintainer's sessions against stock
  PCSX2 v2.9.198 with its VU thread on, built as its releases are; the compiled
  game with VU1 on PCSX2's VU thread. Two timings of two rounds in turns, the
  ratios of their means:

  | Game | Stock PCSX2 | 0.5 | Ratio | 0.4's, against v2.9.189 |
  |---|---:|---:|---:|---:|
  | Jak and Daxter | 33.39 s | 32.85 s | 0.98 | 0.93 |
  | Gran Turismo 3 | 18.28 s | 18.53 s | 1.01 | 1.00 |
  | Agent Under Fire | 26.13 s | 25.52 s | 0.98 | 0.97 |
  | Killzone | 32.92 s | 31.13 s | 0.95 | 0.94 |
  | Jak and Daxter, under WSL 2 | 36.55 s | 35.80 s | 0.98 | 0.96 |

  - **A second timing,** because the release's runtime, timed again on Jak and
    Daxter 20 minutes after the first, took 2% less: a timing differs from the
    next by about that much. That the ratios are the means of both was decided
    before the second was run.
  - **Jak and Daxter on Windows:** its compiled run took about 3% more than
    0.4's, and stock PCSX2 about 2% less than on 0.4's day. The runtime's profile
    of play is not the cause: built with 0.4's, the same code took the same time.
    ADR 0061's rehearsal had measured the compiled run so already, before the
    move to v2.9.198. Whether 0.5 lost something or 0.4's timing fell well is not
    known.
  - **Agent Under Fire's runs,** stock and compiled, part from the session at
    frame 1,900, as that game's runs do with the thread on. Every other run was
    the session at every fingerprint.
- **The draft:** a pre-release on GitHub on `bbe4e56`, both files and the notes,
  for the maintainer to read and publish. Its files' SHA-256, as GitHub gives
  them, are the downloads'.

## The maintainer's answers

Put to them with the draft: publish 0.5, by them or by the session; once it is
published, move the website and the README to it, committed and pushed with
the release's records; give their game folders 0.5's programs; look further
into Jak and Daxter's 3% on Windows, rebuilding 0.4 to time both on matching
code. Their words, on 2026-11-28: "1. Yes", "2. Yes", "3. Yes", "4. No".

How they were read:

- **"1. Yes":** the session publishes it, as it had offered to.
- **"2. Yes":** the website and the README name 0.5, its files and its figures,
  in a commit with the records of the release, as 0.4's did.
- **"3. Yes":** their PS2Native folders are made again from `bbe4e56`, and their
  game folders brought up to date by that folder's wizard: `build\kit\games` on
  Windows (`SLES_505.39` aside, as before), `build/PS2Native/games` inside WSL.
- **"4. No":** Jak and Daxter's 3% is not looked into further. What is known of
  it stays as above.

Done the same morning, docs/app-log.md, "Published, and the maintainer's
folders given its programs (2026-11-28)", has the detail:

- **Published** at 08:23 UTC, a pre-release, the tag `v0.5` on `bbe4e56`. Both
  files download for someone not signed in, at their sizes.
- **Their PS2Native folders,** `build\kit` and inside WSL `build/PS2Native`, are
  the downloads' folders file for file (490 and 595 files), their games and
  PCSX2's patches aside. The licences in each name 0.5 and `bbe4e56`.
- **Their game folders,** the nine on Windows and the one inside WSL, were
  brought up to date by those folders' wizards, each compiling its game again.
  Where a session could be run again, its start was the session at every
  fingerprint compared; asked again, the wizard changed nothing; their memory
  cards are unchanged.
- **The website and the README** moved to 0.5.
