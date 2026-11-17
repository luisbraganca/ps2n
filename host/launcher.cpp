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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace
{
	// The folder this program is in, with its separator at the end.
	std::wstring OwnFolder()
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
				break;
			}
			path.resize(path.size() * 2);
		}
		return path.substr(0, path.find_last_of(L"\\/") + 1);
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
	const std::wstring folder = OwnFolder();
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
	return static_cast<int>(code);
}
