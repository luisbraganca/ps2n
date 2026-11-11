# ADR 0010: The PCSX2 status-flag fix goes upstream through the maintainer

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-11 |
| Decided by | Maintainer |

## Context

PCSX2 `v2.9.170` starts a VU0 micro program without the current status
flags: `vu0DenormalizeMicroStatus` in `pcsx2/VU0micro.cpp` shifts right
where the function it was taken from shifts left
([poc-log.md](../poc-log.md), "Increment 5"). PCSX2's `master` still had it
on 2026-11-11, and no pull request or issue there mentioned it. It costs
PS2Native one line of one console-recorded test (`vu/lower/integer`), as it
costs stock PCSX2.

The correction is two characters. The question was whether PS2Native should
carry it in its patch queue, which is empty; the recommendation was not to,
because it would be the first place where the machine is not stock PCSX2.

## Decision

The maintainer's words: "Help me to create a PR sending the fix to PCSX2, if
the PR becomes stalled for too much time then we'll just add the fix
locally."

- **The fix is offered to PCSX2 first, by the maintainer**, as a pull
  request of their own.
- **If that stalls for too long, PS2Native carries the fix itself**, as the
  first entry in its patch queue, until PCSX2 has it. How long is too long
  is the maintainer's to say; no time was named.

Added on 2026-11-11, when the session asked how long. The maintainer's
words: "Yea I can go with 30 days. Create the fix for both this fix and the
EntryHI, I'll then create the PR." How they were read:

- **Too long is 30 days** from the day the pull request is opened without a
  reply from one of PCSX2's maintainers.
- **One pull request carries two corrections:** this one, and the one for
  the count register's carry into `EntryHi`
  ([ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md)).
- "Create the fix": see below. The corrections are the maintainer's to
  write.

## What an AI session may and may not do for that pull request

PCSX2's `AGENTS.md` decides this, not PS2Native:

- A session does not post to PCSX2's GitHub in any form: no pull request, no
  issue, no comment.
- A session does not write what is posted there: not the change, not the
  commit message, not the pull request's description, not replies to
  reviewers. For a contributor who is new to PCSX2, nothing in the
  contribution may be AI-generated at all.
- The contributor has to say whether an AI tool was used. Here one was: an
  AI session found the bug. That has to be declared as it is.
- The contributor has to be able to explain the change without the tool.
- What a session may do: point to the code and to what it was copied from,
  explain what the two functions do, say how the bug can be seen and how a
  fix can be checked, and ask the questions that show whether the
  explanation has landed.

On 2026-11-11 the maintainer asked the session to create both corrections
on one branch for them to open the pull request from. The session did not
write them, for the reasons above, and said so. What it does instead: it
shows where each defect is and how it can be seen, checks the maintainer's
own change by building it and running the tests that show the defect, and
keeps a test program in this repository that shows the second one
(`tests/programs/countcarry.s`).

## Consequences

- `CLAUDE.md` said "never plan on upstreaming a patch". It now says what is
  above: a session never posts or writes for PCSX2's GitHub; the maintainer
  may contribute.
- Until PCSX2 has the fix, or the patch queue does, `vu/lower/integer` stays
  in the list of known differences with this cause.
- If the patch queue gets its first entry, that is a change to the
  architecture decision "the machine is unmodified PCSX2"
  ([ADR 0002](0002-static-cpus-on-pcsx2-core.md)) and is recorded when it
  happens, with how the patch is applied and how it is dropped again.
