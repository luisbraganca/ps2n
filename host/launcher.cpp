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
// P4.6, docs/adr/0034): when a session has ended, the launcher starts the
// folder's ps2n-remake.exe (wizard/remake_main.cpp) at low priority, which
// compiles it, when there is anything new, into work\next-<time>; and at the
// game's next start, before the runtime loads them, it puts those modules in
// place.
//
// A game does not play while it compiles (docs/adr/0037): started while the
// folder's ps2n-remake compiles, the launcher waits for it, in a window that
// says so, and then plays, with what it compiled put in place; that window
// closed, the game is not started, and the compile goes on.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Windows' common controls of version 6, which have the window a game waits
// for its compile in (TaskDialogIndirect, looked up when it is needed): named
// in the program's manifest.
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

	// The game's name: this program's, which is named after it.
	std::wstring GameName()
	{
		std::wstring name = OwnPath();
		name.erase(0, name.find_last_of(L"\\/") + 1);
		const size_t dot = name.find_last_of(L'.');
		return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
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

	// What the session met for the first time, compiled in the background
	// for the game's next start; ps2n-remake does nothing, and shows nothing,
	// when the session met nothing new.
	void StartRemake(const std::wstring& folder)
	{
		const std::wstring remake = folder + L"ps2n-remake.exe";
		if (GetFileAttributesW(remake.c_str()) == INVALID_FILE_ATTRIBUTES)
			return;
		std::wstring command = Quoted(remake);
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (CreateProcessW(remake.c_str(), command.data(), nullptr, nullptr, FALSE, BELOW_NORMAL_PRIORITY_CLASS, nullptr,
				folder.c_str(), &startup, &process))
		{
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
	}

	// A file's identity, the same whatever path names it: its volume and its
	// number there.
	struct FileId
	{
		DWORD volume = 0;
		DWORD high = 0;
		DWORD low = 0;

		bool operator==(const FileId& other) const
		{
			return volume == other.volume && high == other.high && low == other.low;
		}
	};

	bool IdOf(const std::wstring& path, FileId* id)
	{
		const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		BY_HANDLE_FILE_INFORMATION info = {};
		const bool known = GetFileInformationByHandle(file, &info) != FALSE;
		CloseHandle(file);
		*id = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
		return known;
	}

	// How long a ps2n-remake just started is given to take its lock, which it
	// takes as it starts, once its libraries are loaded: 20 s, in Windows'
	// units of 100 ns.
	constexpr ULONGLONG kTakingItsLock = 20ull * 10000000;

	// Whether a ps2n-remake of this folder's was started less than that ago
	// and runs still. The launcher that started it has ended by then: it is
	// found among the programs running by its file, this folder's
	// ps2n-remake.exe, whatever path it was started by.
	bool JustStarted(const std::wstring& folder)
	{
		FileId remake;
		if (!IdOf(folder + L"ps2n-remake.exe", &remake))
			return false;
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return false;
		const auto ticks = [](const FILETIME& time) {
			return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
		};
		FILETIME now = {};
		GetSystemTimeAsFileTime(&now);
		bool found = false;
		PROCESSENTRY32W entry = {};
		entry.dwSize = sizeof(entry);
		for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
		{
			if (CompareStringOrdinal(entry.szExeFile, -1, L"ps2n-remake.exe", -1, TRUE) != CSTR_EQUAL)
				continue;
			const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
			if (!process)
				continue;
			FILETIME created = {}, exited = {}, kernel = {}, user = {};
			DWORD code = 0;
			std::wstring image(32768, L'\0');
			DWORD length = static_cast<DWORD>(image.size());
			FileId id;
			found = GetProcessTimes(process, &created, &exited, &kernel, &user) && ticks(now) - ticks(created) < kTakingItsLock &&
					GetExitCodeProcess(process, &code) && code == STILL_ACTIVE &&
					QueryFullProcessImageNameW(process, 0, image.data(), &length) && IdOf(image.substr(0, length), &id) && id == remake;
			CloseHandle(process);
		}
		CloseHandle(snapshot);
		return found;
	}

	// Whether the folder's own recompile is compiling: ps2n-remake holds
	// work\remake.lock, open for nobody else to write, from its start until
	// its modules are ready (wizard/remake_main.cpp); and one started a moment
	// ago may not have taken it yet.
	bool Compiling(const std::wstring& folder)
	{
		const HANDLE lock = CreateFileW((folder + L"work\\remake.lock").c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (lock != INVALID_HANDLE_VALUE)
			CloseHandle(lock); // left by one that did not end as it should have
		else if (GetLastError() == ERROR_SHARING_VIOLATION)
			return true;
		return JustStarted(folder);
	}

	struct Waiting
	{
		std::wstring folder;
		bool done = false;
	};

	HRESULT CALLBACK WhileWaiting(HWND window, UINT notification, WPARAM, LPARAM, LONG_PTR data)
	{
		Waiting& waiting = *reinterpret_cast<Waiting*>(data);
		if (notification == TDN_CREATED)
		{
			SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 0);
		}
		else if (notification == TDN_TIMER && !waiting.done && !Compiling(waiting.folder))
		{
			waiting.done = true;
			SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
		}
		return S_OK;
	}

	// Waits while the folder's own recompile compiles: a moment without a
	// window (one that finds nothing new is done at once), then in a window
	// that says so, which, closed, leaves the game unstarted. Whether the
	// compile is done.
	bool WaitForTheCompile(const std::wstring& folder)
	{
		for (int i = 0; i < 15; i++)
		{
			Sleep(100);
			if (!Compiling(folder))
				return true;
		}
		const std::wstring game = GameName();
		const std::wstring content = game + L" starts by itself when that is done.\n\nCancel to play later instead: the compile goes "
										   L"on, and a notification says when it is done.";
		using Show = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
		const HMODULE controls = LoadLibraryW(L"comctl32.dll");
		const Show show =
			controls ? reinterpret_cast<Show>(reinterpret_cast<void*>(GetProcAddress(controls, "TaskDialogIndirect"))) : nullptr;
		Waiting waiting;
		waiting.folder = folder;
		TASKDIALOGCONFIG config = {};
		config.cbSize = sizeof(config);
		config.hInstance = GetModuleHandleW(nullptr);
		config.dwFlags = TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION;
		config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
		config.pszWindowTitle = game.c_str();
		config.pszMainIcon = MAKEINTRESOURCEW(1); // PS2Native's (assets/ps2n.rc)
		config.pszMainInstruction = L"Compiling what your last session met for the first time";
		config.pszContent = content.c_str();
		config.pfCallback = WhileWaiting;
		config.lpCallbackData = reinterpret_cast<LONG_PTR>(&waiting);
		if (!show || FAILED(show(&config, nullptr, nullptr, nullptr)))
		{
			MessageBoxW(nullptr,
				(game + L" is compiling what your last session met for the first time. Start it again when it says it is done.").c_str(),
				game.c_str(), MB_OK | MB_ICONINFORMATION);
			return false;
		}
		return waiting.done;
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

	// Not while the game compiles (docs/adr/0037): it waits, and plays what was
	// compiled; or, the window closed, it is not started.
	if (Compiling(folder) && !WaitForTheCompile(folder))
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
		StartRemake(folder);
	}
	return static_cast<int>(code);
}
