// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.h"
#include "platform.h"

// PS2Native's version: version.txt at the repository's root, which the build
// writes into this header (cmake/ps2n-inject.cmake).
#include "ps2n_version.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ps2n::wizard
{
	namespace
	{
		// The end of a line of the folder's text files, as the system writes them.
#ifdef _WIN32
		const QString kNewline = QStringLiteral("\r\n");
#else
		const QString kNewline = QStringLiteral("\n");
#endif

		// A path inside the game's folder, %GAME%, with the system's separators.
		QString InFolder(const QString& relative)
		{
			return QDir::toNativeSeparators(QStringLiteral("%GAME%/") + relative);
		}

		// What of the runtime folder a game's folder has: all of it but what
		// only the wizard needs, as the folder's wizard-files.txt names it
		// (scripts/windows/kit.ps1: the wizard itself; Qt goes with the game,
		// for its ps2n-remake), and the launcher, which it has under the
		// game's name.
		class GameFiles
		{
		public:
			explicit GameFiles(const QDir& runtime)
			{
				QFile list(runtime.filePath(QStringLiteral("wizard-files.txt")));
				if (list.open(QIODevice::ReadOnly | QIODevice::Text))
				{
					for (const QString& line : QString::fromUtf8(list.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
					{
						if (!line.trimmed().isEmpty())
							m_left_out.push_back(line.trimmed().toLower());
					}
				}
				m_left_out.push_back(platform::Program(QStringLiteral("ps2n-launcher")).toLower());
			}

			bool Has(const QString& relative) const
			{
				const QString lower = relative.toLower();
				for (const QString& out : m_left_out)
				{
					if (lower == out || lower.startsWith(out + QLatin1Char('/')))
						return false;
				}
				return true;
			}

		private:
			QStringList m_left_out;
		};

		bool CopyReplacing(const QString& from, const QString& to)
		{
			if (QFileInfo::exists(to))
				QFile::remove(to);
			return QFile::copy(from, to);
		}

		// A name in the game's folder for a file read where it is: a hard link; on
		// Linux a symbolic one where a hard one cannot be made (another file
		// system), which Windows does not let a user make.
		bool HardLink(const QString& target, const QString& link)
		{
#ifdef _WIN32
			return CreateHardLinkW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(link).utf16()),
					   reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()), nullptr) != 0;
#else
			const QByteArray from = QFile::encodeName(target);
			const QByteArray to = QFile::encodeName(link);
			return ::link(from.constData(), to.constData()) == 0 || ::symlink(from.constData(), to.constData()) == 0;
#endif
		}

		QString LauncherIn(const QDir& folder, const QString& title)
		{
			return folder.filePath(platform::Program(PlainName(title)));
		}

		// PS2Native's programs into a game's folder, from a kit's runtime and
		// toolchain: what a folder the wizard makes gets of them, and what one
		// brought up to date gets again.
		bool CopyPrograms(const QDir& runtime, const QDir& toolchain, const QDir& folder, const QString& launcher, QString* error)
		{
			// The runtime.
			const GameFiles game_files(runtime);
			QDirIterator it(runtime.path(), QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext())
			{
				const QString from = it.next();
				const QString relative = runtime.relativeFilePath(from);
				if (!game_files.Has(relative))
					continue;
				const QString to = folder.filePath(relative);
				QDir().mkpath(QFileInfo(to).path());
				if (!CopyReplacing(from, to))
				{
					*error = QObject::tr("Cannot copy %1 into %2.").arg(relative, QDir::toNativeSeparators(folder.path()));
					return false;
				}
			}

			// The launcher, named after the game.
			if (!CopyReplacing(runtime.filePath(platform::Program(QStringLiteral("ps2n-launcher"))), launcher))
			{
				*error = QObject::tr("Cannot make the launcher %1.").arg(QDir::toNativeSeparators(launcher));
				return false;
			}

			// The toolchain, a copy of the kit's own, which the game compiles what
			// its sessions meet for the first time with (docs/adr/0034); whatever
			// an older one had goes.
			QDir(folder.filePath(QStringLiteral("toolchain"))).removeRecursively();
			QDirIterator tools(toolchain.path(), QDir::Files, QDirIterator::Subdirectories);
			while (tools.hasNext())
			{
				const QString from = tools.next();
				const QString to = folder.filePath(QStringLiteral("toolchain/") + toolchain.relativeFilePath(from));
				QDir().mkpath(QFileInfo(to).path());
				if (!QFile::copy(from, to))
				{
					*error = QObject::tr("Cannot copy %1 into %2.").arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(folder.path()));
					return false;
				}
				// With the time it was last written, which a copy on Linux does not
				// keep (on Windows it does): the game's own recompile tells the
				// objects the wizard compiled from another toolchain's by the
				// toolchain's files' sizes and times (Pipeline::toolchainStamp), and
				// would otherwise compile them all again after the first session.
				QFile copied(to);
				if (copied.open(QIODevice::ReadWrite))
				{
					copied.setFileTime(QFileInfo(from).lastModified(), QFileDevice::FileModificationTime);
					copied.close();
				}
			}
			return true;
		}

		// A fingerprint of the programs a game's folder gets from a kit: each
		// file's name, size and time, of the runtime's it has, of the launcher and
		// of the toolchain. Two kits whose files are the same give the same one,
		// and a folder says which its programs were copied from (programs.txt).
		QString ProgramsFingerprint(const QDir& runtime, const QDir& toolchain)
		{
			static QString s_for, s_fingerprint;
			const QString key = runtime.absolutePath() + QLatin1Char('\n') + toolchain.absolutePath();
			if (key == s_for)
				return s_fingerprint;
			QStringList lines;
			const auto add = [&lines](const QString& name, const QFileInfo& file) {
				lines.push_back(QStringLiteral("%1 %2 %3").arg(name).arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()));
			};
			const GameFiles game_files(runtime);
			const QString launcher = platform::Program(QStringLiteral("ps2n-launcher"));
			QDirIterator it(runtime.path(), QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext())
			{
				const QString file = it.next();
				const QString relative = runtime.relativeFilePath(file);
				if (game_files.Has(relative) || relative.compare(launcher, Qt::CaseInsensitive) == 0)
					add(QStringLiteral("runtime/") + relative, QFileInfo(file));
			}
			QDirIterator tools(toolchain.path(), QDir::Files, QDirIterator::Subdirectories);
			while (tools.hasNext())
			{
				const QString file = tools.next();
				add(QStringLiteral("toolchain/") + toolchain.relativeFilePath(file), QFileInfo(file));
			}
			std::sort(lines.begin(), lines.end());
			s_for = key;
			s_fingerprint = QString::fromLatin1(QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
			return s_fingerprint;
		}

		// A fingerprint of a toolchain folder alone, the same way: whether a
		// game's folder compiles with the same tools after it was given programs
		// as before.
		QString ToolchainFingerprint(const QDir& toolchain)
		{
			QStringList lines;
			QDirIterator tools(toolchain.path(), QDir::Files, QDirIterator::Subdirectories);
			while (tools.hasNext())
			{
				const QFileInfo file(tools.next());
				lines.push_back(QStringLiteral("%1 %2 %3")
									.arg(toolchain.relativeFilePath(file.filePath()))
									.arg(file.size())
									.arg(file.lastModified().toMSecsSinceEpoch()));
			}
			std::sort(lines.begin(), lines.end());
			return QString::fromLatin1(QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
		}

		// The lines "key=value" of one of a folder's text files, in order; lines
		// that begin with # are comments.
		QList<std::pair<QString, QString>> KeyLines(const QString& path)
		{
			QList<std::pair<QString, QString>> lines;
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
				return lines;
			for (const QString& raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')))
			{
				const QString line = raw.trimmed();
				const qsizetype equals = line.indexOf(QLatin1Char('='));
				if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || equals <= 0)
					continue;
				lines.push_back({line.left(equals).trimmed(), line.mid(equals + 1).trimmed()});
			}
			return lines;
		}

		bool WriteText(const QString& path, const QStringList& lines)
		{
			QSaveFile file(path);
			return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write((lines.join(kNewline) + kNewline).toUtf8()) >= 0 &&
				   file.commit();
		}

		// programs.txt: which PS2Native's the folder's programs are.
		bool WritePrograms(const QDir& runtime, const QDir& toolchain, const QDir& folder)
		{
			return WriteText(folder.filePath(QStringLiteral("programs.txt")),
				{QStringLiteral("# The programs in this folder (the runtime, the launcher, the toolchain) are PS2Native %1's. Its wizard")
						.arg(QStringLiteral(PS2N_VERSION)),
					QStringLiteral("# wrote this when it made the folder or brought it up to date, and tells by it whether a later PS2Native"),
					QStringLiteral("# has newer ones for it (PS2Native's docs/adr/0048)."), QStringLiteral("version=") + QStringLiteral(PS2N_VERSION),
					QStringLiteral("programs=") + ProgramsFingerprint(runtime, toolchain)});
		}

		// 0.2 against 0.10: the numbers between the dots, one after the other.
		int CompareVersions(const QString& a, const QString& b)
		{
			const QStringList as = a.split(QLatin1Char('.')), bs = b.split(QLatin1Char('.'));
			// (Not std::max: windows.h has a macro of that name.)
			const qsizetype parts = as.size() > bs.size() ? as.size() : bs.size();
			for (qsizetype i = 0; i < parts; i++)
			{
				const int x = i < as.size() ? as[i].toInt() : 0, y = i < bs.size() ? bs[i].toInt() : 0;
				if (x != y)
					return x < y ? -1 : 1;
			}
			return 0;
		}

		// A program of the folder's that is running, by its name; nothing when
		// none is. On Windows a program that runs cannot be opened to be written;
		// on Linux the processes say which file each is (/proc).
		QString RunningProgram(const QDir& folder)
		{
#ifdef _WIN32
			for (const QFileInfo& program : folder.entryInfoList({QStringLiteral("*.exe")}, QDir::Files))
			{
				const HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(program.absoluteFilePath()).utf16()),
					GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file != INVALID_HANDLE_VALUE)
					CloseHandle(file);
				else if (GetLastError() == ERROR_SHARING_VIOLATION)
					return program.fileName();
			}
#else
			const QString inside = folder.canonicalPath() + QLatin1Char('/');
			for (const QString& process : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
			{
				bool number = false;
				process.toLongLong(&number);
				if (!number)
					continue;
				const QString program = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(process));
				if (program.startsWith(inside))
					return QFileInfo(program).fileName();
			}
#endif
			return QString();
		}
	} // namespace

	QString PlainName(const QString& title)
	{
		QString name = title;
		name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral(" "));
		return name.simplified();
	}

	QString LauncherPath(const PackageInput& input)
	{
		return LauncherIn(QDir(input.folder), input.title);
	}

	bool Package(const PackageInput& input, QString* error)
	{
		const QDir runtime(input.runtime);
		const QDir folder(input.folder);

		// The runtime, the launcher named after the game, and the toolchain.
		if (!CopyPrograms(runtime, QDir(input.toolchain), folder, LauncherPath(input), error))
			return false;

		// The BIOS image linked into the folder with the ROM1, ROM2 and EROM
		// images beside it, so that the core keeps the console's settings
		// there and not beside the image; read where it is where it cannot be
		// linked (another drive).
		const QFileInfo bios(input.bios);
		const QString bios_dir = folder.filePath(QStringLiteral("bios"));
		QDir().mkpath(bios_dir);
		QString bios_used = InFolder(QStringLiteral("bios/") + bios.fileName());
		const QDir images(bios.absolutePath());
		for (const QFileInfo& file : images.entryInfoList(QDir::Files))
		{
			const QString extension = file.suffix().toLower();
			const bool beside = (extension == QStringLiteral("rom1") || extension == QStringLiteral("rom2") || extension == QStringLiteral("erom")) &&
								(file.completeBaseName() == bios.completeBaseName() || file.completeBaseName() == bios.fileName());
			if (file.absoluteFilePath() != bios.absoluteFilePath() && !beside)
				continue;
#ifdef _WIN32
			const QString link = QDir(bios_dir).filePath(file.fileName());
#else
			// Under the extension in lower case, the one the core asks for: Linux
			// tells the cases of a file's name apart.
			const QString link =
				QDir(bios_dir).filePath(beside ? file.completeBaseName() + QLatin1Char('.') + extension : file.fileName());
#endif
			if (QFileInfo::exists(link))
				continue;
			if (!HardLink(file.absoluteFilePath(), link) && file.absoluteFilePath() == bios.absoluteFilePath())
				bios_used = QDir::toNativeSeparators(bios.absoluteFilePath());
		}

		// What the launcher starts the runtime with.
		const QString plain = PlainName(input.title);
		const QStringList settings = {
			QStringLiteral("# %1, made into this folder by PS2Native's wizard on %2.")
				.arg(input.title, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
			QStringLiteral("# What its launcher (%1) starts the runtime with: one option to a line (ps2n-host -help")
				.arg(platform::Program(plain)),
			QStringLiteral("# has them); %GAME% is this folder. The game's own settings are in %1.")
				.arg(QDir::toNativeSeparators(QStringLiteral("data/PCSX2/gamesettings"))),
			QStringLiteral("-bios"), bios_used, QStringLiteral("-data"), InFolder(QStringLiteral("data")), QStringLiteral("-log"),
			InFolder(QStringLiteral("core.log")), QStringLiteral("-out"), InFolder(QStringLiteral("guest.txt")), QStringLiteral("-ee"),
			QStringLiteral("ps2n"), QStringLiteral("-iop"), QStringLiteral("jit"), QStringLiteral("-vu"), QStringLiteral("jit"),
			QStringLiteral("-static"), QStringLiteral("on"), QStringLiteral("-vu-ps2n"), QStringLiteral("both"), QStringLiteral("-iop-ps2n"),
			QStringLiteral("-image"), QStringLiteral("game"), QStringLiteral("-until"), QStringLiteral("@@ no such line @@"),
			QStringLiteral("-packaged"), QStringLiteral("-window"), QStringLiteral("-title"), input.title, QStringLiteral("-sessions"),
			InFolder(QStringLiteral("sessions")), QDir::toNativeSeparators(QFileInfo(input.image).absoluteFilePath())};
		if (!WriteText(folder.filePath(QStringLiteral("settings.txt")), settings))
		{
			*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(folder.filePath(QStringLiteral("settings.txt"))));
			return false;
		}

		// How its modules are made, for the game's own recompile (ps2n-remake).
		QStringList make = {
			QStringLiteral("# How this game's modules are made, for the game to compile by itself what its sessions meet for"),
			QStringLiteral("# the first time (%1; PS2Native's docs/adr/0034). %GAME% is this folder.")
				.arg(platform::Program(QStringLiteral("ps2n-remake"))),
			QStringLiteral("title=") + input.title, QStringLiteral("image=") + QDir::toNativeSeparators(QFileInfo(input.image).absoluteFilePath()),
			QStringLiteral("bios=") + bios_used};
		for (const QString& executable : input.executables)
			make.push_back(QStringLiteral("executable=") + executable);
		// PCSX2's GameDB has the game played with FullVU0SyncHack: what the
		// recompiler decides COP2 instructions with (ADR 0046).
		if (input.cop2_full_sync)
			make.push_back(QStringLiteral("cop2-full-sync=on"));
		if (!WriteText(folder.filePath(QStringLiteral("make.txt")), make))
		{
			*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(folder.filePath(QStringLiteral("make.txt"))));
			return false;
		}

		// Which PS2Native's its programs are, for a later one to tell.
		if (!WritePrograms(runtime, QDir(input.toolchain), folder))
		{
			*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(folder.filePath(QStringLiteral("programs.txt"))));
			return false;
		}
		return true;
	}

	// ---- A game's folder brought up to date (docs/adr/0048) ---------------------

	bool IsGameFolder(const QString& folder)
	{
		const QDir dir(folder);
		return QFileInfo::exists(dir.filePath(QStringLiteral("make.txt"))) && QFileInfo::exists(dir.filePath(QStringLiteral("settings.txt"))) &&
			   QFileInfo::exists(dir.filePath(platform::Program(QStringLiteral("ps2n-host"))));
	}

	QStringList FindGameFolders(const QString& where)
	{
		if (IsGameFolder(where))
			return {QDir(where).absolutePath()};
		QStringList found;
		for (const QString& in : {where, QDir(where).filePath(QStringLiteral("games"))})
		{
			const QDir dir(in);
			for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
			{
				if (IsGameFolder(dir.filePath(name)))
					found.push_back(QDir(dir.filePath(name)).absolutePath());
			}
		}
		found.removeDuplicates();
		return found;
	}

	GameFolder ReadGameFolder(const QString& runtime, const QString& toolchain, const QString& folder)
	{
		GameFolder game;
		const QDir dir(folder);
		game.folder = dir.absolutePath();
		for (const auto& [key, value] : KeyLines(dir.filePath(QStringLiteral("make.txt"))))
		{
			if (key == QStringLiteral("title"))
				game.title = value;
			else if (key == QStringLiteral("executable") && game.boot.isEmpty())
				game.boot = value;
			else if (key == QStringLiteral("cop2-full-sync"))
				game.cop2_full_sync = value == QStringLiteral("on");
		}
		QString programs;
		for (const auto& [key, value] : KeyLines(dir.filePath(QStringLiteral("programs.txt"))))
		{
			if (key == QStringLiteral("version"))
				game.version = value;
			else if (key == QStringLiteral("programs"))
				programs = value;
		}
		// A folder that does not say is an earlier PS2Native's: 0.1's did not.
		if (!game.version.isEmpty() && CompareVersions(game.version, QStringLiteral(PS2N_VERSION)) > 0)
			game.programs = GameFolder::Programs::Later;
		else if (!programs.isEmpty() && programs == ProgramsFingerprint(QDir(runtime), QDir(toolchain)))
			game.programs = GameFolder::Programs::Current;
		else
			game.programs = GameFolder::Programs::Earlier;
		return game;
	}

	bool BringUpToDate(const UpdateInput& input, QString* error)
	{
		const QDir folder(input.folder);
		const QString shown = QDir::toNativeSeparators(folder.absolutePath());
		if (!IsGameFolder(input.folder))
		{
			*error = QObject::tr("%1 is not a game's folder that PS2Native made.").arg(shown);
			return false;
		}
		const GameFolder game = ReadGameFolder(input.runtime, input.toolchain, input.folder);
		if (game.title.isEmpty())
		{
			*error = QObject::tr("%1 does not name its game.").arg(QDir::toNativeSeparators(folder.filePath(QStringLiteral("make.txt"))));
			return false;
		}
		if (game.programs == GameFolder::Programs::Later)
		{
			*error = QObject::tr("%1 has the programs of a later PS2Native (%2) than this one (%3): it is left as it is.")
						 .arg(game.title, game.version, QStringLiteral(PS2N_VERSION));
			return false;
		}

		// Not while it compiles (its ps2n-remake holds work/remake.lock, and one
		// started meanwhile waits for this), nor while it runs.
		QDir().mkpath(folder.filePath(QStringLiteral("work")));
		QLockFile lock(folder.filePath(QStringLiteral("work/remake.lock")));
		lock.setStaleLockTime(0);
		if (!lock.tryLock(0))
		{
			*error = QObject::tr("%1 is compiling: bring it up to date when that is done.").arg(game.title);
			return false;
		}
		if (const QString running = RunningProgram(folder); !running.isEmpty())
		{
			*error = QObject::tr("%1 is running (%2): close it first.").arg(game.title, running);
			return false;
		}

		const bool same_tools =
			ToolchainFingerprint(QDir(folder.filePath(QStringLiteral("toolchain")))) == ToolchainFingerprint(QDir(input.toolchain));
		if (!CopyPrograms(QDir(input.runtime), QDir(input.toolchain), folder, LauncherIn(folder, game.title), error))
			return false;

		// What the folder counted of how its game is played (docs/adr/0045) was
		// counted of the code the earlier toolchain made. With another one the
		// game is compiled with those counts once more, what still fits of them
		// taken, and counts again after a session (ps2n-remake: work/count-again.txt);
		// and a game the earlier programs could not count for is tried again.
		if (!same_tools)
		{
			QFile::remove(folder.filePath(QStringLiteral("work/count/failed.txt")));
			if (QFileInfo::exists(folder.filePath(QStringLiteral("captures/profile.profdata"))))
				WriteText(folder.filePath(QStringLiteral("work/count-again.txt")), {QStringLiteral(PS2N_VERSION)});
		}

		// PCSX2's GameDB has the game played with FullVU0SyncHack, which a
		// folder made before the recompiler decided COP2 instructions with it
		// (ADR 0046) does not say: one line more, the rest of make.txt as it is.
		if (input.cop2_full_sync && !game.cop2_full_sync)
		{
			const QString path = folder.filePath(QStringLiteral("make.txt"));
			QFile file(path);
			QString text;
			if (file.open(QIODevice::ReadOnly))
				text = QString::fromUtf8(file.readAll());
			file.close();
			if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
				text += kNewline;
			text += QStringLiteral("cop2-full-sync=on") + kNewline;
			QSaveFile out(path);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(text.toUtf8()) < 0 || !out.commit())
			{
				*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(path));
				return false;
			}
		}

		// For its own recompile to say why it compiles the game again before
		// its next start (ps2n-remake --before-play), and which PS2Native's its
		// programs are now.
		WriteText(folder.filePath(QStringLiteral("work/brought-up-to-date.txt")), {QStringLiteral(PS2N_VERSION)});
		if (!WritePrograms(QDir(input.runtime), QDir(input.toolchain), folder))
		{
			*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(folder.filePath(QStringLiteral("programs.txt"))));
			return false;
		}
		return true;
	}
} // namespace ps2n::wizard
