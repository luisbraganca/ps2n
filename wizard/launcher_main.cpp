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
// P4.6, docs/adr/0034), in a window of its own, in front (docs/adr/0040): when
// a session has ended, the launcher starts the folder's ps2n-remake
// (wizard/remake_main.cpp), which, when there is anything new, compiles it into
// work/next-<time>, saying so, unless it is left for later. The game never
// starts with anything left to compile: before the runtime, the launcher has
// ps2n-remake compile what is left, in the same window, and starts the game
// only when that is done, with those modules put in place; left for later
// there, the game does not start.
//
// Its message boxes are Qt's, which every game's folder carries for
// ps2n-remake: an application of Qt's is made only when one is shown.

#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

#include <dirent.h>
#include <fcntl.h>
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

	// The folder's own recompile, ps2n-remake (wizard/remake_main.cpp), which
	// compiles what the game's sessions met for the first time in a window of
	// its own, in front, and shows nothing when there is nothing to compile
	// (docs/adr/0040). After a session it is started apart from this program,
	// which ends at once, so that it is nobody's child to wait for. Before the
	// game starts (before_play) it is waited for, and what it ends with is
	// returned: 0 when nothing is left to compile, 3 when it was left for
	// later, 1 when it could not compile (its window said why); -1 when it did
	// not start. A folder without one (older than its own recompile) has
	// nothing to compile: 0.
	int Remake(const std::string& folder, bool before_play)
	{
		const std::string remake = folder + "ps2n-remake";
		if (!Exists(remake))
			return 0;
		const pid_t child = fork();
		if (child == 0)
		{
			if (!before_play)
			{
				setsid();
				if (fork() != 0)
					_exit(0);
			}
			if (chdir(folder.c_str()) != 0)
				_exit(127);
			const int none = open("/dev/null", O_RDWR);
			if (none >= 0)
			{
				dup2(none, 0);
				dup2(none, 1);
				dup2(none, 2);
			}
			if (before_play)
				execl(remake.c_str(), remake.c_str(), "--before-play", static_cast<char*>(nullptr));
			else
				execl(remake.c_str(), remake.c_str(), static_cast<char*>(nullptr));
			_exit(127);
		}
		if (child < 0)
			return -1;
		int status = 0;
		while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		{
		}
		if (!before_play)
			return 0;
		if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
			return WIFEXITED(status) ? -1 : 1;
		return WEXITSTATUS(status);
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

	// Nothing left to compile when the game starts (docs/adr/0040): what its
	// sessions met for the first time is compiled first, in ps2n-remake's
	// window, which says so, and the game starts when that is done; left for
	// later there, or not compiled, the game does not start. A ps2n-remake
	// compiling already, after a session, is waited for by this one.
	const int compiled = Remake(folder, true);
	if (compiled < 0)
	{
		Say(GameName(), "The game could not be started: " + folder +
							"ps2n-remake, which compiles first what its sessions met for the first time, did not start. Is the game's "
							"folder whole?");
		return 1;
	}
	if (compiled != 0)
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
		// What the session met for the first time, compiled now, in its window.
		Remake(folder, false);
	}
	return code;
}
