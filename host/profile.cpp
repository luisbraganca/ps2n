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
			kMicro, // the vector units' micro code, compiled ahead of time (ps2n-recomp vu)
			kIop, // the IOP's code, compiled ahead of time (ps2n-recomp iop)
			kRuntime, // PS2Native's runtime: dispatcher, block record, interpreter, instruction semantics
			kCore, // PCSX2's core, the C++ of it
			kGenerated, // host code PCSX2's recompilers generated at run time
			kSystem, // Windows, drivers, other libraries
			kUnknown, // an address in no module and no known area
			kKindCount,
		};
		constexpr const char* kKindNames[kKindCount] = {
			"the game's code, compiled ahead of time",
			"the vector units' micro code, compiled ahead of time",
			"the IOP's code, compiled ahead of time",
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
			if (begins("PS2N_LOAD") || begins("PS2N_LQ("))
				return "loads into general registers, on locals";
			if (begins("PS2N_STORE") || begins("PS2N_SQ(") || begins("PS2N_SQ_CORE("))
				return "stores of general registers, on locals";
			if (begins("PS2N_ACCESS"))
				return "loads and stores through register numbers";
			if (begins("PS2N_GET"))
				return "registers taken from the core into locals";
			if (begins("PS2N_PUT"))
				return "registers put back from locals";
			if (begins("PS2N_COP2"))
				return "COP2 instructions: the call of the runtime";
			if (begins("PS2N_VU0"))
				return "VU0 instructions in compiled code";
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

		// The FPU's instruction a line of generated code carries out on the
		// locals, or nothing: its arithmetic, comparisons and moves by the
		// function of ops_fpu.h it calls, its loads and stores by their macros
		// (a store as code before generator 0.10.0 wrote it, PS2N_STORE of an
		// FPU register's local, as well). MULA.S, ADDA.S and SUBA.S are filed
		// under MUL_S, ADD_S and SUB_S, MSUBA.S under MSUB_S.
		std::string FpuInstructionOfLine(const std::string* text)
		{
			if (!text)
				return {};
			const size_t at = text->find_first_not_of(" \t");
			if (at == std::string::npos)
				return {};
			const std::string_view line(text->data() + at, text->size() - at);
			const auto begins = [&line](const char* with) { return line.rfind(with, 0) == 0; };
			if (begins("PS2N_LOAD_F("))
				return "LWC1";
			if (begins("PS2N_STORE_F(") || (begins("PS2N_STORE(mem32_t") && line.find(", f") != std::string_view::npos))
				return "SWC1";
			if (begins("fpu::setCondition(false)"))
				return "C_F_S";
			const bool fpu = (line.size() > 1 && line[0] == 'f' && line[1] >= '0' && line[1] <= '9') || begins("facc") ||
				begins("fpu::setCondition") || line.find("= val::MFC1(") != std::string_view::npos;
			if (!fpu)
				return {};
			const size_t name = line.find("val::");
			if (name == std::string_view::npos)
				return "MOV_S";
			const size_t bracket = line.find('(', name);
			return bracket == std::string_view::npos ? std::string() : std::string(line.substr(name + 5, bracket - name - 5));
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

			// With the compiled code in modules of its own (runtime/game_module.h;
			// a game's folder), each holds one kind of it, the game's and the
			// BIOS's EE code, the micro code or the IOP's code, and what of the
			// runtime's headers it was not compiled into. Where a module has its
			// debugging information beside it, its functions are told apart by
			// name as the host's are; where it has none, as in a game's folder,
			// the symbol handler knows only what it exports, and its code is filed
			// under the module's kind.
			Kind module_kind = kKindCount;
			if (_stricmp(module.ModuleName, "ps2n-game") == 0 || _stricmp(module.ModuleName, "ps2n-bios") == 0)
				module_kind = kGame;
			else if (_stricmp(module.ModuleName, "ps2n-vu") == 0)
				module_kind = kMicro;
			else if (_stricmp(module.ModuleName, "ps2n-iop") == 0)
				module_kind = kIop;
			if (module_kind != kKindCount)
			{
				IMAGEHLP_MODULE64 loaded = {};
				loaded.SizeOfStruct = sizeof(loaded);
				const bool debugging = named && SymGetModuleInfo64(process, address, &loaded) && loaded.SymType != SymExport &&
									   loaded.SymType != SymNone && loaded.SymType != SymDeferred;
				if (!debugging)
					return {module_kind, std::string(module.ModuleName) + " (no debugging information)"};
				const std::string name(symbol->Name);
				const bool micro = name.find("ps2n_vu_image_") != std::string::npos;
				const bool iop = name.find("ps2n_iop_image_") != std::string::npos;
				if (micro || iop)
				{
					// With the line, for the list of the hottest instructions of
					// the micro code and the IOP's code.
					IMAGEHLP_LINE64 line = {};
					line.SizeOfStruct = sizeof(line);
					DWORD column = 0;
					const bool located = SymGetLineFromAddr64(process, address, &column, &line) && line.FileName;
					return {micro ? kMicro : kIop, name, located ? std::string(line.FileName) : std::string(),
						located ? static_cast<u32>(line.LineNumber) : 0, static_cast<u64>(displacement)};
				}
				if (name.find("ps2n_image_") != std::string::npos)
				{
					IMAGEHLP_LINE64 line = {};
					line.SizeOfStruct = sizeof(line);
					DWORD column = 0;
					const bool located = SymGetLineFromAddr64(process, address, &column, &line) && line.FileName;
					return {kGame, name, located ? std::string(line.FileName) : std::string(), located ? static_cast<u32>(line.LineNumber) : 0,
						static_cast<u64>(displacement)};
				}
				// What of the runtime's headers a module's code calls and was
				// not compiled into it: askAbout<>(), the semantics left out of line.
				return {kRuntime, std::string(module.ModuleName) + "!" + name};
			}

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
				if (name.find("ps2n_vu_image_") != std::string::npos)
					return {kMicro, name};
				if (name.find("ps2n_iop_image_") != std::string::npos)
					return {kIop, name};
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

		// ---- how much host code the generated files became (-code-size) ----------
		// A line record of the host's debugging information: where its code
		// begins, and the file and line it was compiled from. What an inlined
		// function does is on the line of its call (CodeView's line tables), so
		// in compiled code that is the generated line, as for the profile.
		struct LineRecord
		{
			u64 address;
			u32 file; // index into LineRecords::files
			u32 line;
		};
		struct LineRecords
		{
			std::vector<LineRecord> records;
			std::unordered_map<std::string, u32> index;
			std::vector<std::string> files;
		};

		BOOL CALLBACK OneLineRecord(PSRCCODEINFO info, PVOID context)
		{
			LineRecords& lines = *static_cast<LineRecords*>(context);
			const auto [it, added] = lines.index.try_emplace(info->FileName, static_cast<u32>(lines.files.size()));
			if (added)
				lines.files.emplace_back(info->FileName);
			lines.records.push_back({info->Address, it->second, info->LineNumber});
			return TRUE;
		}

		// A function compiled ahead of time, from its symbol: where it is, how
		// large, and the image it belongs to (the namespace ps2n_image_<id>).
		struct CompiledFunction
		{
			u64 address;
			u64 size;
			std::string image;
		};

		BOOL CALLBACK OneSymbol(PSYMBOL_INFO symbol, ULONG, PVOID context)
		{
			constexpr ULONG kFunction = 5; // SymTagFunction
			constexpr std::string_view kPrefix = "ps2n_image_";
			if (symbol->Tag != kFunction || symbol->Size == 0)
				return TRUE;
			const std::string_view name(symbol->Name, symbol->NameLen);
			if (name.substr(0, kPrefix.size()) != kPrefix)
				return TRUE;
			const size_t colons = name.find("::", kPrefix.size());
			if (colons == std::string_view::npos)
				return TRUE;
			static_cast<std::vector<CompiledFunction>*>(context)->push_back(
				{symbol->Address, symbol->Size, std::string(name.substr(kPrefix.size(), colons - kPrefix.size()))});
			return TRUE;
		}

		std::string Thousands(u64 n)
		{
			const std::string digits = std::to_string(n);
			std::string out;
			for (size_t i = 0; i < digits.size(); i++)
			{
				if (i && (digits.size() - i) % 3 == 0)
					out.push_back(',');
				out.push_back(digits[i]);
			}
			return out;
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

	namespace
	{
		// The name the compiler gave the function that begins at `address`, as
		// the module's public symbols have it, decorated: what a sampled profile
		// names a function by.
		std::string DecoratedName(HANDLE process, u64 address)
		{
			const DWORD options = SymGetOptions();
			SymSetOptions((options & ~static_cast<DWORD>(SYMOPT_UNDNAME)) | SYMOPT_PUBLICS_ONLY);
			char buffer[sizeof(SYMBOL_INFO) + 1024] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 1023;
			DWORD64 displacement = 0;
			std::string name;
			if (SymFromAddr(process, address, &displacement, symbol) && displacement == 0)
				name = symbol->Name;
			SymSetOptions(options);
			return name;
		}

		// The line of a generated file that declares the function named `name`
		// (its name and an opening parenthesis, the line not a declaration that
		// ends in a semicolon), the nearest at or above `line`: a function's
		// lines are counted from it in a sampled profile. 0 when there is none.
		u32 DeclarationLine(const std::string& file, u32 line, const std::string& name)
		{
			const std::string opening = name + "(";
			for (u32 n = line; n > 0; n--)
			{
				const std::string* text = SourceLine(file, n);
				if (!text)
					return 0;
				if (text->find(opening) == std::string::npos)
					continue;
				size_t end = text->find_last_not_of(" \t\r");
				if (end != std::string::npos && (*text)[end] != ';')
					return n;
			}
			return 0;
		}
	} // namespace

	bool WriteSampleProfile(const char* path)
	{
		std::unordered_map<u64, u64> by_address;
		for (const u64 address : s_samples)
			by_address[address]++;

		const HANDLE process = GetCurrentProcess();
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_LOAD_LINES);
		if (!SymInitialize(process, nullptr, TRUE) && GetLastError() != ERROR_INVALID_PARAMETER)
			return false;

		// The functions the samples fell in, by where they begin. Only those of
		// the generated files: what of the runtime's headers a module did not
		// inline is not the game's code, and code a header inlined is on the line
		// of its call (CodeView's line tables).
		struct Function
		{
			std::string name;
			u32 declared = 0;
			u64 total = 0;
			std::map<u32, u64> lines;
		};
		std::unordered_map<u64, Function> functions;
		u64 counted = 0, passed_over = 0;
		for (const auto& [address, count] : by_address)
		{
			IMAGEHLP_MODULE64 module = {};
			module.SizeOfStruct = sizeof(module);
			if (!SymGetModuleInfo64(process, address, &module))
				continue;
			if (_stricmp(module.ModuleName, "ps2n-game") != 0 && _stricmp(module.ModuleName, "ps2n-bios") != 0 &&
				_stricmp(module.ModuleName, "ps2n-vu") != 0 && _stricmp(module.ModuleName, "ps2n-iop") != 0)
				continue;
			char buffer[sizeof(SYMBOL_INFO) + 1024] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 1023;
			DWORD64 displacement = 0;
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(line);
			DWORD column = 0;
			if (!SymFromAddr(process, address, &displacement, symbol) || !SymGetLineFromAddr64(process, address, &column, &line) ||
				!line.FileName)
			{
				passed_over += count;
				continue;
			}
			std::string file(line.FileName);
			for (char& c : file)
				c = (c == '\\') ? '/' : c;
			if (file.find("/generated/") == std::string::npos)
			{
				passed_over += count;
				continue;
			}
			auto [it, added] = functions.try_emplace(symbol->Address);
			Function& f = it->second;
			if (added)
			{
				std::string short_name(symbol->Name);
				const size_t colon = short_name.rfind("::");
				if (colon != std::string::npos)
					short_name = short_name.substr(colon + 2);
				f.name = DecoratedName(process, symbol->Address);
				f.declared = DeclarationLine(line.FileName, line.LineNumber, short_name);
			}
			if (f.name.empty() || f.declared == 0 || line.LineNumber < f.declared)
			{
				passed_over += count;
				continue;
			}
			f.lines[line.LineNumber - f.declared] += count;
			f.total += count;
			counted += count;
		}

		std::FILE* fp = std::fopen(path, "wb");
		if (!fp)
			return false;
		// LLVM's text format: "name:total:head", then " offset: samples" for each
		// line. The head, how often the function was entered, is taken as the
		// samples of its first line that has any.
		std::vector<const Function*> order;
		for (const auto& [start, f] : functions)
		{
			if (f.total)
				order.push_back(&f);
		}
		std::sort(order.begin(), order.end(), [](const Function* a, const Function* b) { return a->total > b->total; });
		for (const Function* f : order)
		{
			std::fprintf(fp, "%s:%llu:%llu\n", f->name.c_str(), static_cast<unsigned long long>(f->total),
				static_cast<unsigned long long>(f->lines.begin()->second));
			for (const auto& [offset, count] : f->lines)
				std::fprintf(fp, " %u: %llu\n", offset, static_cast<unsigned long long>(count));
		}
		std::fclose(fp);
		std::fprintf(stderr, "ps2n-host: a sampled profile of the compiled code: %zu functions, %llu samples (%llu passed over): %s\n",
			order.size(), static_cast<unsigned long long>(counted), static_cast<unsigned long long>(passed_over), path);
		return true;
	}

	bool Write(const char* path, uint32_t first_frame, uint32_t frames, double seconds)
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
		std::unordered_map<std::string, u64> by_fpu_instruction; // the same, its FPU's lines
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
		// The same for the micro code's and the IOP's compiled code, where their
		// module has line tables (a game's folder made with -LineTables).
		std::vector<Hot> hot_micro, hot_iop;
		for (const auto& [address, count] : by_address)
		{
			const Place place = symbols ? Identify(process, address) : Place{kUnknown, "(no symbols)"};
			by_kind[place.kind] += count;
			auto& entry = by_name[place.name];
			entry.first = place.kind;
			entry.second += count;
			if ((place.kind == kMicro || place.kind == kIop) && !place.file.empty())
			{
				const std::string* text = SourceLine(place.file, place.line);
				char where[256];
				std::snprintf(where, sizeof(where), "%s+0x%llx", place.name.c_str(), static_cast<unsigned long long>(place.offset));
				std::string line = text ? *text : std::string();
				const size_t first = line.find_first_not_of(" \t");
				line = (first == std::string::npos) ? std::string() : line.substr(first);
				if (line.size() > 110)
					line = line.substr(0, 110) + "...";
				(place.kind == kMicro ? hot_micro : hot_iop).push_back({count, where, std::to_string(place.line), line});
			}
			if (place.kind == kGame)
			{
				const std::string* text = place.file.empty() ? nullptr : SourceLine(place.file, place.line);
				const std::string kind = place.file.empty() ? std::string("(no line known)") : KindOfLine(text);
				by_line_kind[kind] += count;
				const std::string fpu = FpuInstructionOfLine(text);
				if (!fpu.empty())
					by_fpu_instruction[fpu] += count;

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
			"# %zu samples over %u frames from frame %u on, in %.1f s (%.0f samples a second, %.1f frames a second)%s\n"
			"# The host's own symbols: %s\n\n",
			s_samples.size(), frames, first_frame, seconds, seconds > 0.0 ? static_cast<double>(s_samples.size()) / seconds : 0.0,
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

			// The FPU's lines among them, by instruction: its arithmetic and
			// comparisons, and its loads, stores and moves, which the kinds above
			// count with the general registers'.
			std::vector<std::pair<std::string, u64>> fpu(by_fpu_instruction.begin(), by_fpu_instruction.end());
			std::sort(fpu.begin(), fpu.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			if (!fpu.empty())
			{
				std::fputs("\nThe FPU's lines of the game's compiled code, by instruction (share of that code, of the run, samples):\n", fp);
				for (const auto& [name, count] : fpu)
				{
					std::fprintf(fp, "  %6.2f%%  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(count) / static_cast<double>(by_kind[kGame]),
						100.0 * static_cast<double>(count) / total, static_cast<unsigned long long>(count), name.c_str());
				}
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

		// The same for the micro code and the IOP's code, by line of their
		// generated files.
		const auto list_hot = [&](std::vector<Hot>& list, const char* what) {
			if (list.empty())
				return;
			std::sort(list.begin(), list.end(), [](const Hot& a, const Hot& b) { return a.count > b.count; });
			std::fprintf(fp, "\nThe instructions of %s with most of the samples (share of the run, samples, where, line):\n", what);
			for (size_t i = 0; i < list.size() && i < 40; i++)
			{
				std::fprintf(fp, "  %6.2f%%  %7llu  %-60s  %5s  %s\n", 100.0 * static_cast<double>(list[i].count) / total,
					static_cast<unsigned long long>(list[i].count), list[i].where.c_str(), list[i].kind.c_str(), list[i].text.c_str());
			}
		};
		list_hot(hot_iop, "the IOP's compiled code");
		list_hot(hot_micro, "the compiled micro code");

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

	bool WriteCodeSize(const char* path, const std::vector<ImageCode>& images)
	{
		// The host's debugging information, as Write() reads it (ps2n-host.pdb,
		// beside the executable).
		const HANDLE process = GetCurrentProcess();
		std::string messages;
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_DEBUG | SYMOPT_LOAD_LINES);
		const bool initialized = SymInitialize(process, nullptr, TRUE) != FALSE;
		if (!initialized && GetLastError() != ERROR_INVALID_PARAMETER)
		{
			std::fprintf(stderr, "ps2n-host: the system's symbol handler cannot be set up (error %lu)\n", GetLastError());
			return false;
		}
		SymRegisterCallback64(process, SymbolMessages, static_cast<ULONG64>(reinterpret_cast<uintptr_t>(&messages)));

		const DWORD64 base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
		std::vector<CompiledFunction> functions;
		LineRecords lines;
		const bool enumerated = SymEnumSymbols(process, base, "*", OneSymbol, &functions) &&
								SymEnumLines(process, base, nullptr, nullptr, OneLineRecord, &lines);
		const DWORD error = enumerated ? 0 : GetLastError();
		if (initialized)
			SymCleanup(process);
		if (!enumerated || functions.empty() || lines.records.empty())
		{
			std::fprintf(stderr,
				"ps2n-host: the host's debugging information gave %zu compiled functions and %zu line records (error %lu)\n",
				functions.size(), lines.records.size(), static_cast<unsigned long>(error));
			return false;
		}

		std::sort(functions.begin(), functions.end(), [](const CompiledFunction& a, const CompiledFunction& b) { return a.address < b.address; });
		std::sort(lines.records.begin(), lines.records.end(), [](const LineRecord& a, const LineRecord& b) { return a.address < b.address; });

		// Which of the files are generated ones: those under ps2n/generated in
		// the build tree.
		std::vector<char> generated(lines.files.size());
		for (size_t i = 0; i < lines.files.size(); i++)
		{
			std::string file = lines.files[i];
			for (char& c : file)
				c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			generated[i] = file.find("/ps2n/generated/") != std::string::npos;
		}

		// By image, and over all of them: the functions, their bytes, and the
		// bytes by the kind of line they were compiled from. A line record's
		// bytes run to the next record of the same function, or to its end.
		struct Tally
		{
			u64 functions = 0;
			u64 bytes = 0;
			std::unordered_map<std::string, u64> by_kind;
		};
		std::map<std::string, Tally> by_image;
		Tally all;
		const auto add = [&all](Tally& t, const std::string& kind, u64 bytes) {
			t.by_kind[kind] += bytes;
			all.by_kind[kind] += bytes;
		};
		for (const CompiledFunction& f : functions)
		{
			Tally& t = by_image[f.image];
			t.functions++;
			t.bytes += f.size;
			all.functions++;
			all.bytes += f.size;

			const u64 end = f.address + f.size;
			auto it = std::lower_bound(lines.records.begin(), lines.records.end(), f.address,
				[](const LineRecord& r, u64 address) { return r.address < address; });
			if (it == lines.records.end() || it->address >= end)
			{
				add(t, "(no line record)", f.size);
				continue;
			}
			if (it->address > f.address)
				add(t, "(before the first line record)", it->address - f.address);
			for (; it != lines.records.end() && it->address < end; ++it)
			{
				const auto next = std::next(it);
				const u64 stop = (next != lines.records.end() && next->address < end) ? next->address : end;
				if (stop == it->address)
					continue;
				const std::string kind =
					generated[it->file] ? KindOfLine(SourceLine(lines.files[it->file], it->line)) : std::string("(a line of another file)");
				add(t, kind, stop - it->address);
			}
		}
		s_sources.clear();

		std::FILE* fp = std::fopen(path, "wb");
		if (!fp)
			return false;

		std::unordered_map<std::string, u64> instructions;
		for (const ImageCode& image : images)
			instructions[image.id] = image.instructions;
		const auto instructionsOf = [&instructions](const std::string& id) -> u64 {
			const auto it = instructions.find(id);
			return it == instructions.end() ? 0 : it->second;
		};
		u64 all_instructions = 0;
		for (const auto& [id, t] : by_image)
			all_instructions += instructionsOf(id);

		std::fputs("# How much host code the generated files became (ps2n-host -code-size), from the host's debugging information:\n"
				   "# each compiled function's size, and from the line tables which generated line each piece of it came from.\n"
				   "# A line record's bytes run to the next record of its function, or to the function's end. The C++ compiler\n"
				   "# moves and merges instructions, and what a macro does, its slow path included, is on the line that names it.\n\n",
			fp);

		std::vector<std::pair<std::string, const Tally*>> order;
		for (const auto& [id, t] : by_image)
			order.push_back({id, &t});
		std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second->bytes > b.second->bytes; });

		std::fputs("By image: functions, guest instructions, bytes of host code, bytes a guest instruction\n", fp);
		const auto row = [fp](const std::string& name, u64 count, u64 guest, u64 bytes) {
			std::fprintf(fp, "  %-36s %9s %11s %13s %8.1f\n", name.c_str(), Thousands(count).c_str(), Thousands(guest).c_str(),
				Thousands(bytes).c_str(), guest ? static_cast<double>(bytes) / static_cast<double>(guest) : 0.0);
		};
		for (const auto& [id, t] : order)
			row(id, t->functions, instructionsOf(id), t->bytes);
		row("(all of them)", all.functions, all_instructions, all.bytes);

		// By the kind of line: over all the images, and for each image of at
		// least a megabyte of code.
		const auto kinds = [fp](const std::string& title, const Tally& t, u64 guest) {
			std::vector<std::pair<std::string, u64>> list(t.by_kind.begin(), t.by_kind.end());
			std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			std::fprintf(fp, "\n%s, by the kind of generated line (share, bytes, bytes a guest instruction):\n", title.c_str());
			for (const auto& [kind, bytes] : list)
			{
				std::fprintf(fp, "  %6.2f%%  %13s  %7.2f  %s\n", t.bytes ? 100.0 * static_cast<double>(bytes) / static_cast<double>(t.bytes) : 0.0,
					Thousands(bytes).c_str(), guest ? static_cast<double>(bytes) / static_cast<double>(guest) : 0.0, kind.c_str());
			}
		};
		kinds("All the images", all, all_instructions);
		for (const auto& [id, t] : order)
		{
			if (t->bytes < 1000000)
				break;
			kinds("The image " + id, *t, instructionsOf(id));
		}

		std::fclose(fp);
		return true;
	}
} // namespace ps2n::host::profile

#elif defined(__linux__)

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Memory.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <csignal>
#include <ctime>
#include <cxxabi.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

// On Linux a thread of the profile's own wakes every millisecond and, when the
// CPU thread has run for most of it (by its own processor time), sends it a
// signal, whose handler notes where it was and the words on top of its stack.
// So a sample is a millisecond the thread ran, in its own code or in the
// kernel for it, and none is taken while it waits (on Windows the samples are
// of the clock's time, waiting included). The names come from the symbol
// tables of the files the process has loaded; there are no lines: the report
// says where the time went by kind of code, by function, for compiled code by
// function and offset, to read beside its disassembly, and for the system's
// libraries by what called them, as far as the stack's top words say.
namespace ps2n::host::profile
{
	namespace
	{
		// Half an hour at a thousand samples a second, the room taken before the
		// first: the handler that takes them may not allocate.
		constexpr size_t kMaxSamples = 2 * 1000 * 1000;
		constexpr int kStackWords = 6;

		struct Sample
		{
			u64 at;
			u64 stack[kStackWords];
		};
		Sample* s_samples = nullptr;
		std::atomic<size_t> s_taken{0};
		std::atomic<u64> s_dropped{0};
		std::atomic<bool> s_enabled{false};
		std::once_flag s_ready_once;
		std::atomic<bool> s_ready{false};
		std::atomic<bool> s_running{false};
		std::thread s_sampler;
		// The CPU thread, as it gave itself in Enable: its number for the
		// signal, and the clock of its processor time.
		std::atomic<pid_t> s_tid{0};
		clockid_t s_cpu_clock = 0;
		// How long the CPU thread ran while it was looked at, by that clock.
		u64 s_ran_from = 0;
		u64 s_ran = 0;

		// The areas PCSX2's recompilers generate host code into. They belong to
		// no file, so they are told apart by address.
		struct Area
		{
			const u8* begin;
			const u8* end;
			const char* name;
		};
		std::vector<Area> s_areas;

		// The kinds of code a sample can fall into, in the order they are
		// reported: the same as on Windows, and by the same names but the
		// system's.
		enum Kind
		{
			kGame,
			kMicro,
			kIop,
			kRuntime,
			kCore,
			kGenerated,
			kSystem,
			kUnknown,
			kKindCount,
		};
		constexpr const char* kKindNames[kKindCount] = {
			"the game's code, compiled ahead of time",
			"the vector units' micro code, compiled ahead of time",
			"the IOP's code, compiled ahead of time",
			"PS2Native's runtime",
			"PCSX2's core",
			"host code PCSX2 generated at run time",
			"the system's libraries, and the kernel's work for them",
			"not identified",
		};

		// (0 when the clock is not there: a thread's, once the thread has ended.)
		u64 Nanoseconds(clockid_t clock)
		{
			struct timespec now = {};
			if (clock_gettime(clock, &now) != 0)
				return 0;
			return static_cast<u64>(now.tv_sec) * 1000000000ull + static_cast<u64>(now.tv_nsec);
		}

		// The signal, on the CPU thread: where it was, and what its stack had on
		// top (the address a function of the system's was called from is among
		// those words more often than not).
		void OnSample(int, siginfo_t*, void* context)
		{
			if (!s_enabled.load(std::memory_order_relaxed))
				return;
			const size_t at = s_taken.load(std::memory_order_relaxed);
			if (at >= kMaxSamples)
			{
				s_dropped.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			const greg_t* const registers = static_cast<const ucontext_t*>(context)->uc_mcontext.gregs;
			Sample& sample = s_samples[at];
			sample.at = static_cast<u64>(registers[REG_RIP]);
			const u64 top = static_cast<u64>(registers[REG_RSP]);
			for (int i = 0; i < kStackWords; i++)
				sample.stack[i] = (top && (top & 7) == 0) ? reinterpret_cast<const u64*>(top)[i] : 0;
			s_taken.store(at + 1, std::memory_order_relaxed);
		}

		// Every millisecond: has the CPU thread run for most of it? Then it is
		// asked where it is. A thread that waits is left alone, so that nothing
		// it waits in is cut short.
		void SamplerMain()
		{
			struct timespec next = {};
			clock_gettime(CLOCK_MONOTONIC, &next);
			u64 before = 0;
			bool looking = false;
			while (s_running.load())
			{
				next.tv_nsec += 1000000;
				if (next.tv_nsec >= 1000000000)
				{
					next.tv_nsec -= 1000000000;
					next.tv_sec++;
				}
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
				const pid_t tid = s_tid.load();
				if (!s_enabled.load() || tid == 0)
				{
					looking = false;
					continue;
				}
				const u64 now = Nanoseconds(s_cpu_clock);
				if (now == 0)
				{
					looking = false;
					continue;
				}
				if (looking && now >= before + 500000)
					syscall(SYS_tgkill, getpid(), tid, SIGPROF);
				before = now;
				looking = true;
			}
		}

		void MakeReady()
		{
			std::call_once(s_ready_once, []() {
				s_samples = new (std::nothrow) Sample[kMaxSamples];
				if (!s_samples)
					return;
				struct sigaction action = {};
				action.sa_sigaction = OnSample;
				action.sa_flags = SA_SIGINFO | SA_RESTART;
				sigemptyset(&action.sa_mask);
				s_ready.store(sigaction(SIGPROF, &action, nullptr) == 0);
			});
		}

		// A file the process has loaded: where, its code, and its functions as
		// its symbol table names them, read when a sample falls into it.
		struct Symbol
		{
			u64 address; // in the file
			u64 size;
			u32 name; // where in `names`
		};
		struct Object
		{
			std::string path;
			std::string name; // its last part
			u64 base = 0;
			std::vector<std::pair<u64, u64>> code; // its executable segments, as loaded
			bool read = false;
			std::vector<Symbol> symbols; // by address
			std::string names; // the symbol table's strings, as they are in the file
		};
		std::vector<Object> s_objects;

		// What is mapped where, as the system lists it: for an address in no
		// file's code.
		struct Mapping
		{
			u64 begin;
			u64 end;
			std::string rights;
			std::string path;
		};
		std::vector<Mapping> s_mappings;

		int OneObject(struct dl_phdr_info* info, size_t, void* context)
		{
			std::vector<Object>& objects = *static_cast<std::vector<Object>*>(context);
			Object object;
			object.path = info->dlpi_name ? info->dlpi_name : "";
			// The first is the program itself, which the loader gives no name.
			if (object.path.empty() && objects.empty())
			{
				char self[4096];
				const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
				if (length > 0)
					object.path.assign(self, static_cast<size_t>(length));
			}
			const size_t slash = object.path.rfind('/');
			object.name = (slash == std::string::npos) ? object.path : object.path.substr(slash + 1);
			if (object.name.empty())
				object.name = "(a file without a name)";
			object.base = info->dlpi_addr;
			for (int i = 0; i < info->dlpi_phnum; i++)
			{
				const ElfW(Phdr)& segment = info->dlpi_phdr[i];
				if (segment.p_type == PT_LOAD && (segment.p_flags & PF_X))
					object.code.emplace_back(object.base + segment.p_vaddr, object.base + segment.p_vaddr + segment.p_memsz);
			}
			objects.push_back(std::move(object));
			return 0;
		}

		// The file's functions: its whole symbol table where it has one, what
		// it exports otherwise.
		void ReadSymbols(Object& object)
		{
			object.read = true;
			const int fd = open(object.path.c_str(), O_RDONLY | O_CLOEXEC);
			if (fd < 0)
				return;
			struct stat status;
			if (fstat(fd, &status) != 0 || status.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr)))
			{
				close(fd);
				return;
			}
			const size_t size = static_cast<size_t>(status.st_size);
			void* const map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
			close(fd);
			if (map == MAP_FAILED)
				return;

			const u8* const file = static_cast<const u8*>(map);
			const Elf64_Ehdr* const header = reinterpret_cast<const Elf64_Ehdr*>(file);
			const bool elf = std::memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
							 header->e_shentsize == sizeof(Elf64_Shdr) && header->e_shoff != 0 &&
							 header->e_shoff + static_cast<u64>(header->e_shnum) * sizeof(Elf64_Shdr) <= size;
			if (elf)
			{
				const Elf64_Shdr* const sections = reinterpret_cast<const Elf64_Shdr*>(file + header->e_shoff);
				const Elf64_Shdr* table = nullptr;
				for (unsigned i = 0; i < header->e_shnum; i++)
				{
					if (sections[i].sh_type == SHT_SYMTAB)
					{
						table = &sections[i];
						break;
					}
					if (sections[i].sh_type == SHT_DYNSYM && !table)
						table = &sections[i];
				}
				if (table && table->sh_link < header->e_shnum && table->sh_entsize == sizeof(Elf64_Sym) &&
					table->sh_offset + table->sh_size <= size)
				{
					const Elf64_Shdr& strings = sections[table->sh_link];
					if (strings.sh_type == SHT_STRTAB && strings.sh_offset + strings.sh_size <= size)
					{
						object.names.assign(reinterpret_cast<const char*>(file + strings.sh_offset), strings.sh_size);
						object.names.push_back('\0');
						const Elf64_Sym* const symbols = reinterpret_cast<const Elf64_Sym*>(file + table->sh_offset);
						const size_t count = table->sh_size / sizeof(Elf64_Sym);
						for (size_t i = 0; i < count; i++)
						{
							const Elf64_Sym& symbol = symbols[i];
							if (ELF64_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_shndx == SHN_UNDEF || symbol.st_name >= strings.sh_size)
								continue;
							object.symbols.push_back({symbol.st_value, symbol.st_size, symbol.st_name});
						}
						std::sort(object.symbols.begin(), object.symbols.end(), [](const Symbol& a, const Symbol& b) {
							return a.address != b.address ? a.address < b.address : a.name < b.name;
						});
					}
				}
			}
			munmap(map, size);
		}

		// The function of the file that holds the address `at` (in the file).
		const Symbol* FunctionAt(Object& object, u64 at)
		{
			if (!object.read)
				ReadSymbols(object);
			auto it = std::upper_bound(object.symbols.begin(), object.symbols.end(), at, [](u64 address, const Symbol& symbol) {
				return address < symbol.address;
			});
			if (it == object.symbols.begin())
				return nullptr;
			--it;
			// (One without a size, as hand-written assembly has them, reaches to
			// the next.)
			return (it->size == 0 || at < it->address + it->size) ? &*it : nullptr;
		}

		// A symbol's name as the source has it. What the compiler and the
		// linker add after a dot (".llvm.123" on a function link-time
		// optimization made local, ".cold" on the part of one it moved away) is
		// left out, but that a part is the cold one.
		std::string Readable(const char* symbol)
		{
			std::string plain(symbol);
			bool cold = false;
			const size_t dot = plain.find('.');
			if (dot != std::string::npos && dot > 0)
			{
				cold = plain.find(".cold", dot) != std::string::npos;
				plain.resize(dot);
			}
			int status = 0;
			char* const text = abi::__cxa_demangle(plain.c_str(), nullptr, nullptr, &status);
			std::string out = (status == 0 && text) ? std::string(text) : plain;
			std::free(text);
			if (out.size() > 150)
				out = out.substr(0, 150) + "...";
			if (cold)
				out += " [its cold part]";
			return out;
		}

		// Whether a symbol is of PS2Native's own code: in the namespace ps2n
		// (the name as the compiler wrote it says so near its start), or one of
		// its functions with C names.
		bool OfPs2Native(const char* symbol)
		{
			if (std::strncmp(symbol, "ps2n_", 5) == 0)
				return true;
			if (std::strncmp(symbol, "_Z", 2) != 0)
				return false;
			const char* const found = std::strstr(symbol, "4ps2n");
			return found && found - symbol <= 6;
		}

		struct Place
		{
			Kind kind;
			std::string name;
			u64 offset = 0; // for compiled code: how far into its function the instruction is
			bool compiled = false; // a function compiled ahead of time, by name
		};

		Place Identify(u64 address)
		{
			for (size_t i = 0; i < s_objects.size(); i++)
			{
				Object& object = s_objects[i];
				bool inside = false;
				for (const auto& [begin, end] : object.code)
				{
					if (address >= begin && address < end)
					{
						inside = true;
						break;
					}
				}
				if (!inside)
					continue;

				const Symbol* const symbol = FunctionAt(object, address - object.base);
				const char* const raw = symbol ? object.names.c_str() + symbol->name : nullptr;
				const u64 offset = symbol ? address - object.base - symbol->address : 0;

				// The functions compiled ahead of time say what they are in
				// their names, wherever they are: in the host, or in a module
				// of their own (runtime/game_module.h; a game's folder).
				if (raw)
				{
					const Kind compiled = std::strstr(raw, "ps2n_vu_image_") ? kMicro :
										  std::strstr(raw, "ps2n_iop_image_") ? kIop :
										  std::strstr(raw, "ps2n_image_") ? kGame : kKindCount;
					if (compiled != kKindCount)
					{
						std::string name = Readable(raw);
						const size_t bracket = name.find('(');
						if (bracket != std::string::npos)
							name.resize(bracket);
						return {compiled, name, offset, true};
					}
				}

				Kind module_kind = kKindCount;
				if (object.name == "ps2n-game.so" || object.name == "ps2n-bios.so")
					module_kind = kGame;
				else if (object.name == "ps2n-vu.so")
					module_kind = kMicro;
				else if (object.name == "ps2n-iop.so")
					module_kind = kIop;
				if (module_kind != kKindCount)
				{
					if (!raw)
						return {module_kind, object.name + " (a place without a name)"};
					// What of the runtime's headers a module's code calls and
					// was not compiled into it.
					return {kRuntime, object.name + "!" + Readable(raw)};
				}

				// The host itself: PS2Native's own code by its names, the rest
				// PCSX2's core and what it is built from.
				if (i == 0)
				{
					if (!raw)
						return {kCore, "(a function of the host without a name)"};
					return {OfPs2Native(raw) ? kRuntime : kCore, Readable(raw)};
				}
				if (raw)
					return {kSystem, object.name + "!" + Readable(raw)};
				// (A library's own functions have no names in it: by where in
				// the file, to the nearest 256 bytes, for its disassembly.)
				char where[64];
				std::snprintf(where, sizeof(where), "!(no name, at +0x%llx)", static_cast<unsigned long long>((address - object.base) & ~0xffull));
				return {kSystem, object.name + where};
			}

			for (const Area& area : s_areas)
			{
				if (reinterpret_cast<const u8*>(address) >= area.begin && reinterpret_cast<const u8*>(address) < area.end)
					return {kGenerated, area.name};
			}
			// What the system says is mapped there (/proc/self/maps).
			for (const Mapping& mapping : s_mappings)
			{
				if (address >= mapping.begin && address < mapping.end)
				{
					char where[700];
					std::snprintf(where, sizeof(where), "(in no file's code and no known area: %s, %s, %llu KB mapped at %llx)",
						mapping.path.empty() ? "memory of no file" : mapping.path.c_str(), mapping.rights.c_str(),
						static_cast<unsigned long long>((mapping.end - mapping.begin) / 1024), static_cast<unsigned long long>(mapping.begin));
					return {kUnknown, where};
				}
			}
			return {kUnknown, "(an address where nothing is mapped)"};
		}

		void ReadMappings()
		{
			s_mappings.clear();
			std::FILE* const fp = std::fopen("/proc/self/maps", "r");
			if (!fp)
				return;
			char line[1024];
			while (std::fgets(line, sizeof(line), fp))
			{
				unsigned long long begin = 0, end = 0;
				char rights[8] = {};
				char path[600] = {};
				if (std::sscanf(line, "%llx-%llx %7s %*s %*s %*s %599[^\n]", &begin, &end, rights, path) >= 3)
					s_mappings.push_back({begin, end, rights, path});
			}
			std::fclose(fp);
		}

		// Whether an address is in the host's own code or in a module of
		// compiled code: where a function of the system's can have been
		// called from.
		bool InOurCode(u64 address)
		{
			for (size_t i = 0; i < s_objects.size(); i++)
			{
				const Object& object = s_objects[i];
				if (i != 0 && object.name != "ps2n-game.so" && object.name != "ps2n-bios.so" && object.name != "ps2n-vu.so" && object.name != "ps2n-iop.so")
					continue;
				for (const auto& [begin, end] : object.code)
				{
					if (address >= begin && address < end)
						return true;
				}
			}
			for (const Area& area : s_areas)
			{
				if (reinterpret_cast<const u8*>(address) >= area.begin && reinterpret_cast<const u8*>(address) < area.end)
					return true;
			}
			return false;
		}
	} // namespace

	bool Available()
	{
		return true;
	}

	// (The CPU thread says which it is itself, in Enable: `thread` is not
	// needed.)
	bool Start(void*)
	{
		MakeReady();
		if (!s_ready.load())
			return false;
		if (!s_running.exchange(true))
			s_sampler = std::thread(SamplerMain);
		return true;
	}

	void Enable(bool on)
	{
		if (!on)
		{
			if (s_enabled.exchange(false))
				s_ran += Nanoseconds(CLOCK_THREAD_CPUTIME_ID) - s_ran_from;
			return;
		}

		MakeReady();
		if (!s_ready.load())
			return;
		s_areas = {
			{SysMemory::GetEERec(), SysMemory::GetEERecEnd(), "host code from PCSX2's EE recompiler"},
			{SysMemory::GetIOPRec(), SysMemory::GetIOPRecEnd(), "host code from PCSX2's IOP recompiler"},
			{SysMemory::GetVU0Rec(), SysMemory::GetVU0RecEnd(), "host code from PCSX2's VU0 recompiler"},
			{SysMemory::GetVU1Rec(), SysMemory::GetVU1RecEnd(), "host code from PCSX2's VU1 recompiler"},
			{SysMemory::GetVIFUnpackRec(), SysMemory::GetVIFUnpackRecEnd(), "host code from PCSX2's VIF unpack generator"},
			{SysMemory::GetSWRec(), SysMemory::GetSWRecEnd(), "host code from PCSX2's software renderer"},
		};
		// This thread is the one to look at: the clock of its processor time,
		// then its number, which the sampler waits for.
		if (pthread_getcpuclockid(pthread_self(), &s_cpu_clock) != 0)
		{
			std::fputs("ps2n-host: no clock of the CPU thread's own time: there will be no profile\n", stderr);
			return;
		}
		s_ran_from = Nanoseconds(CLOCK_THREAD_CPUTIME_ID);
		s_enabled.store(true);
		s_tid.store(static_cast<pid_t>(syscall(SYS_gettid)));
	}

	void Stop()
	{
		s_enabled.store(false);
		s_tid.store(0);
		if (s_running.exchange(false) && s_sampler.joinable())
			s_sampler.join();
	}

	bool Write(const char* path, uint32_t first_frame, uint32_t frames, double seconds)
	{
		std::FILE* fp = std::fopen(path, "wb");
		if (!fp)
			return false;

		const size_t taken = s_samples ? std::min(s_taken.load(), kMaxSamples) : 0;
		std::unordered_map<u64, u64> by_address;
		for (size_t i = 0; i < taken; i++)
			by_address[s_samples[i].at]++;

		s_objects.clear();
		dl_iterate_phdr(OneObject, &s_objects);
		ReadMappings();

		u64 by_kind[kKindCount] = {};
		std::unordered_map<std::string, std::pair<Kind, u64>> by_name;
		std::unordered_map<u64, Place> places;
		// The compiled code, instruction by instruction: where in which function,
		// and how often.
		struct Hot
		{
			u64 count;
			std::string where;
		};
		std::vector<Hot> hot[kKindCount];
		for (const auto& [address, count] : by_address)
		{
			const Place& place = places.emplace(address, Identify(address)).first->second;
			by_kind[place.kind] += count;
			auto& entry = by_name[place.name];
			entry.first = place.kind;
			entry.second += count;
			if (place.compiled)
			{
				char where[400];
				std::snprintf(where, sizeof(where), "%s+0x%llx", place.name.c_str(), static_cast<unsigned long long>(place.offset));
				hot[place.kind].push_back({count, where});
			}
		}

		// The system's functions and the places not identified, by what called
		// them: the first word on top of the stack that is an address in the
		// host's code, in compiled code or in code PCSX2 generated. A guess that
		// is right for a function called straight from there, which most are.
		std::unordered_map<std::string, u64> by_call;
		std::unordered_map<u64, std::string> callers;
		for (size_t i = 0; i < taken; i++)
		{
			const Place& place = places.at(s_samples[i].at);
			if (place.kind != kSystem && place.kind != kUnknown)
				continue;
			std::string from = "(nothing of ours on top of the stack)";
			for (const u64 word : s_samples[i].stack)
			{
				if (!InOurCode(word))
					continue;
				auto it = callers.find(word);
				if (it == callers.end())
				{
					// With where in the host's file the call returns to, for
					// llvm-symbolizer, which says what was compiled into there.
					std::string name = Identify(word).name;
					if (!s_objects.empty() && !s_objects[0].code.empty() && word >= s_objects[0].code.front().first &&
						word < s_objects[0].code.back().second)
					{
						char where[48];
						std::snprintf(where, sizeof(where), " (the host's file at 0x%llx)", static_cast<unsigned long long>(word - s_objects[0].base));
						name += where;
					}
					it = callers.emplace(word, std::move(name)).first;
				}
				from = it->second;
				break;
			}
			by_call[place.name + "  <-  " + from]++;
		}

		const std::string host_symbols = s_objects.empty() ? std::string("none") :
			std::to_string(s_objects[0].symbols.size()) + " functions named in " + s_objects[0].path;
		const double total = taken ? static_cast<double>(taken) : 1.0;
		const double ran = static_cast<double>(s_ran) / 1e9;
		std::fprintf(fp,
			"# Where the CPU thread was, looked at after every millisecond it ran (ps2n-host -profile).\n"
			"# The CPU thread carries the EE, the IOP and the VUs. The GS has a thread of its own, which was not looked at.\n"
			"# %zu samples over %u frames from frame %u on, in %.1f s, of which the CPU thread ran %.1f s (%.0f samples for each second it ran; %.1f frames a second)%s\n"
			"# The host's own symbols: %s\n\n",
			taken, frames, first_frame, seconds, ran, ran > 0.0 ? static_cast<double>(taken) / ran : 0.0,
			seconds > 0.0 ? static_cast<double>(frames) / seconds : 0.0, s_dropped.load() ? "; more would have been taken had there been room" : "",
			host_symbols.c_str());

		std::fputs("By kind of code:\n", fp);
		for (int kind = 0; kind < kKindCount; kind++)
		{
			std::fprintf(fp, "  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(by_kind[kind]) / total,
				static_cast<unsigned long long>(by_kind[kind]), kKindNames[kind]);
		}

		// The instructions of the compiled code that had most of the samples, by
		// function and offset: to be read beside the function's disassembly
		// (llvm-objdump --disassemble-symbols). A sample falls on the instruction
		// the processor was about to complete, which is often the one after the
		// one waiting.
		const auto list_hot = [&](Kind kind, const char* what, size_t most) {
			std::vector<Hot>& list = hot[kind];
			if (list.empty())
				return;
			std::sort(list.begin(), list.end(), [](const Hot& a, const Hot& b) { return a.count > b.count; });
			std::fprintf(fp, "\nThe instructions of %s with most of the samples (share of the run, samples, where):\n", what);
			for (size_t i = 0; i < list.size() && i < most; i++)
			{
				std::fprintf(fp, "  %6.2f%%  %7llu  %s\n", 100.0 * static_cast<double>(list[i].count) / total,
					static_cast<unsigned long long>(list[i].count), list[i].where.c_str());
			}
		};
		list_hot(kGame, "the game's compiled code", 80);
		list_hot(kIop, "the IOP's compiled code", 40);
		list_hot(kMicro, "the compiled micro code", 40);

		if (!by_call.empty())
		{
			std::vector<std::pair<std::string, u64>> calls(by_call.begin(), by_call.end());
			std::sort(calls.begin(), calls.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			std::fputs("\nThe system's functions and the places not identified, by what called them, as far as the stack's top says (share of the run, samples):\n", fp);
			for (size_t i = 0; i < calls.size() && i < 40; i++)
			{
				std::fprintf(fp, "  %6.2f%%  %9llu  %s\n", 100.0 * static_cast<double>(calls[i].second) / total,
					static_cast<unsigned long long>(calls[i].second), calls[i].first.c_str());
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
			for (size_t i = 0; i < names.size() && listed < 40; i++)
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

	// (A sampled profile for the compiler, and how much host code the generated
	// files became, need the lines of the code: not written for Linux.)
	bool WriteSampleProfile(const char*)
	{
		return false;
	}

	bool WriteCodeSize(const char*, const std::vector<ImageCode>&)
	{
		return false;
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

	bool Write(const char* path, uint32_t first_frame, uint32_t frames, double seconds)
	{
		return false;
	}

	bool WriteSampleProfile(const char* path)
	{
		return false;
	}

	bool WriteCodeSize(const char* path, const std::vector<ImageCode>& images)
	{
		return false;
	}
} // namespace ps2n::host::profile

#endif
