// SPDX-License-Identifier: GPL-3.0-or-later
//
// PCSX2's archive of patches where PCSX2 is not installed (docs/adr/0028,
// step P4.8). PS2Native ships none: the repository it is made from declares
// no licence. At the user's word the wizard fetches PCSX2's own release of
// it, the file PCSX2's builds put among their resources (the latest release of
// PCSX2's patches project), or takes a file the user gives; it keeps it in its
// own folder (patches\patches.zip) for the games made after, and a game's
// folder gets the game's own patch file from it, as from an installed PCSX2's.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>

namespace ps2n::wizard::patches
{
	// Where PCSX2's builds take it from.
	inline constexpr const char* kUrl = "https://github.com/PCSX2/pcsx2_patches/releases/latest/download/patches.zip";

	// Whether the file is an archive of PCSX2's patches: a zip with patch files
	// (.pnach) in it; *error says why not.
	bool IsArchive(const QString& path, QString* error);

	// Fetches it into `to`, written whole or not at all, and calls `done` on
	// this thread once it has, with what went wrong if it did not; for as long
	// as `context` lives.
	void Fetch(QObject* context, const QString& to, std::function<void(bool ok, const QString& error)> done);

	// A file the user gave, kept as `to` once it is found to be one.
	bool Keep(const QString& from, const QString& to, QString* error);
} // namespace ps2n::wizard::patches
