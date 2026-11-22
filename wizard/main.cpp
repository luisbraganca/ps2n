// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-wizard, the application a game is made with (docs/phase4-proposal.md,
// step P4.5): the brief's five steps in one window (wizard_window.h). It lives
// in the runtime folder of the folder scripts/windows/kit.ps1 puts together,
// beside the runtime whose libraries and resources it shares, and makes games
// into that folder's games folder, with the toolchain beside it.
//
//   ps2n-wizard [--kit <folder>] [--image <disc image>] [--bios <BIOS image>]
//               [--shots <folder>] [--auto [--until <step>]]
//               [--without-pcsx2 [--fetch-patches]]
//               [--update <folder> | --updates <folder>]
//
// --kit names the folder kit.ps1 made, where this program is not in it (a
// build tree). --image and --bios fill the first two steps in; --shots saves
// each step as a picture once it is complete; --auto goes on to the next step
// whenever one is complete, and closes at the end without playing, or after
// the step --until names (1 to 5); --without-pcsx2 does not look for an
// installed PCSX2's archive of patches, as on a computer without PCSX2, and
// --fetch-patches then fetches it as if the user had asked: for checks.
// --update brings the games made earlier at a folder (a game's folder, or one
// that holds games) up to date without a window, a line written for each, and
// ends with 0 when none failed; --updates opens the window of the games made
// earlier with those of that folder listed too, saved into --shots, and with
// --auto brings them up to date, saves it again and closes: for checks
// (update_window.h, docs/adr/0048).

#include "game_settings.h"
#include "platform.h"
#include "update_window.h"
#include "wizard_window.h"

#include "common/MemorySettingsInterface.h"

#include "pcsx2/Config.h"
#include "pcsx2/Host.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtGui/QIcon>
#include <QtGui/QStyleHints>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

#include <string>

namespace
{
	// The defaults the settings pages show, and a game's settings are written
	// over: PS2Native's for a game, the runtime's (docs/adr/0032).
	MemorySettingsInterface s_base;
} // namespace

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("PS2Native"));
	// The icons of PCSX2's pages, from its theme for the window's colours, as
	// PCSX2 chooses it (pcsx2-qt/Themes.cpp).
	QIcon::setThemeName(QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("white") :
																									QStringLiteral("black"));

	QString kit_root, image, bios, shots, update, updates;
	bool automatic = false;
	bool without_pcsx2 = false;
	bool fetch_patches = false;
	int until = 5;
	const QStringList arguments = QCoreApplication::arguments();
	for (qsizetype i = 1; i < arguments.size(); i++)
	{
		const QString& arg = arguments[i];
		const bool has_value = i + 1 < arguments.size();
		if (arg == QStringLiteral("--kit") && has_value)
			kit_root = arguments[++i];
		else if (arg == QStringLiteral("--image") && has_value)
			image = arguments[++i];
		else if (arg == QStringLiteral("--bios") && has_value)
			bios = arguments[++i];
		else if (arg == QStringLiteral("--shots") && has_value)
			shots = arguments[++i];
		else if (arg == QStringLiteral("--auto"))
			automatic = true;
		else if (arg == QStringLiteral("--until") && has_value)
			until = arguments[++i].toInt();
		else if (arg == QStringLiteral("--without-pcsx2"))
			without_pcsx2 = true;
		else if (arg == QStringLiteral("--fetch-patches"))
			fetch_patches = true;
		else if (arg == QStringLiteral("--update") && has_value)
			update = arguments[++i];
		else if (arg == QStringLiteral("--updates") && has_value)
			updates = arguments[++i];
	}

	// The core's resources (its databases of games and of patches) beside this
	// program, as beside the runtime.
	EmuFolders::SetAppRoot();
	if (!EmuFolders::SetResourcesDirectory())
	{
		QMessageBox::critical(nullptr, QStringLiteral("PS2Native"), QObject::tr("PS2Native's folder is not whole: its resources are missing."));
		return 1;
	}
	ps2n::game::SetDefaults(s_base);
	Host::Internal::SetBaseSettingsLayer(&s_base);

	// The folder kit.ps1 made: this program in its runtime folder.
	ps2n::wizard::Kit kit;
	const QDir here(QCoreApplication::applicationDirPath());
	kit.root = kit_root.isEmpty() ? QDir::cleanPath(here.filePath(QStringLiteral(".."))) : QDir(kit_root).absolutePath();
	kit.runtime = kit_root.isEmpty() ? here.absolutePath() : QDir(kit.root).filePath(QStringLiteral("runtime"));
	kit.toolchain = QDir(kit.root).filePath(QStringLiteral("toolchain"));
	if (!QDir(kit.toolchain).exists() ||
		!QFileInfo::exists(QDir(kit.runtime).filePath(ps2n::wizard::platform::Program(QStringLiteral("ps2n-host")))))
	{
		QMessageBox::critical(nullptr, QStringLiteral("PS2Native"),
			QObject::tr("PS2Native's folder is not whole: %1 should have its runtime and its toolchain.").arg(QDir::toNativeSeparators(kit.root)));
		return 1;
	}

	// For checks: the games made earlier at a folder brought up to date, without
	// a window.
	if (!update.isEmpty())
		return ps2n::wizard::UpdateWithoutWindow(kit, QDir(update).absolutePath());

	ps2n::wizard::WizardWindow window(kit);
	if (without_pcsx2)
		window.WithoutPcsx2(fetch_patches);
	window.show();
	if (!updates.isEmpty())
		window.ShowUpdates(QDir(updates).absolutePath(), shots, automatic);
	else
		window.Prepare(image, bios, shots, automatic, until);
	return app.exec();
}
