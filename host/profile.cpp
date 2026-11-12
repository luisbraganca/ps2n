// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a run's time goes: a sampling profiler for the CPU thread. Windows
// only so far: on other platforms Available() says no.

#include "profile.h"

#ifdef _WIN32

#include "common/RedtapeWindows.h"

#include <dbghelp.h>

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Memory.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace ps2n::host::profile
{
	namespace
	{
		// An hour at a thousand samples a second. The room is taken up front:
		// nothing may be allocated while the CPU thread is held, because it may
		// be holding the allocator's lock.
		constexpr size_t kMaxSamples = 4 * 1000 * 1000;

		std::thread s_sampler;
		std::atomic<bool> s_running{false};
		std::atomic<bool> s_enabled{false};
		HANDLE s_thread = nullptr;
		std::vector<u64> s_samples;
		u64 s_dropped = 0;

		// The areas PCSX2's recompilers generate host code into. They belong to
		// no module, so they are told apart by address.
		struct Area
		{
			const u8* begin;
			const u8* end;
			const char* name;
		};
		std::vector<Area> s_areas;

		// PS2Native's source directory as the build knew it, in lower case with
		// forward slashes, without one at the end (host/targets.cmake).
		std::string s_source_root;

		// The kinds of code a sample can fall into, in the order they are reported.
		enum Kind
		{
			kGame, // the game's code, compiled ahead of time
			kRuntime, // PS2Native's runtime: dispatcher, block record, interpreter, instruction semantics
			kCore, // PCSX2's core, the C++ of it
			kGenerated, // host code PCSX2's recompilers generated at run time
			kSystem, // Windows, drivers, other libraries
			kUnknown, // an address in no module and no known area
			kKindCount,
		};
		constexpr const char* kKindNames[kKindCount] = {
			"the game's code, compiled ahead of time",
			"PS2Native's runtime",
			"PCSX2's core",
			"host code PCSX2 generated at run time",
			"Windows, drivers and other libraries",
			"not identified",
		};

		void SamplerMain()
		{
			// A timer that really comes every millisecond.
			const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			if (timer)
			{
				LARGE_INTEGER due;
				due.QuadPart = -10000; // 1 ms, in units of 100 ns
				SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE);
			}

			while (s_running.load())
			{
				if (timer)
					WaitForSingleObject(timer, 100);
				else
					Sleep(1);
				if (!s_enabled.load())
					continue;
				if (s_samples.size() >= kMaxSamples)
				{
					s_dropped++;
					continue;
				}

				CONTEXT context;
				context.ContextFlags = CONTEXT_CONTROL;
				if (SuspendThread(s_thread) == static_cast<DWORD>(-1))
					continue;
				const BOOL ok = GetThreadContext(s_thread, &context);
				ResumeThread(s_thread);
				if (ok)
					s_samples.push_back(context.Rip);
			}

			if (timer)
				CloseHandle(timer);
		}

		// What the symbol handler says while it looks for symbols, kept for the
		// report: when the host's functions come out without names, it says why.
		BOOL CALLBACK SymbolMessages(HANDLE process, ULONG action, ULONG64 data, ULONG64 context)
		{
			if (action != CBA_DEBUG_INFO || !data)
				return FALSE;

			std::string* text = reinterpret_cast<std::string*>(static_cast<uintptr_t>(context));
			if (text->size() < 6000)
				text->append(reinterpret_cast<const char*>(static_cast<uintptr_t>(data)));
			return TRUE;
		}

		struct Place
		{
			Kind kind;
			std::string name;
		};

		Place Identify(HANDLE process, u64 address)
		{
			for (const Area& area : s_areas)
			{
				const u8* p = reinterpret_cast<const u8*>(static_cast<uintptr_t>(address));
				if (p >= area.begin && p < area.end)
					return {kGenerated, area.name};
			}

			IMAGEHLP_MODULE64 module = {};
			module.SizeOfStruct = sizeof(module);
			if (!SymGetModuleInfo64(process, address, &module))
				return {kUnknown, "(an address in no module)"};

			char buffer[sizeof(SYMBOL_INFO) + 1024] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 1023;
			DWORD64 displacement = 0;
			const bool named = SymFromAddr(process, address, &displacement, symbol) != FALSE;

			// The host itself holds three kinds of code. The game's compiled
			// functions say so in their names. The rest is told apart by the
			// source file the instruction came from: PS2Native's own directories,
			// or anything else, which is PCSX2's core and what it is built from.
			if (_stricmp(module.ModuleName, "ps2n-host") == 0)
			{
				if (!named)
					return {kCore, "(a function of the host without a name)"};
				const std::string name(symbol->Name);
				if (name.find("ps2n_image_") != std::string::npos)
					return {kGame, name};

				IMAGEHLP_LINE64 line = {};
				line.SizeOfStruct = sizeof(line);
				DWORD column = 0;
				if (SymGetLineFromAddr64(process, address, &column, &line) && line.FileName)
				{
					std::string file(line.FileName);
					for (char& c : file)
						c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					for (const char* directory : {"/runtime/", "/host/", "/core/"})
					{
						if (file.rfind(s_source_root + directory, 0) == 0)
							return {kRuntime, name};
					}
					return {kCore, name};
				}
				return {name.rfind("ps2n::", 0) == 0 ? kRuntime : kCore, name};
			}
			return {kSystem, std::string(module.ModuleName) + "!" + (named ? symbol->Name : "(no name)")};
		}
	} // namespace

	bool Available()
	{
		return true;
	}

	bool Start(void* thread)
	{
		if (s_running.load() || !thread)
			return false;

		// A handle of our own: the caller's may be closed before the sampler ends.
		if (!DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(thread), GetCurrentProcess(), &s_thread,
				THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
		{
			return false;
		}

		s_samples.clear();
		s_samples.reserve(kMaxSamples);
		s_dropped = 0;
		s_running.store(true);
		s_sampler = std::thread(SamplerMain);
		return true;
	}

	void Enable(bool on)
	{
		if (on)
		{
			s_areas = {
				{SysMemory::GetEERec(), SysMemory::GetEERecEnd(), "host code from PCSX2's EE recompiler"},
				{SysMemory::GetIOPRec(), SysMemory::GetIOPRecEnd(), "host code from PCSX2's IOP recompiler"},
				{SysMemory::GetVU0Rec(), SysMemory::GetVU0RecEnd(), "host code from PCSX2's VU0 recompiler"},
				{SysMemory::GetVU1Rec(), SysMemory::GetVU1RecEnd(), "host code from PCSX2's VU1 recompiler"},
				{SysMemory::GetVIFUnpackRec(), SysMemory::GetVIFUnpackRecEnd(), "host code from PCSX2's VIF unpack generator"},
				{SysMemory::GetSWRec(), SysMemory::GetSWRecEnd(), "host code from PCSX2's software renderer"},
			};
		}
		s_enabled.store(on);
	}

	void Stop()
	{
		if (!s_running.exchange(false))
			return;

		s_enabled.store(false);
		if (s_sampler.joinable())
			s_sampler.join();
		if (s_thread)
		{
			CloseHandle(s_thread);
			s_thread = nullptr;
		}
	}

	bool Write(const char* path, uint32_t frames, double seconds)
	{
		std::FILE* fp = std::fopen(path, "wb");
		if (!fp)
			return false;

		std::unordered_map<u64, u64> by_address;
		for (const u64 address : s_samples)
			by_address[address]++;

		// The names come from the host's own debugging information (ps2n-host.pdb,
		// beside the executable) and from what the system's libraries export.
		const HANDLE process = GetCurrentProcess();
		std::string messages;

		s_source_root = PS2N_SOURCE_ROOT;
		for (char& c : s_source_root)
			c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		while (!s_source_root.empty() && s_source_root.back() == '/')
			s_source_root.pop_back();

		// With SYMOPT_DEBUG and the callback below the handler loads the host's
		// debugging information; without them it took the executable's exports
		// instead, which name nothing.
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_DEBUG | SYMOPT_LOAD_LINES);
		const bool initialized = SymInitialize(process, nullptr, TRUE) != FALSE;
		// Someone else in the process may have set the symbol handler up already.
		const bool symbols = initialized || GetLastError() == ERROR_INVALID_PARAMETER;
		if (symbols)
			SymRegisterCallback64(process, SymbolMessages, static_cast<ULONG64>(reinterpret_cast<uintptr_t>(&messages)));

		// What the symbol handler has for the host itself, for the report: when
		// the functions come out without names, this says why.
		std::string host_symbols = "none";
		if (symbols)
		{
			const DWORD64 here = reinterpret_cast<DWORD64>(&Write);
			char buffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;
			DWORD64 displacement = 0;
			const BOOL found = SymFromAddr(process, here, &displacement, symbol);
			const DWORD error = found ? 0 : GetLastError();

			IMAGEHLP_MODULE64 module = {};
			module.SizeOfStruct = sizeof(module);
			if (SymGetModuleInfo64(process, here, &module))
			{
				char line[1024];
				std::snprintf(line, sizeof(line), "kind %d from [%s]; a function of the host %s%s (error %lu)",
					static_cast<int>(module.SymType), module.LoadedPdbName[0] ? module.LoadedPdbName : module.LoadedImageName,
					found ? "has the name " : "has no name", found ? symbol->Name : "", static_cast<unsigned long>(error));
				host_symbols = line;
			}

			// Without names for the host, what the symbol handler said about it.
			if (!found)
			{
				std::string said;
				size_t start = 0;
				while (start < messages.size() && said.size() < 3000)
				{
					size_t end = messages.find('\n', start);
					if (end == std::string::npos)
						end = messages.size();
					const std::string one = messages.substr(start, end - start);
					if (one.find("ps2n-host") != std::string::npos)
						said += "\n#   " + one;
					start = end + 1;
				}
				host_symbols += said;
			}
		}

		u64 by_kind[kKindCount] = {};
		std::unordered_map<std::string, std::pair<Kind, u64>> by_name;
		for (const auto& [address, count] : by_address)
		{
			const Place place = symbols ? Identify(process, address) : Place{kUnknown, "(no symbols)"};
			by_kind[place.kind] += count;
			auto& entry = by_name[place.name];
			entry.first = place.kind;
			entry.second += count;
		}
		if (initialized)
			SymCleanup(process);

		const double total = s_samples.empty() ? 1.0 : static_cast<double>(s_samples.size());
		std::fprintf(fp,
			"# Where the CPU thread was, looked at about a thousand times a second (ps2n-host -profile).\n"
			"# The CPU thread carries the EE, the IOP and the VUs. The GS has a thread of its own, which was not looked at.\n"
			"# %zu samples over %u frames in %.1f s (%.0f samples a second, %.1f frames a second)%s\n"
			"# The host's own symbols: %s\n\n",
			s_samples.size(), frames, seconds, seconds > 0.0 ? static_cast<double>(s_samples.size()) / seconds : 0.0,
			seconds > 0.0 ? static_cast<double>(frames) / seconds : 0.0,
			s_dropped ? "; more would have been taken had there been room" : "", host_symbols.c_str());

		std::fputs("By kind of code:\n", fp);
		for (int kind = 0; kind < kKindCount; kind++)
		{
			std::fprintf(fp, "  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(by_kind[kind]) / total,
				static_cast<unsigned long long>(by_kind[kind]), kKindNames[kind]);
		}

		// Within each kind, the functions that had most of the samples.
		std::vector<std::pair<std::string, std::pair<Kind, u64>>> names(by_name.begin(), by_name.end());
		std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });

		std::fputs("\nThe sixty places with most of the samples:\n", fp);
		for (size_t i = 0; i < names.size() && i < 60; i++)
		{
			std::fprintf(fp, "  %6.2f%%  %9llu  %s  [%s]\n", 100.0 * static_cast<double>(names[i].second.second) / total,
				static_cast<unsigned long long>(names[i].second.second), names[i].first.c_str(), kKindNames[names[i].second.first]);
		}

		for (int kind = 0; kind < kKindCount; kind++)
		{
			if (by_kind[kind] == 0)
				continue;
			std::fprintf(fp, "\nMost of the samples in %s:\n", kKindNames[kind]);
			size_t listed = 0;
			for (size_t i = 0; i < names.size() && listed < 25; i++)
			{
				if (names[i].second.first != kind)
					continue;
				std::fprintf(fp, "  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(names[i].second.second) / total,
					static_cast<unsigned long long>(names[i].second.second), names[i].first.c_str());
				listed++;
			}
		}

		std::fclose(fp);
		return true;
	}
} // namespace ps2n::host::profile

#else // not written for this platform yet

namespace ps2n::host::profile
{
	bool Available()
	{
		return false;
	}

	bool Start(void* thread)
	{
		return false;
	}

	void Enable(bool on)
	{
	}

	void Stop()
	{
	}

	bool Write(const char* path, uint32_t frames, double seconds)
	{
		return false;
	}
} // namespace ps2n::host::profile

#endif
