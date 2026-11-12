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

## An issue each instead of a pull request (2026-11-11)

Later the same day the maintainer changed the way the two corrections reach
PCSX2. Their words: "I haven an idea, instead of creating a PR I'll just
fill an issue with a suggested fix. Help me with it, you can help me to
write an issue. I need, for each issue (it's 2):", followed by the fields of
PCSX2's bug report form; and, when the session said it would first look
whether either was reported already: "first make sure this would be a new
issue. If not, give me the URLs of existing issues."

How they were read:

- **Each of the two defects is reported to PCSX2 as an issue of its own**,
  by the maintainer, with the correction they suggest written into it. No
  pull request is opened unless PCSX2 asks for one.
- **The 30 days run from the day an issue is opened** without a reply from
  one of PCSX2's maintainers. That is the session's reading: the maintainer
  named the 30 days for a pull request, and has been asked whether they
  hold for an issue.
- **What a session may do has not changed**, because PCSX2's `AGENTS.md`
  names issue descriptions and issue comments beside pull requests. A
  session does not write an issue's title or text, nor the correction
  suggested in it, and posts nothing. It gives what the next section
  allows: where each defect is, how it is seen, what was measured and on
  what, and whether it is reported already.

Whether they would be new was looked up on 2026-11-11 by reading only,
without signing in: PCSX2's issues and pull requests, open and closed,
searched for the names in the code (`vu0DenormalizeMicroStatus`,
`mVUallocSFLAGd`, `vu0SetMicroFlags`, `micro_statusflags`, `vu0ExecMicro`;
`recMFC0`, `lastCOP0Cycle`, `CP0.n.Count`, `EntryHi`) and for the words a
report of the symptoms would use; the files changed by the open pull
requests nearest to this code; the threads nearest to each defect; and the
web.

- **Neither defect is reported**, and none of those open pull requests
  changes either line. `master` (`c8081bf5`, 2026-11-10) has both, and so
  has the release 2.8.2.
- The status-flag function came in with pull request
  [5571](https://github.com/PCSX2/pcsx2/pull/5571), merged on 2022-02-28.
  Nobody in it remarks on the direction of the shifts. Issue
  [8595](https://github.com/PCSX2/pcsx2/issues/8595), open, is a regression
  its reporter traced to that pull request: models that turn invisible in
  Final Fantasy X. No cause is named there. The reporter finds it gone with
  the EE recompiler switched off, and so is what `vu/lower/integer` shows
  (below), so that does not tell the two apart. Related by where it came
  from; not shown to be the same. (The session first wrote that this defect
  does not need the EE recompiler. Measured, the test does.)
- The count register's 64-bit addition came in with pull request
  [14105](https://github.com/PCSX2/pcsx2/pull/14105), "EE: Switch to 64 bit
  cycle counter", merged on 2026-03-07. Its review did not come to that
  line. Pull request [14847](https://github.com/PCSX2/pcsx2/pull/14847),
  about the TLB and `EntryHi`, does not mention the carry; it was closed
  unmerged the day it was opened, for the part an AI tool had in it.

What `vu/lower/integer` prints against the recording, by setting (PCSX2's
core at `v2.9.170` in `ps2n-host`, 2026-11-11): with everything recompiled,
line 3 differs (the zero flag is lost); with everything interpreted, no line
differs; with only the EE interpreted, no line differs either, because the
interpreter's `CTC2` writes the whole status register and leaves no flag to
lose ([poc-log.md](../poc-log.md), "Increment 5"); with only the VUs
interpreted, 144 lines differ in another way (`st=+0040`), which has not
been looked into.

**The maintainer opened both issues on 2026-11-11**:
[15185](https://github.com/PCSX2/pcsx2/issues/15185) for this defect and
[15186](https://github.com/PCSX2/pcsx2/issues/15186) for the count
register's carry. The 30 days end on 2026-12-11. Their words on telling the
session: "make sure they make sense and keep them in memory because if
they're ignored, after 30 minutes we'll just implement the fixes locally on
our project." How they were read, and asked back:

- "30 minutes" as the 30 days above.
- "The fixes", in the plural, may mean that the count register's correction
  is carried as well when the time comes, where
  [ADR 0013](0013-pcsx2s-count-register-carry-mirrored-until-fixed.md) says
  it is reproduced and not carried.

The maintainer's answer (2026-11-12): "Yes, 30 days, my bad. If the issues
are ignored, we'll implement the fixes locally, but they seem to be taking
care of the issues." So:

- **30 days it is.**
- **If an issue is ignored, its correction is carried locally, the count
  register's as well.** That changes ADR 0013, and is recorded there.
- **They were not ignored.** By 00:45 UTC on 2026-11-12, on 15186, members
  of PCSX2's team had asked for a program to verify it with and asked that
  no LLM-generated issues be sent; the maintainer had posted
  `tests/programs/countcarry.s` (the program a session wrote for this
  repository) and a build of it; and the author of the change that brought
  the defect in had confirmed the defect and the correction. 15185 had been
  labelled and had no reply yet. An answer from PCSX2 is what the 30 days
  were waiting for. What remains for 15186 is that a PCSX2 with the
  correction becomes the one PS2Native pins.

What the session found in them: both say what was measured, and the
corrections they suggest are right by reading. Neither says that the runs
were made on PCSX2's core at `v2.9.170` inside PS2Native's host rather than
on a build of PCSX2's own, nor that an AI session found the defects; the
second names the program `countcarry` without saying where it is
(`tests/programs/countcarry.s` here). The maintainer was told.

One more thing follows from PCSX2's rule against automated posting. GitHub
adds a line to an issue or a pull request when a commit that is pushed
anywhere names it. So **a commit message in this repository never names one
of PCSX2's issues or pull requests in a form GitHub links**: not by its
address, and not as `PCSX2/pcsx2#` and a number. In a file, as above, it is
only text.

## What an AI session may and may not do for what goes to PCSX2

PCSX2's `AGENTS.md` decides this, not PS2Native:

- A session does not post to PCSX2's GitHub in any form: no pull request, no
  issue, no comment.
- A session does not write what is posted there: not the change, not the
  commit message, not the pull request's description, not an issue's text,
  not replies to reviewers. For a contributor who is new to PCSX2, nothing
  in the contribution may be AI-generated at all.
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
