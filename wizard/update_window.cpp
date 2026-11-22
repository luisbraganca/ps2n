// SPDX-License-Identifier: GPL-3.0-or-later

#include "update_window.h"

#include "package.h"

// PS2Native's version: version.txt at the repository's root, which the build
// writes into this header (cmake/ps2n-inject.cmake).
#include "ps2n_version.h"

#include "pcsx2/Config.h"
#include "pcsx2/GameDatabase.h"

#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cstdio>

namespace ps2n::wizard
{
	namespace
	{
		// How a folder's programs stand, in the user's words.
		QString Standing(const GameFolder& game)
		{
			const QString ours = QStringLiteral(PS2N_VERSION);
			switch (game.programs)
			{
				case GameFolder::Programs::Current:
					return QObject::tr("Up to date.");
				case GameFolder::Programs::Later:
					return QObject::tr("Made with PS2Native %1, later than this one (%2): left as it is.").arg(game.version, ours);
				case GameFolder::Programs::Earlier:
					break;
			}
			if (game.version.isEmpty())
				return QObject::tr("Made with an earlier PS2Native.");
			if (game.version == ours)
				return QObject::tr("Made with another build of PS2Native %1.").arg(ours);
			return QObject::tr("Made with PS2Native %1.").arg(game.version);
		}

		UpdateInput InputFor(const Kit& kit, const GameFolder& game)
		{
			UpdateInput input;
			input.runtime = kit.runtime;
			input.toolchain = kit.toolchain;
			input.folder = game.folder;
			input.cop2_full_sync = FullVu0Sync(SerialOf(game.boot).toStdString());
			return input;
		}
	} // namespace

	bool FullVu0Sync(const std::string& serial)
	{
		const GameDatabaseSchema::GameEntry* game = GameDatabase::findGame(serial);
		return game && std::find(game->gameFixes.begin(), game->gameFixes.end(), Fix_FullVU0Sync) != game->gameFixes.end();
	}

	QString SerialOf(const QString& boot)
	{
		QString serial = boot.toUpper();
		serial.replace(QLatin1Char('_'), QLatin1Char('-'));
		serial.remove(QLatin1Char('.'));
		return serial;
	}

	int GamesMadeEarlier(const Kit& kit, const QString& where)
	{
		int earlier = 0;
		for (const QString& folder : FindGameFolders(where))
			earlier += ReadGameFolder(kit.runtime, kit.toolchain, folder).programs == GameFolder::Programs::Earlier ? 1 : 0;
		return earlier;
	}

	int UpdateWithoutWindow(const Kit& kit, const QString& where)
	{
		const QStringList folders = FindGameFolders(where);
		if (folders.isEmpty())
		{
			std::fprintf(stderr, "ps2n-wizard: no game's folder at %s\n", QDir::toNativeSeparators(where).toLocal8Bit().constData());
			return 1;
		}
		int failed = 0;
		for (const QString& folder : folders)
		{
			const GameFolder game = ReadGameFolder(kit.runtime, kit.toolchain, folder);
			const QByteArray name = QDir::toNativeSeparators(folder).toLocal8Bit();
			if (game.programs != GameFolder::Programs::Earlier)
			{
				std::fprintf(stderr, "ps2n-wizard: %s: %s\n", name.constData(), Standing(game).toLocal8Bit().constData());
				continue;
			}
			QString error;
			const UpdateInput input = InputFor(kit, game);
			if (BringUpToDate(input, &error))
			{
				std::fprintf(stderr, "ps2n-wizard: %s: %s Brought up to date (PS2Native %s)%s.\n", name.constData(),
					Standing(game).toLocal8Bit().constData(), PS2N_VERSION,
					input.cop2_full_sync && !game.cop2_full_sync ? ", its make.txt given cop2-full-sync" : "");
			}
			else
			{
				std::fprintf(stderr, "ps2n-wizard: %s: not brought up to date: %s\n", name.constData(), error.toLocal8Bit().constData());
				failed++;
			}
		}
		return failed == 0 ? 0 : 1;
	}

	UpdateDialog::UpdateDialog(const Kit& kit, QWidget* parent)
		: QDialog(parent)
		, m_kit(kit)
	{
		setWindowTitle(tr("Games made earlier"));
		auto* layout = new QVBoxLayout(this);
		auto* what = new QLabel(
			tr("<p>A game's folder has its own copy of PS2Native's programs, so a game made with an earlier PS2Native keeps the "
			   "programs it was made with.</p>"
			   "<p><b>Bringing it up to date</b> gives its folder the programs of PS2Native %1. The game then compiles itself again "
			   "before it next starts, which takes a few minutes. Its saves, its settings and its sessions stay as they are.</p>")
				.arg(QStringLiteral(PS2N_VERSION)),
			this);
		what->setWordWrap(true);
		what->setTextFormat(Qt::RichText);
		layout->addWidget(what);

		m_list = new QListWidget(this);
		m_list->setSelectionMode(QAbstractItemView::NoSelection);
		m_list->setWordWrap(true);
		layout->addWidget(m_list, 1);

		m_status = new QLabel(this);
		m_status->setWordWrap(true);
		layout->addWidget(m_status);

		auto* buttons = new QHBoxLayout;
		auto* add = new QPushButton(tr("Add a folder..."), this);
		add->setToolTip(tr("A game's folder, or a folder that holds games: an earlier PS2Native's folder, or its games folder."));
		m_bring = new QPushButton(tr("Bring up to date"), this);
		m_bring->setDefault(true);
		auto* close = new QPushButton(tr("Close"), this);
		buttons->addWidget(add);
		buttons->addStretch(1);
		buttons->addWidget(m_bring);
		buttons->addWidget(close);
		layout->addLayout(buttons);

		connect(add, &QPushButton::clicked, this, [this]() {
			const QString folder = QFileDialog::getExistingDirectory(this, tr("A game's folder, or a folder that holds games"));
			if (folder.isEmpty())
				return;
			const qsizetype before = m_folders.size();
			Add(folder);
			if (m_folders.size() == before)
				m_status->setText(tr("No game's folder that PS2Native made is in %1, or they are all listed already.")
									  .arg(QDir::toNativeSeparators(folder)));
		});
		connect(m_bring, &QPushButton::clicked, this, [this]() { BringTicked(); });
		connect(close, &QPushButton::clicked, this, &QDialog::accept);
		connect(m_list, &QListWidget::itemChanged, this, [this]() {
			bool any = false;
			for (int i = 0; i < m_list->count(); i++)
				any |= m_list->item(i)->checkState() == Qt::Checked;
			m_bring->setEnabled(any);
		});

		Add(QDir(m_kit.root).filePath(QStringLiteral("games")));
		resize(760, 560);
	}

	void UpdateDialog::Add(const QString& where)
	{
		for (const QString& folder : FindGameFolders(where))
		{
			if (m_folders.contains(folder, Qt::CaseInsensitive))
				continue;
			m_folders.push_back(folder);
			m_said.push_back(QString());
		}
		Refresh();
	}

	void UpdateDialog::Refresh()
	{
		m_list->blockSignals(true);
		m_list->clear();
		int earlier = 0;
		for (qsizetype i = 0; i < m_folders.size(); i++)
		{
			const GameFolder game = ReadGameFolder(m_kit.runtime, m_kit.toolchain, m_folders[i]);
			const bool can = game.programs == GameFolder::Programs::Earlier;
			earlier += can ? 1 : 0;
			auto* item = new QListWidgetItem(QStringLiteral("%1\n%2\n%3")
												 .arg(game.title.isEmpty() ? QDir(game.folder).dirName() : game.title,
													 m_said[i].isEmpty() ? Standing(game) : m_said[i], QDir::toNativeSeparators(game.folder)),
				m_list);
			item->setData(Qt::UserRole, game.folder);
			// One that can be brought up to date is ticked; the others cannot be.
			item->setFlags(can ? (Qt::ItemIsEnabled | Qt::ItemIsUserCheckable) : Qt::ItemIsEnabled);
			item->setCheckState(can ? Qt::Checked : Qt::Unchecked);
			QFont font = item->font();
			font.setBold(can);
			item->setFont(font);
		}
		m_list->blockSignals(false);
		m_bring->setEnabled(earlier > 0);
		if (m_folders.isEmpty())
			m_status->setText(tr("There are no games in %1. Add the folder of a game made earlier, or the folder that holds them.")
								  .arg(QDir::toNativeSeparators(QDir(m_kit.root).filePath(QStringLiteral("games")))));
		else if (earlier == 0)
			m_status->setText(tr("Every game listed is up to date."));
		else if (earlier == 1)
			m_status->setText(tr("One game can be brought up to date."));
		else
			m_status->setText(tr("%1 games can be brought up to date.").arg(earlier));
	}

	void UpdateDialog::BringTicked()
	{
		QStringList ticked;
		for (int i = 0; i < m_list->count(); i++)
		{
			if (m_list->item(i)->checkState() == Qt::Checked)
				ticked.push_back(m_list->item(i)->data(Qt::UserRole).toString());
		}
		int done = 0, failed = 0;
		QGuiApplication::setOverrideCursor(Qt::WaitCursor);
		setEnabled(false);
		for (const QString& folder : ticked)
		{
			const GameFolder game = ReadGameFolder(m_kit.runtime, m_kit.toolchain, folder);
			m_status->setText(tr("Bringing %1 up to date...").arg(game.title));
			QApplication::processEvents();
			QString error;
			const bool ok = BringUpToDate(InputFor(m_kit, game), &error);
			const qsizetype at = m_folders.indexOf(folder);
			if (at >= 0)
				m_said[at] = ok ? tr("Brought up to date: it compiles its game again before it next starts.") : tr("Not brought up to date: %1").arg(error);
			(ok ? done : failed)++;
		}
		setEnabled(true);
		QGuiApplication::restoreOverrideCursor();
		Refresh();
		if (failed == 0 && done == 1)
			m_status->setText(tr("One game brought up to date. It compiles itself again before it next starts."));
		else if (failed == 0)
			m_status->setText(tr("%1 games brought up to date. Each compiles itself again before it next starts.").arg(done));
		else
			m_status->setText(tr("%1 brought up to date, %2 not: the list says why. Bring those up to date again once that is put right.")
								  .arg(done)
								  .arg(failed));
	}

	void UpdateDialog::Check(const QString& shots, bool automatic)
	{
		QTimer::singleShot(700, this, [this, shots, automatic]() {
			if (!shots.isEmpty())
			{
				QDir().mkpath(shots);
				grab().save(QDir(shots).filePath(QStringLiteral("updates-1.png")));
			}
			if (!automatic)
				return;
			BringTicked();
			QApplication::processEvents();
			if (!shots.isEmpty())
				grab().save(QDir(shots).filePath(QStringLiteral("updates-2.png")));
			accept();
		});
	}
} // namespace ps2n::wizard
