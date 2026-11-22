# ADR 0052: VU1 on a thread of its own is committed, the maintainer's folders get it, and 0.3 is prepared with it

- Date: 2026-11-22
- Status: accepted (the maintainer's answers, below).

## Context

[ADR 0051](0051-the-comparison-with-stock-pcsx2-said-as-it-is-vu1-on-a-thread-of-its-own-next.md)'s
step was built and measured the same day ([docs/app-log.md](../app-log.md),
"VU1 on a thread of its own"): PS2Native's VU provider carries VU1 out on
PCSX2's own VU thread where that thread is on, and a compiled game then takes
0.95 to 1.01 times the time of stock PCSX2 with its VU thread on, every test
passing on both systems. The session put three decisions to the maintainer,
each with its recommendation, and said what it had not done: a game in a
window on Linux with the thread (its window would show on the maintainer's
screen), the Linux runtime's profile of play made again (which a release
does), and its last edits carried into the Linux tree with a second look at
the website.

## Decision

The maintainer's answers: "1. Yes", "2. Yes", "3. Yes", and "Do whatever
needs doing from your side (from your "Not done" category)."

1. **The step is committed and pushed,** with this record.
2. **The maintainer's seven game folders get these programs,** the six on
   Windows and the one inside WSL, the wizard's own way: each is given the
   new runtime, launcher and toolchain and compiles its game again before it
   next starts. As with 0.2, they get the release's own programs, made from
   0.3's tree.
3. **0.3 is prepared with it,** as [ADR 0042](0042-releases-on-github-beginning-with-0-1.md)
   and [ADR 0049](0049-0-2-is-released-a-folder-brought-up-to-date-counts-once-more-linuxs-speed-after-it.md)
   have a release made: both downloads from the committed and pushed tree,
   the runtime built with link-time optimization and a profile of play made
   again on each system from that tree, each download unpacked and tried,
   and the release a draft on GitHub with its notes, which the maintainer
   reads and publishes. The version is 0.3 from this commit.
   How the answer was read: the session had recommended preparing it after
   the maintainer had played with the step. Their "Yes", with "Do whatever
   needs doing from your side", is read as: prepare it now. A draft is
   published only at their word, so they can still play with their folders
   first.
4. **What was not done is done:** the session's last edits carried into the
   Linux tree; the website looked at again; the Linux runtime's profile of
   play made again with the thread; and a game in a window on Linux with the
   thread, inside WSL on the development computer, muted and for as long as
   the check needs, its window on the maintainer's screen meanwhile. The
   maintainer said afterwards which list they meant: "Fyi when I said "Not
   done" I'm referring to your last message": the "Not done" list of the
   session's report, these three things and nothing more.

The session's choices, for the maintainer to decide otherwise:

- **The Windows runtime's profile of play is made again too,** from the tree
  released: the runtime's source changed after the profile measured with
  (the thread's setting decided at a run's start, a count of VU1's runs on
  the thread).
- **The release's runtime is timed again** on the maintainer's sessions,
  against stock PCSX2 with its VU thread on, before the notes quote a number.
- **0.3 is a pre-release,** as 0.1 and 0.2 are. Its notes say which stock
  PCSX2 every speed claim is against, that Agent Under Fire's run with the
  thread on does not repeat, and that a game's folder made with 0.2 compiles
  its game again when brought up to date.

## Done the same day

[docs/app-log.md](../app-log.md), "0.3 (ADR 0052)", has every number.

- The step committed and pushed, `d10604d`.
- The runtime's profile of play made again on both systems from that
  commit; every test passing in the trees the downloads are made from, none
  skipped; both downloads made, each unpacked and tried, and a game's folder
  made by 0.2's own download brought up to date by 0.3's wizard and compiled
  again.
- The release's runtime timed again on the maintainer's sessions against
  stock PCSX2 with its VU thread on: Jak and Daxter 0.97, Gran Turismo 3
  1.00, Agent Under Fire 0.95, Killzone 0.94 times its time; under WSL 2,
  Jak and Daxter 0.99.
- Jak and Daxter played in a window inside WSL with the thread (its window on
  the maintainer's screen for 28 seconds, muted), its session run again the
  session at every fingerprint with the thread and without.
- The maintainer's seven game folders given 0.3's programs, each compiled
  again, the start of one of its sessions run again the session at every
  fingerprint, their memory cards the same bytes.
- 0.3 a draft pre-release on GitHub, with both downloads and its notes, for
  the maintainer to read and publish. Asked then whether to publish it, and
  whether the website and the README then move to it with this record
  committed and pushed, the maintainer answered "1. Publish" and "2. Yes":
  0.3 was published the same day (the tag `v0.3` on `d10604d`), and the
  website's downloads and the README moved to it.

## Consequences

- From this commit the runtime and its modules share version 4 of what a
  module and the host hold in common: a game's folder made with 0.2 is
  brought up to date by the wizard before it plays again
  ([ADR 0048](0048-a-games-folder-of-an-earlier-version-is-brought-up-to-date-by-the-wizard.md)).
- The website's downloads and the README stay 0.2's until the maintainer has
  published 0.3.
