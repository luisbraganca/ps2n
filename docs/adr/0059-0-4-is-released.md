# ADR 0059: 0.4 is released

- Date: 2026-11-25
- Status: accepted (the maintainer's words, below).

## Context

ADR 0058 had 0.4 prepared once the maintainer had played a session or two
with their game folders brought up to date. Those folders hold `2cc8125`'s
programs. Since 0.3, PS2Native has:

- **Moved to PCSX2 v2.9.189**
  ([ADR 0054](0054-ps2native-moves-to-pcsx2-2-9-189-and-the-maintainers-three-new-games-are-made-ready.md)).
- **Written down the pace a session keeps,** and shown its notice for any new
  code
  ([ADR 0055](0055-a-session-writes-down-the-pace-it-kept-and-the-notice-shows-for-any-new-code.md)).
- **Compiled every version** of the code a game runs outside its executable,
  the runtime picking the one memory holds
  ([ADR 0057](0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md)).
  The module version is now 6.

## Decision

The maintainer's words: "I did some play tests, everything looks smooth.
Let's release 0.4."

0.4 is made as
[ADR 0042](0042-releases-on-github-beginning-with-0-1.md),
[ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md)
and
[ADR 0052](0052-vu1-on-its-own-thread-committed-the-maintainers-folders-get-it-and-0-3-is-prepared-with-it.md)
have a release made:

- **The version** is 0.4 (`version.txt`), committed and pushed.
- **The runtime's profile of play** is made again on each system from the
  tree released. The folders it is trained on are brought up to date with
  that tree first, since the module version changed.
- **Both downloads** are made from the committed and pushed tree, its runtime
  built with link-time optimization and that profile, every test passing in
  the trees they are made from. Each is unpacked and tried:
  - its wizard makes a game, and a session is run again from that folder;
  - a folder made by 0.3's own download is brought up to date and compiles
    again.
- **The release's runtime is timed again** before the notes quote a number,
  each speed claim saying which stock PCSX2 it compares with.
- **A draft pre-release on GitHub,** which the maintainer publishes.

The session's choices, for the maintainer to decide otherwise:

- **0.4 is a pre-release,** as 0.1 to 0.3 are.
- **Their game folders** are not given the release's own programs without
  their word. They hold `2cc8125`'s, which the release differs from in its
  runtime's profile of play.
- **The website's downloads and the README** move to 0.4 only after it is
  published.

## Done the same day

- **The commit:** `a243df4`, committed and pushed at 11:18 UTC.
- **The training folders** were brought up to date with that tree's programs and
  compiled, seven on each system.
- **The runtime's profile of play** was made again from them on Windows (925 s)
  and on Linux (959 s). Every training run of a session was the session at every
  fingerprint compared.
- **Stock PCSX2's trees built again.** Windows' counting tree and stock tree had
  last been built before PCSX2's dependency package changed (ADR 0054). Both were
  moved aside and built from nothing. Stock PCSX2's trees on both systems are now
  v2.9.189, built as its releases are.
- **Every test passes in the trees the downloads are made from**, none skipped:
  - Windows: 554;
  - Linux: 553, and 556 in its development tree;
  - the three comparisons of `dma/vif/stcycl`, disabled with this runtime, did
    not run.
- **The downloads:**
  - `PS2Native-0.4-windows-x64.zip`, 115,300,159 bytes;
  - `PS2Native-0.4-linux-x64.tar.xz`, 144,266,640 bytes;
  - their licences name 0.4, the commit, and PCSX2 v2.9.189 with its one change.
- **Each download, unpacked and tried:**
  - its runtime read a `.chd`;
  - its wizard made Jak and Daxter from its disc image, and then found that folder
    up to date;
  - a session of the maintainer's, run again whole from that folder, was the
    session at every one of its 167 fingerprints, the same machine on both systems
    (41,554,654,904 EE cycles);
  - on Linux, that run wrote down its pace (`-pace`): the first Linux run of
    ADR 0055's record.
- **The update path:** a folder made by 0.3's own download was brought up to date
  by 0.4's wizard and compiled again. On both systems the start of a session run
  again from it was the session at every fingerprint compared, and the wizard,
  asked again, found it up to date.
- **Their PS2Native folders,** on Windows and inside WSL, were made again from the
  release's tree, their game folders unchanged.
- **The release's runtime, timed** on the maintainer's sessions against stock
  PCSX2 v2.9.189 with its VU thread on, built as its releases are. Two rounds in
  turns, the compiled game with VU1 on PCSX2's VU thread:

  | Game | Stock PCSX2 | 0.4 | Ratio | 0.3's, against v2.9.170 |
  |---|---:|---:|---:|---:|
  | Jak and Daxter | 34.04 s | 31.77 s | 0.93 | 0.97 |
  | Gran Turismo 3 | 18.45 s | 18.47 s | 1.00 | 1.00 |
  | Agent Under Fire | 26.42 s | 25.69 s | 0.97 | 0.95 |
  | Killzone | 33.41 s | 31.25 s | 0.94 | 0.94 |
  | Jak and Daxter, under WSL 2 | 37.48 s | 36.13 s | 0.96 | 0.99 |

  Agent Under Fire's runs, stock and compiled, part from the session at frame
  1,900, as that game's runs do with the thread on. Every other run was the
  session at every fingerprint.
- **The draft:** a pre-release on GitHub, for the maintainer to read and publish.

## The maintainer's answers to the draft

The session put three things to them, each with its recommendation:

1. **Publish 0.4.** Recommended: yes, once they had read the draft.
2. **The website's downloads and the README moved to 0.4,** committed and
   pushed with the record of the release. Recommended: yes.
3. **Their game folders given 0.4's own programs.** Recommended: yes, so that what
   they play is what is released and timed.

Their answers: "1. Yes, publish yourself", "2. Yes do that", "3. Yes please.
When you say my game folders you mean build/kit/games?"

- **Published** by the session at 12:42 UTC (`gh release edit v0.4
  --draft=false`), a pre-release, the tag `v0.4` made on `a243df4`.
- **The website and the README** say 0.4: its two files, the eight games tried,
  and its figures. The website's charts are dated measurements and stay as they
  were.
- **Their game folders,** asked about: on Windows, the nine of `build\kit\games`,
  `SLES_505.39` aside, which a session made for P4.4's check and no round has
  updated; inside WSL, the Jak and Daxter of `build/PS2Native/games`. Each is
  given 0.4's programs the wizard's own way and compiles its game again
  (`build\measure\folders-59.ps1`; the scratchpad's `wsl\p59-linux.sh`).
  Done the same day, 12:43 to 12:58 UTC:
  - each of the ten compiled its game again, in 23 to 317 s;
  - where a folder has a session played with VU1 on the CPU thread (six on
    Windows, the one inside WSL), its start run again with the new programs
    was the session at every fingerprint compared;
  - asked again, the wizard found each up to date and changed nothing;
  - memory cards and sessions as they were;
  - Jak and Daxter's folder on Windows and SSX Tricky's, which had counted
    their profiles, count once more after a session of 3 minutes (ADR 0049).

## Consequences

- **Bringing a folder of 0.3 up to date.** A folder made by 0.3 compiles its
  game again when the wizard brings it up to date (the module version is 6).
  A Jak and Daxter folder records, in its next session, the versions of code
  its earlier captures could not keep.
