// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wizard's window (docs/phase4-proposal.md, step P4.5): the brief's five
// steps, one page each. The game, recognized from its disc image by PCSX2's
// own code; the BIOS, the user's own, read where it is; the settings, PCSX2's
// own pages for the game (docs/adr/0032); the recompile, with what it is doing
// and what failed; the game's folder, made whole, with what may be done with
// it. Every default can be accepted as it is.

#pragma once

#include "pcsx2/GameList.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QWizard>

namespace ps2n::wizard
{
	// Where the wizard is, and what it has beside it.
	struct Kit
	{
		QString root; // the folder the user opened: games go into its games folder
		QString runtime; // the runtime a game's folder gets a copy of
		QString toolchain; // clang-cl, lld-link, ps2n-recomp, PS2Native's headers
	};

	// What the steps find and choose, for the steps after them.
	struct State
	{
		Kit kit;
		QString image;
		GameList::Entry entry;
		bool recognized = false;
		QStringList executables; // those compiled: the one the disc boots first, then those it loads itself the user kept
		QString bios;
		QString title;
		QString folder;
		QString patch_archive; // PCSX2's archive of patches: an installed PCSX2's, or the one this folder keeps (patches.h)
		bool fetch_patches_unasked = false; // for checks: fetched where there is none, as if the user had asked
	};

	class WizardWindow final : public QWizard
	{
		Q_OBJECT

	public:
		explicit WizardWindow(Kit kit, QWidget* parent = nullptr);

		State& state() { return m_state; }

		// For checks: the disc image and the BIOS given beforehand, and each
		// page saved as a picture into a folder as it is shown (the settings,
		// one for each of their categories); with `automatic`, Next pressed
		// for the user whenever a page is complete, and the wizard closed at
		// the end without playing, or after the step `until` (1 to 5).
		void Prepare(const QString& image, const QString& bios, const QString& shots, bool automatic, int until = 5);

		// For checks: as on a computer without PCSX2, its installed archive of
		// patches not looked for; and the archive fetched where there is none, as
		// if the user had asked.
		void WithoutPcsx2(bool fetch_patches);

	private:
		State m_state;
	};
} // namespace ps2n::wizard
