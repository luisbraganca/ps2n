# Game configs

The configs of the games PS2Native runs, one folder per game, for the
releases [docs/games.md](../docs/games.md) lists. They hold file names and
addresses, no game data. [GETTING_STARTED.md](../GETTING_STARTED.md) uses
them: a game's `.toml` files are copied into the game's own folder, beside a
`disc` folder with the programs taken from the disc image, and the game is
recompiled there.

| Folder | Game | Programs in `disc` |
|---|---|---|
| [agent-under-fire](agent-under-fire) | 007: Agent Under Fire, Europe (SLES-50539) | `SLES_505.39`, `MOVIE.ELF`, `ACTION.UK`, `DRIVING.ELF` |
| [code-veronica-x](code-veronica-x) | Resident Evil Code: Veronica X, USA (SLUS-20184) | `SLUS_201.84` |
| [secret-agent-clank](secret-agent-clank) | Secret Agent Clank, USA (SCUS-97623) | `SCUS_976.23` |

The `*-entries.toml` files list entry points the analysis does not find by
itself: addresses the runtime had to interpret in play sessions, so that a
first build has them already. Each config also reads the `ps2n_missing` files
of the folder it is copied to, which the game writes when it exits.

Not here: the VU micro code a session records (`ps2n_missing.vu.toml`, for
`[vu] code_files`), which is the game's own code, and PCSX2's widescreen
patch files (`[pnach]`), which come with PCSX2.
