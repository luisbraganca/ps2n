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
#include <QtCore/QLockFile>
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

#include <functional>

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
	// and the executables compiled, the one the disc boots first.
	struct Recipe
	{
		QString title;
		QString image;
		QString bios;
		QStringList executables;
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
		void Progress(int done, int total)
		{
			if (m_ended || total <= 0)
				return;
			m_bar->setRange(0, total);
			m_bar->setValue(done);
			m_bar->setFormat(QObject::tr("%v of %m files"));
			m_bar->setTextVisible(true);
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
	// what it does, and then sees whether anything is left to compile.
	QLockFile lock(work.filePath(QStringLiteral("remake.lock")));
	lock.setStaleLockTime(0);
	if (before_play ? !lock.lock() : !lock.tryLock(0))
		return before_play ? 1 : 0;

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

	const auto round = [&]() {
		// What a round that did not end left behind.
		for (const QString& next : work.entryList({QStringLiteral("next-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
		{
			if (!QFileInfo::exists(QDir(work.filePath(next)).filePath(QStringLiteral("ready"))))
				QDir(work.filePath(next)).removeRecursively();
		}
		round_started = QDateTime::currentDateTime();
		modules = work.filePath(QStringLiteral("next-") + round_started.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
		ps2n::wizard::PipelineInput input;
		input.toolchain = folder.filePath(QStringLiteral("toolchain"));
		input.runtime = folder.path();
		input.image = recipe.image;
		input.bios = recipe.bios;
		input.executables = recipe.executables;
		input.folder = folder.path();
		input.modules = modules;
		input.incremental = true;
		pipeline.start(input);
	};

	// Later: what it compiled so far stays (objects newer than their sources
	// are not compiled again), what it was linking goes with the round, and the
	// game compiles the rest before it next starts.
	window.later = [&]() {
		stopped = true;
		say(QStringLiteral("left for later: the game compiles it before it next starts"));
		pipeline.cancel();
		lock.unlock();
		app.exit(3);
	};
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
			ended(0);
			return;
		}
		// Ready for the launcher, which takes the newest such folder.
		QSaveFile ready(QDir(modules).filePath(QStringLiteral("ready")));
		QString names;
		for (const char* kind : {"game", "bios", "vu", "iop"})
			names += ps2n::wizard::platform::Module(QString::fromLatin1(kind)) + QLatin1Char('\n');
		if (!ready.open(QIODevice::WriteOnly) || ready.write(names.toUtf8()) < 0 || !ready.commit())
		{
			// Not for the launcher to take: the next round compiles it all again.
			QFile::remove(work.filePath(QStringLiteral("compiled-from.txt")));
			failed(QObject::tr("%1 cannot be written.").arg(QDir::toNativeSeparators(ready.fileName())));
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
		ended(0);
	});

	QTimer::singleShot(0, &app, round);
	return app.exec();
}
