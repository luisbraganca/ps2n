// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-host: boots a PS2 executable on the PCSX2 core without any user
// interface, captures what the program prints, and stops at a marker, a frame
// count or a time limit. It is the proof of concept's test bench: the same
// run can be repeated with different CPU providers and the outputs compared.
//
// The start-up sequence follows pcsx2-gsrunner/Main.cpp of PCSX2 v2.9.170,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+.

#include "host_state.h"
#include "selftest.h"

#include "runtime/ee_provider.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#endif

#include "common/Console.h"
#include "common/CrashHandler.h"
#include "common/FileSystem.h"
#include "common/Image.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#include "fmt/format.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Config.h"
#include "pcsx2/Counters.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/VMManager.h"

// The executables compiled ahead of time that this host is linked with: a list
// the recompiler writes (tests/tests.cmake). The array ends with a null entry.
extern const ps2n::ee::ImageInfo* const ps2n_images[];
extern const size_t ps2n_image_count;

namespace
{
	// Guest calls become host calls in compiled code, so the CPU thread gets a
	// large stack. Compiled code may use all of it but the reserve, which is
	// for the core underneath and for what compiled code calls.
	constexpr u32 kCpuThreadStackSize = 64 * 1024 * 1024;
	constexpr u32 kCpuThreadStackReserve = 16 * 1024 * 1024;

	enum ExitCode : int
	{
		EXIT_MARKER = 0, // the guest printed the stop marker
		EXIT_ERROR = 1, // the machine could not be set up or booted
		EXIT_FRAME_LIMIT = 2, // the frame limit passed first
		EXIT_TIME_LIMIT = 3, // the wall-clock limit passed first
		EXIT_USAGE = 4,
	};

	enum class EEProvider
	{
		Jit, // PCSX2's EE recompiler
		Interp, // PCSX2's EE interpreter
		Ps2n, // PS2Native's provider (runtime/ee_provider.h)
	};

	const char* EEProviderName(EEProvider provider)
	{
		switch (provider)
		{
			case EEProvider::Jit: return "jit";
			case EEProvider::Interp: return "interp";
			default: return "ps2n";
		}
	}

	struct Options
	{
		std::string boot_path; // the ELF or disc image to boot
		std::string bios_path; // the BIOS image file
		std::string data_dir; // private data directory; never PCSX2's own
		std::string out_path; // where the guest's console output goes
		std::string times_path; // where it goes with the frame and EE cycle count of every line
		std::string log_path; // where the core's whole log goes
		std::string marker = "-- TEST END";
		EEProvider ee = EEProvider::Jit;
		bool iop_jit = true;
		bool vu_jit = true;
		GSRendererType renderer = GSRendererType::Null;
		u32 max_frames = 3600;
		u32 max_seconds = 60;
		bool echo = false; // copy the core's log to this process's console
		bool selftest = false; // run the self-test instead of a program
		bool static_code = true; // with -ee ps2n: run code compiled ahead of time where there is some
		std::string capture_path; // where the block starts that had to be interpreted are written
		std::string trace_path; // where the record of event tests is written
		std::string trace_against_path; // an earlier run's record, to compare this run with
		std::optional<u64> trace_detail_from; // the event test from which the record includes the registers
		std::optional<u64> stack_budget; // host stack compiled code may use, when not the default
		std::string image_id; // the one compiled image to use, when not all of them
		bool host_fs = false; // let the guest open files beside its executable ("host:")
		u32 upscale = 1; // internal resolution, for the hardware renderers
		u32 settle_frames = 0; // frames to run on after the marker before stopping
		std::string frame_path; // where the last displayed frame is written, as an image
		ps2n::selftest::Options selftest_options;
	};

	Options s_options;
	MemorySettingsInterface s_settings;

	// The guest's console output, one line per entry, in the order the core
	// logged it. EE lines and IOP lines are told apart by the colour the core
	// gives them.
	std::mutex s_guest_mutex;
	std::vector<std::string> s_guest_lines;

	// For each of those lines, when the core logged it: frames shown so far,
	// and the EE's cycle count.
	struct GuestLineTime
	{
		u32 frames;
		u64 cycle;
	};
	std::vector<GuestLineTime> s_guest_times;

	std::atomic<bool> s_marker_seen{false};
	std::atomic<bool> s_time_limit_passed{false};
	std::atomic<bool> s_cpu_thread_done{false};
	std::atomic<int> s_exit_code{EXIT_ERROR};

	// CPU thread only.
	std::thread::id s_cpu_thread_id;
	u32 s_frames = 0;
	bool s_stop_requested = false;
	bool s_provider_replaced = false;

	std::mutex s_queue_mutex;
	std::deque<std::function<void()>> s_queue;

	// The frame on display when the run was stopped: its size and fingerprint,
	// in words, for the report. CPU thread only.
	std::string s_frame_line;
	u32 s_frames_since_marker = 0;

	// The workload a benchmark program is running, and what has been measured.
	std::string s_bench_name;
	std::chrono::steady_clock::time_point s_bench_start;
	u64 s_bench_start_cycle = 0;
	std::vector<std::string> s_bench_lines;

	void PrintUsage()
	{
		std::fputs(
			"Usage: ps2n-host -bios <file> -data <dir> [options] <file.elf | disc image>\n"
			"\n"
			"  -bios <file>       PS2 BIOS image. It is opened read-only.\n"
			"  -data <dir>        Private data directory for logs and caches.\n"
			"  -out <file>        Write the program's console output here.\n"
			"  -times <file>      Write it here as well with, before each line, the number\n"
			"                     of frames shown and the EE's cycle count when the line\n"
			"                     was printed: to see where two runs part in time.\n"
			"  -log <file>        Write the core's whole log here.\n"
			"  -ee jit|interp|ps2n  PCSX2's EE recompiler, its interpreter, or PS2Native's\n"
			"                     own EE provider (default jit).\n"
			"  -iop jit|interp    PCSX2's IOP recompiler or interpreter (default jit).\n"
			"  -vu jit|interp     PCSX2's VU recompilers or interpreters (default jit).\n"
			"  -static on|off     With -ee ps2n: run code compiled ahead of time where there\n"
			"                     is some for the executable (default on). Off, PS2Native's\n"
			"                     interpreter runs everything.\n"
			"  -capture <file>    With -ee ps2n: write what the recompiler needs to compile\n"
			"                     what had to be interpreted: the addresses inside the\n"
			"                     executable at which a block was interpreted, and the code\n"
			"                     found in memory where a function had been compiled from\n"
			"                     other code.\n"
			"  -image <id>        With -ee ps2n: of the compiled images this host is linked\n"
			"                     with, use only the one with this id.\n"
			"  -trace <file>      With -ee ps2n: write one line per event test (its number,\n"
			"                     the PC, the cycle count, a fingerprint of the registers).\n"
			"  -trace-against <file>  With -ee ps2n: compare this run's event tests with\n"
			"                     the file an earlier run wrote with -trace, and stop with\n"
			"                     an error at the first that differs.\n"
			"  -trace-detail <n>  With -trace: from event test n on, write the registers too.\n"
			"  -stack-budget <n>  With -ee ps2n: bytes of host stack compiled code may use\n"
			"                     before its frames are abandoned (default: most of the\n"
			"                     CPU thread's stack). 0 abandons them at every event test,\n"
			"                     which must not change a run.\n"
			"  -hostfs            Let the program open files on this computer through the\n"
			"                     core's \"host:\" device, relative to its executable. Off\n"
			"                     unless given: only for programs you trust.\n"
			"  -renderer <name>   GS renderer: null (default; nothing is drawn), sw (PCSX2's\n"
			"                     software renderer), vulkan, opengl, dx11, dx12. There is\n"
			"                     no window; the last displayed frame is fingerprinted.\n"
			"  -upscale <n>       Internal resolution multiplier for a hardware renderer.\n"
			"  -frame <file>      Write the last displayed frame to this image file (.png).\n"
			"  -settle <n>        Run this many frames on after the marker line before\n"
			"                     stopping, so that what was drawn last is displayed.\n"
			"  -until <text>      Stop once the program prints a line containing this\n"
			"                     (default \"-- TEST END\").\n"
			"  -frames <n>        Stop after this many frames (default 3600).\n"
			"  -seconds <n>       Stop after this much wall-clock time (default 60).\n"
			"  -echo              Copy the core's log to the console.\n"
			"  -selftest          Instead of booting a file, check PS2Native's instruction\n"
			"                     tables and semantics against the PCSX2 core.\n"
			"  -selftest-trials <n>  Random cases per instruction against PCSX2's\n"
			"                     recompiler (default 400); ten times as many against\n"
			"                     its interpreter.\n"
			"\n"
			"Exit code: 0 marker seen, 1 error, 2 frame limit, 3 time limit, 4 usage.\n",
			stderr);
	}

	bool ParseProvider(const char* name, const char* value, bool* jit)
	{
		if (!std::strcmp(value, "jit"))
			*jit = true;
		else if (!std::strcmp(value, "interp"))
			*jit = false;
		else
		{
			std::fprintf(stderr, "ps2n-host: %s takes jit or interp, not '%s'\n", name, value);
			return false;
		}
		return true;
	}

	bool ParseCommandLine(int argc, char* argv[])
	{
		Options& o = s_options;
		for (int i = 1; i < argc; i++)
		{
			const char* arg = argv[i];
			const bool has_value = (i + 1) < argc;
			const auto is = [arg](const char* name) { return !std::strcmp(arg, name); };

			if (is("-help") || is("--help") || is("-h"))
				return false;
			else if (is("-echo"))
				o.echo = true;
			else if (is("-hostfs"))
				o.host_fs = true;
			else if (is("-selftest"))
				o.selftest = true;
			else if (is("-capture") && has_value)
				o.capture_path = argv[++i];
			else if (is("-times") && has_value)
				o.times_path = argv[++i];
			else if (is("-image") && has_value)
				o.image_id = argv[++i];
			else if (is("-trace") && has_value)
				o.trace_path = argv[++i];
			else if (is("-trace-against") && has_value)
				o.trace_against_path = argv[++i];
			else if (is("-stack-budget") && has_value)
			{
				o.stack_budget = StringUtil::FromChars<u64>(argv[++i]);
				if (!o.stack_budget)
				{
					std::fputs("ps2n-host: -stack-budget takes a number of bytes\n", stderr);
					return false;
				}
			}
			else if (is("-trace-detail") && has_value)
			{
				o.trace_detail_from = StringUtil::FromChars<u64>(argv[++i]);
				if (!o.trace_detail_from)
				{
					std::fputs("ps2n-host: -trace-detail takes the number of an event test\n", stderr);
					return false;
				}
			}
			else if (is("-static") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "on"))
					o.static_code = true;
				else if (!std::strcmp(value, "off"))
					o.static_code = false;
				else
				{
					std::fprintf(stderr, "ps2n-host: -static takes on or off, not '%s'\n", value);
					return false;
				}
			}
			else if (is("-selftest-trials") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -selftest-trials takes a positive number\n", stderr);
					return false;
				}
				o.selftest_options.interp_trials = *n * 10;
				o.selftest_options.jit_trials = *n;
			}
			else if (is("-bios") && has_value)
				o.bios_path = argv[++i];
			else if (is("-data") && has_value)
				o.data_dir = argv[++i];
			else if (is("-out") && has_value)
				o.out_path = argv[++i];
			else if (is("-log") && has_value)
				o.log_path = argv[++i];
			else if (is("-until") && has_value)
				o.marker = argv[++i];
			else if (is("-ee") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "jit"))
					o.ee = EEProvider::Jit;
				else if (!std::strcmp(value, "interp"))
					o.ee = EEProvider::Interp;
				else if (!std::strcmp(value, "ps2n"))
					o.ee = EEProvider::Ps2n;
				else
				{
					std::fprintf(stderr, "ps2n-host: -ee takes jit, interp or ps2n, not '%s'\n", value);
					return false;
				}
			}
			else if (is("-iop") && has_value)
			{
				if (!ParseProvider("-iop", argv[++i], &o.iop_jit))
					return false;
			}
			else if (is("-vu") && has_value)
			{
				if (!ParseProvider("-vu", argv[++i], &o.vu_jit))
					return false;
			}
			else if (is("-renderer") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "null"))
					o.renderer = GSRendererType::Null;
				else if (!std::strcmp(value, "sw"))
					o.renderer = GSRendererType::SW;
				else if (!std::strcmp(value, "vulkan"))
					o.renderer = GSRendererType::VK;
				else if (!std::strcmp(value, "opengl"))
					o.renderer = GSRendererType::OGL;
				else if (!std::strcmp(value, "dx11"))
					o.renderer = GSRendererType::DX11;
				else if (!std::strcmp(value, "dx12"))
					o.renderer = GSRendererType::DX12;
				else
				{
					std::fprintf(stderr, "ps2n-host: unknown renderer '%s'\n", value);
					return false;
				}
			}
			else if (is("-upscale") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0 || *n > 16)
				{
					std::fputs("ps2n-host: -upscale takes a whole number from 1 to 16\n", stderr);
					return false;
				}
				o.upscale = *n;
			}
			else if (is("-settle") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n)
				{
					std::fputs("ps2n-host: -settle takes a number of frames\n", stderr);
					return false;
				}
				o.settle_frames = *n;
			}
			else if (is("-frame") && has_value)
				o.frame_path = argv[++i];
			else if (is("-frames") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -frames takes a positive number\n", stderr);
					return false;
				}
				o.max_frames = *n;
			}
			else if (is("-seconds") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -seconds takes a positive number\n", stderr);
					return false;
				}
				o.max_seconds = *n;
			}
			else if (arg[0] == '-')
			{
				std::fprintf(stderr, "ps2n-host: unknown option '%s', or its value is missing\n", arg);
				return false;
			}
			else if (o.boot_path.empty())
				o.boot_path = arg;
			else
			{
				std::fprintf(stderr, "ps2n-host: more than one file to boot ('%s')\n", arg);
				return false;
			}
		}

		if ((o.boot_path.empty() && !o.selftest) || o.bios_path.empty() || o.data_dir.empty())
		{
			std::fputs("ps2n-host: a file to boot (or -selftest), -bios and -data are all required\n", stderr);
			return false;
		}
		if (o.selftest && (!o.boot_path.empty() || o.ee != EEProvider::Jit))
		{
			std::fputs("ps2n-host: -selftest takes no file to boot and needs PCSX2's EE recompiler (-ee jit)\n", stderr);
			return false;
		}

		// On Windows the core's file functions turn an absolute path into an
		// extended-length one (\\?\C:\...), which does not take forward slashes
		// (FileSystem::GetWin32Path in common/FileSystem.cpp). Build tools pass
		// forward slashes, so every path is put in the platform's form here.
		for (std::string* path : {&o.boot_path, &o.bios_path, &o.data_dir, &o.out_path, &o.times_path, &o.log_path, &o.capture_path,
				 &o.trace_path, &o.trace_against_path, &o.frame_path})
			Path::ToNativePath(path);
		return true;
	}

	// Every log line of the core comes through here, on whichever thread wrote
	// it. The guest's own output is the lines in the two colours the core
	// reserves for the EE and IOP consoles (pcsx2/DebugTools/Debug.h).
	void LogCallback(LOGLEVEL level, ConsoleColors color, std::string_view message)
	{
		if (color != Color_Cyan && color != Color_Yellow)
			return;

		{
			// The guest prints on the CPU thread, so the frame count and the cycle
			// count read here are those of that moment.
			std::lock_guard lock(s_guest_mutex);
			s_guest_lines.emplace_back(message);
			s_guest_times.push_back({s_frames, cpuRegs.cycle});
		}

		// Only note it here. This can run in the middle of a guest instruction,
		// so the machine is stopped from the frame callback instead.
		if (!s_options.marker.empty() && message.find(s_options.marker) != std::string_view::npos)
			s_marker_seen.store(true);

		// A program that times its own workloads prints "bench <name> start"
		// before one and "bench <name> = <result>" after it (tests/programs/
		// bench.s). The guest prints on the CPU thread, so the clock and the
		// cycle count read here are those of that moment.
		if (message.starts_with("bench "))
		{
			std::string_view rest = message.substr(6);
			while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r'))
				rest.remove_suffix(1);

			const auto now = std::chrono::steady_clock::now();
			if (rest.ends_with(" start"))
			{
				s_bench_name = std::string(rest.substr(0, rest.size() - 6));
				s_bench_start = now;
				s_bench_start_cycle = cpuRegs.cycle;
			}
			else if (const size_t equals = rest.find(" = "); equals != std::string_view::npos && rest.substr(0, equals) == s_bench_name)
			{
				const double ms = std::chrono::duration<double, std::milli>(now - s_bench_start).count();
				const u64 cycles = cpuRegs.cycle - s_bench_start_cycle;
				s_bench_lines.push_back(fmt::format("ps2n-host: bench {}: {:.1f} ms, {} EE cycles, {:.0f} million cycles per second",
					s_bench_name, ms, cycles, ms > 0.0 ? static_cast<double>(cycles) / ms / 1000.0 : 0.0));
				s_bench_name.clear();
			}
		}
	}

	bool WriteGuestOutput()
	{
		if (s_options.out_path.empty())
			return true;

		std::FILE* fp = FileSystem::OpenCFile(s_options.out_path.c_str(), "wb");
		if (!fp)
		{
			std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.out_path.c_str());
			return false;
		}

		std::lock_guard lock(s_guest_mutex);
		for (const std::string& line : s_guest_lines)
		{
			std::fwrite(line.data(), 1, line.size(), fp);
			std::fputc('\n', fp);
		}
		std::fclose(fp);

		if (!s_options.times_path.empty())
		{
			fp = FileSystem::OpenCFile(s_options.times_path.c_str(), "wb");
			if (!fp)
			{
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.times_path.c_str());
				return false;
			}
			std::fputs("# frames shown, EE cycle count, line\n", fp);
			for (size_t i = 0; i < s_guest_lines.size(); i++)
			{
				std::fprintf(fp, "%5u %12llu  ", s_guest_times[i].frames, static_cast<unsigned long long>(s_guest_times[i].cycle));
				std::fwrite(s_guest_lines[i].data(), 1, s_guest_lines[i].size(), fp);
				std::fputc('\n', fp);
			}
			std::fclose(fp);
		}
		return true;
	}

	bool InitializeConfig()
	{
		const Options& o = s_options;

		if (!FileSystem::FileExists(o.bios_path.c_str()))
		{
			std::fprintf(stderr, "ps2n-host: BIOS image not found: %s\n", o.bios_path.c_str());
			return false;
		}
		if (!o.selftest && !FileSystem::FileExists(o.boot_path.c_str()))
		{
			std::fprintf(stderr, "ps2n-host: file to boot not found: %s\n", o.boot_path.c_str());
			return false;
		}

		EmuFolders::SetAppRoot();
		if (!EmuFolders::SetResourcesDirectory())
			return false;

		// Without this the core would use the user's own PCSX2 data directory
		// (Documents\PCSX2 on Windows): its settings, memory cards and logs.
		if (!FileSystem::EnsureDirectoryExists(o.data_dir.c_str(), true))
		{
			std::fprintf(stderr, "ps2n-host: cannot create data directory %s\n", o.data_dir.c_str());
			return false;
		}
		EmuConfig.CustomDataPath = Path::RealPath(o.data_dir);
		if (!EmuFolders::SetDataDirectory(nullptr))
		{
			std::fprintf(stderr, "ps2n-host: cannot use data directory %s\n", o.data_dir.c_str());
			return false;
		}
		if (!EmuFolders::DataRoot.starts_with(EmuConfig.CustomDataPath))
		{
			// A portable.ini beside the executable takes priority over the custom
			// path. Refuse rather than write somewhere unexpected.
			std::fprintf(stderr, "ps2n-host: the core chose data directory %s instead of %s\n",
				EmuFolders::DataRoot.c_str(), EmuConfig.CustomDataPath.c_str());
			return false;
		}

		CrashHandler::SetWriteDirectory(EmuFolders::DataRoot);

		const char* error;
		if (!VMManager::PerformEarlyHardwareChecks(&error))
		{
			std::fprintf(stderr, "ps2n-host: %s\n", error);
			return false;
		}

		// The core's on-screen display needs a font even when nothing is shown.
		{
			const std::string font_path =
				EmuFolders::GetOverridableResourcePath("fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
			const auto font_data = FileSystem::MapBinaryFileForRead(font_path.c_str());
			if (font_data.empty())
			{
				std::fprintf(stderr, "ps2n-host: cannot load font %s\n", font_path.c_str());
				return false;
			}

			std::vector<ImGuiManager::FontInfo> fonts;
			ImGuiManager::FontInfo fi{};
			fi.data = font_data;
			fi.exclude_ranges = {};
			fi.face_name = nullptr;
			fi.is_emoji_font = false;
			fonts.push_back(fi);
			ImGuiManager::SetFonts(std::move(fonts));
		}

		MemorySettingsInterface& si = s_settings;
		Host::Internal::SetBaseSettingsLayer(&si);
		VMManager::SetDefaultSettings(si, true, true, true, true, true);

		// The BIOS is used where it is. The core keeps the console's NVRAM in
		// files beside this path, so a link given here is deliberately not
		// resolved to what it points at.
		const std::string bios_path = Path::Canonicalize(
			Path::IsAbsolute(o.bios_path) ? o.bios_path : Path::Combine(FileSystem::GetWorkingDirectory(), o.bios_path));
		si.SetStringValue("Folders", "Bios", std::string(Path::GetDirectory(bios_path)).c_str());
		si.SetStringValue("Filenames", "BIOS", std::string(Path::GetFileName(bios_path)).c_str());

		// CPU providers. VU1 stays on the CPU thread so that runs repeat.
		//
		// With PS2Native's EE provider the core is told its recompiler is in
		// use: code shared by all providers then behaves as it does for the
		// recompiler, which is the configuration games are tested with (no
		// cache emulation, TLB misses logged rather than raised).
		si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEE", o.ee != EEProvider::Interp);
		si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableIOP", o.iop_jit);
		si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU0", o.vu_jit);
		si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU1", o.vu_jit);
		si.SetBoolValue("EmuCore/Speedhacks", "vuThread", false);

		si.SetBoolValue("EmuCore", "HostFs", o.host_fs);

		// Run as fast as the host allows, with nothing presented.
		si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(o.renderer));
		si.SetFloatValue("EmuCore/GS", "upscale_multiplier", static_cast<float>(o.upscale));
		si.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
		si.SetIntValue("EmuCore/GS", "VsyncEnable", 0);

		// No sound device, no input devices, no memory cards.
		si.SetStringValue("SPU2/Output", "Backend", "Null");
		si.SetBoolValue("InputSources", "SDL", false);
		si.SetBoolValue("InputSources", "XInput", false);
		Pad::ClearPortBindings(si, 0);
		si.ClearSection("Hotkeys");
		for (u32 i = 0; i < 2; i++)
		{
			si.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", i + 1).c_str(), false);
			si.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", i + 1).c_str(), "");
		}

		// The core only forwards the guest's console output when one of its own
		// log sinks is on, so the file log is always written.
		if (!o.log_path.empty())
			VMManager::Internal::SetFileLogPath(o.log_path);
		si.SetBoolValue("Logging", "EnableSystemConsole", o.echo);
		si.SetBoolValue("Logging", "EnableFileLogging", true);
		si.SetBoolValue("Logging", "EnableTimestamps", false);
		si.SetBoolValue("Logging", "EnableVerbose", false);
		si.SetBoolValue("Logging", "EnableEEConsole", true);
		si.SetBoolValue("Logging", "EnableIOPConsole", true);

		VMManager::Internal::LoadStartupSettings();
		Log::SetHostOutputLevel(LOGLEVEL_INFO, &LogCallback);
		return true;
	}

	void DrainQueue()
	{
		for (;;)
		{
			std::function<void()> function;
			{
				std::lock_guard lock(s_queue_mutex);
				if (s_queue.empty())
					return;
				function = std::move(s_queue.front());
				s_queue.pop_front();
			}
			function();
		}
	}

	// Reads back the frame the GS has on display and fingerprints it, so that
	// two runs can be held to having drawn the same picture. At the internal
	// resolution, without the aspect-ratio correction a window would apply.
	void CaptureFrame()
	{
		if (s_options.renderer == GSRendererType::Null)
			return;

		u32 width = 0, height = 0;
		std::vector<u32> pixels;
		if (!MTGS::SaveMemorySnapshot(0, 0, false, false, &width, &height, &pixels) || pixels.empty())
		{
			s_frame_line = "ps2n-host: no frame was on display";
			return;
		}

		// What is seen is the colour; the fourth byte is whatever the program
		// left in the frame buffer's alpha.
		u64 hash = 14695981039346656037ull;
		for (const u32 pixel : pixels)
		{
			for (u32 shift = 0; shift < 24; shift += 8)
				hash = (hash ^ ((pixel >> shift) & 0xFFu)) * 1099511628211ull;
		}
		s_frame_line = fmt::format("ps2n-host: frame {}x{}, fingerprint {:016X}", width, height, hash);

		if (!s_options.frame_path.empty())
		{
			for (u32& pixel : pixels)
				pixel |= 0xFF000000u;
			const RGBA8Image image(width, height, pixels.data());
			if (!image.SaveToFile(s_options.frame_path.c_str()))
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.frame_path.c_str());
		}
	}

	// What PS2Native's EE provider did, so that nothing is interpreted or
	// delegated without it showing.
	void ReportProvider()
	{
		using ull = unsigned long long;
		const ps2n::ee::Stats& stats = ps2n::ee::GetStats();
		const auto percent = [](u64 part, u64 whole) { return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0; };

		const u64 total_cycles = cpuRegs.cycle;
		const u64 other_cycles = total_cycles > stats.interpreted_cycles ? total_cycles - stats.interpreted_cycles : 0;

		if (stats.image)
		{
			std::fprintf(stderr,
				"ps2n-host: compiled code of %s: %s; %llu dispatcher entries; functions verified against memory %llu, found"
				" different %llu, no longer write-protected %llu; calls passed on to a variant %llu; frames abandoned: %llu after"
				" writes to code, %llu for stack depth; blocks finished by the interpreter %llu; entries without code %llu\n",
				stats.image->name, s_options.static_code ? "in use" : "switched off", static_cast<ull>(stats.static_entries),
				static_cast<ull>(stats.verified), static_cast<ull>(stats.verify_failed), static_cast<ull>(stats.unprotected),
				static_cast<ull>(stats.redirected), static_cast<ull>(stats.forced_unwinds), static_cast<ull>(stats.stack_unwinds),
				static_cast<ull>(stats.left), static_cast<ull>(stats.no_progress));
		}
		else
		{
			std::fputs("ps2n-host: no compiled code for this executable: everything was interpreted\n", stderr);
		}

		// What the interpreter did not count is compiled code, plus the idle time
		// the core itself skips: a read of INTC_STAT in a waiting loop moves the
		// cycle count to the next event (IntCHackCheck, pcsx2/HwRead.cpp).
		std::fprintf(stderr,
			"ps2n-host: EE cycles %llu: interpreter %llu (%.2f%%), of which inside the executable's code %llu (%.2f%% of all);"
			" compiled code and idle time the core skips %llu (%.2f%%)\n",
			static_cast<ull>(total_cycles), static_cast<ull>(stats.interpreted_cycles), percent(stats.interpreted_cycles, total_cycles),
			static_cast<ull>(stats.interpreted_cycles_in_image), percent(stats.interpreted_cycles_in_image, total_cycles),
			static_cast<ull>(other_cycles), percent(other_cycles, total_cycles));
		std::fprintf(stderr,
			"ps2n-host: interpreter: %llu instructions (%llu inside the executable's code), %llu (%.1f%%) carried out by PCSX2's"
			" interpreter functions\n",
			static_cast<ull>(stats.interpreted), static_cast<ull>(stats.interpreted_in_image), static_cast<ull>(stats.delegated),
			percent(stats.delegated, stats.interpreted));
		if (!s_options.trace_path.empty() || !s_options.trace_against_path.empty())
		{
			std::fprintf(stderr, "ps2n-host: %llu event tests, fingerprint of the run %016llX\n", static_cast<ull>(stats.event_tests),
				static_cast<ull>(stats.trace_hash));
		}
		else
		{
			std::fprintf(stderr, "ps2n-host: %llu event tests\n", static_cast<ull>(stats.event_tests));
		}

		// The instructions most often left to PCSX2's functions.
		std::vector<std::pair<u64, size_t>> delegated;
		for (size_t i = 0; i < std::size(stats.delegated_by_op); i++)
		{
			if (stats.delegated_by_op[i])
				delegated.emplace_back(stats.delegated_by_op[i], i);
		}
		std::sort(delegated.rbegin(), delegated.rend());
		std::string line;
		for (size_t i = 0; i < delegated.size() && i < 12; i++)
		{
			const auto op = static_cast<ps2n::r5900::Op>(delegated[i].second);
			line += fmt::format("{}{} {}", i ? ", " : "", op == ps2n::r5900::Op::Invalid ? "(unknown)" : ps2n::r5900::mnemonic(op),
				delegated[i].first);
		}
		if (!line.empty())
			std::fprintf(stderr, "ps2n-host: most delegated: %s\n", line.c_str());

		const std::vector<uint32_t> missing = ps2n::ee::GetMissingEntries();
		const std::vector<ps2n::ee::CapturedCode>& captured = ps2n::ee::GetCapturedCode();
		if (stats.image && s_options.static_code)
		{
			const bool anything = !missing.empty() || !captured.empty();
			std::fprintf(stderr,
				"ps2n-host: %zu block starts inside the executable's code had to be interpreted; in the place of %zu functions memory"
				" held code that nothing was compiled from%s\n",
				missing.size(), captured.size(),
				(anything && s_options.capture_path.empty()) ? " (pass -capture <file> to record both for the recompiler)" : "");
		}
		if (!s_options.capture_path.empty() && stats.image)
		{
			if (std::FILE* fp = FileSystem::OpenCFile(s_options.capture_path.c_str(), "wb"))
			{
				std::fprintf(fp,
					"# What PS2Native had to interpret of %s (CRC %08X).\n"
					"# Give this file to the recompiler (ps2n-recomp compile --capture) to have it compiled.\n"
					"\n"
					"# Addresses at which a block was interpreted.\n"
					"entries = [\n",
					stats.image->name, stats.image->crc);
				for (const uint32_t pc : missing)
					std::fprintf(fp, "  0x%08x,\n", pc);
				std::fputs("]\n", fp);

				for (const ps2n::ee::CapturedCode& code : captured)
				{
					std::fprintf(fp,
						"\n# Code found in memory in the place of the function at 0x%08x.\n"
						"[[code]]\n"
						"start = 0x%08x\n"
						"words = [",
						code.start, code.start);
					for (size_t i = 0; i < code.words.size(); i++)
						std::fprintf(fp, "%s0x%08x,", (i % 8) ? " " : "\n  ", code.words[i]);
					std::fputs("\n]\n", fp);
				}
				std::fclose(fp);
			}
			else
			{
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.capture_path.c_str());
			}
		}
	}

	void CPUThreadMain()
	{
		s_cpu_thread_id = std::this_thread::get_id();

		if (VMManager::Internal::CPUThreadInitialize())
		{
			VMManager::ApplySettings();

			// The core lets the BIOS loader open the boot ELF only when the path it
			// asks for equals this one exactly, and it asks with an absolute path
			// (host_path() in pcsx2/IopBios.cpp).
			VMBootParameters params;
			if (!s_options.selftest)
				params.filename = Path::RealPath(s_options.boot_path);

			if (VMManager::Initialize(params) == VMBootResult::StartupSuccess && s_options.selftest)
			{
				// The machine is whole but has executed nothing: the state the
				// self-test needs.
				VMManager::SetState(VMState::Running);
				const int failures = ps2n::selftest::Run(s_options.selftest_options);
				VMManager::Shutdown(false);
				s_exit_code.store(failures ? EXIT_ERROR : EXIT_MARKER);
			}
			else if (VMManager::HasValidVM())
			{
				// The core has just chosen its own EE provider. Replace it. It
				// only chooses again when a recompiler is switched on or off
				// (VMManager::CheckForCPUConfigChanges), which a fixed
				// configuration never does; OnGuestFrame() checks that it did not.
				bool trace_ok = true;
				if (s_options.ee == EEProvider::Ps2n)
				{
					if (s_options.image_id.empty())
					{
						ps2n::ee::SetImages(ps2n_images, ps2n_image_count);
					}
					else
					{
						static std::vector<const ps2n::ee::ImageInfo*> chosen;
						for (size_t i = 0; i < ps2n_image_count; i++)
						{
							if (s_options.image_id == ps2n_images[i]->id)
								chosen.push_back(ps2n_images[i]);
						}
						if (chosen.empty())
						{
							std::fprintf(stderr, "ps2n-host: this host has no compiled image '%s'\n", s_options.image_id.c_str());
							trace_ok = false;
						}
						ps2n::ee::SetImages(chosen.data(), chosen.size());
					}
					ps2n::ee::SetStaticCodeEnabled(s_options.static_code);
					ps2n::ee::SetHostStackBudget(
						s_options.stack_budget ? static_cast<size_t>(*s_options.stack_budget) : kCpuThreadStackSize - kCpuThreadStackReserve);
					if (!s_options.trace_path.empty() && !ps2n::ee::StartTrace(s_options.trace_path.c_str()))
					{
						std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.trace_path.c_str());
						trace_ok = false;
					}
					if (s_options.trace_detail_from)
						ps2n::ee::SetTraceDetailFrom(*s_options.trace_detail_from);
					if (!s_options.trace_against_path.empty() && !ps2n::ee::CompareTraceWith(s_options.trace_against_path.c_str()))
					{
						std::fprintf(stderr, "ps2n-host: cannot read %s\n", s_options.trace_against_path.c_str());
						trace_ok = false;
					}
					Cpu = ps2n::ee::Provider();
					Cpu->Reset();
				}

				// As fast as the host goes: the core otherwise holds the machine to
				// the console's speed, however little work a frame is.
				VMManager::SetState(VMState::Running);
				VMManager::SetLimiterMode(LimiterModeType::Unlimited);
				while (trace_ok && VMManager::GetState() == VMState::Running)
					VMManager::Execute();

				if (s_options.ee == EEProvider::Ps2n)
					ps2n::ee::StopTrace();

				int code = EXIT_FRAME_LIMIT;
				const char* reason = "frame limit";
				if (!trace_ok)
				{
					code = EXIT_ERROR;
					reason = "an error: the run could not be set up as asked";
				}
				else if (s_provider_replaced)
				{
					code = EXIT_ERROR;
					reason = "an error: the core replaced PS2Native's EE provider with its own";
				}
				else if (s_options.ee == EEProvider::Ps2n && ps2n::ee::GetTraceMismatch())
				{
					code = EXIT_ERROR;
					reason = "an error: the run differs from the one it was compared with";
					std::fprintf(stderr, "ps2n-host: %s\n", ps2n::ee::GetTraceMismatch());
				}
				else if (s_marker_seen.load())
				{
					code = EXIT_MARKER;
					reason = "marker";
				}
				else if (s_time_limit_passed.load())
				{
					code = EXIT_TIME_LIMIT;
					reason = "time limit";
				}
				std::fprintf(stderr, "ps2n-host: stopped at %s after %u frames, EE cycle %llu, EE %s, IOP %s, VU %s\n", reason,
					s_frames, static_cast<unsigned long long>(cpuRegs.cycle), EEProviderName(s_options.ee),
					s_options.iop_jit ? "jit" : "interp", s_options.vu_jit ? "jit" : "interp");
				std::fprintf(stderr, "ps2n-host: EE COP0 Config %08x (double issue %s), Status %08x, EE cycle rate setting %d\n",
					cpuRegs.CP0.n.Config, (cpuRegs.CP0.n.Config >> 18) & 1 ? "on" : "off", cpuRegs.CP0.n.Status.val,
					static_cast<int>(EmuConfig.Speedhacks.EECycleRate));
				if (s_options.ee == EEProvider::Ps2n)
					ReportProvider();
				for (const std::string& line : s_bench_lines)
					std::fprintf(stderr, "%s\n", line.c_str());
				if (!s_frame_line.empty())
					std::fprintf(stderr, "%s\n", s_frame_line.c_str());

				VMManager::Shutdown(false);
				s_exit_code.store(code);
			}
			else
			{
				std::fputs("ps2n-host: the machine failed to boot\n", stderr);
			}
		}
		else
		{
			std::fputs("ps2n-host: the CPU thread failed to initialize\n", stderr);
		}

		DrainQueue();
		VMManager::Internal::CPUThreadShutdown();
		s_cpu_thread_done.store(true);
	}

	// Ends the process if the machine does not stop on its own: a guest that
	// never reaches a frame boundary cannot be stopped from the frame callback.
	void WatchdogMain()
	{
		const auto start = std::chrono::steady_clock::now();
		const auto limit = std::chrono::seconds(s_options.max_seconds);
		const auto grace = std::chrono::seconds(20);

		while (!s_cpu_thread_done.load())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			const auto elapsed = std::chrono::steady_clock::now() - start;
			if (elapsed >= limit)
				s_time_limit_passed.store(true);
			if (elapsed >= limit + grace)
			{
				std::fputs("ps2n-host: the machine did not stop after the time limit; ending the process\n", stderr);
				WriteGuestOutput();
				std::fflush(nullptr);
				std::_Exit(EXIT_TIME_LIMIT);
			}
		}
	}

	int Run(int argc, char* argv[])
	{
		if (!ParseCommandLine(argc, argv))
		{
			PrintUsage();
			return EXIT_USAGE;
		}

		CrashHandler::Install();

		if (!InitializeConfig())
			return EXIT_ERROR;

		std::thread watchdog(WatchdogMain);

		// Compiled guest code calls compiled guest code: a guest call is a host
		// call, so the guest's call depth is the host's.
		Threading::Thread cpu_thread;
		cpu_thread.SetStackSize(kCpuThreadStackSize);
		if (!cpu_thread.Start(CPUThreadMain))
		{
			std::fputs("ps2n-host: cannot start the CPU thread\n", stderr);
			s_cpu_thread_done.store(true);
			watchdog.join();
			return EXIT_ERROR;
		}

#ifdef _WIN32
		// Components of the core may create windows owned by this thread; keep
		// its message queue served while the machine runs.
		HANDLE handle = static_cast<HANDLE>(static_cast<void*>(cpu_thread));
		for (;;)
		{
			const DWORD wait = MsgWaitForMultipleObjects(1, &handle, FALSE, INFINITE, QS_ALLINPUT);
			if (wait != WAIT_OBJECT_0 + 1)
				break;

			MSG msg;
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
#endif

		cpu_thread.Join();
		watchdog.join();

		if (!WriteGuestOutput())
			return EXIT_ERROR;
		return s_exit_code.load();
	}
} // namespace

void ps2n::host::OnGuestFrame()
{
	s_frames++;
	DrainQueue();

	if (s_stop_requested || s_options.selftest)
		return;

	// Never run on with a provider other than the one that was asked for.
	if (s_options.ee == EEProvider::Ps2n && Cpu != ps2n::ee::Provider())
		s_provider_replaced = true;

	// A run that has parted from the one it is compared with has nothing more to
	// show.
	const bool parted = s_options.ee == EEProvider::Ps2n && ps2n::ee::GetTraceMismatch() != nullptr;

	// After the marker the machine may be asked to run a few frames more, so
	// that what the program drew last has reached the display.
	bool marker = s_marker_seen.load();
	if (marker && s_frames_since_marker < s_options.settle_frames)
	{
		s_frames_since_marker++;
		marker = false;
	}

	if (s_provider_replaced || parted || marker || s_time_limit_passed.load() || s_frames >= s_options.max_frames)
	{
		CaptureFrame();
		s_stop_requested = true;
		VMManager::SetState(VMState::Stopping);
	}
}

void ps2n::host::RunOnCPUThread(std::function<void()> function, bool block)
{
	if (std::this_thread::get_id() == s_cpu_thread_id)
	{
		function();
		return;
	}

	if (!block)
	{
		std::lock_guard lock(s_queue_mutex);
		s_queue.push_back(std::move(function));
		return;
	}

	std::mutex done_mutex;
	std::condition_variable done_cv;
	bool done = false;
	{
		std::lock_guard lock(s_queue_mutex);
		s_queue.push_back([&]() {
			function();
			std::lock_guard done_lock(done_mutex);
			done = true;
			done_cv.notify_one();
		});
	}
	std::unique_lock done_lock(done_mutex);
	done_cv.wait(done_lock, [&done]() { return done; });
}

#ifdef _WIN32

// File names are only handled correctly on Windows when they arrive as UTF-16.
int wmain(int argc, wchar_t** argv)
{
	std::vector<std::string> u8_args;
	u8_args.reserve(static_cast<size_t>(argc));
	for (int i = 0; i < argc; i++)
		u8_args.push_back(StringUtil::WideStringToUTF8String(argv[i]));

	std::vector<char*> u8_argptrs;
	u8_argptrs.reserve(u8_args.size() + 1);
	for (int i = 0; i < argc; i++)
		u8_argptrs.push_back(u8_args[i].data());
	u8_argptrs.push_back(nullptr);

	return Run(argc, u8_argptrs.data());
}

#else

int main(int argc, char* argv[])
{
	return Run(argc, argv);
}

#endif
