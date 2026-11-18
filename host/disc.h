// SPDX-License-Identifier: GPL-3.0-or-later
//
// A disc image's files, read through PCSX2's own readers, for what makes a
// game (docs/adr/0033, step P4.8): every format PCSX2 plays (an .iso; a raw
// .bin, .img or .mdf; a .chd; a .cso or .zso; a .gz), where PS2Native's own
// reader of an image takes only an .iso of 2048-byte sectors. What is looked
// for on the disc is PS2Native's (ps2n/iso9660.h): its executables, and their
// files. The wizard, a game's own recompile (ps2n-remake) and make-game.ps1
// call these:
//
//   ps2n-host -disc-executables <image>
//       A line for each of the EE's executables on the disc: "boot <path>" for
//       the one SYSTEM.CNF boots, "loads <path>" for each other, which a game
//       made of several executables loads itself (as ps2n-recomp executables).
//   ps2n-host -disc-extract <image> <folder> [<path>...]
//       The files named, the boot executable when none is, written into the
//       folder (a file the same as the disc's is left alone), with a line
//       "<path> <file written>" for each (as ps2n-recomp extract).
//
// Modes of their own: they take nothing else, load no module of compiled code
// and run no machine, so that the runtime in the wizard's folder, which has no
// game's modules beside it, reads a disc too.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ps2n::host::disc
{
	// What the process exits with, when the command line (the program's name
	// first) asks for one of the modes above; nothing when it does not.
	std::optional<int> Run(const std::vector<std::string>& arguments);
} // namespace ps2n::host::disc
