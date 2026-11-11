# ADR 0014: The website is served from `docs/`, beside the documentation

| | |
|---|---|
| Status | Accepted |
| Date | 2026-11-11 |
| Decided by | Maintainer |
| Changes | [brief.md](../brief.md), section 7: "The previous website is no longer required." It still is not. The project has a new one |

## Context

The brief did without a website. On 2026-11-11 the maintainer switched on
GitHub Pages for the repository and asked for one.

GitHub Pages, set to deploy from a branch, publishes one of two places: the
repository's root or its `docs` folder. `docs` is where the project's
documentation is: the brief, the assessment, the logs, the execution model
and these records, Markdown files that `CLAUDE.md`, `README.md` and each
other link to by path.

## Decision

The maintainer's words: "I activated GitHub Pages (Deploy from a branch).
Unfortunately I had to use "/docs" folder, so make sure there are no
conflicts. Anyway, I want you to generate a website for this project with
fancy animations."

How they were read:

- **The project has a website**: one page, at
  <https://luisbraganca.github.io/ps2n/>, the address GitHub gives a
  repository's site. The address and the branch (`main`) are the session's
  assumptions: it could not look at the repository's settings.
- **"No conflicts" means the documentation stays where it is and as it is.**
  No file was moved, renamed or changed for the site, so every link into
  `docs/` still holds. The site is what was added, under names the
  documentation does not use: `index.html`, `404.html`, `.nojekyll` and the
  folder `site/`. [docs/site/README.md](../site/README.md) says which is
  which.
- **Pages serves the folder as it is.** That is what the empty file
  `docs/.nojekyll` is for, and it is the session's choice, made without
  asking. Left to itself, Pages runs Jekyll over the folder it publishes
  from a branch: every Markdown file in `docs/` would become a page of the
  site in a theme of GitHub's, and `{{` or `{%` in any of them would be read
  as Jekyll's template language (none has either today). With the file, a
  Markdown file is published as the text it is, the documentation is read
  on GitHub, and the page links to it there. Undoing the choice is deleting
  one file.
- **"Fancy animations" came with two limits the maintainer did not state.**
  They never hold content back: with motion off, or without JavaScript, the
  page shows everything. And they are off for a reader whose system asks for
  reduced motion; a switch in the footer sets it either way.

## What the page may say

The project's rules apply to it as to a log:

- **Only what was measured, with its date.** Every number on the page is a
  copy of a line in [game-log.md](../game-log.md), [poc-log.md](../poc-log.md)
  or a decision record; `docs/site/README.md` lists which. That includes
  what is not flattering: the page says that the compiled game is slower so
  far, and that nothing of a level has run.
- **Nothing from a game or a BIOS** ([ADR 0009](0009-phase-3-first-game-killzone.md)).
  The code it shows is from `tests/programs/bench.s` and what the recompiler
  makes of it.
- **PS2Native's branding, and PCSX2's logo nowhere**
  ([ADR 0012](0012-ps2native-branding-not-pcsx2s.md)). PCSX2 is named as
  what the project builds on, with its license and its copyright, and the
  page says that the project is affiliated neither with PCSX2 nor with Sony.

## What was done (2026-11-11)

- `docs/index.html`, `docs/site/site.css` and `docs/site/site.js`: the page.
  No build step, no dependency, nothing loaded from another host.
- What moves: the logo puts itself together out of squares sampled from its
  own picture; a field of squares behind it follows the pointer; labels
  decode themselves out of hexadecimal; numbers count up to what was
  measured; squares travel the wires of the build pipeline, and the compiled
  code drops into the machine's socket; seven instructions of
  `tests/programs/bench.s` turn into the C++ generated for them, line by
  line, while their cost adds up to the 65 eighths of a cycle the generated
  code names; two lanes of patterns pass a line that compares them; bars
  grow; the roadmap's line is drawn as far as the phase in progress.
- `docs/404.html`, for an address that holds no file.
- `docs/site/brand/`: six files of the brand kit, copied byte for byte and
  compared, because Pages cannot reach `assets/`.
- `scripts\windows\site.ps1`: serves `docs` on this machine as Pages serves
  it.
- `README.md` links to the site. `CLAUDE.md` and the brand kit's README say
  that it exists.
- Looked at in a browser on this machine at 1440 by 900, 1366 by 768 and 390
  by 844, with motion on, with reduced motion, and through the page for a
  missing address. The page reported no error or warning in any of them. At
  phone width nothing is wider than the screen. The two colours of the
  charts were put through a palette validator against the charts'
  background.

Not looked at when this record was committed, because only the published
site can show it: the first deployment. GitHub fetches a repository's
submodules when it builds its site, which needs them to be public and
addressed by `https`; both are. Whether a deployment went through shows in
the repository's Actions tab ("pages build and deployment").

## Consequences

- **The page goes out of date when a log changes.** Whoever changes a number
  or a state in `game-log.md`, `poc-log.md` or `CLAUDE.md` that the page
  repeats changes the page too, and the date at its foot.
  `docs/site/README.md` has the list.
- `docs/` now holds two kinds of things. Nothing of the site goes outside
  `index.html`, `404.html`, `.nojekyll` and `site/`, and no documentation
  goes into `site/`.
- Everything in `docs/` is published, the documentation included, as plain
  files. That adds nothing to what the repository already shows. It is a
  reason more to keep to the rule that nothing derived from a game or a BIOS
  is committed.
- A custom domain, or another name for the repository, changes the site's
  address, which is written in three places (`docs/site/README.md`, "If the
  site moves").
- The site is not the application's interface, and the brief's rule against
  premature interface work is not touched by it: nothing here is part of
  what a player gets.
