// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.h"
#include "platform.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

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
	} // namespace

	QString PlainName(const QString& title)
	{
		QString name = title;
		name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral(" "));
		return name.simplified();
	}

	QString LauncherPath(const PackageInput& input)
	{
		return QDir(input.folder).filePath(platform::Program(PlainName(input.title)));
	}

	bool Package(const PackageInput& input, QString* error)
	{
		const QDir runtime(input.runtime);
		const QDir folder(input.folder);

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
				*error = QObject::tr("Cannot copy %1 into %2.").arg(relative, folder.path());
				return false;
			}
		}

		// The launcher, named after the game.
		if (!CopyReplacing(runtime.filePath(platform::Program(QStringLiteral("ps2n-launcher"))), LauncherPath(input)))
		{
			*error = QObject::tr("Cannot make the launcher %1.").arg(LauncherPath(input));
			return false;
		}

		// The toolchain, a copy of the kit's own, which the game compiles what
		// its sessions meet for the first time with (docs/adr/0034); whatever
		// an older one had goes.
		const QDir toolchain(input.toolchain);
		QDir(folder.filePath(QStringLiteral("toolchain"))).removeRecursively();
		QDirIterator tools(toolchain.path(), QDir::Files, QDirIterator::Subdirectories);
		while (tools.hasNext())
		{
			const QString from = tools.next();
			const QString to = folder.filePath(QStringLiteral("toolchain/") + toolchain.relativeFilePath(from));
			QDir().mkpath(QFileInfo(to).path());
			if (!QFile::copy(from, to))
			{
				*error = QObject::tr("Cannot copy %1 into %2.").arg(QDir::toNativeSeparators(from), folder.path());
				return false;
			}
		}

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
		QSaveFile file(folder.filePath(QStringLiteral("settings.txt")));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			*error = QObject::tr("Cannot write %1.").arg(file.fileName());
			return false;
		}
		file.write((settings.join(kNewline) + kNewline).toUtf8());
		if (!file.commit())
		{
			*error = QObject::tr("Cannot write %1.").arg(file.fileName());
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
		QSaveFile recipe(folder.filePath(QStringLiteral("make.txt")));
		if (!recipe.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
			recipe.write((make.join(kNewline) + kNewline).toUtf8()) < 0 || !recipe.commit())
		{
			*error = QObject::tr("Cannot write %1.").arg(recipe.fileName());
			return false;
		}
		return true;
	}
} // namespace ps2n::wizard
