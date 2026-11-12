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
#include <string_view>
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
			std::string file; // for the game's compiled code: the generated file and line the instruction is from
			u32 line = 0;
			u64 offset = 0; // and how far into its function the instruction is
		};

		// The lines of the generated files, read as they are needed, to say what
		// kind of line a sample in the game's compiled code fell on.
		std::unordered_map<std::string, std::vector<std::string>> s_sources;

		const std::string* SourceLine(const std::string& file, u32 line)
		{
			auto it = s_sources.find(file);
			if (it == s_sources.end())
			{
				std::vector<std::string> lines;
				if (std::FILE* fp = std::fopen(file.c_str(), "rb"))
				{
					std::string text;
					char buffer[65536];
					size_t got;
					while ((got = std::fread(buffer, 1, sizeof(buffer), fp)) > 0)
						text.append(buffer, got);
					std::fclose(fp);

					size_t start = 0;
					while (start <= text.size())
					{
						size_t end = text.find('\n', start);
						if (end == std::string::npos)
							end = text.size();
						lines.emplace_back(text, start, end - start);
						start = end + 1;
					}
				}
				it = s_sources.emplace(file, std::move(lines)).first;
			}
			return (line >= 1 && line <= it->second.size()) ? &it->second[line - 1] : nullptr;
		}

		// What a line of generated code is, by how it begins
		// (recompiler/src/codegen.cpp writes them; runtime/include/ps2n/recomp.h
		// says what each does).
		std::string KindOfLine(const std::string* text)
		{
			if (!text)
				return "(the line could not be read)";

			size_t at = text->find_first_not_of(" \t");
			if (at == std::string::npos)
				return "(an empty line)";
			const std::string_view line(text->data() + at, text->size() - at);
			const auto begins = [&line](const char* with) { return line.rfind(with, 0) == 0; };

			if (begins("PS2N_BLOCK_START"))
				return "the start of a block: what the runtime knows of it";
			if (begins("PS2N_BLOCK_END") || begins("PS2N_SPLIT_END") || begins("if ((ps2n_end") || begins("acc +=") || begins("bool ends") ||
				begins("if (ends)"))
				return "the end of a block: its cycles, the test for events";
			if (begins("PS2N_LOAD"))
				return "loads into general registers, on locals";
			if (begins("PS2N_STORE"))
				return "stores of general registers, on locals";
			if (begins("PS2N_ACCESS"))
				return "loads and stores through register numbers";
			if (begins("PS2N_GET"))
				return "registers taken from the core into locals";
			if (begins("PS2N_PUT"))
				return "registers put back from locals";
			if (begins("PS2N_COP2"))
				return "COP2 instructions: the call of the runtime";
			if (begins("PS2N_CALL") || begins("PS2N_JUMP") || begins("return") || begins("goto") || begins("switch") || begins("case") ||
				begins("default") || begins("pc =") || begins("if (pc ==") || begins("E_"))
				return "calls, jumps, returns, the entry switch";
			if (begins("const bool taken") || begins("if (taken)") || begins("const uint32_t target") || begins("else"))
				return "branches: the condition, the target";
			if (begins("PS2N_FLUSH") || begins("PS2N_SYSCALL") || begins("PS2N_SYSTEM") || begins("PS2N_DELEGATE") || begins("PS2N_TIME"))
				return "cycles added in a block, system instructions, delegated ones";
			if (begins("uint32_t fn_") || begins("static uint32_t fn_") || begins("uint32_t acc") || begins("uint64_t g") ||
				begins("const vtlb_private") || begins("[[maybe_unused]]") || begins("uint32_t ps2n_end") || begins("{") || begins("}"))
				return "a function's entry and exit";
			if (line.size() > 1 && line[0] == 'g' && line[1] >= '0' && line[1] <= '9')
				return "arithmetic on locals";
			if ((line.size() > 1 && line[0] == 'f' && line[1] >= '0' && line[1] <= '9') || begins("facc"))
				return "FPU arithmetic on locals";
			if (begins("fpu::setCondition"))
				return "FPU comparisons on locals";
			if (begins("PS2N_GET_F") || begins("PS2N_GET_ACC"))
				return "registers taken from the core into locals";
			if (begins("PS2N_PUT_F") || begins("PS2N_PUT_ACC"))
				return "registers put back from locals";

			// Anything else is an instruction carried out through register numbers:
			// named after its function, up to the bracket.
			const size_t bracket = line.find('(');
			if (bracket != std::string_view::npos && bracket > 0 && bracket < 16)
				return "through register numbers: " + std::string(line.substr(0, bracket));
			return "(another kind of line)";
		}

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
				IMAGEHLP_LINE64 line = {};
				line.SizeOfStruct = sizeof(line);
				DWORD column = 0;
				const bool located = SymGetLineFromAddr64(process, address, &column, &line) && line.FileName;
				if (name.find("ps2n_image_") != std::string::npos)
					return {kGame, name, located ? std::string(line.FileName) : std::string(), located ? static_cast<u32>(line.LineNumber) : 0,
						static_cast<u64>(displacement)};

				if (located)
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
		std::unordered_map<std::string, u64> by_line_kind; // within the game's compiled code
		// The game's compiled code, instruction by instruction: where in which
		// function, how often, and the generated line it came from.
		struct Hot
		{
			u64 count;
			std::string where;
			std::string kind;
			std::string text;
		};
		std::vector<Hot> hot;
		for (const auto& [address, count] : by_address)
		{
			const Place place = symbols ? Identify(process, address) : Place{kUnknown, "(no symbols)"};
			by_kind[place.kind] += count;
			auto& entry = by_name[place.name];
			entry.first = place.kind;
			entry.second += count;
			if (place.kind == kGame)
			{
				const std::string* text = place.file.empty() ? nullptr : SourceLine(place.file, place.line);
				const std::string kind = place.file.empty() ? std::string("(no line known)") : KindOfLine(text);
				by_line_kind[kind] += count;

				char where[256];
				std::snprintf(where, sizeof(where), "%s+0x%llx", place.name.c_str(), static_cast<unsigned long long>(place.offset));
				std::string line = text ? *text : std::string();
				const size_t first = line.find_first_not_of(" \t");
				line = (first == std::string::npos) ? std::string() : line.substr(first);
				if (line.size() > 110)
					line = line.substr(0, 110) + "...";
				hot.push_back({count, where, kind, line});
			}
		}
		s_sources.clear();
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

		// The game's compiled code, by what its lines are. A sample is filed under
		// the line of the generated file its instruction came from, as the
		// debugging information has it; the compiler moves and merges
		// instructions, so this is a guide, not a count.
		if (by_kind[kGame])
		{
			std::vector<std::pair<std::string, u64>> lines(by_line_kind.begin(), by_line_kind.end());
			std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			std::fputs("\nThe game's compiled code, by the kind of generated line (share of that code, of the run, samples):\n", fp);
			for (size_t i = 0; i < lines.size() && i < 40; i++)
			{
				std::fprintf(fp, "  %6.2f%%  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(lines[i].second) / static_cast<double>(by_kind[kGame]),
					100.0 * static_cast<double>(lines[i].second) / total, static_cast<unsigned long long>(lines[i].second), lines[i].first.c_str());
			}
		}

		// The instructions of the game's compiled code that had most of the
		// samples, by function and offset: to be read beside the function's
		// disassembly (llvm-objdump --disassemble-symbols), which says what each
		// instruction is. A sample falls on the instruction the processor was
		// about to complete, which is often the one after the one waiting.
		if (!hot.empty())
		{
			std::sort(hot.begin(), hot.end(), [](const Hot& a, const Hot& b) { return a.count > b.count; });
			std::fputs("\nThe instructions of the game's compiled code with most of the samples (share of the run, samples, where, kind, line):\n", fp);
			for (size_t i = 0; i < hot.size() && i < 80; i++)
			{
				std::fprintf(fp, "  %6.2f%%  %7llu  %-44s  [%s]  %s\n", 100.0 * static_cast<double>(hot[i].count) / total,
					static_cast<unsigned long long>(hot[i].count), hot[i].where.c_str(), hot[i].kind.c_str(), hot[i].text.c_str());
			}
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
