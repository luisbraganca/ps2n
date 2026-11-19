// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game's launcher (docs/phase4-proposal.md, step P4.4): the program a game's
// folder is started from, copied there under the game's name
// (scripts/windows/kit/make-game.ps1). It starts the runtime beside it,
// ps2n-host.exe, with the game's settings (settings.txt beside it; ps2n-host
// -settings) and whatever options it was given itself (-mute, for one),
// without a console window; keeps what the runtime reported in last-run.log
// beside it; and, when the runtime could not play the game, says why in a
// message box. The runtime is a console program, which started by itself
// would open a console beside the game.
//
// It needs nothing but Windows: Microsoft's C runtime is built into it
// (host/targets.cmake).
//
// The same program is the folder kit.ps1 puts together's PS2Native.exe: where
// there is no settings.txt beside it but the wizard is in the runtime folder
// beside it, it starts the wizard (docs/phase4-proposal.md, step P4.5).
//
// And a game compiles what its sessions meet for the first time itself (step
// P4.6, docs/adr/0034), in a window of its own, in front (docs/adr/0040): when
// a session has ended, the launcher starts the folder's ps2n-remake.exe
// (wizard/remake_main.cpp), which, when there is anything new, compiles it into
// work\next-<time>, saying so, unless it is left for later. The game never
// starts with anything left to compile: before the runtime, the launcher has
// ps2n-remake compile what is left, in the same window, and starts the game
// only when that is done, with those modules put in place; left for later
// there, the game does not start.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Windows' common controls of version 6, which give its message boxes
// Windows' own look: named in the program's manifest.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
	// This program's own path.
	std::wstring OwnPath()
	{
		std::wstring path(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0)
				return std::wstring();
			if (length < path.size())
			{
				path.resize(length);
				return path;
			}
			path.resize(path.size() * 2);
		}
	}

	// The folder this program is in, with its separator at the end.
	std::wstring OwnFolder()
	{
		const std::wstring path = OwnPath();
		return path.substr(0, path.find_last_of(L"\\/") + 1);
	}

	// Its windows drawn at the screen's scale rather than stretched to it
	// (Windows 10's version 1703 and after; looked up, so that an older one
	// starts it all the same).
	void DrawAtTheScreensScale()
	{
		using Set = BOOL(WINAPI*)(HANDLE);
		if (const HMODULE user = GetModuleHandleW(L"user32.dll"))
		{
			if (const Set set = reinterpret_cast<Set>(reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext"))))
				set(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		}
	}

	// An argument quoted so that the runtime's CommandLineToArgvW reads it back
	// as it was.
	std::wstring Quoted(const std::wstring& argument)
	{
		if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos)
			return argument;
		std::wstring out = L"\"";
		size_t backslashes = 0;
		for (const wchar_t c : argument)
		{
			if (c == L'\\')
			{
				backslashes++;
				continue;
			}
			out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
			backslashes = 0;
			out.push_back(c);
		}
		out.append(backslashes * 2, L'\\');
		out.push_back(L'"');
		return out;
	}

	std::wstring Widened(const std::string& text)
	{
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
		return wide;
	}

	void Say(const std::wstring& text)
	{
		MessageBoxW(nullptr, text.c_str(), L"PS2Native", MB_OK | MB_ICONERROR);
	}

	// A folder of files, and it.
	void RemoveFolder(const std::wstring& folder)
	{
		WIN32_FIND_DATAW found = {};
		const HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &found);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					DeleteFileW((folder + L"\\" + found.cFileName).c_str());
			} while (FindNextFileW(find, &found));
			FindClose(find);
		}
		RemoveDirectoryW(folder.c_str());
	}

	// The modules a recompile of the game's own (ps2n-remake) has finished
	// since the game last started. Each of its rounds links them into a folder
	// of its own, work\next-<time>, and writes "ready" there last. The newest
	// ready one is put in place before the runtime loads them, and every ready
	// one is taken away, an older one having less than the newer. Where a
	// module cannot be replaced (the game is running beside), the rest is left
	// for the next start.
	void TakeNewModules(const std::wstring& folder)
	{
		const std::wstring work = folder + L"work\\";
		std::vector<std::wstring> ready;
		WIN32_FIND_DATAW found = {};
		const HANDLE find = FindFirstFileW((work + L"next-*").c_str(), &found);
		if (find == INVALID_HANDLE_VALUE)
			return;
		do
		{
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				GetFileAttributesW((work + found.cFileName + L"\\ready").c_str()) != INVALID_FILE_ATTRIBUTES)
				ready.push_back(found.cFileName);
		} while (FindNextFileW(find, &found));
		FindClose(find);
		if (ready.empty())
			return;
		std::sort(ready.begin(), ready.end());
		const std::wstring newest = work + ready.back() + L"\\";
		for (const wchar_t* module : {L"ps2n-game.dll", L"ps2n-bios.dll", L"ps2n-vu.dll", L"ps2n-iop.dll"})
		{
			const std::wstring from = newest + module;
			// (Not there: put in place at a start that could not finish.)
			if (GetFileAttributesW(from.c_str()) != INVALID_FILE_ATTRIBUTES &&
				!MoveFileExW(from.c_str(), (folder + module).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
				return;
		}
		for (const std::wstring& name : ready)
			RemoveFolder(work + name);
	}

	// The folder's own recompile, ps2n-remake.exe (wizard/remake_main.cpp), at
	// normal priority, which compiles what the game's sessions met for the
	// first time in a window of its own, in front, and shows nothing when
	// there is nothing to compile (docs/adr/0040). Before the game starts
	// (before_play) it is waited for, and what it ends with is returned: 0 when
	// nothing is left to compile, 3 when it was left for later, 1 when it could
	// not compile (its window said why); kNotStarted, with Windows' error in
	// *error, when it did not start. A folder without one (older than its own
	// recompile) has nothing to compile: 0.
	constexpr DWORD kNotStarted = 0xFFFFFFFF;

	DWORD Remake(const std::wstring& folder, bool before_play, DWORD* error)
	{
		const std::wstring remake = folder + L"ps2n-remake.exe";
		if (GetFileAttributesW(remake.c_str()) == INVALID_FILE_ATTRIBUTES)
			return 0;
		std::wstring command = Quoted(remake);
		if (before_play)
			command += L" --before-play";
		// Its window in front, where this program may give it the right to be:
		// a program started a moment ago by the user's double-click may; after
		// a session the game's runtime gave that right as its window closed
		// (host/window.cpp).
		AllowSetForegroundWindow(ASFW_ANY);
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (!CreateProcessW(remake.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process))
		{
			*error = GetLastError();
			return kNotStarted;
		}
		CloseHandle(process.hThread);
		DWORD code = 0;
		if (before_play && (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
							   !GetExitCodeProcess(process.hProcess, &code)))
			code = 1;
		CloseHandle(process.hProcess);
		return code;
	}

	// What the runtime said of itself: its last few lines that begin with
	// "ps2n-host:" (the help it prints after an option it does not take, and
	// the core's own messages, are left to the log), or, where it said nothing
	// of the kind, the end of what it wrote.
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
			text += said[i % kLines] + "\r\n";
		return text;
	}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	DrawAtTheScreensScale();
	const std::wstring folder = OwnFolder();

	// PS2Native's own folder: the wizard, which has a window of its own and
	// says itself what goes wrong.
	const std::wstring wizard = folder + L"runtime\\ps2n-wizard.exe";
	if (GetFileAttributesW((folder + L"settings.txt").c_str()) == INVALID_FILE_ATTRIBUTES &&
		GetFileAttributesW(wizard.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		std::wstring command = Quoted(wizard);
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (!CreateProcessW(wizard.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, (folder + L"runtime").c_str(),
				&startup, &process))
		{
			Say(L"PS2Native could not be started: " + wizard + L" did not start (Windows error " + std::to_wstring(GetLastError()) +
				L"). Is PS2Native's folder whole?");
			return 1;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return 0;
	}

	// Nothing left to compile when the game starts (docs/adr/0040): what its
	// sessions met for the first time is compiled first, in ps2n-remake's
	// window, which says so, and the game starts when that is done; left for
	// later there, or not compiled, the game does not start. A ps2n-remake
	// compiling already, after a session, is waited for by this one.
	DWORD error = 0;
	const DWORD compiled = Remake(folder, true, &error);
	if (compiled == kNotStarted)
	{
		Say(L"The game could not be started: " + folder + L"ps2n-remake.exe, which compiles first what its sessions met for the first "
			L"time, did not start (Windows error " + std::to_wstring(error) + L"). Is the game's folder whole?");
		return 1;
	}
	if (compiled != 0)
		return 0;

	TakeNewModules(folder);

	const std::wstring host = folder + L"ps2n-host.exe";
	std::wstring command = Quoted(host) + L" -settings " + Quoted(folder + L"settings.txt");
	int count = 0;
	if (LPWSTR* given = CommandLineToArgvW(GetCommandLineW(), &count))
	{
		for (int i = 1; i < count; i++)
			command += L" " + Quoted(given[i]);
		LocalFree(given);
	}

	// What the runtime writes, read as it comes, so that it never waits on a
	// full pipe.
	SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
	HANDLE read = nullptr, write = nullptr;
	if (!CreatePipe(&read, &write, &inherit, 0))
	{
		Say(L"The game could not be started (no pipe for the runtime's report).");
		return 1;
	}
	SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = write;
	startup.hStdError = write;
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(),
			&startup, &process))
	{
		Say(L"The game could not be started: " + host + L" did not start (Windows error " + std::to_wstring(GetLastError()) +
			L"). Is the game's folder whole?");
		return 1;
	}
	CloseHandle(write);
	CloseHandle(process.hThread);

	std::string output;
	char buffer[4096];
	DWORD got = 0;
	while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got > 0)
	{
		output.append(buffer, got);
		if (output.size() > (1u << 20))
			output.erase(0, output.size() - (1u << 19));
	}
	CloseHandle(read);
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hProcess);

	const HANDLE log = CreateFileW((folder + L"last-run.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		WriteFile(log, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
		CloseHandle(log);
	}

	// The runtime's exit codes: 0 the session ended; 1 the machine could not be
	// set up; 4 its options were not ones it takes. (2 and 3: a limit it was
	// given passed.)
	if (code == 1 || code == 4)
	{
		Say(L"The game could not be played. What the runtime said (all of it is in last-run.log in the game's folder):\n\n" +
			Widened(WhatItSaid(output)));
	}
	else if (code == 0 || code == 2 || code == 3)
	{
		// What the session met for the first time, compiled now, in its window.
		DWORD not_started = 0;
		Remake(folder, false, &not_started);
	}
	return static_cast<int>(code);
}
