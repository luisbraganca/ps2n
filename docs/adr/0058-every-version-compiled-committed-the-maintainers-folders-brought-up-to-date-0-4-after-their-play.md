# ADR 0058: Every version compiled, committed; the maintainer's folders brought up to date; 0.4 after their play

- Date: 2026-11-23
- Status: accepted (the maintainer's answers, below).

## Context

[ADR 0057](0057-code-run-outside-an-executable-every-version-compiled-picked-by-what-memory-holds.md)'s
change was built and measured the same evening ([docs/app-log.md](../app-log.md),
"Code run outside an executable, every version compiled"):

- **Jak and Daxter.** The maintainer's session, run again with the code its
  first run met compiled, recorded nothing new to compile and interpreted
  48,071 EE cycles where it had interpreted 580,691,693.
- **Gran Turismo 3.** Its generated code is the same but for an empty table,
  and its patched program loader runs compiled.
- **Killzone and Agent Under Fire** recorded nothing.
- **Every run** was its session at every fingerprint.
- **Tests.** Every test passes on both systems.

The session put three things to the maintainer, each with its recommendation:

1. **Commit and push.** Recommended: yes.
2. **Bring all of their game folders up to date** with these programs.
   Recommended: yes. Their PS2Native folder is made again from the committed
   tree too.
3. **Prepare a 0.4 release** with all of the day's work, as a draft for them
   to publish, as 0.3 was. Recommended: yes, after they have played a session
   or two with the updated folders.

## Decision

The maintainer's answers: "1. Yes", "2. Yes", "3. Yes".

1. **Committed and pushed.**
2. **Their game folders are brought up to date.**
   - Every folder in their PS2Native folder's `games` gets these programs and
     compiles its game again, as its launcher would before its next start:
     the three new games, and the earlier ones that kept 0.3's or the
     morning's programs.
   - Their PS2Native folder is made again from the committed tree first, its
     games kept.
   - Their folder inside WSL likewise, from the Linux folder made again from
     the committed tree.
   - Each game's own settings stay as they are: Jak and Daxter's 130% EE
     cycle rate (ADR 0056) among them.
3. **0.4 is prepared after their play.** "Yes" is read as the
   recommendation: 0.4 is prepared once the maintainer has played a session
   or two with the updated folders, as ADR 0042 and ADR 0052 have a release
   made:
   - the runtime's profile of play made again on each system from the tree
     released;
   - both downloads made from the committed and pushed tree and tried;
   - a draft on GitHub, which the maintainer publishes.

## Done the same evening

- **Committed and pushed:** `2cc8125`.
- **Their PS2Native folders made again from it**, their games kept: on Windows
  (`build\kit`) and inside WSL (`build/PS2Native`).
- **Nine folders on Windows** given its programs, each compiling its game
  again (23 to 319 s; `build\measure\folders-58.ps1`, as `folders-02.ps1`):
  - Jak and Daxter;
  - Killzone;
  - Silent Hill 3;
  - Gran Turismo 3;
  - both Agent Under Fire folders;
  - Resident Evil – Code: Veronica X;
  - Secret Agent Clank;
  - SSX Tricky.
- **Checking them:**
  - Where a folder has a session played with VU1 on the CPU thread, its start
    was run again with the new programs and was the session at every
    fingerprint compared.
  - Asked again, the wizard found every folder up to date and changed
    nothing.
  - Every folder's memory cards are as they were, and every session is
    there.
  - `SLES_505.39`, a folder a session made for P4.4's check, which no round
    has updated, is left as it is.
- **Inside WSL:** their Jak and Daxter folder the same, the start of its
  longest session run again and the session at every fingerprint compared.
- **What to expect:** Jak and Daxter's next session, on either system, still
  meets versions of code its old captures could not keep. It records them,
  shows the notice, and compiles once more after the session.

## Consequences

- **Their sessions now.** The maintainer's next sessions run on what was
  measured.
  - Jak and Daxter's first session records the versions of code it meets as
    blocks. The compile after it takes them in, and later sessions that go
    where it went run them compiled.
  - A folder brought up to date with another toolchain counts its profile
    once more after a session (ADR 0049).
- **0.4 waits** for the maintainer to have played with the updated folders.
