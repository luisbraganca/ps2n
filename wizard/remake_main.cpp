// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-remake: a game's folder compiling by itself what its sessions met for
// the first time (docs/phase4-proposal.md, step P4.6; docs/adr/0034). The
// game's launcher starts it, at low priority, when a session has ended. It runs
// the wizard's recompile (pipeline.h) with the folder's own toolchain, as
// make.txt in the folder says the game is made, keeping what the last compile
// left and compiling only what changed since; and it links the four modules
// into work\next-<time>, writing "ready" there last, which the launcher puts in
// place at the game's next start (host/launcher.cpp). When the sessions met
// nothing new it does nothing and shows nothing; otherwise an icon in the
// notification area while it compiles, and a notification when it is done or
// could not be done (work\remake.log says what it did).
//
//   ps2n-remake [<the game's folder>]      by default the folder it is in

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
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QSystemTrayIcon>

#ifdef _WIN32
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
		if (const HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0)))
		{
			QIcon result(QPixmap::fromImage(QImage::fromHICON(icon)));
			DestroyIcon(icon);
			return result;
		}
		return QIcon();
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
} // namespace

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("PS2Native"));
	QApplication::setQuitOnLastWindowClosed(false);

	const QStringList arguments = QCoreApplication::arguments();
	const QDir folder(QDir(arguments.size() > 1 ? arguments[1] : QCoreApplication::applicationDirPath()).absolutePath());
	const QDir work(folder.filePath(QStringLiteral("work")));
	QDir().mkpath(work.path());

	// One at a time in a folder. One started by a session that ends while
	// another compiles leaves at once; the one compiling takes what that
	// session met in a round after. While it holds the lock the game does not
	// start: its launcher waits for it (host/launcher.cpp, docs/adr/0037), so
	// it lets go of it as soon as its modules are ready.
	QLockFile lock(work.filePath(QStringLiteral("remake.lock")));
	lock.setStaleLockTime(0);
	if (!lock.tryLock(0))
		return 0;

	// What it does, for the user to read when it could not compile: without
	// the file it compiles all the same.
	QFile log(work.filePath(QStringLiteral("remake.log")));
	const bool logging = log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
	const auto say = [&log, logging](const QString& text) {
		if (!logging)
			return;
		log.write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss  ")) + text + QLatin1Char('\n')).toUtf8());
		log.flush();
	};

	Recipe recipe;
	QString error;
	if (!ReadRecipe(folder, &recipe, &error))
	{
		say(error);
		return 1;
	}

	const QIcon icon = OwnIcon();
	QSystemTrayIcon tray(icon);
	tray.setToolTip(QStringLiteral("%1: compiling what your last session met for the first time").arg(recipe.title));
	const bool can_show = QSystemTrayIcon::isSystemTrayAvailable() && !icon.isNull();

	ps2n::wizard::Pipeline pipeline;
	QDateTime round_started;
	QString modules;
	bool compiled = false;

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
	// Two notifications (docs/adr/0035): one as soon as the session is found to
	// have met new code, another, different, at the end.
	const auto end = [&](int code, bool tell) {
		// Done: a game started now plays without waiting for the notification.
		lock.unlock();
		if (tell && tray.isVisible())
		{
			if (code == 0)
			{
				tray.showMessage(recipe.title,
					QStringLiteral("Compiled: the game's next start has what your session met for the first time."),
					QSystemTrayIcon::Information, 8000);
			}
			else
			{
				tray.showMessage(recipe.title,
					QStringLiteral("What your session met for the first time could not be compiled; %1 in the game's folder says why.")
						.arg(QDir::toNativeSeparators(QStringLiteral("work/remake.log"))),
					QSystemTrayIcon::Warning, 10000);
			}
		}
		// Long enough for the notification to be seen.
		QTimer::singleShot(tray.isVisible() ? 10000 : 0, &app, [&app, code]() { app.exit(code); });
	};

	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::newCode, &app, [&]() {
		say(QStringLiteral("new code met: compiling it into %1").arg(QDir::toNativeSeparators(modules)));
		if (can_show && !tray.isVisible())
		{
			tray.show();
			tray.showMessage(recipe.title,
				QStringLiteral("Compiling what your session met for the first time, for the game's next start."),
				QSystemTrayIcon::Information, 6000);
		}
	});
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::stage, &app, [&](const QString& what) { say(what); });
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::line, &app, [&](const QString& text) { say(text); });
	QObject::connect(&pipeline, &ps2n::wizard::Pipeline::finished, &app, [&](bool ok, const QString& why) {
		if (!ok)
		{
			say(QStringLiteral("not compiled: ") + why);
			end(1, true);
			return;
		}
		if (pipeline.nothingNew())
		{
			say(QStringLiteral("nothing new: the modules have all the sessions met"));
			end(0, compiled);
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
			say(QStringLiteral("not compiled: %1 cannot be written").arg(QDir::toNativeSeparators(ready.fileName())));
			end(1, true);
			return;
		}
		compiled = true;
		say(QStringLiteral("ready for the game's next start"));
		// A session that ended while this round compiled.
		const QDateTime last = LastSession(folder);
		if (last.isValid() && last > round_started)
		{
			say(QStringLiteral("a session ended meanwhile: another round"));
			round();
			return;
		}
		end(0, true);
	});

	QTimer::singleShot(0, &app, round);
	return app.exec();
}
