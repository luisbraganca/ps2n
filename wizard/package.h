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
//
// And a game's folder made by an earlier PS2Native brought up to date
// (docs/adr/0048): given this PS2Native's programs in the place of its own,
// the runtime, the launcher and the toolchain, and nothing else of it touched.
// Its own recompile then compiles the game again before it next starts, the
// toolchain being another (Pipeline::toolchainStamp).

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

	// ---- A game's folder brought up to date (docs/adr/0048) ---------------------

	// What a game's folder says of itself, and how its programs stand to this
	// PS2Native's: the folder's programs.txt, which the wizard writes when it
	// makes a folder and when it brings one up to date, has the version they are
	// of and a fingerprint of the runtime and the toolchain they were copied from.
	struct GameFolder
	{
		enum class Programs
		{
			Current, // this PS2Native's
			Earlier, // an earlier PS2Native's, or another build's of this version: can be brought up to date
			Later, // a later PS2Native's: left as they are
		};

		QString folder;
		QString title; // as its make.txt has it
		QString boot; // the executable its disc boots, as its make.txt has it ("SCES_503.61")
		QString version; // the PS2Native its programs are of; empty where the folder does not say (0.1's did not)
		Programs programs = Programs::Earlier;
		bool cop2_full_sync = false; // its make.txt says so already
	};

	// Whether `folder` is a game's folder as the wizard makes them: it says how
	// its modules are made (make.txt), what its launcher starts the runtime with
	// (settings.txt), and has a runtime.
	bool IsGameFolder(const QString& folder);

	// The game's folders at `where`: `where` itself when it is one; otherwise
	// those among the folders in it, and among those in its games folder (a
	// PS2Native folder's). Sorted by name.
	QStringList FindGameFolders(const QString& where);

	// What `folder` says of itself, against the programs of `runtime` and
	// `toolchain` (a kit's).
	GameFolder ReadGameFolder(const QString& runtime, const QString& toolchain, const QString& folder);

	struct UpdateInput
	{
		QString runtime; // the kit's runtime folder
		QString toolchain; // the kit's toolchain folder
		QString folder; // the game's folder
		bool cop2_full_sync = false; // PCSX2's GameDB sets FullVU0SyncHack for the game: make.txt is given the line where it has none
	};

	// Gives the game's folder this PS2Native's programs: every file of the
	// runtime a game's folder has, the launcher under the game's name, and the
	// toolchain in the place of its own. Its sessions, its saves, its settings,
	// what its sessions met and what it compiled stay as they are; modules an
	// earlier compile left for its next start go, being the earlier programs'.
	// Nothing is changed while the game runs or compiles: false then, and
	// whenever the folder could not be given them all, with *error saying why.
	bool BringUpToDate(const UpdateInput& input, QString* error);
} // namespace ps2n::wizard
