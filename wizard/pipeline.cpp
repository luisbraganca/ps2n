// SPDX-License-Identifier: GPL-3.0-or-later

#include "pipeline.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>

#include <algorithm>
#include <set>

namespace ps2n::wizard
{
	namespace
	{
		// What a module is compiled with: the development build's flags
		// (tests/tests.cmake, ps2n_compile_freestanding), PS2Native's headers and
		// the compiler's own, nothing else.
		QStringList CompileFlags(const QString& toolchain)
		{
			return {QStringLiteral("/nologo"), QStringLiteral("/c"), QStringLiteral("/X"), QStringLiteral("/clang:-ffreestanding"),
				QStringLiteral("/Zl"), QStringLiteral("/GS-"), QStringLiteral("/O2"), QStringLiteral("/Ob2"),
				QStringLiteral("-std:c++20"), QStringLiteral("-msse4.1"), QStringLiteral("/DNDEBUG"), QStringLiteral("/DPS2N_MODULE"),
				QStringLiteral("-I") + QDir::toNativeSeparators(QDir(toolchain).filePath(QStringLiteral("include"))),
				QStringLiteral("-Werror=global-constructors"), QStringLiteral("-Werror=exit-time-destructors")};
		}

		// An executable's file name on the disc, "ACTION.UK" of "DATA/ACTION.UK".
		QString Leaf(const QString& path)
		{
			return QFileInfo(path).fileName();
		}

		// The name of the image an executable the game loads itself is
		// compiled as, as make-game.ps1 names it: game_loads_action_uk.
		QString LoadedId(const QString& path)
		{
			QString id = Leaf(path).toLower();
			for (QChar& c : id)
			{
				const ushort u = c.unicode();
				if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')))
					c = QLatin1Char('_');
			}
			return QStringLiteral("game_loads_") + id;
		}

		QStringList Files(const QString& folder, const QString& pattern)
		{
			QStringList out;
			const QDir dir(folder);
			for (const QString& name : dir.entryList({pattern}, QDir::Files, QDir::Name))
				out.push_back(dir.filePath(name));
			return out;
		}

		// Whether an object is older than what it is compiled from: its source,
		// and for an image's tables and parts the image's header, which they
		// include (ps2n-recomp writes a file only when it changes).
		bool Stale(const QString& source, const QString& object)
		{
			const QFileInfo built(object);
			if (!built.exists())
				return true;
			const QFileInfo from(source);
			QDateTime newest = from.lastModified();
			QString image = from.completeBaseName();
			image.remove(QRegularExpression(QStringLiteral("_part\\d{3}$")));
			const QFileInfo header(from.dir().filePath(image + QStringLiteral(".h")));
			if (header.exists())
				newest = std::max(newest, header.lastModified());
			return built.lastModified() < newest;
		}

		bool WriteText(const QString& path, const QString& text)
		{
			QSaveFile file(path);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return false;
			file.write(text.toUtf8());
			return file.commit();
		}

		QString ReadText(const QString& path)
		{
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
		}
	} // namespace

	Pipeline::Pipeline(QObject* parent)
		: QObject(parent)
	{
	}

	QString Pipeline::tool(const char* name) const
	{
		return QDir::toNativeSeparators(QDir(m_in.toolchain).filePath(QStringLiteral("bin/") + QString::fromLatin1(name)));
	}

	QString Pipeline::fingerprint() const
	{
		QCryptographicHash hash(QCryptographicHash::Sha256);
		const QDir own(QDir(m_in.folder).filePath(QStringLiteral("captures")));
		for (const char* kind : {"capture", "bios-capture", "vu-capture", "iop-capture"})
		{
			hash.addData(QByteArray(kind) + "\n");
			QFile file(own.filePath(QString::fromLatin1(kind) + QStringLiteral(".toml")));
			if (file.open(QIODevice::ReadOnly))
				hash.addData(file.readAll());
			hash.addData(QByteArrayView("\n--\n"));
		}
		hash.addData(m_in.executables.join(QLatin1Char('\n')).toUtf8());
		return QString::fromLatin1(hash.result().toHex());
	}

	QString Pipeline::toolchainStamp() const
	{
		QStringList files = {QStringLiteral("bin/ps2n-recomp.exe"), QStringLiteral("bin/clang-cl.exe"), QStringLiteral("bin/lld-link.exe"),
			QStringLiteral("lib/ps2n-host.lib"), QStringLiteral("src/module_entry.cpp")};
		const QDir toolchain(m_in.toolchain);
		QDirIterator headers(toolchain.filePath(QStringLiteral("include")), QDir::Files, QDirIterator::Subdirectories);
		while (headers.hasNext())
			files.push_back(toolchain.relativeFilePath(headers.next()));
		std::sort(files.begin(), files.end());
		QString stamp;
		for (const QString& file : files)
		{
			const QFileInfo info(toolchain.filePath(file));
			stamp += QStringLiteral("%1 %2 %3\n").arg(file).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
		}
		return stamp;
	}

	void Pipeline::start(const PipelineInput& input)
	{
		m_in = input;
		m_running = true;
		m_cancelled = false;
		m_nothing_new = false;
		m_fingerprint.clear();
		m_compiles.clear();
		m_todo.clear();
		m_next_compile = 0;
		m_compiles_done = 0;
		m_compiles_running = 0;
		m_program_ids.clear();
		m_have_vu = m_have_iop = false;
		m_jobs = std::max(1, QThread::idealThreadCount());
		for (const char* name : {"ps2n-recomp.exe", "clang-cl.exe", "lld-link.exe"})
		{
			if (!QFileInfo::exists(tool(name)))
			{
				fail(tr("The toolchain is not whole: %1 is missing.").arg(tool(name)));
				return;
			}
		}
		mergeCaptures();
	}

	void Pipeline::cancel()
	{
		m_cancelled = true;
		if (m_process)
			m_process->kill();
		for (QProcess* p : m_compile_processes)
			p->kill();
	}

	void Pipeline::fail(const QString& error)
	{
		if (!m_running)
			return;
		m_running = false;
		for (QProcess* p : m_compile_processes)
			p->kill();
		emit finished(false, error);
	}

	// What the sessions in the game's folder had to interpret, put into its
	// captures, which the compile takes: each kind merged by ps2n-recomp,
	// every block or address once.
	void Pipeline::mergeCaptures()
	{
		emit stage(tr("Gathering what earlier sessions met for the first time"));
		const QString own = QDir(m_in.folder).filePath(QStringLiteral("captures"));
		QDir().mkpath(own);
		const std::pair<const char*, const char*> kinds[] = {
			{"capture", "capture"}, {"vu-capture", "vu-capture"}, {"iop-capture", "iop-capture"}, {"bios-capture", "capture"}};
		std::deque<Job> jobs;
		std::set<QString> will_exist;
		const QDir sessions(QDir(m_in.folder).filePath(QStringLiteral("sessions")));
		for (const QString& session : sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		{
			for (const auto& [kind, command] : kinds)
			{
				const QString name = QString::fromLatin1(kind) + QStringLiteral(".toml");
				const QString from = QDir(sessions.filePath(session)).filePath(name);
				if (!QFileInfo::exists(from))
					continue;
				// The first one too as the merge writes it, so that the file
				// changes only when a session adds to it (fingerprint()).
				const QString mine = QDir(own).filePath(name);
				QStringList arguments = {QString::fromLatin1(command), QStringLiteral("--out"), mine};
				if (QFileInfo::exists(mine) || will_exist.count(mine))
					arguments.push_back(mine);
				arguments.push_back(from);
				jobs.push_back({tool("ps2n-recomp.exe"), arguments, tr("adding %1 of %2").arg(name, session)});
				will_exist.insert(mine);
			}
		}
		runInTurn(std::move(jobs), [this]() {
			// A game's own recompile has nothing to do when the sessions met
			// nothing its modules were not compiled from.
			m_fingerprint = fingerprint();
			if (m_in.incremental &&
				ReadText(QDir(m_in.folder).filePath(QStringLiteral("work/compiled-from.txt"))).trimmed() == m_fingerprint)
			{
				m_nothing_new = true;
				m_running = false;
				emit finished(true, QString());
				return;
			}
			emit newCode();
			extract();
		});
	}

	void Pipeline::extract()
	{
		emit stage(tr("Taking the game's executables from its disc image"));
		const QString disc = QDir(m_in.folder).filePath(QStringLiteral("work/disc"));
		QDir().mkpath(disc);
		QStringList arguments = {QStringLiteral("extract"), m_in.image};
		arguments += m_in.executables;
		arguments += {QStringLiteral("--out-dir"), disc};
		runInTurn({{tool("ps2n-recomp.exe"), arguments, tr("extract %1").arg(m_in.executables.join(QStringLiteral(", ")))}},
			[this]() { translate(); });
	}

	void Pipeline::translate()
	{
		emit stage(tr("Translating the game's code and the BIOS's to C++"));
		const QDir folder(m_in.folder);
		const QString disc = folder.filePath(QStringLiteral("work/disc"));
		const QString gen = folder.filePath(QStringLiteral("work/generated"));
		const QString obj = folder.filePath(QStringLiteral("work/objects"));
		const QString own = folder.filePath(QStringLiteral("captures"));
		if (!m_in.incremental)
		{
			// What an earlier compile left is no guide to this one, nor are the
			// modules a recompile of the game's own left for its next start.
			QDir(gen).removeRecursively();
			QDir(obj).removeRecursively();
			const QDir work(folder.filePath(QStringLiteral("work")));
			for (const QString& next : work.entryList({QStringLiteral("next-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
				QDir(work.filePath(next)).removeRecursively();
		}
		else if (ReadText(QDir(obj).filePath(QStringLiteral("toolchain.txt"))) != toolchainStamp())
		{
			// Objects another toolchain compiled.
			QDir(obj).removeRecursively();
		}
		QDir().mkpath(gen);

		const auto captured = [&](const char* kind) {
			const QString file = QDir(own).filePath(QString::fromLatin1(kind) + QStringLiteral(".toml"));
			return QFileInfo::exists(file) ? file : QString();
		};
		const QString capture = captured("capture");
		const QString bios_capture = captured("bios-capture");
		const QString vu_capture = captured("vu-capture");
		const QString iop_capture = captured("iop-capture");

		std::deque<Job> jobs;
		const QString recomp = tool("ps2n-recomp.exe");
		const QString boot = QDir(disc).filePath(Leaf(m_in.executables.front()));

		// As the development build does (tests/tests.cmake): the boot executable
		// in 64 files, one the game loads itself in one file for each 64 KB of
		// it, at most 64, the micro code and the IOP code in 16. And the code
		// sessions met outside the executable in 16 as well, which is all of a
		// game's own code when its executable is a loader (Gran Turismo 3's).
		m_program_ids = {QStringLiteral("game")};
		QStringList arguments = {QStringLiteral("compile"), boot, QStringLiteral("--id"), QStringLiteral("game"), QStringLiteral("--out"),
			QDir(gen).filePath(QStringLiteral("game.cpp")), QStringLiteral("--parts"), QStringLiteral("64")};
		if (!capture.isEmpty())
		{
			arguments += {QStringLiteral("--capture"), capture, QStringLiteral("--outside"), QDir(gen).filePath(QStringLiteral("game_outside.cpp")),
				QStringLiteral("--outside-parts"), QStringLiteral("16")};
			m_program_ids.push_back(QStringLiteral("game_outside"));
		}
		jobs.push_back({recomp, arguments, tr("translate %1").arg(Leaf(boot))});
		for (int i = 1; i < m_in.executables.size(); i++)
		{
			const QString file = QDir(disc).filePath(Leaf(m_in.executables[i]));
			const QString id = LoadedId(m_in.executables[i]);
			const qint64 parts = std::min<qint64>(64, QFileInfo(file).size() / 65536 + 1);
			arguments = {QStringLiteral("compile"), file, QStringLiteral("--id"), id, QStringLiteral("--out"), QDir(gen).filePath(id + QStringLiteral(".cpp")),
				QStringLiteral("--loaded-by"), boot};
			if (parts > 1)
				arguments += {QStringLiteral("--parts"), QString::number(parts)};
			if (!capture.isEmpty())
				arguments += {QStringLiteral("--capture"), capture};
			jobs.push_back({recomp, arguments, tr("translate %1").arg(Leaf(file))});
			m_program_ids.push_back(id);
		}
		arguments = {QStringLiteral("bios"), m_in.bios, QStringLiteral("--out-dir"), QDir(gen).filePath(QStringLiteral("bios"))};
		if (!bios_capture.isEmpty())
			arguments += {QStringLiteral("--capture"), bios_capture};
		jobs.push_back({recomp, arguments, tr("translate the BIOS")});
		m_have_vu = !vu_capture.isEmpty();
		if (m_have_vu)
		{
			jobs.push_back({recomp, {QStringLiteral("vu"), vu_capture, QStringLiteral("--id"), QStringLiteral("game"), QStringLiteral("--out"),
										QDir(gen).filePath(QStringLiteral("vu/vu_game.cpp")), QStringLiteral("--parts"), QStringLiteral("16")},
				tr("translate the micro code met so far")});
		}
		m_have_iop = !iop_capture.isEmpty();
		if (m_have_iop)
		{
			jobs.push_back({recomp, {QStringLiteral("iop"), iop_capture, QStringLiteral("--id"), QStringLiteral("game"), QStringLiteral("--out"),
										QDir(gen).filePath(QStringLiteral("iop/iop_game.cpp")), QStringLiteral("--parts"), QStringLiteral("16")},
				tr("translate the IOP code met so far")});
		}
		const auto registry = [&](const char* kind, QStringList list) {
			list.prepend(QDir(gen).filePath(QStringLiteral("registry_%1.cpp").arg(QString::fromLatin1(kind))));
			list.prepend(QStringLiteral("--out"));
			list.prepend(QStringLiteral("registry"));
			jobs.push_back({recomp, list, tr("list the %1 module's code").arg(QString::fromLatin1(kind))});
		};
		registry("game", m_program_ids);
		registry("bios", {QStringLiteral("bios_rom0"), QStringLiteral("bios_rom1"), QStringLiteral("bios_kernel"), QStringLiteral("bios_loader")});
		registry("vu", m_have_vu ? QStringList{QStringLiteral("--vu"), QStringLiteral("game")} : QStringList{});
		registry("iop", m_have_iop ? QStringList{QStringLiteral("--iop"), QStringLiteral("game")} : QStringList{});
		runInTurn(std::move(jobs), [this]() { compileAll(); });
	}

	void Pipeline::compileAll()
	{
		const QDir folder(m_in.folder);
		const QString gen = folder.filePath(QStringLiteral("work/generated"));
		const QString obj = folder.filePath(QStringLiteral("work/objects"));
		const QString entry = QDir(m_in.toolchain).filePath(QStringLiteral("src/module_entry.cpp"));
		const std::pair<const char*, QStringList> kinds[] = {
			{"game", Files(gen, QStringLiteral("game*.cpp"))},
			{"bios", Files(QDir(gen).filePath(QStringLiteral("bios")), QStringLiteral("*.cpp"))},
			{"vu", Files(QDir(gen).filePath(QStringLiteral("vu")), QStringLiteral("*.cpp"))},
			{"iop", Files(QDir(gen).filePath(QStringLiteral("iop")), QStringLiteral("*.cpp"))},
		};
		for (const auto& [kind_name, sources] : kinds)
		{
			const QString kind = QString::fromLatin1(kind_name);
			const QString dir = QDir(obj).filePath(kind);
			QDir().mkpath(dir);
			QStringList all = sources;
			all += {QDir(gen).filePath(QStringLiteral("registry_%1.cpp").arg(kind)), entry};
			for (const QString& source : all)
				m_compiles.push_back({kind, source, QDir(dir).filePath(QFileInfo(source).completeBaseName() + QStringLiteral(".obj"))});
		}
		// The objects from now on are this toolchain's; a game's own recompile
		// compiles only what is newer than its object.
		if (!WriteText(QDir(obj).filePath(QStringLiteral("toolchain.txt")), toolchainStamp()))
		{
			fail(tr("Cannot write %1.").arg(QDir(obj).filePath(QStringLiteral("toolchain.txt"))));
			return;
		}
		for (size_t i = 0; i < m_compiles.size(); i++)
		{
			if (!m_in.incremental || Stale(m_compiles[i].source, m_compiles[i].object))
				m_todo.push_back(i);
		}
		emit stage(m_todo.size() == m_compiles.size() ?
					   tr("Compiling %1 files, %2 at a time").arg(m_todo.size()).arg(m_jobs) :
					   tr("Compiling %1 of %2 files, the others as they were, %3 at a time").arg(m_todo.size()).arg(m_compiles.size()).arg(m_jobs));
		emit progress(0, static_cast<int>(m_todo.size()));
		startCompiles();
	}

	void Pipeline::startCompiles()
	{
		if (!m_running)
			return;
		if (m_compiles_done == static_cast<int>(m_todo.size()))
		{
			link();
			return;
		}
		while (m_compiles_running < m_jobs && m_next_compile < m_todo.size())
		{
			const Compile c = m_compiles[m_todo[m_next_compile++]];
			QProcess* p = new QProcess(this);
			m_compile_processes.push_back(p);
			p->setProcessChannelMode(QProcess::MergedChannels);
			p->setProgram(tool("clang-cl.exe"));
			QStringList arguments = CompileFlags(m_in.toolchain);
			arguments += {QStringLiteral("/Fo") + QDir::toNativeSeparators(c.object), QDir::toNativeSeparators(c.source)};
			p->setArguments(arguments);
			m_compiles_running++;
			connect(p, &QProcess::finished, this, [this, p, c](int code, QProcess::ExitStatus status) {
				m_compiles_running--;
				m_compile_processes.erase(std::find(m_compile_processes.begin(), m_compile_processes.end(), p));
				const QString output = QString::fromLocal8Bit(p->readAll()).trimmed();
				p->deleteLater();
				// One stopped because another failed, or because the user stopped it all.
				if (!m_running)
					return;
				if (m_cancelled)
				{
					fail(tr("Stopped."));
					return;
				}
				if (status != QProcess::NormalExit || code != 0)
				{
					emit line(tr("%1 did not compile:").arg(QFileInfo(c.source).fileName()));
					if (!output.isEmpty())
						emit line(output);
					fail(tr("%1 did not compile (clang-cl exit code %2).").arg(QFileInfo(c.source).fileName()).arg(code));
					return;
				}
				m_compiles_done++;
				emit progress(m_compiles_done, static_cast<int>(m_todo.size()));
				startCompiles();
			});
			connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
				if (error == QProcess::FailedToStart)
					fail(tr("%1 did not start.").arg(p->program()));
			});
			p->start();
		}
	}

	void Pipeline::link()
	{
		emit stage(tr("Linking the game's four modules"));
		const QString obj = QDir(m_in.folder).filePath(QStringLiteral("work/objects"));
		const QString modules = m_in.modules.isEmpty() ? m_in.folder : m_in.modules;
		if (!QDir().mkpath(modules))
		{
			fail(tr("Cannot make %1.").arg(QDir::toNativeSeparators(modules)));
			return;
		}
		std::deque<Job> jobs;
		for (const char* kind_name : {"game", "bios", "vu", "iop"})
		{
			const QString kind = QString::fromLatin1(kind_name);
			const QString rsp = QDir(obj).filePath(kind + QStringLiteral(".rsp"));
			QFile file(rsp);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			{
				fail(tr("Cannot write %1.").arg(rsp));
				return;
			}
			for (const Compile& c : m_compiles)
			{
				if (c.kind == kind)
					file.write((QStringLiteral("\"") + QDir::toNativeSeparators(c.object) + QStringLiteral("\"\n")).toUtf8());
			}
			file.close();
			jobs.push_back({tool("lld-link.exe"),
				{QStringLiteral("/nologo"), QStringLiteral("/DLL"), QStringLiteral("/NOENTRY"), QStringLiteral("/NODEFAULTLIB"),
					QStringLiteral("/MACHINE:X64"), QStringLiteral("/OPT:REF"), QStringLiteral("/OPT:ICF"),
					QStringLiteral("/OUT:") + QDir::toNativeSeparators(QDir(modules).filePath(QStringLiteral("ps2n-%1.dll").arg(kind))),
					QStringLiteral("/IMPLIB:") + QDir::toNativeSeparators(QDir(obj).filePath(QStringLiteral("ps2n-%1.lib").arg(kind))),
					QStringLiteral("@") + QDir::toNativeSeparators(rsp),
					QDir::toNativeSeparators(QDir(m_in.toolchain).filePath(QStringLiteral("lib/ps2n-host.lib")))},
				tr("link ps2n-%1.dll").arg(kind)});
		}
		runInTurn(std::move(jobs), [this]() {
			// What these modules were compiled from, for a recompile of the
			// game's own to tell whether a session met anything new.
			if (!WriteText(QDir(m_in.folder).filePath(QStringLiteral("work/compiled-from.txt")), m_fingerprint + QLatin1Char('\n')))
			{
				fail(tr("Cannot write %1.").arg(QDir(m_in.folder).filePath(QStringLiteral("work/compiled-from.txt"))));
				return;
			}
			m_running = false;
			emit finished(true, QString());
		});
	}

	void Pipeline::runInTurn(std::deque<Job> jobs, std::function<void()> next)
	{
		m_turn = std::move(jobs);
		m_after_turn = std::move(next);
		runNextInTurn();
	}

	void Pipeline::runNextInTurn()
	{
		if (!m_running)
			return;
		if (m_cancelled)
		{
			fail(tr("Stopped."));
			return;
		}
		if (m_turn.empty())
		{
			std::function<void()> next = std::move(m_after_turn);
			m_after_turn = nullptr;
			next();
			return;
		}
		const Job job = std::move(m_turn.front());
		m_turn.pop_front();
		emit line(job.describe);
		m_process = new QProcess(this);
		m_process->setProcessChannelMode(QProcess::MergedChannels);
		m_process->setProgram(job.program);
		m_process->setArguments(job.arguments);
		connect(m_process, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus status) {
			const QString output = QString::fromLocal8Bit(m_process->readAll()).trimmed();
			m_process->deleteLater();
			m_process = nullptr;
			if (m_cancelled)
			{
				fail(tr("Stopped."));
				return;
			}
			if (status != QProcess::NormalExit || code != 0)
			{
				if (!output.isEmpty())
					emit line(output);
				fail(tr("%1 failed (exit code %2).").arg(job.describe).arg(code));
				return;
			}
			runNextInTurn();
		});
		connect(m_process, &QProcess::errorOccurred, this, [this, job](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart)
				fail(tr("%1: %2 did not start.").arg(job.describe, job.program));
		});
		m_process->start();
	}
} // namespace ps2n::wizard
