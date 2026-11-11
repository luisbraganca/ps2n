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
#include "sync_trace.h"

#include "runtime/ee_provider.h"
#include "runtime/rec_blocks.h"

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
#include "pcsx2/SIO/Pad/PadDualshock2.h"
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
		std::string sync_path; // where the record of sync points is written (sync_trace.h)
		std::string sync_against_path; // an earlier run's record of them, to compare this run with
		std::optional<u64> sync_detail_from; // the sync point from which the record includes the registers
		std::optional<u64> stack_budget; // host stack compiled code may use, when not the default
		std::string image_id; // the one compiled image to use, when not all of them
		bool host_fs = false; // let the guest open files beside its executable ("host:")
		u32 upscale = 1; // internal resolution, for the hardware renderers
		u32 settle_frames = 0; // frames to run on after the marker before stopping
		std::string frame_path; // where the last displayed frame is written, as an image
		u32 frame_every = 0; // when not zero: the frame on display is also fingerprinted every so many frames
		std::string pad_path; // a script of controller input for port 1
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
	// in words, for the report. Before it, the same for the frames looked at
	// along the way (-frame-every). CPU thread only.
	std::string s_frame_line;
	std::vector<std::string> s_frame_lines;
	u32 s_frames_since_marker = 0;

	// Scripted controller input (-pad): what the controller in port 1 is made
	// to report from a given frame on, in frame order, and how far the run has
	// got in it. CPU thread only once the machine runs.
	struct PadEvent
	{
		u32 frame;
		u32 input; // PadDualshock2::Inputs
		float value; // 0 released, 1 fully pressed or pushed
	};
	std::vector<PadEvent> s_pad_events;
	size_t s_pad_next = 0;

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
			"  -sync <file>       With any -ee: write one line for every moment the core lets\n"
			"                     the IOP catch up with the EE (a sync point): where the EE\n"
			"                     is, its cycle count, its registers folded. A stock run can\n"
			"                     write it too, so it is what compares a run with stock.\n"
			"  -sync-against <file>  Compare this run's sync points with the file an\n"
			"                     earlier run wrote with -sync, and report the first that\n"
			"                     differs in place or time and the first in registers.\n"
			"  -sync-detail <n>   With -sync: from sync point n on, write the registers too.\n"
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
			"  -frame-every <n>   Fingerprint the frame on display every n frames as well,\n"
			"                     to see where two runs show different pictures. With\n"
			"                     -frame, each is also written, with its frame number in\n"
			"                     the file name.\n"
			"  -settle <n>        Run this many frames on after the marker line before\n"
			"                     stopping, so that what was drawn last is displayed.\n"
			"  -pad <file>        Controller input for port 1, from a script: one line for\n"
			"                     each press, \"<frame> <input> [<frames held> [<value>]]\".\n"
			"                     Inputs: up down left right triangle circle cross square\n"
			"                     select start l1 l2 r1 r2 l3 r3, and for the sticks l_up\n"
			"                     l_down l_left l_right r_up r_down r_left r_right. Held\n"
			"                     for 2 frames unless said; the value, 0 to 1, is how far a\n"
			"                     stick is pushed. The input changes at frame boundaries, so\n"
			"                     that every run gets it at the same moments.\n"
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
			else if (is("-pad") && has_value)
				o.pad_path = argv[++i];
			else if (is("-image") && has_value)
				o.image_id = argv[++i];
			else if (is("-trace") && has_value)
				o.trace_path = argv[++i];
			else if (is("-trace-against") && has_value)
				o.trace_against_path = argv[++i];
			else if (is("-sync") && has_value)
				o.sync_path = argv[++i];
			else if (is("-sync-against") && has_value)
				o.sync_against_path = argv[++i];
			else if (is("-sync-detail") && has_value)
			{
				o.sync_detail_from = StringUtil::FromChars<u64>(argv[++i]);
				if (!o.sync_detail_from)
				{
					std::fputs("ps2n-host: -sync-detail takes the number of a sync point\n", stderr);
					return false;
				}
			}
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
			else if (is("-frame-every") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -frame-every takes a positive number of frames\n", stderr);
					return false;
				}
				o.frame_every = *n;
			}
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
				 &o.trace_path, &o.trace_against_path, &o.sync_path, &o.sync_against_path, &o.frame_path, &o.pad_path})
			Path::ToNativePath(path);
		return true;
	}

	// Reads a script of controller input (-pad; PrintUsage() has the format).
	bool LoadPadScript(const std::string& path)
	{
		static const struct
		{
			const char* name;
			u32 input;
		} kInputs[] = {
			{"up", PadDualshock2::PAD_UP}, {"right", PadDualshock2::PAD_RIGHT}, {"down", PadDualshock2::PAD_DOWN},
			{"left", PadDualshock2::PAD_LEFT}, {"triangle", PadDualshock2::PAD_TRIANGLE}, {"circle", PadDualshock2::PAD_CIRCLE},
			{"cross", PadDualshock2::PAD_CROSS}, {"square", PadDualshock2::PAD_SQUARE}, {"select", PadDualshock2::PAD_SELECT},
			{"start", PadDualshock2::PAD_START}, {"l1", PadDualshock2::PAD_L1}, {"l2", PadDualshock2::PAD_L2},
			{"r1", PadDualshock2::PAD_R1}, {"r2", PadDualshock2::PAD_R2}, {"l3", PadDualshock2::PAD_L3},
			{"r3", PadDualshock2::PAD_R3}, {"l_up", PadDualshock2::PAD_L_UP}, {"l_right", PadDualshock2::PAD_L_RIGHT},
			{"l_down", PadDualshock2::PAD_L_DOWN}, {"l_left", PadDualshock2::PAD_L_LEFT}, {"r_up", PadDualshock2::PAD_R_UP},
			{"r_right", PadDualshock2::PAD_R_RIGHT}, {"r_down", PadDualshock2::PAD_R_DOWN}, {"r_left", PadDualshock2::PAD_R_LEFT},
		};

		const std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str());
		if (!text)
		{
			std::fprintf(stderr, "ps2n-host: cannot read %s\n", path.c_str());
			return false;
		}

		u32 line_number = 0;
		for (std::string_view line : StringUtil::SplitString(*text, '\n', false))
		{
			line_number++;
			if (const size_t hash = line.find('#'); hash != std::string_view::npos)
				line = line.substr(0, hash);
			std::string columns(line);
			for (char& c : columns)
			{
				if (c == '\t' || c == '\r')
					c = ' ';
			}
			const std::vector<std::string_view> words = StringUtil::SplitString(columns, ' ', true);
			if (words.empty())
				continue;

			const std::optional<u32> frame = StringUtil::FromChars<u32>(words[0]);
			const std::optional<u32> held = words.size() > 2 ? StringUtil::FromChars<u32>(words[2]) : std::optional<u32>(2);
			const std::optional<float> value = words.size() > 3 ? StringUtil::FromChars<float>(words[3]) : std::optional<float>(1.0f);
			std::optional<u32> input;
			if (words.size() > 1)
			{
				for (const auto& known : kInputs)
				{
					if (words[1] == known.name)
						input = known.input;
				}
			}
			if (!frame || !input || !held || *held == 0 || !value || *value < 0.0f || *value > 1.0f || words.size() > 4)
			{
				std::fprintf(stderr, "ps2n-host: %s, line %u: expected \"<frame> <input> [<frames held> [<value from 0 to 1>]]\"\n",
					path.c_str(), line_number);
				return false;
			}

			s_pad_events.push_back({*frame, *input, *value});
			s_pad_events.push_back({*frame + *held, *input, 0.0f});
		}

		// In frame order; at the same frame, as the script has them.
		std::stable_sort(s_pad_events.begin(), s_pad_events.end(), [](const PadEvent& a, const PadEvent& b) { return a.frame < b.frame; });
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
	// `along_the_way`: one of the frames looked at before the run stops
	// (-frame-every), reported and named with its frame number.
	void CaptureFrame(bool along_the_way = false)
	{
		if (s_options.renderer == GSRendererType::Null)
			return;

		const std::string label = along_the_way ? fmt::format("frame {}: ", s_frames) : std::string("frame ");
		std::string& line = along_the_way ? s_frame_lines.emplace_back() : s_frame_line;

		u32 width = 0, height = 0;
		std::vector<u32> pixels;
		if (!MTGS::SaveMemorySnapshot(0, 0, false, false, &width, &height, &pixels) || pixels.empty())
		{
			line = along_the_way ? fmt::format("ps2n-host: {}nothing on display", label) : std::string("ps2n-host: no frame was on display");
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
		line = fmt::format("ps2n-host: {}{}x{}, fingerprint {:016X}", label, width, height, hash);

		if (!s_options.frame_path.empty())
		{
			// Along the way: the file's name with the frame number before its extension.
			std::string path = s_options.frame_path;
			if (along_the_way)
			{
				const std::string_view extension = Path::GetExtension(path);
				const std::string stem(Path::StripExtension(path));
				path = fmt::format("{}-{:06}{}{}", stem, s_frames, extension.empty() ? "" : ".", extension);
			}

			for (u32& pixel : pixels)
				pixel |= 0xFF000000u;
			const RGBA8Image image(width, height, pixels.data());
			if (!image.SaveToFile(path.c_str()))
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", path.c_str());
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
				"ps2n-host: compiled code of %s: %s; %llu dispatcher entries; blocks compared with what was compiled %llu, found to be"
				" code nothing was compiled from %llu; times compiled code was told to run a block %llu, times the interpreter"
				" carried one out at a label instead %llu, times it was a timeout loop's turn %llu; calls passed on to a variant"
				" %llu; frames abandoned: %llu for another executable, %llu for stack depth; blocks finished by the interpreter"
				" %llu\n",
				stats.image->name, s_options.static_code ? "in use" : "switched off", static_cast<ull>(stats.static_entries),
				static_cast<ull>(stats.blocks_checked), static_cast<ull>(stats.blocks_unknown), static_cast<ull>(stats.blocks_compiled),
				static_cast<ull>(stats.blocks_interpreted_at_labels), static_cast<ull>(stats.timeout_loop_entries),
				static_cast<ull>(stats.redirected), static_cast<ull>(stats.forced_unwinds), static_cast<ull>(stats.stack_unwinds),
				static_cast<ull>(stats.left));

			// Where compiled code handed over to the interpreter most often.
			const std::vector<ps2n::ee::Handover> handovers = ps2n::ee::GetHandovers();
			if (!handovers.empty())
			{
				std::string places;
				for (size_t i = 0; i < handovers.size() && i < 8; i++)
				{
					places += fmt::format("{}{:08X} {} times{}", i ? ", " : "", handovers[i].pc, handovers[i].count,
						handovers[i].whole_block ? " (the block at this label)" : " (the rest of a block from here)");
				}
				std::fprintf(stderr, "ps2n-host: compiled code handed over to the interpreter in %zu places, most often: %s\n",
					handovers.size(), places.c_str());
			}
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
		std::fprintf(stderr,
			"ps2n-host: interpreted outside the executable's code: %llu instructions in the kernel's segments (the BIOS), %llu below"
			" 00100000 (the BIOS's program loader), %llu elsewhere in user memory (code the executable does not hold)\n",
			static_cast<ull>(stats.interpreted_kernel), static_cast<ull>(stats.interpreted_loader),
			static_cast<ull>(stats.interpreted_elsewhere));
		{
			const ps2n::ee::blocks::Stats& blocks = ps2n::ee::blocks::GetStats();
			std::fprintf(stderr,
				"ps2n-host: time counted as PCSX2's recompiler counts it: %llu blocks formed (%llu in pages that are not"
				" write-protected); every block thrown away %llu times; %llu requests to clear a range threw away %llu blocks;"
				" blocks found changed at execution %llu, pages put back under write protection %llu, older blocks found"
				" changed when a new one was formed %llu\n",
				static_cast<ull>(blocks.formed), static_cast<ull>(blocks.manual), static_cast<ull>(blocks.resets),
				static_cast<ull>(blocks.clears), static_cast<ull>(blocks.cleared), static_cast<ull>(blocks.discarded),
				static_cast<ull>(blocks.page_resets), static_cast<ull>(blocks.stale));
			std::fprintf(stderr,
				"ps2n-host: FlushCache calls not made %llu; loads and stores that hit memory that is not plain, each once, %llu;"
				" blocks carried out at another address than they were formed at %llu; branches dropped from delay slots %llu;"
				" other things in delay slots that are not reproduced %llu\n",
				static_cast<ull>(stats.syscalls_skipped), static_cast<ull>(blocks.fastmem_faults), static_cast<ull>(stats.other_mirror),
				static_cast<ull>(blocks.delay_slot_branches), static_cast<ull>(blocks.delay_slot_oddities));
		}
		if (stats.timeout_loop_cycles)
		{
			std::fprintf(stderr, "ps2n-host: %llu EE cycles passed in timeout loops that were not run\n",
				static_cast<ull>(stats.timeout_loop_cycles));
		}

		// The system calls made most often, by number.
		{
			std::vector<std::pair<u64, size_t>> calls;
			u64 total = 0;
			for (size_t i = 0; i < std::size(stats.syscalls); i++)
			{
				total += stats.syscalls[i];
				if (stats.syscalls[i])
					calls.emplace_back(stats.syscalls[i], i);
			}
			std::sort(calls.rbegin(), calls.rend());
			std::string line;
			for (size_t i = 0; i < calls.size() && i < 10; i++)
				line += fmt::format("{}number {} {} times", i ? ", " : "", calls[i].second, calls[i].first);
			if (total)
				std::fprintf(stderr, "ps2n-host: %llu system calls: %s\n", static_cast<ull>(total), line.c_str());
		}
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

				// The record of sync points, in any configuration.
				if (!s_options.sync_path.empty() || !s_options.sync_against_path.empty())
				{
					if (ps2n::host::StartSyncTrace(s_options.sync_path.empty() ? nullptr : s_options.sync_path.c_str(),
							s_options.sync_against_path.empty() ? nullptr : s_options.sync_against_path.c_str(),
							s_options.sync_detail_from.value_or(~0ull)))
					{
						ps2n::host::InstallSyncTrace();
					}
					else
					{
						std::fputs("ps2n-host: cannot open the files of the record of sync points\n", stderr);
						trace_ok = false;
					}
				}

				// As fast as the host goes: the core otherwise holds the machine to
				// the console's speed, however little work a frame is.
				VMManager::SetState(VMState::Running);
				VMManager::SetLimiterMode(LimiterModeType::Unlimited);
				while (trace_ok && VMManager::GetState() == VMState::Running)
					VMManager::Execute();

				if (s_options.ee == EEProvider::Ps2n)
					ps2n::ee::StopTrace();
				ps2n::host::StopSyncTrace();

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
				if (!s_options.sync_path.empty() || !s_options.sync_against_path.empty())
				{
					std::fprintf(stderr, "ps2n-host: %llu sync points\n", static_cast<unsigned long long>(ps2n::host::GetSyncPointCount()));
					if (!s_options.sync_against_path.empty())
					{
						const char* time = ps2n::host::GetSyncTimeMismatch();
						const char* state = ps2n::host::GetSyncStateMismatch();
						if (!time && !state)
							std::fputs("ps2n-host: sync: the same as the reference run at every sync point\n", stderr);
						if (state)
							std::fprintf(stderr, "ps2n-host: sync: registers first differ %s\n", state);
						if (time)
							std::fprintf(stderr, "ps2n-host: sync: place or time first differs %s\n", time);
					}
				}
				for (const std::string& line : s_bench_lines)
					std::fprintf(stderr, "%s\n", line.c_str());
				for (const std::string& line : s_frame_lines)
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
		if (!s_options.pad_path.empty() && !LoadPadScript(s_options.pad_path))
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

	// Scripted controller input: what the script has for the frames up to this
	// one. It changes here, between two frames, in every run alike.
	while (s_pad_next < s_pad_events.size() && s_pad_events[s_pad_next].frame <= s_frames)
	{
		const PadEvent& event = s_pad_events[s_pad_next++];
		Pad::SetControllerState(0, event.input, event.value);
	}

	// Never run on with a provider other than the one that was asked for.
	if ((s_options.ee == EEProvider::Ps2n && Cpu != ps2n::ee::Provider()) || !ps2n::host::SyncTraceInstalled())
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
	else if (s_options.frame_every != 0 && s_frames % s_options.frame_every == 0)
	{
		CaptureFrame(true);
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
