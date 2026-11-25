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

## Consequences

- **Bringing a folder of 0.3 up to date.** A folder made by 0.3 compiles its
  game again when the wizard brings it up to date (the module version is 6).
  A Jak and Daxter folder records, in its next session, the versions of code
  its earlier captures could not keep.
