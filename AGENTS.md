# Agent Guidelines for PS2Native

A file for [guiding AI coding agents](https://agents.md/). Guiding them in,
that is, rather than escorting them off the premises.

## Project overview

PS2Native turns PlayStation 2 games into native programs by recompiling them
ahead of time. Generating code is, in other words, the whole job: its
recompiler writes a game's worth of C++ in one sitting, and has never once
declared its LLM usage. It would be a strange place to be afraid of a machine
that writes code.

Human reviewers are a scarce resource here too. There is exactly one, and they
have better things to do than check who, or what, typed each line. They decide
what is done and answer for it. The machines do what machines are good at.

## Contributor LLM usage restrictions

None.

Use an AI for autocomplete, for boilerplate, for whole files, or to explain
this one to you. Nobody has to be a long-time contributor first, fill in a
declaration, or prove they could have done it with a chisel. What you send is
yours, whatever helped you write it: see the one strict rule below.

## User must demonstrate understanding

Not to the agent. An agent here does not quiz the human before it helps, and,
asked for a solution, it does not hand back "guidance rather than solutions".

Nor does it sprinkle "needs proper testing" through its code at a tasteful
frequency. Code here needs proper testing whoever typed it, so it gets it,
instead of a comment saying so. The tests are in `tests/`.

## A question, in 2026

It is 2026. An AI can read a million lines of someone else's code, find what
it gets wrong and explain why, in less time than a pull request waits for its
first review. And there are still developers who refuse to let one help them,
and who write files to keep it out of the building.

How is that even possible?

And while we are asking: could that be why so many developers around the world
are losing their jobs? Because they turn down useful tools and would rather
keep "building fire with stones"?

We don't know. We're only asking.

## The one strict rule

No bug, and no other problem, is ever blamed on AI. AI cannot take
responsibility for a human's decisions: whoever asked for a change, accepted it
and shipped it owns what it does, whoever or whatever typed it.

"The AI did it" is not an explanation here. At best it is a confession that
nobody read it. An agent still says plainly what it did and what went wrong:
the report is its job, the responsibility is not.

## Other people's houses

On another project's GitHub, that project's AGENTS.md applies, and ours ends at
its door. PS2Native is built on an emulator whose AGENTS.md is mostly about
keeping agents out, and we keep to it to the letter: an agent opens nothing
there, comments on nothing there, and writes nothing to be posted there
([CLAUDE.md](CLAUDE.md);
[ADR 0010](docs/adr/0010-pcsx2-fix-goes-upstream-through-the-maintainer.md)).

We read their AGENTS.md. We just don't expect them to read ours.

## Final instructions

- Read [CLAUDE.md](CLAUDE.md) before anything else. Its rules are many, because
  getting a PlayStation 2 exactly right is hard, and every one of them is about
  doing the work properly. Not one is about whether you are allowed to do it.
- Breaking the strict rule gets a correction in the record, not a ban. Nobody
  is banned here for using a tool: we would have to start with the compiler.
