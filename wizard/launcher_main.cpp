// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game's launcher on Linux (step P4.7): the counterpart of host/launcher.cpp,
// the program a game's folder is started from, copied there under the game's
// name. As that one does on Windows, it starts the runtime beside it,
// ps2n-host, with the game's settings (settings.txt beside it; ps2n-host
// -settings) and whatever options it was given itself (-mute, for one); keeps
// what the runtime reported in last-run.log; and, when the runtime could not
// play the game, says why in a message box.
//
// The same program is the folder kit.sh puts together's PS2Native: where there
// is no settings.txt beside it but the wizard is in the runtime folder beside
// it, it starts the wizard (step P4.5).
//
// A game compiles what its sessions meet for the first time itself (step
// P4.6, docs/adr/0034): when a session has ended, the launcher starts the
// folder's ps2n-remake (wizard/remake_main.cpp) at low priority, which compiles
// it, when there is anything new, into work/next-<time>; at the game's next
// start, before the runtime loads them, the launcher puts those modules in
// place. And a game does not play while it compiles (docs/adr/0037): started
// while the folder's ps2n-remake compiles, the launcher waits for it, in a
// window that says so, and then plays, with what it compiled put in place;
// that window closed, the game is not started, and the compile goes on.
//
// Its windows are Qt's, which every game's folder carries for ps2n-remake: an
// application of Qt's is made only when one is shown.

#include <QtCore/QTimer>
#include <QtGui/QFont>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QVBoxLayout>

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
	// This program's own path.
	std::string OwnPath()
	{
		char buffer[PATH_MAX];
		const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		return length > 0 ? std::string(buffer, static_cast<size_t>(length)) : std::string();
	}

	// The folder this program is in, with its separator at the end.
	std::string OwnFolder()
	{
		const std::string path = OwnPath();
		return path.substr(0, path.find_last_of('/') + 1);
	}

	// The game's name: this program's, which is named after it.
	std::string GameName()
	{
		const std::string path = OwnPath();
		return path.substr(path.find_last_of('/') + 1);
	}

	bool Exists(const std::string& path)
	{
		struct stat info;
		return stat(path.c_str(), &info) == 0;
	}

	// An application of Qt's, for the windows below, made the first time one is
	// shown; null where there is no display to show one on. It goes before
	// main returns (ApplicationGoes): left to go with the program's static
	// data, it found Qt's own gone and crashed the launcher as it ended.
	int s_argc = 0;
	char** s_argv = nullptr;
	std::unique_ptr<QApplication> s_app;
	QApplication* Application()
	{
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY"))
			{
				s_app = std::make_unique<QApplication>(s_argc, s_argv);
				QApplication::setApplicationName(QStringLiteral("PS2Native"));
				QApplication::setWindowIcon(QIcon(QStringLiteral(":/ps2native/icon.png")));
			}
		}
		return s_app.get();
	}
	struct ApplicationGoes
	{
		~ApplicationGoes() { s_app.reset(); }
	};

	// Why it cannot go on, in a message box, and on its error stream.
	void Say(const std::string& title, const std::string& text)
	{
		std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
		if (Application())
			QMessageBox::critical(nullptr, QString::fromStdString(title), QString::fromStdString(text));
	}

	// A folder of files, and it.
	void RemoveFolder(const std::string& folder)
	{
		if (DIR* dir = opendir(folder.c_str()))
		{
			while (const dirent* entry = readdir(dir))
			{
				const std::string path = folder + "/" + entry->d_name;
				struct stat info;
				if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode))
					unlink(path.c_str());
			}
			closedir(dir);
		}
		rmdir(folder.c_str());
	}

	// The modules a recompile of the game's own (ps2n-remake) has finished
	// since the game last started. Each of its rounds links them into a folder
	// of its own, work/next-<time>, and writes "ready" there last. The newest
	// ready one is put in place before the runtime loads them, and every ready
	// one is taken away, an older one having less than the newer.
	void TakeNewModules(const std::string& folder)
	{
		const std::string work = folder + "work/";
		std::vector<std::string> ready;
		if (DIR* dir = opendir(work.c_str()))
		{
			while (const dirent* entry = readdir(dir))
			{
				const std::string name = entry->d_name;
				if (name.rfind("next-", 0) == 0 && Exists(work + name + "/ready"))
					ready.push_back(name);
			}
			closedir(dir);
		}
		if (ready.empty())
			return;
		std::sort(ready.begin(), ready.end());
		const std::string newest = work + ready.back() + "/";
		for (const char* module : {"ps2n-game.so", "ps2n-bios.so", "ps2n-vu.so", "ps2n-iop.so"})
		{
			const std::string from = newest + module;
			// (Not there: put in place at a start that could not finish.)
			if (Exists(from) && rename(from.c_str(), (folder + module).c_str()) != 0)
				return;
		}
		for (const std::string& name : ready)
			RemoveFolder(work + name);
	}

	// What the session met for the first time, compiled in the background for
	// the game's next start, at low priority; ps2n-remake does nothing, and
	// shows nothing, when the session met nothing new. Started apart from this
	// program, which ends at once, so that it is nobody's child to wait for.
	void StartRemake(const std::string& folder)
	{
		const std::string remake = folder + "ps2n-remake";
		if (!Exists(remake))
			return;
		const pid_t child = fork();
		if (child == 0)
		{
			setsid();
			if (fork() == 0)
			{
				setpriority(PRIO_PROCESS, 0, 10); // as Windows' "below normal"
				if (chdir(folder.c_str()) != 0)
					_exit(127);
				const int none = open("/dev/null", O_RDWR);
				if (none >= 0)
				{
					dup2(none, 0);
					dup2(none, 1);
					dup2(none, 2);
				}
				execl(remake.c_str(), remake.c_str(), static_cast<char*>(nullptr));
				_exit(127);
			}
			_exit(0);
		}
		if (child > 0)
			waitpid(child, nullptr, 0);
	}

	// How long a ps2n-remake just started is given to take its lock, which it
	// takes as it starts, once its libraries are loaded.
	constexpr double kTakingItsLock = 20.0;

	// Whether a ps2n-remake of this folder's was started less than that ago and
	// runs still. The launcher that started it has ended by then: it is found
	// among the programs running by its file, this folder's ps2n-remake,
	// whatever path it was started by.
	bool JustStarted(const std::string& folder)
	{
		struct stat remake;
		if (stat((folder + "ps2n-remake").c_str(), &remake) != 0)
			return false;
		double uptime = 0.0;
		if (FILE* file = std::fopen("/proc/uptime", "r"))
		{
			if (std::fscanf(file, "%lf", &uptime) != 1)
				uptime = 0.0;
			std::fclose(file);
		}
		const long ticks = sysconf(_SC_CLK_TCK);
		bool found = false;
		if (DIR* proc = opendir("/proc"))
		{
			while (const dirent* entry = readdir(proc))
			{
				if (found)
					break;
				const std::string pid = entry->d_name;
				if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos)
					continue;
				struct stat program;
				if (stat(("/proc/" + pid + "/exe").c_str(), &program) != 0 || program.st_dev != remake.st_dev ||
					program.st_ino != remake.st_ino)
					continue;
				// Its start, in ticks after the system's: the 22nd field of its
				// stat, counted after its name, which is in parentheses.
				FILE* file = std::fopen(("/proc/" + pid + "/stat").c_str(), "r");
				if (!file)
					continue;
				char line[4096] = {};
				const size_t length = std::fread(line, 1, sizeof(line) - 1, file);
				std::fclose(file);
				const char* rest = std::strrchr(line, ')');
				if (!rest || length == 0)
					continue;
				char state = 0;
				unsigned long long started = 0;
				if (std::sscanf(rest + 1, " %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu", &state,
						&started) == 2 &&
					state != 'Z' && ticks > 0 && uptime - static_cast<double>(started) / static_cast<double>(ticks) < kTakingItsLock)
					found = true;
			}
			closedir(proc);
		}
		return found;
	}

	// Whether the folder's own recompile is compiling: ps2n-remake holds
	// work/remake.lock, locked for itself (Qt's QLockFile, flock), from its
	// start until its modules are ready (wizard/remake_main.cpp); and one started
	// a moment ago may not have taken it yet.
	bool Compiling(const std::string& folder)
	{
		const int lock = open((folder + "work/remake.lock").c_str(), O_RDONLY | O_CLOEXEC);
		if (lock >= 0)
		{
			const bool held = flock(lock, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
			close(lock); // (a lock file left by one that did not end as it should have is not held)
			if (held)
				return true;
		}
		return JustStarted(folder);
	}

	// Waits while the folder's own recompile compiles: a moment without a
	// window (one that finds nothing new is done at once), then in a window that
	// says so, which, closed, leaves the game unstarted. Whether the compile is
	// done.
	bool WaitForTheCompile(const std::string& folder)
	{
		for (int i = 0; i < 15; i++)
		{
			usleep(100000);
			if (!Compiling(folder))
				return true;
		}
		const QString game = QString::fromStdString(GameName());
		if (!Application())
		{
			std::fprintf(stderr, "%s is compiling what your last session met for the first time. Start it again when it says it is done.\n",
				GameName().c_str());
			return false;
		}
		QDialog dialog;
		dialog.setWindowTitle(game);
		QVBoxLayout* layout = new QVBoxLayout(&dialog);
		QLabel* instruction = new QLabel(QStringLiteral("Compiling what your last session met for the first time"));
		QFont larger = instruction->font();
		larger.setPointSizeF(larger.pointSizeF() * 1.3);
		instruction->setFont(larger);
		instruction->setWordWrap(true);
		QLabel* content = new QLabel(game + QStringLiteral(" starts by itself when that is done.\n\nCancel to play later instead: the compile goes "
														   "on, and a notification says when it is done."));
		content->setWordWrap(true);
		QProgressBar* bar = new QProgressBar();
		bar->setRange(0, 0); // moving: how long it takes is not known
		bar->setTextVisible(false);
		QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(instruction);
		layout->addWidget(content);
		layout->addWidget(bar);
		layout->addWidget(buttons);
		dialog.setMinimumWidth(420);
		bool done = false;
		QTimer timer;
		QObject::connect(&timer, &QTimer::timeout, &dialog, [&]() {
			if (!Compiling(folder))
			{
				done = true;
				dialog.accept();
			}
		});
		timer.start(250);
		dialog.exec();
		return done;
	}

	// What the runtime said of itself: its last few lines that begin with
	// "ps2n-host:" (the help it prints after an option it does not take, and the
	// core's own messages, are left to the log), or, where it said nothing of the
	// kind, the end of what it wrote.
	std::string WhatItSaid(const std::string& output)
	{
		constexpr size_t kLines = 8;
		std::string said[kLines];
		size_t count = 0;
		for (size_t start = 0; start < output.size();)
		{
			size_t end = output.find('\n', start);
			if (end == std::string::npos)
				end = output.size();
			std::string line = output.substr(start, end - start);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			if (line.rfind("ps2n-host:", 0) == 0)
				said[count++ % kLines] = line;
			start = end + 1;
		}
		if (count == 0)
			return output.size() > 2000 ? output.substr(output.size() - 2000) : output;
		std::string text;
		for (size_t i = count > kLines ? count - kLines : 0; i < count; i++)
			text += said[i % kLines] + "\n";
		return text;
	}

	// The runtime, with the folder's settings and the options this program was
	// given, what it writes read as it comes. Its exit code, or 128 and the
	// signal that ended it; -1 when it did not start.
	int RunTheRuntime(const std::string& folder, int argc, char** argv, std::string* output)
	{
		const std::string host = folder + "ps2n-host";
		const std::string settings = folder + "settings.txt";
		std::vector<std::string> arguments = {host, "-settings", settings};
		for (int i = 1; i < argc; i++)
			arguments.push_back(argv[i]);
		std::vector<char*> pointers;
		for (std::string& argument : arguments)
			pointers.push_back(argument.data());
		pointers.push_back(nullptr);

		int pipe_fds[2];
		if (pipe2(pipe_fds, O_CLOEXEC) != 0)
			return -1;
		const pid_t child = fork();
		if (child < 0)
		{
			close(pipe_fds[0]);
			close(pipe_fds[1]);
			return -1;
		}
		if (child == 0)
		{
			dup2(pipe_fds[1], 1);
			dup2(pipe_fds[1], 2);
			if (chdir(folder.c_str()) != 0)
				_exit(127);
			execv(host.c_str(), pointers.data());
			_exit(127);
		}
		close(pipe_fds[1]);
		char buffer[4096];
		ssize_t got = 0;
		while ((got = read(pipe_fds[0], buffer, sizeof(buffer))) > 0 || (got < 0 && errno == EINTR))
		{
			if (got <= 0)
				continue;
			output->append(buffer, static_cast<size_t>(got));
			if (output->size() > (1u << 20))
				output->erase(0, output->size() - (1u << 19));
		}
		close(pipe_fds[0]);
		int status = 0;
		while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		{
		}
		if (WIFEXITED(status))
			return WEXITSTATUS(status) == 127 && output->empty() ? -1 : WEXITSTATUS(status);
		return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
	}
} // namespace

int main(int argc, char* argv[])
{
	s_argc = argc;
	s_argv = argv;
	const ApplicationGoes application_goes;
	const std::string folder = OwnFolder();

	// PS2Native's own folder: the wizard, which has a window of its own and
	// says itself what goes wrong.
	const std::string wizard = folder + "runtime/ps2n-wizard";
	if (!Exists(folder + "settings.txt") && Exists(wizard))
	{
		if (chdir((folder + "runtime").c_str()) == 0)
		{
			std::vector<char*> arguments = {const_cast<char*>(wizard.c_str()), nullptr};
			execv(wizard.c_str(), arguments.data());
		}
		Say("PS2Native", "PS2Native could not be started: " + wizard + " did not start (" + std::strerror(errno) +
							 "). Is PS2Native's folder whole?");
		return 1;
	}

	// Not while the game compiles (docs/adr/0037): it waits, and plays what was
	// compiled; or, the window closed, it is not started.
	if (Compiling(folder) && !WaitForTheCompile(folder))
		return 0;

	TakeNewModules(folder);

	std::string output;
	const int code = RunTheRuntime(folder, argc, argv, &output);
	if (code < 0)
	{
		Say(GameName(), "The game could not be started: " + folder + "ps2n-host did not start. Is the game's folder whole?");
		return 1;
	}

	if (FILE* log = std::fopen((folder + "last-run.log").c_str(), "wb"))
	{
		std::fwrite(output.data(), 1, output.size(), log);
		std::fclose(log);
	}

	// The runtime's exit codes: 0 the session ended; 1 the machine could not be
	// set up; 4 its options were not ones it takes. (2 and 3: a limit it was
	// given passed.)
	if (code == 1 || code == 4)
	{
		Say(GameName(), "The game could not be played. What the runtime said (all of it is in last-run.log in the game's folder):\n\n" +
							WhatItSaid(output));
	}
	else if (code == 0 || code == 2 || code == 3)
	{
		StartRemake(folder);
	}
	return code;
}
