# ADR 0067: An AGENTS.md of PS2Native's own; no bug is ever blamed on AI

- Date: 2026-11-28
- Status: accepted (the maintainer's words, below). Committed and pushed at
  their word.

## Context

- **PCSX2's `AGENTS.md`** (in `third_party/pcsx2`) restricts contributors' use
  of LLMs: new contributors may not use them at all, nobody may have one
  generate a whole contribution, and everyone declares it. It forbids agents
  to post anything on PCSX2's GitHub, or to write what is published there. And
  it has an agent check that the human understands the problem before it
  helps.
- **PS2Native keeps to it on PCSX2's GitHub** (`CLAUDE.md`, under "PCSX2";
  ADR 0010). The maintainer writes their issues to PCSX2 themselves; sessions,
  asked for a draft, have written none.
- **PS2Native had no `AGENTS.md`.** Its sessions' instructions are
  `CLAUDE.md`.

## Decision

The maintainer's words, on 2026-11-28:

> I want you to create an AGENDS.md file but, since the AGENTS.md file from
> PCSX2 has given me so many headaches already (because it limits you) I want
> our AGENTS.md to be sarcastical and a bit of an indirect attack/joke against
> the PCSX2's. You should mention how is it even possible that in 2026 there's
> still developers that refuse to use AI to help with their work. Also leave it
> as a question that maybe that's why there's a major world developers getting
> fired? Because they refuse to use useful tools and want to keep "building
> fire with stones".
> The only strict rule is that, bugs or any issues that come up can never be
> blamed by AI since AI can never take responsibility for human's decisions.

They then corrected that sentence: "*can never be blamed to AI".

How they were read:

- **"AGENDS.md"** as `AGENTS.md`, at the repository's root, where agents look
  for it ([agents.md](https://agents.md/)).
- **Sarcastic, and indirect:** the file mirrors PCSX2's. It has its title, its
  overview, and three of its headings: "Contributor LLM usage restrictions",
  "User must demonstrate understanding" and "Final instructions". Each is
  answered, with a few of PCSX2's phrases quoted. PCSX2 is not named in it.
- **The two questions are put as questions,** as asked: how developers can
  still refuse AI's help in 2026, and whether that is why so many are losing
  their jobs, keeping "building fire with stones".
- **"can never be blamed to AI",** as they corrected it, as "can never be
  blamed on AI". AI cannot take responsibility for a human's decisions, so
  whoever asked for a change, accepted it and shipped it owns what it does.

## What the session chose within it, for the maintainer to decide otherwise

- **The file sends agents to `CLAUDE.md`,** whose rules all stand. The one
  strict rule is the only rule about AI's place here, not the only rule of the
  project. Other tools' agents look for `AGENTS.md`, not `CLAUDE.md`, so one
  that reads only this file still finds them.
- **It keeps ADR 0010's boundary in so many words.** On PCSX2's GitHub,
  PCSX2's `AGENTS.md` applies: an agent opens nothing there, comments on
  nothing there and writes nothing to be posted there. Without it, an agent
  reading only our file could take its welcome as leave to post there, at the
  maintainer's cost.
- **An agent still says plainly what it did wrong.** The rule says where the
  responsibility lies, not that a mistake goes unsaid.
- **The rule is in `CLAUDE.md` too,** among the rules for every session.
  Claude Code's sessions read that file.
- **It names no person, none of PCSX2's issues, and nothing the maintainer has
  sent there.** It has no number that would go stale.
- **It is not in the README's table of documents,** where `CLAUDE.md` is not
  either.

## Consequences

- **Nothing changes on PCSX2's side.** ADR 0010 stands, and PCSX2's
  `AGENTS.md` governs PCSX2's GitHub. Asked for an issue's text for PCSX2, a
  session still writes none.
- **The records put no defect down to AI.** A log, a record, a commit message
  or a report says what caused it and what was decided.

## The maintainer's answers

The session put two things to them, each with its recommendation:

1. Keep its two additions: the file sending agents to `CLAUDE.md` and keeping
   ADR 0010's boundary, and an agent still saying plainly what it got wrong.
   Recommended: keep them.
2. Commit and push. Recommended: yes, once they had read the file.

Their answers: "1. Yes", "2. Yes". So both additions stay, and it was committed
and pushed.
