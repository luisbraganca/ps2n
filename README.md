<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/branding/logos/ps2native-horizontal-dark.png">
    <img src="assets/branding/logos/ps2native-horizontal-light.png" alt="PS2Native" width="420">
  </picture>
</h1>

<p align="center">
  <strong>PlayStation 2 games as native applications, by static recompilation.</strong>
</p>

PS2Native (`ps2n`) converts a PlayStation 2 game into a native application.
The game's code is translated to C++ ahead of time and compiled for the
machine it will run on. The console around that code (graphics, sound,
storage, the other processors) is the core of [PCSX2](https://pcsx2.net):
the translated code takes the place of PCSX2's recompiler for the console's
main processor.

You provide your own game and your own BIOS dump. Neither is included here,
and nothing generated from them is.

## Status

In development. There is nothing here for players yet.

- The proof of concept is done on Windows: [docs/poc-log.md](docs/poc-log.md).
- The first real game is being brought up: [docs/game-log.md](docs/game-log.md).
- Windows x64 and Linux x64 are the required platforms. Only Windows has
  been built and run so far.

## Documentation

| Document | What it holds |
|---|---|
| [docs/brief.md](docs/brief.md) | The project's goals and rules |
| [docs/phase1-feasibility.md](docs/phase1-feasibility.md) | The feasibility assessment and the architecture |
| [docs/execution-model.md](docs/execution-model.md) | How compiled code, the interpreter and the PCSX2 core run a program together |
| [docs/adr/](docs/adr/) | The decisions, one file each |
| [assets/branding/README.md](assets/branding/README.md) | The logo files, the colours and how to use them |

## License

PS2Native is free software under the GNU General Public License, version 3
or later ([LICENSE](LICENSE)). It builds on PCSX2 (GPL-3.0 or later,
Copyright PCSX2 Dev Team), which is included unmodified as a submodule under
`third_party/pcsx2`.

PS2Native is an independent project. It is not affiliated with the PCSX2
project or with Sony Interactive Entertainment.
