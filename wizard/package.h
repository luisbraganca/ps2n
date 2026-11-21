// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wizard's packaging step (docs/phase4-proposal.md, step P4.5): the game's
// folder made into one that plays it, as scripts/windows/kit/make-game.ps1
// makes it (docs/app-log.md, "P4.4"), and as a game whose settings the wizard
// wrote (docs/adr/0032): beside the modules the compile step linked there,
// the runtime, the launcher under the game's name, the BIOS image linked into
// the folder where it can be, and settings.txt, what the launcher starts the
// runtime with, with -packaged (the game's own settings file and patch file
// are already in the folder's data). And what the game compiles what its
// sessions meet with itself (step P4.6, docs/adr/0034): a copy of the
// toolchain, ps2n-remake with Qt (in the runtime folder), and make.txt, how
// its modules are made.

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace ps2n::wizard
{
	struct PackageInput
	{
		QString runtime; // the kit's runtime folder
		QString folder; // the game's folder
		QString title; // the launcher's name and the window's title
		QString image; // the disc image, read where it is
		QString bios; // the BIOS image
		QString toolchain; // the kit's toolchain folder, which the game's folder gets a copy of
		QStringList executables; // compiled from the disc: the one it boots first, then those it loads itself
		bool cop2_full_sync = false; // PCSX2's GameDB sets FullVU0SyncHack for the game (PipelineInput), for make.txt
	};

	// A name Windows takes for a file or a folder: the title without what it
	// does not take.
	QString PlainName(const QString& title);

	// The launcher the game's folder is started from.
	QString LauncherPath(const PackageInput& input);

	// False, with *error saying why, when the folder could not be made whole.
	bool Package(const PackageInput& input, QString* error);
} // namespace ps2n::wizard
