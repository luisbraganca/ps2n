// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-remake: a game's folder compiling what its sessions met for the first
// time (docs/phase4-proposal.md, step P4.6; docs/adr/0034, docs/adr/0040). The
// game's launcher starts it when a session has ended, and before the game
// starts (--before-play; host/launcher.cpp, wizard/launcher_main.cpp). It runs
// the wizard's recompile (pipeline.h) with the folder's own toolchain, as
// make.txt in the folder says the game is made, keeping what the last compile
// left and compiling only what changed since; and it links the four modules
// into work/next-<time>, writing "ready" there last, which the launcher puts in
// place before the runtime loads them.
//
// When there is nothing new to compile it does nothing and shows nothing.
// Otherwise it compiles in a window of its own, in front, every processor at
// work, which says how far it has got (docs/adr/0040). "Later", or the window
// closed, stops it, what it compiled kept. The game never starts with anything
// left to compile: started, its launcher has this compile it first, in the
// same window, and plays only when that is done. work/remake.log says what it
// did.
//
// Once for a game, after a session of at least 3 minutes (docs/adr/0045), it
// then compiles the game for how its user plays it, in the same window: the
// modules compiled to count what of their code runs (work/count), the
// session's first 3 minutes run again with them without a window, which
// counts, and the modules compiled again with the counts (work/objects-profiled,
// then the folder's objects), the counts kept in captures/profile.profdata for
// every compile after. That never keeps the game from starting: Later puts it
// off to another session's end, and the game started meanwhile stops it
// (work/stop-profile), what it did kept.
//
//   ps2n-remake [--before-play] [<the game's folder>]      by default the folder it is in
//
// It ends with 0 when what there was is compiled, or there was nothing; 3 when
// it was left for later; 1 when it could not compile, its window having said
// why. Before the game starts, only 0 lets it start.

#include "pipeline.h"
#include "platform.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QLockFile>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QFont>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <functional>
#include <memory>

#ifdef _WIN32
#include <QtGui/private/qguiapplication_p.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
	// How the folder's modules are made (make.txt, which the wizard and
	// make-game.ps1 write): the game's title, its disc image and the BIOS image,
	// the executables compiled, the one the disc boots first; and whether PCSX2's
	// GameDB has the game played with FullVU0SyncHack (ADR 0046).
	struct Recipe
	{
		QString title;
		QString image;
		QString bios;
		QStringList executables;
		bool cop2FullSync = false;
	};

	bool ReadRecipe(const QDir& folder, Recipe* recipe, QString* error)
	{
		QFile file(folder.filePath(QStringLiteral("make.txt")));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			*error = QStringLiteral("%1 cannot be read").arg(QDir::toNativeSeparators(file.fileName()));
			return false;
		}
		const QString here = QDir::toNativeSeparators(folder.absolutePath());
		for (QString line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')))
		{
			line = line.trimmed();
			const qsizetype equals = line.indexOf(QLatin1Char('='));
			if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || equals < 0)
				continue;
			const QString key = line.left(equals).trimmed();
			QString value = line.mid(equals + 1).trimmed();
			value.replace(QStringLiteral("%GAME%"), here, Qt::CaseInsensitive);
			if (key == QStringLiteral("title"))
				recipe->title = value;
			else if (key == QStringLiteral("image"))
				recipe->image = value;
			else if (key == QStringLiteral("bios"))
				recipe->bios = value;
			else if (key == QStringLiteral("executable"))
				recipe->executables.push_back(value);
			else if (key == QStringLiteral("cop2-full-sync"))
				recipe->cop2FullSync = value == QStringLiteral("on");
		}
		if (recipe->image.isEmpty() || recipe->bios.isEmpty() || recipe->executables.isEmpty())
		{
			*error = QStringLiteral("%1 does not say how the game is made (image, bios, executable)").arg(QDir::toNativeSeparators(file.fileName()));
			return false;
		}
		if (recipe->title.isEmpty())
			recipe->title = folder.dirName();
		return true;
	}

	// The program's own icon, PS2Native's: on Windows the executable's
	// (assets/ps2n.rc), elsewhere a resource of Qt's (wizard/targets.cmake).
	QIcon OwnIcon()
	{
#ifdef _WIN32
		QIcon result;
		for (const int size : {GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXICON)})
		{
			if (const HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0)))
			{
				result.addPixmap(QPixmap::fromImage(QImage::fromHICON(icon)));
				DestroyIcon(icon);
			}
		}
		return result;
#else
		return QIcon(QStringLiteral(":/ps2native/icon.png"));
#endif
	}

	// When a session last wrote down what it met: a round begun before then
	// has not got it.
	QDateTime LastSession(const QDir& folder)
	{
		QDateTime last;
		QDirIterator it(folder.filePath(QStringLiteral("sessions")), {QStringLiteral("*capture.toml")}, QDir::Files,
			QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QDateTime written = QFileInfo(it.next()).lastModified();
			if (!last.isValid() || written > last)
				last = written;
		}
		return last;
	}

	// ---- How the user plays the game, counted (docs/adr/0045) -------------------

	// The session counted: its first 3 minutes at 60 frames a second, of one at
	// least that long.
	constexpr int kCountFrames = 10800;

	QString ReadText(const QString& path)
	{
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
	}

	// A line of a session's input.txt (host/session_record.h) that begins with
	// `key` and a space: the rest of it, or nothing.
	QString SessionLine(const QString& session, const QString& key)
	{
		const QRegularExpression line(QStringLiteral("^%1 +(.*)$").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption);
		const QRegularExpressionMatch match = line.match(ReadText(QDir(session).filePath(QStringLiteral("input.txt"))));
		return match.hasMatch() ? match.captured(1).trimmed() : QString();
	}

	// The newest session in the folder that ended and lasted kCountFrames frames
	// at least: the one counted. Nothing when there is none.
	QString CountableSession(const QDir& folder)
	{
		const QDir sessions(folder.filePath(QStringLiteral("sessions")));
		QStringList names = sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		std::reverse(names.begin(), names.end());
		for (const QString& name : names)
		{
			bool ok = false;
			const int frames = SessionLine(sessions.filePath(name), QStringLiteral("end")).toInt(&ok);
			if (ok && frames >= kCountFrames)
				return sessions.filePath(name);
		}
		return QString();
	}

	bool CopyInto(const QString& from, const QString& to)
	{
		QFile::remove(to);
		return QFile::copy(from, to);
	}

	// What the runtime is started with to run the session `session` again,
	// without a window, with the modules made to count (`modules`), writing
	// their counts to `counts`: the folder's own options (settings.txt, as its
	// launcher starts it), but those of a window and of where it keeps what it
	// plays with, which the run has copies of in `run` (PrepareRun). False,
	// with why, when settings.txt cannot be read.
	bool ReplayArguments(const QDir& folder, const QString& session, const QString& run, const QString& modules, const QString& counts,
		QStringList* arguments, QString* bios, QString* error)
	{
		const QString settings = folder.filePath(QStringLiteral("settings.txt"));
		QFile file(settings);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			*error = QObject::tr("%1 cannot be read.").arg(QDir::toNativeSeparators(settings));
			return false;
		}
		QStringList given;
		const QString here = QDir::toNativeSeparators(folder.absolutePath());
		for (QString line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')))
		{
			line = line.trimmed();
			if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
				continue;
			line.replace(QStringLiteral("%GAME%"), here, Qt::CaseInsensitive);
			given.push_back(line);
		}
		// A window's, and where the folder keeps what it plays with: the run has
		// copies of those, as the session began with them.
		const QStringList without_value = {QStringLiteral("-window"), QStringLiteral("-mute"), QStringLiteral("-osd")};
		const QStringList with_value = {QStringLiteral("-title"), QStringLiteral("-sessions"), QStringLiteral("-record"),
			QStringLiteral("-window-size"), QStringLiteral("-log"), QStringLiteral("-out"), QStringLiteral("-data"), QStringLiteral("-bios"),
			QStringLiteral("-upscale"), QStringLiteral("-frames"), QStringLiteral("-seconds"), QStringLiteral("-input"),
			QStringLiteral("-memcards"), QStringLiteral("-clock")};
		QStringList kept;
		bool renderer = false;
		for (qsizetype i = 0; i < given.size(); i++)
		{
			if (without_value.contains(given[i]))
				continue;
			if (with_value.contains(given[i]))
			{
				if (given[i] == QStringLiteral("-bios") && i + 1 < given.size())
					*bios = given[i + 1];
				i++;
				continue;
			}
			renderer |= given[i] == QStringLiteral("-renderer");
			kept.push_back(given[i]);
		}
		if (bios->isEmpty())
		{
			*error = QObject::tr("%1 names no BIOS image.").arg(QDir::toNativeSeparators(settings));
			return false;
		}
		const QDir r(run);
		*arguments = {QStringLiteral("-modules"), QDir::toNativeSeparators(modules), QStringLiteral("-module-counts"),
			QDir::toNativeSeparators(counts)};
		*arguments += kept;
		*arguments += {QStringLiteral("-bios"), QDir::toNativeSeparators(r.filePath(QStringLiteral("bios/") + QFileInfo(*bios).fileName())),
			QStringLiteral("-data"), QDir::toNativeSeparators(r.filePath(QStringLiteral("data"))), QStringLiteral("-log"),
			QDir::toNativeSeparators(r.filePath(QStringLiteral("core.log"))), QStringLiteral("-out"),
			QDir::toNativeSeparators(r.filePath(QStringLiteral("guest.txt"))), QStringLiteral("-memcards"),
			QDir::toNativeSeparators(r.filePath(QStringLiteral("memcards"))), QStringLiteral("-input"),
			QDir::toNativeSeparators(QDir(session).filePath(QStringLiteral("input.txt"))), QStringLiteral("-frames"),
			QString::number(kCountFrames), QStringLiteral("-seconds"), QStringLiteral("7200"), QStringLiteral("-upscale"), QStringLiteral("1"),
			QStringLiteral("-progress")};
		// The renderer a window has when the folder's options name none: a run
		// without one draws nothing, which not every game's drawing allows.
		if (!renderer)
			*arguments += {QStringLiteral("-renderer"), QStringLiteral("vulkan")};
		return true;
	}

	// The run's own copies of what the session began with, as a session run
	// again has them (tests/guest_test.cmake): the BIOS image with the images
	// beside it the session had (its bios-roms line) and the console's NVRAM as
	// it was; the memory cards; the game's own settings file and patch file.
	bool PrepareRun(const QString& session, const QString& bios, const QString& run, QString* error)
	{
		QDir(run).removeRecursively();
		const QDir r(run);
		for (const char* dir : {"bios", "memcards", "data/PCSX2/gamesettings", "data/PCSX2/patches"})
		{
			if (!QDir().mkpath(r.filePath(QString::fromLatin1(dir))))
			{
				*error = QObject::tr("Cannot make %1.").arg(QDir::toNativeSeparators(r.filePath(QString::fromLatin1(dir))));
				return false;
			}
		}
		const QFileInfo image(bios);
		const QString stem = image.completeBaseName();
		bool copied = CopyInto(bios, r.filePath(QStringLiteral("bios/") + image.fileName()));
		const QString roms = SessionLine(session, QStringLiteral("bios-roms"));
		const QStringList wanted = roms.split(QLatin1Char(','), Qt::SkipEmptyParts);
		for (const QFileInfo& beside : image.dir().entryInfoList(QDir::Files))
		{
			const QString kind = beside.suffix().toLower();
			if (beside.completeBaseName().compare(stem, Qt::CaseInsensitive) != 0 ||
				!(kind == QStringLiteral("rom1") || kind == QStringLiteral("rom2") || kind == QStringLiteral("erom")))
				continue;
			// Under the extension in lower case, the one the core asks for.
			if (kind != QStringLiteral("erom") && !roms.isEmpty() && !wanted.contains(kind))
				continue;
			copied &= CopyInto(beside.absoluteFilePath(), r.filePath(QStringLiteral("bios/%1.%2").arg(stem, kind)));
		}
		const QDir s(session);
		for (const char* extension : {"nvm", "mec"})
		{
			const QString nvram = s.filePath(QStringLiteral("%1.%2").arg(stem, QString::fromLatin1(extension)));
			if (QFileInfo::exists(nvram))
				copied &= CopyInto(nvram, r.filePath(QStringLiteral("bios/%1.%2").arg(stem, QString::fromLatin1(extension))));
		}
		for (const char* card : {"Mcd001.ps2", "Mcd002.ps2"})
		{
			if (QFileInfo::exists(s.filePath(QString::fromLatin1(card))))
				copied &= CopyInto(s.filePath(QString::fromLatin1(card)), r.filePath(QStringLiteral("memcards/") + QString::fromLatin1(card)));
		}
		for (const QString& ini : s.entryList({QStringLiteral("*.ini")}, QDir::Files))
			copied &= CopyInto(s.filePath(ini), r.filePath(QStringLiteral("data/PCSX2/gamesettings/") + ini));
		for (const QString& pnach : s.entryList({QStringLiteral("*.pnach")}, QDir::Files))
			copied &= CopyInto(s.filePath(pnach), r.filePath(QStringLiteral("data/PCSX2/patches/") + pnach));
		if (!copied)
		{
			*error = QObject::tr("Cannot copy what %1 began with into %2.").arg(QDir::toNativeSeparators(session), QDir::toNativeSeparators(run));
			return false;
		}
		return true;
	}

	// The window it compiles in (docs/adr/0040), shown when there is something
	// to compile: what it compiles and for what, how far it has got, and
	// "Later", which closing it is too. Once the compile has ended, done or
	// not, it says so, and closing it is just that.
	class CompileWindow : public QWidget
	{
	public:
		CompileWindow(const QString& title, const QIcon& icon)
		{
			setWindowTitle(title);
			setWindowIcon(icon);
			setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);

			m_heading = new QLabel(this);
			QFont larger = m_heading->font();
			larger.setPointSizeF(larger.pointSizeF() * 1.3);
			m_heading->setFont(larger);
			m_heading->setWordWrap(true);
			m_text = new QLabel(this);
			m_text->setWordWrap(true);
			m_bar = new QProgressBar(this);
			m_bar->setRange(0, 0); // moving until the files to compile are counted
			m_bar->setTextVisible(false);
			m_stage = new QLabel(this);
			m_note = new QLabel(this);
			m_note->setWordWrap(true);
			// What is said in passing, in a colour between the text's and the
			// window's, readable in a light style and a dark one.
			QPalette passing = palette();
			const QColor text = passing.color(QPalette::WindowText);
			const QColor back = passing.color(QPalette::Window);
			passing.setColor(QPalette::WindowText, QColor((text.red() * 2 + back.red()) / 3, (text.green() * 2 + back.green()) / 3,
													   (text.blue() * 2 + back.blue()) / 3));
			m_stage->setPalette(passing);
			m_note->setPalette(passing);
			m_button = new QPushButton(QObject::tr("Later"), this);
			m_button->setDefault(true);
			QObject::connect(m_button, &QPushButton::clicked, this, [this]() { close(); });

			auto* bottom = new QHBoxLayout();
			bottom->addWidget(m_note, 1);
			bottom->addWidget(m_button, 0, Qt::AlignBottom);
			auto* layout = new QVBoxLayout(this);
			layout->setSpacing(10);
			layout->addWidget(m_heading);
			layout->addWidget(m_text);
			layout->addSpacing(4);
			layout->addWidget(m_bar);
			layout->addWidget(m_stage);
			layout->addSpacing(6);
			layout->addLayout(bottom);
			setMinimumWidth(560);
		}

		// Later, or the window closed, while it compiles.
		std::function<void()> later;
		// The window closed once the compile has ended.
		std::function<void()> closed;

		void Compiling(const QString& heading, const QString& text, const QString& note)
		{
			m_heading->setText(heading);
			m_text->setText(text);
			m_note->setText(note);
		}
		void Stage(const QString& stage) { m_stage->setText(stage); }
		void Progress(int done, int total, const QString& format = QObject::tr("%v of %m files"))
		{
			if (m_ended || total <= 0)
				return;
			m_bar->setRange(0, total);
			m_bar->setValue(done);
			m_bar->setFormat(format);
			m_bar->setTextVisible(true);
		}
		// Moving, until there is something to count again.
		void Busy()
		{
			m_bar->setRange(0, 0);
			m_bar->setTextVisible(false);
		}
		void Ended(const QString& heading, const QString& text, bool done)
		{
			m_ended = true;
			m_heading->setText(heading);
			m_text->setText(text);
			m_stage->clear();
			m_note->clear();
			m_bar->setVisible(done);
			if (done)
			{
				m_bar->setRange(0, 1);
				m_bar->setValue(1);
				m_bar->setTextVisible(false);
			}
			m_button->setText(QObject::tr("Close"));
		}
		// Shown, in front, in the middle of the screen.
		void Bring()
		{
			if (!isVisible())
			{
				adjustSize();
				if (const QScreen* where = screen())
					move(where->availableGeometry().center() - rect().center());
				show();
			}
			raise();
			activateWindow();
		}

	protected:
		void closeEvent(QCloseEvent* event) override
		{
			event->accept();
			const std::function<void()>& then = m_ended ? closed : later;
			if (then)
				QTimer::singleShot(0, this, then);
		}

	private:
		QLabel* m_heading = nullptr;
		QLabel* m_text = nullptr;
		QProgressBar* m_bar = nullptr;
		QLabel* m_stage = nullptr;
		QLabel* m_note = nullptr;
		QPushButton* m_button = nullptr;
		bool m_ended = false;
	};
} // namespace

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("PS2Native"));
	QApplication::setQuitOnLastWindowClosed(false);
#ifdef _WIN32
	// Its window in front when it shows (docs/adr/0040), the user having just
	// closed the game or started it: Windows would otherwise leave a program
	// started by another behind the window in front, its button flashing.
	if (auto* windows = app.nativeInterface<QNativeInterface::Private::QWindowsApplication>())
		windows->setWindowActivationBehavior(QNativeInterface::Private::QWindowsApplication::AlwaysActivateWindow);
#endif

	bool before_play = false;
	QString given;
	const QStringList arguments = QCoreApplication::arguments();
	for (qsizetype i = 1; i < arguments.size(); i++)
	{
		if (arguments[i] == QStringLiteral("--before-play"))
			before_play = true;
		else
			given = arguments[i];
	}
	const QDir folder(QDir(given.isEmpty() ? QCoreApplication::applicationDirPath() : given).absolutePath());
	const QDir work(folder.filePath(QStringLiteral("work")));
	QDir().mkpath(work.path());

	// One at a time in a folder. After a session, one that finds another
	// compiling leaves at once: that one takes what the session met in a round
	// after. Before the game starts, it waits for the other, whose window says
	// what it does, and then sees whether anything is left to compile; one
	// compiling the game for how its user plays it is asked to stop first
	// (work/stop-profile), which it does at once: that never keeps the game from
	// starting (docs/adr/0045).
	const QString stop_profile = work.filePath(QStringLiteral("stop-profile"));
	QLockFile lock(work.filePath(QStringLiteral("remake.lock")));
	lock.setStaleLockTime(0);
	if (before_play)
	{
		if (!lock.tryLock(0))
		{
			QFile stop(stop_profile);
			if (stop.open(QIODevice::WriteOnly))
				stop.close();
			if (!lock.lock())
				return 1;
		}
		QFile::remove(stop_profile);
	}
	else if (!lock.tryLock(0))
	{
		return 0;
	}

	// What it does, for the user to read when it could not compile: without
	// the file it compiles all the same. Each run after the ones before (one
	// before the game starts, one after its session), the file started again
	// past a megabyte.
	QFile log(work.filePath(QStringLiteral("remake.log")));
	const bool logging =
		log.open(QIODevice::WriteOnly | QIODevice::Text | (log.size() > (1 << 20) ? QIODevice::Truncate : QIODevice::Append));
	const auto say = [&log, logging](const QString& text) {
		if (!logging)
			return;
		log.write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss  ")) + text + QLatin1Char('\n')).toUtf8());
		log.flush();
	};
	say(before_play ? QStringLiteral("before the game starts") : QStringLiteral("after a session"));

	// A folder that does not say how its modules are made (one older than its
	// own recompile) has nothing it can compile, so nothing to keep the game
	// from starting.
	Recipe recipe;
	QString error;
	if (!ReadRecipe(folder, &recipe, &error))
	{
		say(error);
		return 0;
	}

	const QString log_path = QDir::toNativeSeparators(QStringLiteral("work/remake.log"));
	CompileWindow window(recipe.title, OwnIcon());
	if (before_play)
	{
		window.Compiling(QObject::tr("Compiling what your last session met for the first time"),
			QObject::tr("%1 starts when it is done. Every processor is at work: it takes a few minutes.").arg(recipe.title),
			QObject::tr("Later: %1 does not start now, and compiles this first when it next starts.").arg(recipe.title));
	}
	else
	{
		window.Compiling(QObject::tr("Compiling what this session met for the first time"),
			QObject::tr("So that %1 runs it at full speed from its next start. Every processor is at work: it takes a few minutes.")
				.arg(recipe.title),
			QObject::tr("Later: %1 compiles this first when it next starts.").arg(recipe.title));
	}

	ps2n::wizard::Pipeline pipeline;
	QDateTime round_started;
	QString modules;
	bool stopped = false;

	// What a round compiles with: the folder's own toolchain, as make.txt says
	// the game is made, keeping what the last compile left.
	const auto input_for = [&](const QString& into) {
		ps2n::wizard::PipelineInput input;
		input.toolchain = folder.filePath(QStringLiteral("toolchain"));
		input.runtime = folder.path();
		input.image = recipe.image;
		input.bios = recipe.bios;
		input.executables = recipe.executables;
		input.folder = folder.path();
		input.modules = into;
		input.incremental = true;
		input.cop2_full_sync = recipe.cop2FullSync;
		return input;
	};
	// The folder a round links the modules into, work/next-<time>; what a round
	// that did not end left behind goes.
	const auto next_modules = [&]() {
		for (const QString& next : work.entryList({QStringLiteral("next-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
		{
			if (!QFileInfo::exists(QDir(work.filePath(next)).filePath(QStringLiteral("ready"))))
				QDir(work.filePath(next)).removeRecursively();
		}
		round_started = QDateTime::currentDateTime();
		modules = work.filePath(QStringLiteral("next-") + round_started.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
	};
	const auto round = [&]() {
		next_modules();
		pipeline.start(input_for(modules));
	};
	// The modules of the round ready for the launcher, which takes the newest
	// such folder. False when that cannot be written.
	const auto mark_ready = [&]() {
		QSaveFile ready(QDir(modules).filePath(QStringLiteral("ready")));
		QString names;
		for (const char* kind : {"game", "bios", "vu", "iop"})
			names += ps2n::wizard::platform::Module(QString::fromLatin1(kind)) + QLatin1Char('\n');
		return ready.open(QIODevice::WriteOnly) && ready.write(names.toUtf8()) >= 0 && ready.commit();
	};

	// ---- The game compiled for how its user plays it (docs/adr/0045) -------------
	const QDir count_dir(work.filePath(QStringLiteral("count")));
	const QString counted = count_dir.filePath(QStringLiteral("profile.profdata"));
	const QString count_failed = count_dir.filePath(QStringLiteral("failed.txt"));
	const QString own_profile = folder.filePath(QStringLiteral("captures/profile.profdata"));
	ps2n::wizard::Pipeline counting;
	ps2n::wizard::Pipeline profiled;
	QProcess* runner = nullptr;
	QProcess* merger = nullptr;
	QString counted_session;
	QString step; // "1 of 3: ", what the window says the step is
	bool profiling = false;
	QTimer stop_watch;

	// Once for a game, after a session: not when it has its counts, nor when
	// they could not be made; when a session was long enough, or the counts
	// were made before and not yet compiled with.
	const auto profile_wanted = [&]() {
		if (before_play || QFileInfo::exists(own_profile) || QFileInfo::exists(count_failed) || QFileInfo::exists(stop_profile))
			return false;
		return QFileInfo::exists(counted) || !CountableSession(folder).isEmpty();
	};
	const auto stop_profile_work = [&]() {
		stop_watch.stop();
		counting.cancel();
		profiled.cancel();
		if (runner)
			runner->kill();
		if (merger)
			merger->kill();
	};

	// Later: what it compiled so far stays (objects newer than their sources
	// are not compiled again), what it was linking goes with the round, and the
	// game compiles the rest before it next starts. Compiling the game for how
	// it is played, the same, but the game starts without it.
	window.later = [&]() {
		stopped = true;
		if (profiling)
		{
			say(QStringLiteral("left for later: compiling the game for how it is played, after another session"));
			stop_profile_work();
		}
		else
		{
			say(QStringLiteral("left for later: the game compiles it before it next starts"));
			pipeline.cancel();
		}
		lock.unlock();
		app.exit(3);
	};
	// The game started meanwhile.
	QObject::connect(&stop_watch, &QTimer::timeout, &app, [&]() {
		if (!profiling || stopped || !QFileInfo::exists(stop_profile))
			return;
		stopped = true;
		say(QStringLiteral("the game is starting: compiling it for how it is played is left for after another session"));
		stop_profile_work();
		window.hide();
		lock.unlock();
		app.exit(3);
	});
	// Ended: done, or not compiled, said in the window; the game starts when
	// it is done, before it starts.
	const auto ended = [&](int code) {
		lock.unlock();
		if (!window.isVisible())
		{
			app.exit(code);
			return;
		}
		window.closed = [&app, code]() { app.exit(code); };
		if (code == 0)
		{
			if (before_play)
			{
				app.exit(0);
				return;
			}
			window.Ended(QObject::tr("Compiled"), QObject::tr("%1's next start has what this session met.").arg(recipe.title), true);
			// Long enough to be read.
			QTimer::singleShot(3000, &app, [&app]() { app.exit(0); });
		}
		window.Bring();
	};
	const auto failed = [&](const QString& why) {
		say(QStringLiteral("not compiled: ") + why);
		window.Ended(QObject::tr("It could not be compiled"),
			QObject::tr("%1\n\n%2 in the game's folder says what was done. %3 does not start before it has compiled what its sessions "
						"met for the first time.")
				.arg(why, log_path, recipe.title),
			false);
		window.Bring();
		ended(1);
	};

	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::newCode, &app, [&]() {
		say(QStringLiteral("new code met: compiling it into %1").arg(QDir::toNativeSeparators(modules)));
		window.Bring();
	});
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::stage, &app, [&](const QString& what) {
		say(what);
		window.Stage(what);
	});
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::progress, &app, [&](int done, int total) { window.Progress(done, total); });
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::line, &app, [&](const QString& text) { say(text); });
	// ---- The game compiled for how its user plays it (docs/adr/0045) -------------
	// Not compiled so: the game plays as it is, and the folder does not try again.
	const auto profile_failed = [&](const QString& why) {
		say(QStringLiteral("not compiled for how the game is played: ") + why);
		stopped = true;
		stop_profile_work();
		profiling = false;
		QDir().mkpath(count_dir.path());
		QSaveFile marker(count_failed);
		if (marker.open(QIODevice::WriteOnly))
		{
			marker.write((why + QLatin1Char('\n')).toUtf8());
			marker.commit();
		}
		lock.unlock();
		window.Ended(QObject::tr("It could not be compiled for how you play it"),
			QObject::tr("%1\n\n%2 plays as it is, and does not try this again. %3 in the game's folder says what was done.")
				.arg(why, recipe.title, log_path),
			false);
		window.closed = [&app]() { app.exit(0); };
		window.Bring();
	};

	std::function<void()> count_compile, count_run, count_merge, compile_with_counts;
	const auto start_profile = [&]() {
		profiling = true;
		window.Compiling(QObject::tr("Compiling %1 for how you play it").arg(recipe.title),
			QObject::tr("Once for this game: the first 3 minutes of a session of yours are run again out of sight, counting what the "
						"game's code does most, and the game is compiled again with what was counted. Every processor is at work: it "
						"takes several minutes."),
			QObject::tr("Later: %1 starts as it is, and does this after another session.").arg(recipe.title));
		window.Busy();
		window.Bring();
		stop_watch.start(500);
		if (QFileInfo::exists(counted))
			compile_with_counts();
		else
			count_compile();
	};

	// 1: the modules compiled to count, with objects of their own.
	count_compile = [&]() {
		counted_session = CountableSession(folder);
		if (counted_session.isEmpty())
		{
			profile_failed(QObject::tr("No session lasted 3 minutes."));
			return;
		}
		say(QStringLiteral("compiling the game for how it is played, counting on the session %1: the modules made to count")
				.arg(QDir(counted_session).dirName()));
		step = QObject::tr("1 of 3: ");
		window.Stage(step + QObject::tr("compiling the game to count what its code does"));
		ps2n::wizard::PipelineInput input = input_for(count_dir.filePath(QStringLiteral("modules")));
		input.compile_only = true;
		input.count = true;
		input.objects = QStringLiteral("count/objects");
		counting.start(input);
	};

	// 2: the session's first 3 minutes run again with them, without a window,
	// which writes what they counted.
	count_run = [&]() {
		const QString run = count_dir.filePath(QStringLiteral("run"));
		const QString raw = count_dir.filePath(QStringLiteral("raw"));
		QDir(raw).removeRecursively();
		QStringList arguments;
		QString bios;
		QString error;
		if (!ReplayArguments(folder, counted_session, run, count_dir.filePath(QStringLiteral("modules")), raw, &arguments, &bios, &error) ||
			!PrepareRun(counted_session, bios, run, &error))
		{
			profile_failed(error);
			return;
		}
		const QDateTime began = QDateTime::fromString(QDir(counted_session).dirName(), QStringLiteral("yyyyMMdd-HHmmss"));
		step = QObject::tr("2 of 3: ");
		window.Stage(step + QObject::tr("running the first 3 minutes of your session of %1 again, counting")
								.arg(began.isValid() ? QLocale().toString(began, QLocale::ShortFormat) : QDir(counted_session).dirName()));
		window.Progress(0, kCountFrames, QObject::tr("%v of %m frames"));
		const QString program = folder.filePath(ps2n::wizard::platform::Program(QStringLiteral("ps2n-host")));
		say(QStringLiteral("run again, counting: %1 %2").arg(QDir::toNativeSeparators(program), arguments.join(QLatin1Char(' '))));
		runner = new QProcess(&app);
		runner->setProcessChannelMode(QProcess::MergedChannels);
		runner->setWorkingDirectory(run);
		runner->setProgram(program);
		runner->setArguments(arguments);
		auto pending = std::make_shared<QByteArray>();
		QObject::connect(runner, &QProcess::readyRead, &app, [&, pending]() {
			static const QRegularExpression frame_line(QStringLiteral("^ps2n-host: frame (\\d+) of \\d+$"));
			*pending += runner->readAll();
			for (qsizetype end; (end = pending->indexOf('\n')) >= 0;)
			{
				const QString line = QString::fromLocal8Bit(pending->left(end)).trimmed();
				pending->remove(0, end + 1);
				const QRegularExpressionMatch frame = frame_line.match(line);
				if (frame.hasMatch())
					window.Progress(frame.captured(1).toInt(), kCountFrames, QObject::tr("%v of %m frames"));
				else if (!line.isEmpty())
					say(line);
			}
		});
		QObject::connect(runner, &QProcess::finished, &app, [&, raw](int code, QProcess::ExitStatus status) {
			const QString rest = QString::fromLocal8Bit(runner->readAll()).trimmed();
			if (!rest.isEmpty())
				say(rest);
			runner->deleteLater();
			runner = nullptr;
			if (stopped)
				return;
			// It ends at its last frame (the runtime's exit code 2), with what the
			// four modules counted written.
			const QStringList written = QDir(raw).entryList({QStringLiteral("*.profraw")}, QDir::Files);
			if (status != QProcess::NormalExit || code != 2 || written.size() != 4)
			{
				profile_failed(QObject::tr("The session run again did not end at its 3 minutes with what the game's code did counted "
										   "(exit code %1, %2 of 4 counts written).")
								   .arg(code)
								   .arg(written.size()));
				return;
			}
			count_merge();
		});
		QObject::connect(runner, &QProcess::errorOccurred, &app, [&, program](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart && !stopped)
				profile_failed(QObject::tr("%1 did not start.").arg(QDir::toNativeSeparators(program)));
		});
		runner->start();
	};

	// What the four modules counted, put together by llvm-profdata: the counts.
	count_merge = [&]() {
		const QDir raw(count_dir.filePath(QStringLiteral("raw")));
		window.Busy();
		window.Stage(step + QObject::tr("putting what was counted together"));
		const QString program = QDir(folder.filePath(QStringLiteral("toolchain"))).filePath(ps2n::wizard::platform::ProfileTool());
		const QString part = counted + QStringLiteral(".part");
		QStringList arguments = {QStringLiteral("merge"), QStringLiteral("-o"), QDir::toNativeSeparators(part)};
		for (const QString& file : raw.entryList({QStringLiteral("*.profraw")}, QDir::Files))
			arguments.push_back(QDir::toNativeSeparators(raw.filePath(file)));
		say(QStringLiteral("%1 %2").arg(QDir::toNativeSeparators(program), arguments.join(QLatin1Char(' '))));
		merger = new QProcess(&app);
		merger->setProcessChannelMode(QProcess::MergedChannels);
		merger->setProgram(program);
		merger->setArguments(arguments);
		QObject::connect(merger, &QProcess::finished, &app, [&, part](int code, QProcess::ExitStatus status) {
			const QString output = QString::fromLocal8Bit(merger->readAll()).trimmed();
			merger->deleteLater();
			merger = nullptr;
			if (stopped)
				return;
			if (!output.isEmpty())
				say(output);
			QFile::remove(counted);
			if (status != QProcess::NormalExit || code != 0 || !QFile::rename(part, counted))
			{
				profile_failed(QObject::tr("What was counted could not be put together (llvm-profdata exit code %1).").arg(code));
				return;
			}
			say(QStringLiteral("counted: %1").arg(QDir::toNativeSeparators(counted)));
			// What the run was made with and from goes; the counts stay.
			for (const char* dir : {"run", "raw", "objects", "modules"})
				QDir(count_dir.filePath(QString::fromLatin1(dir))).removeRecursively();
			compile_with_counts();
		});
		QObject::connect(merger, &QProcess::errorOccurred, &app, [&, program](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart && !stopped)
				profile_failed(QObject::tr("%1 did not start.").arg(QDir::toNativeSeparators(program)));
		});
		merger->start();
	};

	// 3: the modules compiled again with the counts, with objects of their own,
	// which become the folder's.
	compile_with_counts = [&]() {
		step = QObject::tr("3 of 3: ");
		window.Busy();
		window.Stage(step + QObject::tr("compiling the game with what was counted"));
		next_modules();
		say(QStringLiteral("compiling the game with the counts into %1").arg(QDir::toNativeSeparators(modules)));
		ps2n::wizard::PipelineInput input = input_for(modules);
		input.compile_only = true;
		input.profile = counted;
		input.objects = QStringLiteral("objects-profiled");
		profiled.start(input);
	};

	for (ps2n::wizard::Pipeline* p : {&counting, &profiled})
	{
		QObject::connect(p, &ps2n::wizard::Pipeline::stage, &app, [&](const QString& what) {
			say(what);
			window.Stage(step + what);
		});
		QObject::connect(p, &ps2n::wizard::Pipeline::progress, &app, [&](int done, int total) { window.Progress(done, total); });
		QObject::connect(p, &ps2n::wizard::Pipeline::line, &app, [&](const QString& text) { say(text); });
	}
	QObject::connect(&counting, &ps2n::wizard::Pipeline::finished, &app, [&](bool ok, const QString& why) {
		if (stopped)
			return;
		if (!ok)
		{
			profile_failed(why);
			return;
		}
		count_run();
	});
	QObject::connect(&profiled, &ps2n::wizard::Pipeline::finished, &app, [&](bool ok, const QString& why) {
		if (stopped)
			return;
		if (!ok)
		{
			profile_failed(why);
			return;
		}
		if (!mark_ready())
		{
			profile_failed(QObject::tr("%1 cannot be written.").arg(QDir::toNativeSeparators(QDir(modules).filePath(QStringLiteral("ready")))));
			return;
		}
		// Its objects are the folder's from now on, and its counts are kept with
		// what the folder's sessions met, for every compile after.
		QDir(work.filePath(QStringLiteral("objects"))).removeRecursively();
		const bool objects = QDir(work).rename(QStringLiteral("objects-profiled"), QStringLiteral("objects"));
		QFile::remove(own_profile);
		const bool kept = QFile::rename(counted, own_profile);
		say(QStringLiteral("compiled for how the game is played: ready for the game's next start%1")
				.arg(objects && kept ? QString() : QStringLiteral(" (the objects or the counts could not be moved: the next compile compiles "
																  "everything again)")));
		if (kept)
			QDir(count_dir.path()).removeRecursively();
		profiling = false;
		stop_watch.stop();
		lock.unlock();
		window.Ended(QObject::tr("Compiled for how you play it"), QObject::tr("%1's next start has it.").arg(recipe.title), true);
		window.closed = [&app]() { app.exit(0); };
		window.Bring();
		QTimer::singleShot(3000, &app, [&app]() { app.exit(0); });
	});

	// A round's end, after a session: the game compiled for how it is played
	// next, where that is wanted.
	const auto done = [&]() {
		if (profile_wanted())
		{
			start_profile();
			return;
		}
		ended(0);
	};

	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::finished, &app, [&](bool ok, const QString& why) {
		if (stopped)
			return;
		if (!ok)
		{
			failed(why);
			return;
		}
		if (pipeline.nothingNew())
		{
			say(QStringLiteral("nothing new: the modules have all the sessions met"));
			done();
			return;
		}
		if (!mark_ready())
		{
			// Not for the launcher to take: the next round compiles it all again.
			QFile::remove(work.filePath(QStringLiteral("compiled-from.txt")));
			failed(QObject::tr("%1 cannot be written.").arg(QDir::toNativeSeparators(QDir(modules).filePath(QStringLiteral("ready")))));
			return;
		}
		say(QStringLiteral("ready for the game's next start"));
		// A session that ended while this round compiled.
		const QDateTime last = LastSession(folder);
		if (last.isValid() && last > round_started)
		{
			say(QStringLiteral("a session ended meanwhile: another round"));
			round();
			return;
		}
		done();
	});

	QTimer::singleShot(0, &app, round);
	return app.exec();
}
