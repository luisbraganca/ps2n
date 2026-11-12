# ADR 0020: No references to Claude in commit messages

| | |
|---|---|
| Status | Accepted; done (2026-11-12) |
| Date | 2026-11-12 |
| Decided by | Maintainer |

## Context

Every commit on `main` ended with two lines the sessions added by default:
`Co-Authored-By: Claude …` and `Claude-Session: https://claude.ai/code/…`.
On 2026-11-12 there were twelve commits, all with both lines; three of
their texts also named the file `CLAUDE.md`.

The maintainer asked: "Is it possible to remove every single Claude
references from past git commit messages?" The session answered that it
is, by rewriting the messages of all twelve commits and force-pushing
`main`, and said what that costs: every commit's hash changes, so the
other session's branch and any other clone have to move onto the new
history, one record citing a hash has to be corrected, and GitHub may
serve the old commits by their hashes for a while. It put three
questions: rewrite and force-push now; keep the mentions of the file
`CLAUDE.md` (it recommended keeping them: they name a file in the
repository, which Claude Code reads by that name); and leave the lines out
of future commits.

## Decision

The maintainer's words: "1. Yes 2. Keep them. 3. Don't reference Claude in
future commits."

How they were read:

- **The history was rewritten and force-pushed** on 2026-11-12: of each
  commit's message, the `Co-Authored-By: Claude …` and `Claude-Session: …`
  lines were removed, and nothing else. Each new commit has the old one's
  files, author, committer and dates. `main` went from `9646899` to
  `db8496e`; G7, which ADR 0017 cites, from `8fb7797` to `53e68ae`.
- **The three mentions of the file `CLAUDE.md` stay**: they name a file,
  not an author.
- **No commit message references Claude from now on**: no `Co-Authored-By`
  line naming Claude, no `Claude-Session` line, and no mention of Claude as
  the one who did the work. This takes precedence over a session's default
  of adding those lines. The file `CLAUDE.md` may be named.
