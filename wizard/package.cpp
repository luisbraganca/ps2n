// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.h"

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
#endif

namespace ps2n::wizard
{
	namespace
	{
		// What of the runtime folder a game's folder has: all of it but what
		// only the wizard needs, as the folder's wizard-files.txt names it
		// (scripts/windows/kit.ps1: the wizard, Qt and its plugins, and what
		// they import), and the launcher, which it has under the game's name.
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
				m_left_out.push_back(QStringLiteral("ps2n-launcher.exe"));
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

		bool HardLink(const QString& target, const QString& link)
		{
#ifdef _WIN32
			return CreateHardLinkW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(link).utf16()),
					   reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()), nullptr) != 0;
#else
			return false;
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
		return QDir(input.folder).filePath(PlainName(input.title) + QStringLiteral(".exe"));
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
		if (!CopyReplacing(runtime.filePath(QStringLiteral("ps2n-launcher.exe")), LauncherPath(input)))
		{
			*error = QObject::tr("Cannot make the launcher %1.").arg(LauncherPath(input));
			return false;
		}

		// The BIOS image linked into the folder with the ROM1, ROM2 and EROM
		// images beside it, so that the core keeps the console's settings
		// there and not beside the image; read where it is where it cannot be
		// linked (another drive).
		const QFileInfo bios(input.bios);
		const QString bios_dir = folder.filePath(QStringLiteral("bios"));
		QDir().mkpath(bios_dir);
		QString bios_used = QStringLiteral("%GAME%\\bios\\") + bios.fileName();
		const QDir images(bios.absolutePath());
		for (const QFileInfo& file : images.entryInfoList(QDir::Files))
		{
			const QString extension = file.suffix().toLower();
			const bool beside = (extension == QStringLiteral("rom1") || extension == QStringLiteral("rom2") || extension == QStringLiteral("erom")) &&
								(file.completeBaseName() == bios.completeBaseName() || file.completeBaseName() == bios.fileName());
			if (file.absoluteFilePath() != bios.absoluteFilePath() && !beside)
				continue;
			const QString link = QDir(bios_dir).filePath(file.fileName());
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
			QStringLiteral("# What its launcher (%1.exe) starts the runtime with: one option to a line (ps2n-host -help").arg(plain),
			QStringLiteral("# has them); %GAME% is this folder. The game's own settings are in data\\PCSX2\\gamesettings."),
			QStringLiteral("-bios"), bios_used, QStringLiteral("-data"), QStringLiteral("%GAME%\\data"), QStringLiteral("-log"),
			QStringLiteral("%GAME%\\core.log"), QStringLiteral("-out"), QStringLiteral("%GAME%\\guest.txt"), QStringLiteral("-ee"),
			QStringLiteral("ps2n"), QStringLiteral("-iop"), QStringLiteral("jit"), QStringLiteral("-vu"), QStringLiteral("jit"),
			QStringLiteral("-static"), QStringLiteral("on"), QStringLiteral("-vu-ps2n"), QStringLiteral("both"), QStringLiteral("-iop-ps2n"),
			QStringLiteral("-image"), QStringLiteral("game"), QStringLiteral("-until"), QStringLiteral("@@ no such line @@"),
			QStringLiteral("-packaged"), QStringLiteral("-window"), QStringLiteral("-title"), input.title, QStringLiteral("-sessions"),
			QStringLiteral("%GAME%\\sessions"), QDir::toNativeSeparators(QFileInfo(input.image).absoluteFilePath())};
		QSaveFile file(folder.filePath(QStringLiteral("settings.txt")));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			*error = QObject::tr("Cannot write %1.").arg(file.fileName());
			return false;
		}
		file.write((settings.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8());
		if (!file.commit())
		{
			*error = QObject::tr("Cannot write %1.").arg(file.fileName());
			return false;
		}
		return true;
	}
} // namespace ps2n::wizard
