// SPDX-License-Identifier: GPL-3.0-or-later
//
// Games made earlier (docs/adr/0048): a game's folder carries its own copy of
// PS2Native's programs, so one made with an earlier PS2Native keeps the
// programs it was made with. This window lists the game folders of this
// PS2Native's games folder, and of any folder the user adds (an earlier
// PS2Native's folder, a game's folder moved elsewhere), with how each one's
// programs stand to this PS2Native's, and brings those ticked up to date
// (wizard/package.h, BringUpToDate). A game brought up to date compiles itself
// again before it next starts, by its own recompile.

#pragma once

#include "wizard_window.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QDialog>

#include <string>

class QLabel;
class QListWidget;
class QPushButton;

namespace ps2n::wizard
{
	// Whether PCSX2's GameDB has the game played with FullVU0SyncHack (a
	// gamefix, which PS2Native fixes at what the GameDB says), which the
	// recompiler decides the game's COP2 instructions with (ADR 0046).
	bool FullVu0Sync(const std::string& serial);

	// A game's serial from the name of the executable its disc boots, as PCSX2
	// makes it: "SCES_503.61" is SCES-50361.
	QString SerialOf(const QString& boot);

	// How many of the game folders at `where` have an earlier PS2Native's
	// programs than this kit's.
	int GamesMadeEarlier(const Kit& kit, const QString& where);

	// For checks, without a window (ps2n-wizard --update <folder>): every game's
	// folder at `where` that has an earlier PS2Native's programs brought up to
	// date, a line written for each (standard error). 0 when none failed.
	int UpdateWithoutWindow(const Kit& kit, const QString& where);

	class UpdateDialog final : public QDialog
	{
		Q_OBJECT

	public:
		explicit UpdateDialog(const Kit& kit, QWidget* parent = nullptr);

		// The game folders at `where` added to the list (those not in it yet).
		void Add(const QString& where);

		// For checks: the window saved as a picture into `shots` as it is shown
		// (updates-1.png), and with `automatic` those ticked brought up to date,
		// the window saved again (updates-2.png) and closed.
		void Check(const QString& shots, bool automatic);

	private:
		void Refresh();
		void BringTicked();

		Kit m_kit;
		QStringList m_folders;
		QStringList m_said; // what was done about each, once something was
		QListWidget* m_list = nullptr;
		QLabel* m_status = nullptr;
		QPushButton* m_bring = nullptr;
	};
} // namespace ps2n::wizard
