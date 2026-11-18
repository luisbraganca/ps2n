// SPDX-License-Identifier: GPL-3.0-or-later

#include "wizard_window.h"

#include "game_settings.h"
#include "package.h"
#include "patches.h"
#include "pipeline.h"

#include "Settings/SettingsWindow.h"

#include "common/FileSystem.h"
#include "common/Path.h"

#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/ps2/BiosTools.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWizardPage>

#include <algorithm>
#include <memory>
#include <optional>

namespace ps2n::wizard
{
	namespace
	{
		enum PageId : int
		{
			kGamePage,
			kBiosPage,
			kSettingsPage,
			kCompilePage,
			kPackagePage,
		};

		QString Html(const QString& text)
		{
			return text.toHtmlEscaped();
		}

		// A path chooser: a line to type or paste a path into, and Browse.
		QHBoxLayout* PathRow(QWidget* parent, QLineEdit** line, QPushButton** browse)
		{
			auto* row = new QHBoxLayout;
			*line = new QLineEdit(parent);
			*browse = new QPushButton(QObject::tr("Browse..."), parent);
			row->addWidget(*line, 1);
			row->addWidget(*browse);
			return row;
		}

		// The game's own data folder, where the runtime will read it (-data), its
		// patch file taken from PCSX2's archive into it, and its own settings
		// file, made with PS2Native's per-game defaults the first time
		// (host/game_settings.h): the frame-rate group enabled, with the EE
		// overclock the groups applied ask for. Returns the settings file, and
		// in *note what was done about the patches.
		std::unique_ptr<INISettingsInterface> SetUpGameSettings(State& s, QString* note)
		{
			const QString data = QDir(s.folder).filePath(QStringLiteral("data"));
			QDir().mkpath(data);
			EmuConfig.CustomDataPath = Path::RealPath(data.toStdString());
			EmuFolders::SetDataDirectory(nullptr);
			{
				auto lock = Host::GetSettingsLock();
				EmuFolders::LoadConfig(*Host::Internal::GetBaseSettingsLayer());
			}
			EmuFolders::EnsureFoldersExist();

			std::optional<ps2n::game::PatchFile> patches;
			if (QFileInfo::exists(s.patch_archive))
			{
				std::string error;
				patches = ps2n::game::ReadPatchFile(s.patch_archive.toStdString(), s.entry.serial, s.entry.crc, &error);
				if (patches)
				{
					FileSystem::WriteStringToFile(Path::Combine(EmuFolders::Patches, patches->name).c_str(), patches->text);
					*note = QObject::tr("The game's patches are PCSX2's, from %1.").arg(QDir::toNativeSeparators(s.patch_archive));
				}
				else
				{
					*note = QObject::tr("PCSX2's archive of patches has none for this game.");
				}
			}
			else
			{
				*note = QObject::tr("PCSX2 is not installed here, and PS2Native comes without PCSX2's patches: without them the "
									"game plays as its disc has it, 4:3. Fetch them, or choose PCSX2's patches.zip.");
			}

			const std::string path = VMManager::GetGameSettingsPath(s.entry.serial, s.entry.crc);
			auto sif = std::make_unique<INISettingsInterface>(path);
			if (FileSystem::FileExists(path.c_str()))
			{
				sif->Load();
			}
			else if (patches)
			{
				std::vector<std::string> applied;
				if (std::find(patches->groups.begin(), patches->groups.end(), ps2n::game::kWidescreenGroup) != patches->groups.end())
					applied.emplace_back(ps2n::game::kWidescreenGroup);
				if (const std::optional<std::string> fps = ps2n::game::FrameRateGroup(*patches))
				{
					sif->AddToStringList("Patches", "Enable", fps->c_str());
					applied.push_back(*fps);
				}
				int asked = 0;
				for (const std::string& group : applied)
					asked = std::max(asked, ps2n::game::OverclockAsked(patches->group_lines.at(group)));
				if (asked != 0)
					sif->SetIntValue("EmuCore/Speedhacks", "EECycleRate", asked);
				sif->Save();
			}
			return sif;
		}

		// A .cue sheet stands for the image its first FILE line names, beside it
		// (the data track's): PCSX2 reads the image, not the sheet. Any other file
		// stands for itself. Empty, with *error, when the sheet names none there.
		QString ImageOf(const QString& path, QString* error)
		{
			if (!path.endsWith(QStringLiteral(".cue"), Qt::CaseInsensitive))
				return path;
			QFile sheet(path);
			if (!sheet.open(QIODevice::ReadOnly | QIODevice::Text))
			{
				*error = QObject::tr("The .cue sheet %1 cannot be read.").arg(QDir::toNativeSeparators(path));
				return QString();
			}
			static const QRegularExpression file_line(QStringLiteral("^\\s*FILE\\s+(?:\"([^\"]+)\"|(\\S+))"),
				QRegularExpression::CaseInsensitiveOption);
			while (!sheet.atEnd())
			{
				const QRegularExpressionMatch m = file_line.match(QString::fromUtf8(sheet.readLine()));
				if (!m.hasMatch())
					continue;
				const QString named = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
				const QString image = QFileInfo(path).dir().filePath(named);
				if (!QFileInfo::exists(image))
				{
					*error = QObject::tr("The .cue sheet names %1, which is not beside it.").arg(named);
					return QString();
				}
				return QDir::toNativeSeparators(QFileInfo(image).absoluteFilePath());
			}
			*error = QObject::tr("The .cue sheet names no file.");
			return QString();
		}

		class Page : public QWizardPage
		{
		public:
			explicit Page(WizardWindow* window)
				: m_window(window)
			{
			}

		protected:
			State& s() const { return m_window->state(); }
			WizardWindow* m_window;
		};

		// 1. The game.
		class GamePage final : public Page
		{
		public:
			explicit GamePage(WizardWindow* window)
				: Page(window)
			{
				setTitle(tr("The game"));
				setSubTitle(tr("Choose the game's disc image, of your own disc: an .iso, .chd, .cso or .bin file (or the .cue "
							   "beside a .bin), any that PCSX2 plays. PS2Native recognizes it with PCSX2's own database, and "
							   "reads it where it is."));
				auto* layout = new QVBoxLayout(this);
				QPushButton* browse;
				layout->addLayout(PathRow(this, &m_path, &browse));
				m_found = new QLabel(this);
				m_found->setTextFormat(Qt::RichText);
				m_found->setWordWrap(true);
				layout->addWidget(m_found);
				// Its executables, each compiled unless left out (docs/adr/0033): the
				// one the disc boots always.
				m_executables_label = new QLabel(tr("Its executables, each compiled for this computer. One you leave out runs "
													"interpreted, slower, until the game has compiled what a session met of it."),
					this);
				m_executables_label->setWordWrap(true);
				layout->addWidget(m_executables_label);
				m_executables = new QListWidget(this);
				m_executables->setSelectionMode(QAbstractItemView::NoSelection);
				layout->addWidget(m_executables);
				ShowExecutables(false);
				auto* form = new QFormLayout;
				m_title = new QLineEdit(this);
				form->addRow(tr("Its name:"), m_title);
				m_folder = new QLabel(this);
				m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
				form->addRow(tr("Its folder:"), m_folder);
				layout->addLayout(form);
				layout->addStretch(1);

				connect(browse, &QPushButton::clicked, this, [this]() {
					const QString file = QFileDialog::getOpenFileName(this, tr("The game's disc image"), QString(),
						tr("Disc images (*.iso *.chd *.cso *.zso *.bin *.img *.mdf *.gz *.cue);;All files (*.*)"));
					if (!file.isEmpty())
					{
						m_path->setText(QDir::toNativeSeparators(file));
						Recognize();
					}
				});
				connect(m_path, &QLineEdit::editingFinished, this, [this]() { Recognize(); });
				connect(m_title, &QLineEdit::textChanged, this, [this]() { UpdateFolder(); });
				connect(m_executables, &QListWidget::itemChanged, this, [this]() { TakeExecutables(); });
			}

			void SetImage(const QString& image)
			{
				m_path->setText(QDir::toNativeSeparators(image));
				Recognize();
			}

			bool isComplete() const override { return s().recognized && !s().title.isEmpty(); }

		private:
			void Recognize()
			{
				const QString chosen = m_path->text().trimmed();
				if (chosen == m_chosen && s().recognized)
					return;
				m_chosen = chosen;
				s().recognized = false;
				s().executables.clear();
				ShowExecutables(false);

				// The image itself, for a .cue sheet the one it names.
				QString error;
				const QString path = ImageOf(chosen, &error);
				s().image = path;
				if (!error.isEmpty())
				{
					m_found->setText(QStringLiteral("<b>%1</b>").arg(Html(error)));
					emit completeChanged();
					return;
				}

				GameList::Entry entry;
				if (path.isEmpty() || !GameList::PopulateEntryFromPath(path.toStdString(), &entry) ||
					entry.type != GameList::EntryType::PS2Disc)
				{
					m_found->setText(tr("<b>This is not a PS2 game's disc image that PCSX2 recognizes.</b>"));
					emit completeChanged();
					return;
				}

				// Its executables: the one it boots, and those it loads itself, read by
				// the runtime through PCSX2's own readers (ps2n-host -disc-executables).
				QProcess reader;
				reader.setProcessChannelMode(QProcess::MergedChannels);
				reader.start(QDir(s().kit.runtime).filePath(QStringLiteral("ps2n-host.exe")),
					{QStringLiteral("-disc-executables"), QDir::toNativeSeparators(path)});
				const bool ran = reader.waitForFinished(60000) && reader.exitStatus() == QProcess::NormalExit && reader.exitCode() == 0;
				const QString listed = QString::fromUtf8(reader.readAll());
				QString boot;
				QStringList loaded;
				for (const QString& line : listed.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
				{
					const QString l = line.trimmed();
					if (l.startsWith(QStringLiteral("boot ")))
						boot = l.mid(5);
					else if (l.startsWith(QStringLiteral("loads ")))
						loaded.push_back(l.mid(6));
				}
				if (!ran || boot.isEmpty())
				{
					m_found->setText(tr("<b>%1 is recognized, but its executables could not be read from the image.</b><br>%2")
										 .arg(Html(QString::fromStdString(entry.title)), Html(listed.trimmed())));
					emit completeChanged();
					return;
				}

				s().entry = entry;
				m_executables->blockSignals(true);
				m_executables->clear();
				const QStringList executables = QStringList{boot} + loaded;
				for (qsizetype i = 0; i < executables.size(); i++)
				{
					auto* item = new QListWidgetItem(i == 0 ? tr("%1, which the disc boots").arg(executables[i]) :
															  tr("%1, which the game loads itself").arg(executables[i]),
						m_executables);
					item->setData(Qt::UserRole, executables[i]);
					item->setCheckState(Qt::Checked);
					// The one the disc boots is always compiled.
					item->setFlags(i == 0 ? Qt::ItemIsEnabled : (Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
				}
				m_executables->blockSignals(false);
				const int rows = static_cast<int>(std::min<qsizetype>(executables.size(), 6));
				m_executables->setFixedHeight(m_executables->sizeHintForRow(0) * rows + 2 * m_executables->frameWidth() + 2);
				ShowExecutables(true);
				TakeExecutables();
				s().recognized = true;
				m_found->setText(tr("<p><b>%1</b><br>Serial %2, CRC %3, %4.<br>PCSX2's compatibility: %5.</p>")
									 .arg(Html(QString::fromStdString(entry.title)), Html(QString::fromStdString(entry.serial)),
										 QStringLiteral("%1").arg(entry.crc, 8, 16, QLatin1Char('0')).toUpper(),
										 Html(QString::fromUtf8(GameList::RegionToString(entry.region, true))),
										 Html(QString::fromUtf8(GameList::EntryCompatibilityRatingToString(entry.compatibility_rating, true)))) +
								 (path != chosen ? tr("<p>Its image, as the .cue sheet names it: %1</p>").arg(Html(path)) : QString()));
				m_title->setText(QString::fromStdString(entry.title));
				UpdateFolder();
			}

			// Those kept in the list, the one the disc boots first.
			void TakeExecutables()
			{
				QStringList kept;
				for (int i = 0; i < m_executables->count(); i++)
				{
					const QListWidgetItem* item = m_executables->item(i);
					if (item->checkState() == Qt::Checked)
						kept.push_back(item->data(Qt::UserRole).toString());
				}
				s().executables = kept;
			}

			void ShowExecutables(bool shown)
			{
				m_executables_label->setVisible(shown);
				m_executables->setVisible(shown);
			}

			void UpdateFolder()
			{
				s().title = m_title->text().trimmed();
				s().folder = QDir(s().kit.root).filePath(QStringLiteral("games/") + PlainName(s().title));
				m_folder->setText(QDir::toNativeSeparators(s().folder));
				emit completeChanged();
			}

			QLineEdit* m_path;
			QString m_chosen; // what m_path had when it was last recognized
			QLabel* m_found;
			QLabel* m_executables_label;
			QListWidget* m_executables;
			QLineEdit* m_title;
			QLabel* m_folder;
		};

		// 2. The BIOS.
		class BiosPage final : public Page
		{
		public:
			explicit BiosPage(WizardWindow* window)
				: Page(window)
			{
				setTitle(tr("The BIOS"));
				setSubTitle(tr("The game runs on a BIOS dumped from your own console. It is read where it is, and nothing "
							   "made from it may be given to anyone."));
				auto* layout = new QVBoxLayout(this);
				QPushButton* browse;
				layout->addLayout(PathRow(this, &m_path, &browse));
				m_found = new QLabel(this);
				m_found->setWordWrap(true);
				layout->addWidget(m_found);
				layout->addStretch(1);
				connect(browse, &QPushButton::clicked, this, [this]() {
					const QString file = QFileDialog::getOpenFileName(this, tr("The BIOS image"), QString(), tr("All files (*.*)"));
					if (!file.isEmpty())
					{
						m_path->setText(QDir::toNativeSeparators(file));
						Recognize();
					}
				});
				connect(m_path, &QLineEdit::editingFinished, this, [this]() { Recognize(); });
			}

			void SetBios(const QString& bios)
			{
				m_path->setText(QDir::toNativeSeparators(bios));
				Recognize();
			}

			void initializePage() override
			{
				if (m_path->text().isEmpty())
				{
					const QString last = QSettings(QStringLiteral("PS2Native"), QStringLiteral("Wizard")).value(QStringLiteral("bios")).toString();
					if (!last.isEmpty() && QFileInfo::exists(last))
						SetBios(last);
				}
			}

			bool isComplete() const override { return !s().bios.isEmpty(); }

		private:
			void Recognize()
			{
				const QString path = m_path->text().trimmed();
				s().bios.clear();
				u32 version = 0, region = 0;
				std::string description, zone;
				if (path.isEmpty() || !IsBIOS(path.toStdString().c_str(), version, description, region, zone))
				{
					m_found->setText(tr("This is not a PS2 BIOS image."));
					emit completeChanged();
					return;
				}
				s().bios = path;
				QSettings(QStringLiteral("PS2Native"), QStringLiteral("Wizard")).setValue(QStringLiteral("bios"), path);
				m_found->setText(tr("%1 (%2).").arg(QString::fromStdString(description), QString::fromStdString(zone)));
				emit completeChanged();
			}

			QLineEdit* m_path;
			QLabel* m_found;
		};

		// 3. The settings: PCSX2's own pages for the game (docs/adr/0032).
		class SettingsPage final : public Page
		{
		public:
			explicit SettingsPage(WizardWindow* window)
				: Page(window)
			{
				setTitle(tr("The settings"));
				setSubTitle(tr("PCSX2's own settings for this game, fixed in its folder when it is made. Each starts at PCSX2's "
							   "default, but for PS2Native's: Vulkan, three times the console's resolution, a 16:9 picture, "
							   "the widescreen and frame-rate patches. They can all be accepted as they are."));
				m_layout = new QVBoxLayout(this);
				auto* row = new QHBoxLayout;
				m_note = new QLabel(this);
				m_note->setWordWrap(true);
				row->addWidget(m_note, 1);
				// PCSX2's archive of patches where PCSX2 is not installed, fetched
				// at the user's word or given (patches.h).
				m_fetch = new QPushButton(tr("Fetch PCSX2's patches"), this);
				m_fetch->setToolTip(tr("From PCSX2's patches project on GitHub (%1): the archive PCSX2 itself comes with, kept in "
									   "PS2Native's folder for the games you make after.")
										.arg(QString::fromLatin1(patches::kUrl)));
				row->addWidget(m_fetch);
				m_give = new QPushButton(tr("Choose patches.zip..."), this);
				m_give->setToolTip(tr("PCSX2's archive of patches from elsewhere (PCSX2 keeps it in its resources folder)."));
				row->addWidget(m_give);
				auto* defaults = new QPushButton(tr("Back to the defaults"), this);
				row->addWidget(defaults);
				m_layout->addLayout(row);
				connect(defaults, &QPushButton::clicked, this, [this]() {
					QFile::remove(QString::fromStdString(VMManager::GetGameSettingsPath(s().entry.serial, s().entry.crc)));
					Build();
				});
				connect(m_fetch, &QPushButton::clicked, this, [this]() { FetchPatches(); });
				connect(m_give, &QPushButton::clicked, this, [this]() {
					const QString file = QFileDialog::getOpenFileName(this, tr("PCSX2's archive of patches"), QString(),
						tr("PCSX2's archive of patches (patches.zip);;Zip archives (*.zip)"));
					if (file.isEmpty())
						return;
					QString error;
					if (!patches::Keep(file, KeptArchive(), &error))
					{
						m_note->setText(QStringLiteral("<b>%1</b>").arg(Html(error)));
						return;
					}
					s().patch_archive = KeptArchive();
					Build();
				});
			}

			void initializePage() override
			{
				Build();
				// For checks: as if the user had pressed Fetch.
				if (s().fetch_patches_unasked && !QFileInfo::exists(s().patch_archive))
					FetchPatches();
			}

			void cleanupPage() override
			{
				delete m_pages;
				m_pages = nullptr;
			}

			bool isComplete() const override { return !m_fetching; }

		private:
			QString KeptArchive() const { return QDir(s().kit.root).filePath(QStringLiteral("patches/patches.zip")); }

			void FetchPatches()
			{
				m_fetching = true;
				m_fetch->setEnabled(false);
				m_give->setEnabled(false);
				m_note->setText(tr("Fetching PCSX2's archive of patches from %1 ...").arg(QString::fromLatin1(patches::kUrl)));
				emit completeChanged();
				patches::Fetch(this, KeptArchive(), [this](bool ok, const QString& error) {
					m_fetching = false;
					m_fetch->setEnabled(true);
					m_give->setEnabled(true);
					if (ok)
					{
						s().patch_archive = KeptArchive();
						Build();
					}
					else
					{
						m_note->setText(QStringLiteral("<b>%1</b>").arg(Html(error)));
					}
					emit completeChanged();
				});
			}

			void Build()
			{
				delete m_pages;
				m_pages = nullptr;
				QString note;
				std::unique_ptr<INISettingsInterface> sif = SetUpGameSettings(s(), &note);
				m_note->setText(note);
				const bool have_archive = QFileInfo::exists(s().patch_archive);
				m_fetch->setVisible(!have_archive);
				m_give->setVisible(!have_archive);
				const QString file = QString::fromStdString(std::string(Path::GetFileName(sif->GetFileName())));
				m_pages = new SettingsWindow(std::move(sif), &s().entry, s().entry.serial, s().entry.crc, file);
				// The wizard's own buttons lead on: PCSX2's close the window,
				// copy settings of all games, or open a new window.
				for (const char* name : {"closeButton", "copyGlobalSettingsButton", "clearGameSettingsButton"})
				{
					if (QPushButton* button = m_pages->findChild<QPushButton*>(QString::fromLatin1(name)))
						button->hide();
				}
				m_layout->addWidget(m_pages, 1);
			}

			QVBoxLayout* m_layout;
			QLabel* m_note;
			QPushButton* m_fetch;
			QPushButton* m_give;
			bool m_fetching = false;
			SettingsWindow* m_pages = nullptr;
		};

		// 4. The recompile.
		class CompilePage final : public Page
		{
		public:
			explicit CompilePage(WizardWindow* window)
				: Page(window)
			{
				setTitle(tr("Recompiling"));
				setSubTitle(tr("The game's code and the BIOS's are translated and compiled for this computer, with the "
							   "compiler that came with PS2Native. It takes a few minutes."));
				auto* layout = new QVBoxLayout(this);
				m_stage = new QLabel(this);
				m_stage->setWordWrap(true);
				layout->addWidget(m_stage);
				m_bar = new QProgressBar(this);
				layout->addWidget(m_bar);
				m_log = new QPlainTextEdit(this);
				m_log->setReadOnly(true);
				m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
				layout->addWidget(m_log, 1);
				m_pipeline = new Pipeline(this);
				connect(m_pipeline, &Pipeline::stage, this, [this](const QString& what) {
					m_stage->setText(what);
					m_log->appendPlainText(what);
				});
				connect(m_pipeline, &Pipeline::progress, this, [this](int done, int total) {
					m_bar->setMaximum(total);
					m_bar->setValue(done);
				});
				connect(m_pipeline, &Pipeline::line, this, [this](const QString& text) { m_log->appendPlainText(text); });
				connect(m_pipeline, &Pipeline::finished, this, [this](bool ok, const QString& error) {
					m_ok = ok;
					m_failed = !ok;
					const QString took = tr("%1 seconds").arg(m_started.secsTo(QDateTime::currentDateTime()));
					m_stage->setText(ok ? tr("<b>Done</b>, in %1.").arg(took) : tr("<b>It did not work:</b> %1").arg(Html(error)));
					m_log->appendPlainText(ok ? tr("Done, in %1.").arg(took) : error);
					emit completeChanged();
				});
			}

			// For checks (--auto): the recompile ended, and not well.
			bool Failed() const { return m_failed; }

			void initializePage() override
			{
				m_ok = false;
				m_failed = false;
				m_log->clear();
				m_bar->setValue(0);
				m_started = QDateTime::currentDateTime();
				PipelineInput input;
				input.toolchain = s().kit.toolchain;
				input.runtime = s().kit.runtime;
				input.image = s().image;
				input.bios = s().bios;
				input.executables = s().executables;
				input.folder = s().folder;
				m_pipeline->start(input);
			}

			void cleanupPage() override
			{
				if (m_pipeline->running())
					m_pipeline->cancel();
			}

			bool isComplete() const override { return m_ok; }

		private:
			QLabel* m_stage;
			QProgressBar* m_bar;
			QPlainTextEdit* m_log;
			Pipeline* m_pipeline;
			QDateTime m_started;
			bool m_ok = false;
			bool m_failed = false;
		};

		// 5. The game's folder.
		class PackagePage final : public Page
		{
		public:
			explicit PackagePage(WizardWindow* window)
				: Page(window)
			{
				setTitle(tr("The game's folder"));
				setSubTitle(tr("The game is made into a folder that plays it."));
				auto* layout = new QVBoxLayout(this);
				m_result = new QLabel(this);
				m_result->setWordWrap(true);
				m_result->setTextFormat(Qt::RichText);
				m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
				layout->addWidget(m_result);
				auto* warning = new QLabel(tr("<p><b>This folder is yours alone.</b> It holds code translated from your game and from "
											  "your console's BIOS: do not give it to anyone, nor put it where others can take it.</p>"),
					this);
				warning->setWordWrap(true);
				layout->addWidget(warning);
				m_play = new QCheckBox(tr("Play it now"), this);
				m_play->setChecked(true);
				layout->addWidget(m_play);
				layout->addStretch(1);
				setFinalPage(true);
			}

			void initializePage() override
			{
				PackageInput input{s().kit.runtime, s().folder, s().title, s().image, s().bios, s().kit.toolchain, s().executables};
				QString error;
				m_ok = Package(input, &error);
				m_launcher = LauncherPath(input);
				m_result->setText(m_ok ? tr("<p>The game is in <b>%1</b>.</p><p>Start it with <b>%2</b> there: double-click it, or make "
											"a shortcut to it.</p>")
											 .arg(Html(QDir::toNativeSeparators(s().folder)), Html(QFileInfo(m_launcher).fileName())) :
										 tr("<b>The folder could not be made:</b> %1").arg(Html(error)));
				m_play->setEnabled(m_ok);
				emit completeChanged();
			}

			bool isComplete() const override { return m_ok; }

			bool validatePage() override
			{
				if (m_ok && m_play->isChecked() && m_play->isEnabled())
					QProcess::startDetached(m_launcher, {}, s().folder);
				return true;
			}

			void SetPlay(bool play) { m_play->setChecked(play); }

		private:
			QLabel* m_result;
			QCheckBox* m_play;
			QString m_launcher;
			bool m_ok = false;
		};
	} // namespace

	WizardWindow::WizardWindow(Kit kit, QWidget* parent)
		: QWizard(parent)
	{
		m_state.kit = std::move(kit);
		// PCSX2's archive of patches: an installed PCSX2's, or else the one this
		// folder keeps once fetched or given (patches.h).
		const char* program_files = std::getenv("ProgramFiles");
		const QString installed =
			QDir(QString::fromLocal8Bit(program_files ? program_files : "C:\\Program Files")).filePath(QStringLiteral("PCSX2/resources/patches.zip"));
		m_state.patch_archive =
			QFileInfo::exists(installed) ? installed : QDir(m_state.kit.root).filePath(QStringLiteral("patches/patches.zip"));
		setWindowTitle(QStringLiteral("PS2Native"));
		setWizardStyle(QWizard::ModernStyle);
		setOption(QWizard::NoBackButtonOnLastPage, true);
		setOption(QWizard::NoCancelButtonOnLastPage, true);
		setPage(kGamePage, new GamePage(this));
		setPage(kBiosPage, new BiosPage(this));
		setPage(kSettingsPage, new SettingsPage(this));
		setPage(kCompilePage, new CompilePage(this));
		setPage(kPackagePage, new PackagePage(this));
		resize(1100, 820);
	}

	void WizardWindow::WithoutPcsx2(bool fetch_patches)
	{
		m_state.patch_archive = QDir(m_state.kit.root).filePath(QStringLiteral("patches/patches.zip"));
		m_state.fetch_patches_unasked = fetch_patches;
	}

	void WizardWindow::Prepare(const QString& image, const QString& bios, const QString& shots, bool automatic, int until)
	{
		if (!image.isEmpty())
			static_cast<GamePage*>(page(kGamePage))->SetImage(image);
		if (!bios.isEmpty())
			static_cast<BiosPage*>(page(kBiosPage))->SetBios(bios);
		if (automatic)
			static_cast<PackagePage*>(page(kPackagePage))->SetPlay(false);
		if (shots.isEmpty() && !automatic)
			return;

		// Each page, once complete (the recompile when it has ended), saved as a
		// picture, and with `automatic` left for the next.
		auto* timer = new QTimer(this);
		auto shot_taken = std::make_shared<int>(-1);
		connect(timer, &QTimer::timeout, this, [this, timer, shots, automatic, until, shot_taken]() {
			QWizardPage* page = currentPage();
			// A recompile that failed ends a check, its page saved, with exit code 1.
			if (automatic && currentId() == kCompilePage && static_cast<CompilePage*>(page)->Failed())
			{
				timer->stop();
				if (!shots.isEmpty())
					grab().save(QDir(shots).filePath(QStringLiteral("step-4-failed.png")));
				QApplication::exit(1);
				return;
			}
			if (!page || !page->isComplete())
				return;
			if (*shot_taken != currentId())
			{
				*shot_taken = currentId();
				if (!shots.isEmpty())
				{
					QDir().mkpath(shots);
					grab().save(QDir(shots).filePath(QStringLiteral("step-%1.png").arg(currentId() + 1)));
					// The settings: each category of PCSX2's pages.
					if (currentId() == kSettingsPage)
					{
						if (QListWidget* categories = page->findChild<QListWidget*>(QStringLiteral("settingsCategory")))
						{
							for (int i = 0; i < categories->count(); i++)
							{
								categories->setCurrentRow(i);
								QApplication::processEvents();
								const QString name = categories->item(i)->text().replace(QLatin1Char(' '), QLatin1Char('-')).remove(QLatin1Char('&'));
								grab().save(QDir(shots).filePath(QStringLiteral("step-3-%1-%2.png").arg(i, 2, 10, QLatin1Char('0')).arg(name)));
							}
							categories->setCurrentRow(0);
						}
					}
				}
			}
			if (!automatic)
				return;
			if (currentId() == kPackagePage || currentId() + 1 >= until)
			{
				timer->stop();
				accept();
				return;
			}
			next();
		});
		timer->start(500);
	}
} // namespace ps2n::wizard
