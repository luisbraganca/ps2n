// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-host: boots a PS2 executable on the PCSX2 core without any user
// interface, captures what the program prints, and stops at a marker, a frame
// count or a time limit. It is the proof of concept's test bench: the same
// run can be repeated with different CPU providers and the outputs compared.
//
// With -window it is also where a game is played: the same machine in a
// window, at the console's speed, with sound, the keyboard and a controller.
//
// The start-up sequence follows pcsx2-gsrunner/Main.cpp of PCSX2 v2.9.170,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+.

#include "host_state.h"
#include "profile.h"
#include "selftest.h"
#include "session_record.h"
#include "sync_trace.h"
#include "iop_report.h"
#include "vu_report.h"
#include "window.h"

#include "runtime/ee_provider.h"
#include "runtime/iop_provider.h"
#include "runtime/pcsx2_check.h"
#include "runtime/vu_interp.h"

#include "ps2n/iop_recomp.h"
#include "ps2n/vu_recomp.h"
#include "runtime/rec_blocks.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#endif

#include "common/Console.h"
#include "common/CrashHandler.h"
#include "common/FileSystem.h"
#include "common/Image.h"
#include "common/ZipHelpers.h"
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
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/SPU2/defs.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VUmicro.h"

#if defined(PS2N_GAME_MODULE)

#include "runtime/game_module.h"

// The compiled code is in modules of its own beside the host
// (runtime/game_module.h), which wmain() loads before anything else. Their
// lists, put together in the modules' order, each ending with a null entry
// as below.
static std::vector<const ps2n::ee::ImageInfo*> s_module_images;
static std::vector<const ps2n::vu::ImageInfo*> s_module_vu_images;
static std::vector<const ps2n::iop::ImageInfo*> s_module_iop_images;
static const ps2n::ee::ImageInfo* const* ps2n_images = nullptr;
static size_t ps2n_image_count = 0;
static const ps2n::vu::ImageInfo* const* ps2n_vu_images = nullptr;
static size_t ps2n_vu_image_count = 0;
static const ps2n::iop::ImageInfo* const* ps2n_iop_images = nullptr;
static size_t ps2n_iop_image_count = 0;

#else

// The executables compiled ahead of time that this host is linked with: a list
// the recompiler writes (tests/tests.cmake). The array ends with a null entry.
extern const ps2n::ee::ImageInfo* const ps2n_images[];
extern const size_t ps2n_image_count;

// The blocks of micro code compiled ahead of time that this host is linked
// with, listed the same way.
extern const ps2n::vu::ImageInfo* const ps2n_vu_images[];
extern const size_t ps2n_vu_image_count;

// The blocks of IOP code compiled ahead of time, likewise.
extern const ps2n::iop::ImageInfo* const ps2n_iop_images[];
extern const size_t ps2n_iop_image_count;

#endif

namespace
{
	// Guest calls become host calls in compiled code, so the CPU thread gets a
	// large stack. Compiled code may use all of it but the reserve, which is
	// for the core underneath and for what compiled code calls.
	constexpr u32 kCpuThreadStackSize = 64 * 1024 * 1024;
	constexpr u32 kCpuThreadStackReserve = 16 * 1024 * 1024;

	enum ExitCode : int
	{
		EXIT_MARKER = 0, // the guest printed the stop marker, or the window was closed
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

	// What PS2Native's IOP provider did, and what it had to interpret, written
	// for the recompiler where the run asked for that.
	void ReportIop();

	// How the blocks of micro code were carried out on PS2Native's VU provider:
	// compiled ahead of time, or interpreted (`which`: bit 0 VU0, bit 1 VU1).
	void ReportVuPlans(u32 which)
	{
		for (int vu = 0; vu < 2; vu++)
		{
			if (!(which & (1u << vu)))
				continue;
			const ps2n::vu::PlanStats st = ps2n::vu::Plans(vu);
			if (!st.plans)
				continue;
			std::fprintf(stderr,
				"ps2n-host: VU%d's blocks of micro code: %llu different ones as they are carried out (%llu steps); %llu of them compiled "
				"ahead of time (this host has %zu), which ran %llu times; %llu interpreted, which ran %llu times\n",
				vu, static_cast<unsigned long long>(st.plans), static_cast<unsigned long long>(st.steps),
				static_cast<unsigned long long>(st.compiled), ps2n::vu::CompiledBlockCount(vu),
				static_cast<unsigned long long>(st.compiledRuns), static_cast<unsigned long long>(st.interpreted),
				static_cast<unsigned long long>(st.interpretedRuns));
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
		bool clock_fixed = true; // the console's clock starts at a fixed time, not at the host's time of day
		std::optional<ps2n::host::session::Clock> clock_at; // -clock <date and time>, or a replay's
		std::string record_dir; // with -window: where the session is written down to be run again
		std::string sessions_dir; // with -window: each session written down in a folder of its own in this one (-sessions)
		bool make_session_folder = false; // record_dir is that folder, to be made when the machine can be set up
		std::string window_title; // with -window: the window's title, whatever the game is (-title)
		std::string input_path; // a session written down, to run again (-input)
		std::string memcards_dir; // without -window: the memory cards to put in the slots (Mcd001.ps2, Mcd002.ps2)
		bool patch_widescreen = false; // apply the game's widescreen patch (-patches; the default with -window)
		bool patch_fps = false; // and its frame-rate patch (the same)
		std::string patch_archive; // the PCSX2 archive of patches the game's patch file is taken from (-patch-archive)
		int ee_cycle_rate = 0; // PCSX2's EE cycle rate setting (-ee-cycle-rate): 0 its default, 1 to 3 an overclock
		bool ee_cycle_rate_given = false; // then a patch's note does not change it
		bool clock_given = false;
		bool iop_jit = true;
		bool iop_ps2n = false; // PS2Native's IOP provider in the place of the core's
		bool iop_static = true; // whether it runs the blocks compiled ahead of time that the host has
		std::string iop_capture_path; // where the blocks it had to interpret are written, for the recompiler
		bool vu_jit = true;
		u32 vu_ps2n = 0; // bit 0 VU0, bit 1 VU1: PS2Native's VU provider in the place of the core's
		std::string vu_programs_path; // where that provider writes a line for each program it records
		bool vu_static = true; // whether it runs the blocks compiled ahead of time that the host has
		std::string vu_capture_path; // where the plans of the blocks it had to interpret are written, for the recompiler
		bool vu_require_compiled = false; // the self-test fails if a block of micro code had to be interpreted
		bool iop_require_compiled = false; // and if a block of IOP code had to be
		GSRendererType renderer = GSRendererType::Null;
		u32 max_frames = 3600;
		u32 max_seconds = 60;
		bool echo = false; // copy the core's log to this process's console
		bool selftest = false; // run the self-test instead of a program
		bool static_code = true; // with -ee ps2n: run code compiled ahead of time where there is some
		bool bios_static = true; // and of that, the BIOS's, when the host has it for the machine's ROM
		bool call_sites = true; // compiled code makes a call through a register itself where dispatch() found its target before
		std::string capture_path; // where the block starts that had to be interpreted are written
		std::string bios_capture_path; // where the block starts formed in the BIOS are written
		std::string trace_path; // where the record of event tests is written
		std::string trace_against_path; // an earlier run's record, to compare this run with
		std::optional<u64> trace_detail_from; // the event test from which the record includes the registers
		std::string sync_path; // where the record of sync points is written (sync_trace.h)
		std::string sync_against_path; // an earlier run's record of them, to compare this run with
		std::optional<u64> sync_detail_from; // the sync point from which the record includes the registers
		std::optional<u64> stack_budget; // host stack compiled code may use, when not the default
		std::string image_id; // the one compiled image to use, when not all of them
		bool host_fs = false; // let the guest open files beside its executable ("host:")
		u32 upscale = 1; // internal resolution, for the hardware renderers (3 with -window unless given)
		u32 window_width = 1920; // with -window: the area the picture goes into, at a display scaling of 100%;
		u32 window_height = 1080; // a window that would not fit on its screen covers the screen instead
		u32 settle_frames = 0; // frames to run on after the marker before stopping
		std::string frame_path; // where the last displayed frame is written, as an image
		u32 frame_every = 0; // when not zero: the frame on display is also fingerprinted every so many frames
		std::string pad_path; // a script of controller input for port 1
		std::string profile_path; // where the report of where the CPU thread's time went is written
		std::string code_size_path; // where the report of how much host code the compiled images became is written
		u32 profile_from = 1; // the frame from which the profile looks (startup left out when later than 1)
		std::string vu_report_path; // where the report of what the vector units ran is written (vu_report.h)
		std::string iop_report_path; // where the report of what the IOP ran is written (iop_report.h)
		std::vector<u32> marks; // frames at which the wall-clock time is noted, for the report
		bool fast_cores = false; // keep the run on the processor's fastest cores, so that its time repeats
		bool window = false; // play in a window: the console's speed, sound, the keyboard, a controller, a memory card
		bool mute = false; // with -window: no sound
		bool osd = false; // with -window: let the core draw its messages and figures over the picture
		ps2n::selftest::Options selftest_options;
	};

	// The window's title before the core knows the game, and after its name
	// once it does (docs/adr/0012-ps2native-branding-not-pcsx2s.md).
	constexpr const char* kWindowTitle = "PS2Native";


	Options s_options;
	ps2n::host::session::Clock s_clock_used{}; // what the console's clock was set to when the machine started
	std::string s_patch_line; // what was done about the game's patches, for the report
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
	std::atomic<bool> s_close_requested{false}; // the window's close button
	std::atomic<u32> s_shot_requested{0}; // a check asked for the picture on display, by this number (0: none)
	std::atomic<bool> s_cpu_thread_done{false};
	std::atomic<int> s_exit_code{EXIT_ERROR};

	// CPU thread only.
	std::thread::id s_cpu_thread_id;
	u32 s_frames = 0;
	bool s_stop_requested = false;
	bool s_provider_replaced = false;
	bool s_vu_unsupported = false; // PS2Native's VU interpreter met what it does not carry out yet
	bool s_iop_unsupported = false; // PS2Native's IOP provider met what it does not carry out

	std::mutex s_queue_mutex;
	std::deque<std::function<void()>> s_queue;

	// The frame on display when the run was stopped: its size and fingerprint,
	// in words, for the report. Before it, the same for the frames looked at
	// along the way (-frame-every). CPU thread only.
	std::string s_frame_line;
	std::vector<std::string> s_frame_lines;
	u32 s_frames_since_marker = 0;

	// The same for the sound: a fingerprint of the sound hardware when the run
	// was stopped and, before it, at the frames looked at along the way.
	std::string s_sound_line;
	std::vector<std::string> s_sound_lines;

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

	// With -window: whether the first controller has been given the pad in
	// port 1, and when the first and the latest frame were shown, to report the
	// pace the session kept. CPU thread only.
	bool s_controller_mapped = false;
	std::chrono::steady_clock::time_point s_first_frame_time;
	std::chrono::steady_clock::time_point s_latest_frame_time;

	// When the host started, when the profile began looking (-profile-from),
	// and the wall-clock time at the frames asked for with -mark, in seconds
	// since the host started. CPU thread only, after the start.
	const std::chrono::steady_clock::time_point s_host_start = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point s_profile_start_time;
	std::vector<std::pair<u32, double>> s_frame_marks;

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
			"  -clock fixed|host|<date and time>  What the console's clock is set to\n"
			"                     when the machine starts: one fixed time (2020-03-04,\n"
			"                     midnight), so that two runs are the same machine; this\n"
			"                     computer's time of day; or a time given as\n"
			"                     2026-11-14T23:45:07. Default fixed; host with -window;\n"
			"                     with -input, the session's.\n"
			"  -record <dir>      With -window: write the session down in <dir>/input.txt,\n"
			"                     to be run again (host/session_record.h): the controller\n"
			"                     in port 1 frame by frame, the clock it started at, its\n"
			"                     patches and the EE cycle rate it was given, and a\n"
			"                     fingerprint of the registers every 50 frames. The memory\n"
			"                     cards and the NVRAM it began with go beside it\n"
			"                     (game.ps1 -Play copies them there; -sessions does it).\n"
			"  -sessions <dir>    With -window: write each session down in a folder of its\n"
			"                     own in <dir>, named after the time it began (-record\n"
			"                     there), with the memory cards and the NVRAM it began with\n"
			"                     beside it, and what it had to interpret (-capture,\n"
			"                     -vu-capture, -iop-capture there, for the providers that\n"
			"                     are PS2Native's): what a game's folder does.\n"
			"  -title <text>      With -window: the window's title, whatever the game is.\n"
			"  -settings <file>   Read options from <file>, one to a line (a line beginning\n"
			"                     with # is left out), in the place of this one; %GAME% in\n"
			"                     them is the folder <file> is in. Started with no options\n"
			"                     at all, the host reads settings.txt beside it, where there\n"
			"                     is one: a game's folder (docs/app-log.md, P4.4).\n"
			"  -input <file>      Run a session written down with -record again, without a\n"
			"                     window: its controller, its clock, its number of frames,\n"
			"                     its patches and its EE cycle rate, unless -clock,\n"
			"                     -frames, -patches or -ee-cycle-rate say otherwise.\n"
			"                     Reports whether the run is the session at every\n"
			"                     fingerprint.\n"
			"  -patches none|widescreen,fps  Apply the game's widescreen patch and its\n"
			"                     frame-rate patch (the group of its patch file whose name\n"
			"                     says FPS; none where several do), where PCSX2's archive\n"
			"                     has them (-patch-archive), with the EE overclock a\n"
			"                     group applied asks for in its note (130% where it names\n"
			"                     no figure), unless -ee-cycle-rate is given. Default\n"
			"                     none; both with -window.\n"
			"  -patch-archive <zip>  PCSX2's archive of patches (patches.zip of an installed\n"
			"                     PCSX2), from which the game's own patch file is taken\n"
			"                     into the run's data folder when the game has booted.\n"
			"  -ee-cycle-rate <n> PCSX2's EE cycle rate setting, -3 to 3: 0 its default, 1,\n"
			"                     2 and 3 its overclock of 130%, 180% and 300%, -1 to -3\n"
			"                     75%, 60% and 50%; given, a patch does not change it.\n"
			"  -memcards <dir>    Without -window: put Mcd001.ps2 and Mcd002.ps2 from <dir>\n"
			"                     in the two slots, and where <dir> lacks one, a new card,\n"
			"                     as a window makes. The run writes to them: give it copies.\n"
			"  -iop-ps2n          Run the IOP's code on PS2Native's IOP provider, which\n"
			"                     follows PCSX2's IOP recompiler (runtime/iop_provider.h):\n"
			"                     by the blocks compiled ahead of time that this host has,\n"
			"                     and by its interpreter elsewhere. The run stops with an\n"
			"                     error at what it does not carry out.\n"
			"  -iop-static on|off With -iop-ps2n: run the blocks of IOP code compiled ahead\n"
			"                     of time that this host has (default on). Off, PS2Native's\n"
			"                     IOP interpreter carries every block out.\n"
			"  -iop-capture <file>  With -iop-ps2n: write what the recompiler needs to\n"
			"                     compile the blocks of IOP code that had to be interpreted\n"
			"                     (ps2n-recomp iop).\n"
			"  -vu jit|interp     PCSX2's VU recompilers or interpreters (default jit).\n"
			"  -vu-ps2n 0|1|both  Run VU0's, VU1's or both VUs' micro programs on\n"
			"                     PS2Native's VU provider, which follows PCSX2's VU\n"
			"                     recompiler (runtime/vu_interp.h): by the blocks compiled\n"
			"                     ahead of time that this host has, and by its interpreter\n"
			"                     elsewhere. The run stops with an error at what it does\n"
			"                     not carry out yet.\n"
			"  -vu-programs <file>  With -vu-ps2n: write a line for each micro program it\n"
			"                     records, as PCSX2's recompiler writes them to its log.\n"
			"  -vu-static on|off  With -vu-ps2n: run the blocks of micro code compiled ahead\n"
			"                     of time that this host has (default on). Off, PS2Native's\n"
			"                     VU interpreter carries every block out.\n"
			"  -vu-capture <file>  With -vu-ps2n (or -selftest): write what the recompiler\n"
			"                     needs to compile the blocks of micro code that had to be\n"
			"                     interpreted (ps2n-recomp vu).\n"
			"  -static on|off     With -ee ps2n: run code compiled ahead of time where there\n"
			"                     is some for the executable (default on). Off, PS2Native's\n"
			"                     interpreter runs everything.\n"
			"  -capture <file>    With -ee ps2n: write what the recompiler needs to compile\n"
			"                     what had to be interpreted: the addresses inside the\n"
			"                     executable at which a block was interpreted, and the code\n"
			"                     found in memory where a function had been compiled from\n"
			"                     other code.\n"
			"  -bios-capture <file>  With -ee ps2n: write the addresses in the BIOS at which\n"
			"                     the interpreter formed a block, in the form of a capture\n"
			"                     file's entries.\n"
			"  -bios-static on|off  With -ee ps2n and -static on: run the BIOS's code\n"
			"                     compiled ahead of time, when this host has it for the\n"
			"                     machine's ROM (default on). Off, the interpreter runs the\n"
			"                     BIOS.\n"
			"  -call-sites on|off  With -ee ps2n: compiled code makes a call through a\n"
			"                     register itself where the runtime found its target\n"
			"                     before (default on). Off, every such call goes through\n"
			"                     the runtime's dispatcher: to time the two in one build.\n"
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
			"  -window            Play: the machine in a window, at the console's speed, with\n"
			"                     sound, a memory card in the data directory, and the pad in\n"
			"                     port 1 on the keyboard and on the first controller, all as\n"
			"                     PCSX2 sets them up by default. Keys: arrows (d-pad), W A S D\n"
			"                     (left stick), T F G H (right stick), I J K L (triangle,\n"
			"                     square, cross, circle), Q E (L1 R1), 1 3 (L2 R2), 2 4 (L3\n"
			"                     R3), Return (start), Backspace (select). Alt+Enter or F11\n"
			"                     covers the screen. The picture is drawn on Vulkan at three\n"
			"                     times the console's resolution, into an area of 1920 by\n"
			"                     1080, which covers the screen where a window that size\n"
			"                     would not fit (-renderer, -upscale, -window-size). The\n"
			"                     run ends when the window is closed; -frames and -seconds\n"
			"                     only apply when given. Windows only.\n"
			"  -window-size <w>x<h>  With -window: the area the picture goes into, at a\n"
			"                     display scaling of 100% (default 1920x1080).\n"
			"  -mute              With -window: no sound.\n"
			"  -osd               With -window: let the core draw its messages and performance\n"
			"                     figures over the picture, as PCSX2 shows them. Off unless\n"
			"                     given: a game shows none of it.\n"
			"  -renderer <name>   GS renderer: null (nothing is drawn; the default without a\n"
			"                     window), sw (PCSX2's software renderer), vulkan, opengl,\n"
			"                     dx11, dx12, auto (the one PCSX2 chooses for this computer).\n"
			"                     With -window the default is vulkan. Without a window\n"
			"                     nothing is shown; the last displayed frame is\n"
			"                     fingerprinted.\n"
			"  -upscale <n>       Internal resolution multiplier for a hardware renderer\n"
			"                     (default 1, and 3 with -window: about 1080p).\n"
			"  -frame <file>      Write the last displayed frame to this image file (.png).\n"
			"  -frame-every <n>   Fingerprint the frame on display every n frames as well,\n"
			"                     to see where two runs show different pictures. With\n"
			"                     -frame, each is also written, with its frame number in\n"
			"                     the file name. The sound hardware (its registers and its\n"
			"                     memory, which holds what it last mixed) is fingerprinted\n"
			"                     at the same frames, and always at the last one.\n"
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
			"  -profile <file>    Look where the CPU thread is executing about a thousand\n"
			"                     times a second, from the first frame to the last, and\n"
			"                     write to this file how much of the time went to the game's\n"
			"                     compiled code, to PS2Native's runtime, to PCSX2's core, to\n"
			"                     the code PCSX2's recompilers generated and to the system,\n"
			"                     with the functions that had most of it. Windows only.\n"
			"  -profile-from <n>  With -profile: look from frame n on, not from the first,\n"
			"                     to leave out what comes before (a game's startup).\n"
			"  -vu-report <file>  Watch the core's two VU providers and write to this file\n"
			"                     what was written into each VU's micro memory, where, with\n"
			"                     how many different contents, at which addresses programs\n"
			"                     were started, and how long each ran. Beside it go the same\n"
			"                     counts frame by frame (-frames.csv) and each distinct\n"
			"                     content written (-uploads.bin), which are the program's.\n"
			"  -iop-report <file> Watch the core's IOP provider and write to this file how\n"
			"                     much IOP time passed, how often the IOP's memory was\n"
			"                     written, which modules its kernel listed where and when,\n"
			"                     and how many blocks PCSX2's IOP recompiler formed in each.\n"
			"                     Beside it go the counts frame by frame (-frames.csv).\n"
			"  -mark <n>          Note the wall-clock time at frame n, and report it after\n"
			"                     the run with the time of the first and the last frame,\n"
			"                     each since the host started. May be given more than once:\n"
			"                     to time a game's startup and its play apart.\n"
			"  -fast-cores        Keep the run on the processor's fastest cores. On a\n"
			"                     processor with fast and slow cores the system otherwise\n"
			"                     moves a run between them, and the same run takes up to\n"
			"                     twice as long from one time to the next. For runs that\n"
			"                     are timed. Windows only; elsewhere it changes nothing.\n"
			"  -until <text>      Stop once the program prints a line containing this\n"
			"                     (default \"-- TEST END\").\n"
			"  -frames <n>        Stop after this many frames (default 3600).\n"
			"  -seconds <n>       Stop after this much wall-clock time (default 60).\n"
			"  -echo              Copy the core's log to the console.\n"
			"  -selftest          Instead of booting a file, check PS2Native's instruction\n"
			"                     tables and semantics against the PCSX2 core.\n"
			"  -selftest-vu <n>   Instead of booting a file, run the self-test's random\n"
			"                     micro programs alone, the first n for each VU and\n"
			"                     setting, on PCSX2's VU recompiler and on PS2Native's\n"
			"                     VU provider. With -vu-capture: for the build that\n"
			"                     compiles them (scripts/windows/vu-selftest.ps1).\n"
			"  -vu-require-compiled  With -selftest-vu: fail if a block of those programs\n"
			"                     had to be interpreted; skipped (PS2N_SKIP) by a host\n"
			"                     that was not built with them.\n"
			"  -selftest-iop <n>  Instead of booting a file, run the self-test's random\n"
			"                     IOP programs alone, n of them, on PCSX2's IOP recompiler\n"
			"                     and on PS2Native's IOP provider. With -iop-capture: for\n"
			"                     the build that compiles their blocks\n"
			"                     (scripts/windows/iop-selftest.ps1).\n"
			"  -iop-require-compiled  With -selftest-iop: fail if a block of those\n"
			"                     programs had to be interpreted; skipped (PS2N_SKIP) by a\n"
			"                     host that was not built with them.\n"
			"  -selftest-trials <n>  Random cases per instruction against PCSX2's\n"
			"                     recompiler (default 400); ten times as many against\n"
			"                     its interpreter.\n"
			"  -code-size <file>  Instead of booting a file, write to this file how much\n"
			"                     host code each compiled image this host holds became,\n"
			"                     by the kind of generated line, from the host's own\n"
			"                     debugging information. Needs nothing else. Windows only.\n"
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

	// The game's patches as -patches takes them and a session writes them down:
	// none, or widescreen and fps, separated by a comma.
	bool ParsePatches(std::string_view value, bool* widescreen, bool* fps)
	{
		*widescreen = false;
		*fps = false;
		if (value == "none")
			return true;
		for (const std::string_view kind : StringUtil::SplitString(value, ',', true))
		{
			if (kind == "widescreen")
				*widescreen = true;
			else if (kind == "fps")
				*fps = true;
			else
				return false;
		}
		return true;
	}

	std::string PatchesText(bool widescreen, bool fps)
	{
		if (widescreen && fps)
			return "widescreen,fps";
		return widescreen ? "widescreen" : fps ? "fps" : "none";
	}

	bool ParseCommandLine(int argc, char* argv[])
	{
		Options& o = s_options;
		bool renderer_given = false, frames_given = false, seconds_given = false, upscale_given = false, patches_given = false;
		for (int i = 1; i < argc; i++)
		{
			const char* arg = argv[i];
			const bool has_value = (i + 1) < argc;
			const auto is = [arg](const char* name) { return !std::strcmp(arg, name); };

			if (is("-help") || is("--help") || is("-h"))
				return false;
			else if (is("-echo"))
				o.echo = true;
			else if (is("-window"))
				o.window = true;
			else if (is("-mute"))
				o.mute = true;
			else if (is("-osd"))
				o.osd = true;
			else if (is("-hostfs"))
				o.host_fs = true;
			else if (is("-selftest"))
				o.selftest = true;
			else if (is("-capture") && has_value)
				o.capture_path = argv[++i];
			else if (is("-bios-capture") && has_value)
				o.bios_capture_path = argv[++i];
			else if (is("-times") && has_value)
				o.times_path = argv[++i];
			else if (is("-pad") && has_value)
				o.pad_path = argv[++i];
			else if (is("-record") && has_value)
				o.record_dir = argv[++i];
			else if (is("-sessions") && has_value)
				o.sessions_dir = argv[++i];
			else if (is("-title") && has_value)
				o.window_title = argv[++i];
			else if (is("-input") && has_value)
				o.input_path = argv[++i];
			else if (is("-memcards") && has_value)
				o.memcards_dir = argv[++i];
			else if (is("-patches") && has_value)
			{
				if (!ParsePatches(argv[++i], &o.patch_widescreen, &o.patch_fps))
				{
					std::fprintf(stderr, "ps2n-host: -patches takes none, or widescreen and fps, not '%s'\n", argv[i]);
					return false;
				}
				patches_given = true;
			}
			else if (is("-patch-archive") && has_value)
				o.patch_archive = argv[++i];
			else if (is("-ee-cycle-rate") && has_value)
			{
				const std::optional<s32> rate = StringUtil::FromChars<s32>(argv[++i]);
				if (!rate || *rate < -3 || *rate > 3)
				{
					std::fprintf(stderr, "ps2n-host: -ee-cycle-rate takes -3 to 3, not '%s'\n", argv[i]);
					return false;
				}
				o.ee_cycle_rate = *rate;
				o.ee_cycle_rate_given = true;
			}
			else if (is("-profile") && has_value)
				o.profile_path = argv[++i];
			else if (is("-code-size") && has_value)
				o.code_size_path = argv[++i];
			else if (is("-vu-report") && has_value)
				o.vu_report_path = argv[++i];
			else if (is("-iop-report") && has_value)
				o.iop_report_path = argv[++i];
			else if ((is("-profile-from") || is("-mark")) && has_value)
			{
				const bool mark = is("-mark");
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fprintf(stderr, "ps2n-host: %s takes a positive frame number\n", mark ? "-mark" : "-profile-from");
					return false;
				}
				if (mark)
					o.marks.push_back(*n);
				else
					o.profile_from = *n;
			}
			else if (is("-fast-cores"))
				o.fast_cores = true;
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
			else if (is("-bios-static") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "on"))
					o.bios_static = true;
				else if (!std::strcmp(value, "off"))
					o.bios_static = false;
				else
				{
					std::fprintf(stderr, "ps2n-host: -bios-static takes on or off, not '%s'\n", value);
					return false;
				}
			}
			else if (is("-call-sites") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "on"))
					o.call_sites = true;
				else if (!std::strcmp(value, "off"))
					o.call_sites = false;
				else
				{
					std::fprintf(stderr, "ps2n-host: -call-sites takes on or off, not '%s'\n", value);
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
			else if (is("-selftest-vu") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -selftest-vu takes a positive number\n", stderr);
					return false;
				}
				o.selftest = true;
				o.selftest_options.vu_only = true;
				o.selftest_options.vu_run = *n;
			}
			else if (is("-vu-require-compiled"))
				o.vu_require_compiled = true;
			else if (is("-selftest-iop") && has_value)
			{
				const std::optional<u32> n = StringUtil::FromChars<u32>(argv[++i]);
				if (!n || *n == 0)
				{
					std::fputs("ps2n-host: -selftest-iop takes a positive number\n", stderr);
					return false;
				}
				o.selftest = true;
				o.selftest_options.iop_only = true;
				o.selftest_options.iop_trials = *n;
			}
			else if (is("-iop-require-compiled"))
				o.iop_require_compiled = true;
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
			else if (is("-vu-ps2n") && has_value)
			{
				const char* which = argv[++i];
				if (!std::strcmp(which, "0"))
					o.vu_ps2n = 1;
				else if (!std::strcmp(which, "1"))
					o.vu_ps2n = 2;
				else if (!std::strcmp(which, "both"))
					o.vu_ps2n = 3;
				else
				{
					std::fprintf(stderr, "ps2n-host: -vu-ps2n takes 0, 1 or both, not %s\n", which);
					return false;
				}
			}
			else if (is("-clock") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "fixed"))
					o.clock_fixed = true;
				else if (!std::strcmp(value, "host"))
					o.clock_fixed = false;
				else if (const std::optional<ps2n::host::session::Clock> at = ps2n::host::session::ParseClock(value))
					o.clock_at = at;
				else
				{
					std::fprintf(stderr, "ps2n-host: -clock takes fixed, host or a date and time (2026-11-14T23:45:07), not '%s'\n", value);
					return false;
				}
				o.clock_given = true;
			}
			else if (is("-iop-ps2n"))
				o.iop_ps2n = true;
			else if (is("-iop-capture") && has_value)
				o.iop_capture_path = argv[++i];
			else if (is("-iop-static") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "on"))
					o.iop_static = true;
				else if (!std::strcmp(value, "off"))
					o.iop_static = false;
				else
				{
					std::fprintf(stderr, "ps2n-host: -iop-static takes on or off, not '%s'\n", value);
					return false;
				}
			}
			else if (is("-vu-programs") && has_value)
				o.vu_programs_path = argv[++i];
			else if (is("-vu-capture") && has_value)
				o.vu_capture_path = argv[++i];
			else if (is("-vu-static") && has_value)
			{
				const char* value = argv[++i];
				if (!std::strcmp(value, "on"))
					o.vu_static = true;
				else if (!std::strcmp(value, "off"))
					o.vu_static = false;
				else
				{
					std::fprintf(stderr, "ps2n-host: -vu-static takes on or off, not '%s'\n", value);
					return false;
				}
			}
			else if (is("-renderer") && has_value)
			{
				const char* value = argv[++i];
				renderer_given = true;
				if (!std::strcmp(value, "null"))
					o.renderer = GSRendererType::Null;
				else if (!std::strcmp(value, "auto"))
					o.renderer = GSRendererType::Auto;
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
				upscale_given = true;
			}
			else if (is("-window-size") && has_value)
			{
				const std::string_view value = argv[++i];
				const size_t x = value.find('x');
				const std::optional<u32> w = x == std::string_view::npos ? std::nullopt : StringUtil::FromChars<u32>(value.substr(0, x));
				const std::optional<u32> h = x == std::string_view::npos ? std::nullopt : StringUtil::FromChars<u32>(value.substr(x + 1));
				if (!w || !h || *w < 64 || *h < 64 || *w > 16384 || *h > 16384)
				{
					std::fprintf(stderr, "ps2n-host: -window-size takes a width and a height, as 1920x1080, not '%s'\n", argv[i]);
					return false;
				}
				o.window_width = *w;
				o.window_height = *h;
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
				frames_given = true;
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
				seconds_given = true;
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

		// A report on the host itself, which runs no machine.
		if (!o.code_size_path.empty())
		{
			if (!o.boot_path.empty() || o.selftest || o.window || !o.profile_path.empty() || !ps2n::host::profile::Available())
			{
				std::fputs("ps2n-host: -code-size takes nothing else, and is not available on this platform yet\n", stderr);
				return false;
			}
			return true;
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
		if (o.window)
		{
			if (o.selftest)
			{
				std::fputs("ps2n-host: -selftest has nothing to show in a window\n", stderr);
				return false;
			}
			if (!ps2n::host::window::Available())
			{
				std::fputs("ps2n-host: -window is not available on this platform yet\n", stderr);
				return false;
			}

			// Someone is playing: the run ends when they close the window, and
			// the picture is drawn on Vulkan at three times the console's
			// resolution, about 1080p (ADR 0025). (0 frames or seconds: no limit.)
			if (!frames_given)
				o.max_frames = 0;
			if (!seconds_given)
				o.max_seconds = 0;
			if (!renderer_given)
				o.renderer = GSRendererType::VK;
			if (!upscale_given)
				o.upscale = 3;
			// And with the game's widescreen and frame-rate patches, where PCSX2
			// has them.
			if (!patches_given)
			{
				o.patch_widescreen = true;
				o.patch_fps = true;
			}
			// And the console has the day it is.
			if (!o.clock_given)
				o.clock_fixed = false;
		}
		else if (o.mute || o.osd || !o.record_dir.empty() || !o.sessions_dir.empty() || !o.window_title.empty())
		{
			std::fputs("ps2n-host: -mute, -osd, -record, -sessions and -title are for -window\n", stderr);
			return false;
		}
		if (o.window && (!o.input_path.empty() || !o.memcards_dir.empty()))
		{
			std::fputs("ps2n-host: -input and -memcards are for a run without a window\n", stderr);
			return false;
		}
		if (!o.profile_path.empty() && (o.selftest || !ps2n::host::profile::Available()))
		{
			std::fputs("ps2n-host: -profile is for a run of a program, and is not available on this platform yet\n", stderr);
			return false;
		}
		if ((!o.vu_report_path.empty() || !o.iop_report_path.empty()) && o.selftest)
		{
			std::fputs("ps2n-host: -vu-report and -iop-report are for a run of a program\n", stderr);
			return false;
		}

		// On Windows the core's file functions turn an absolute path into an
		// extended-length one (\\?\C:\...), which does not take forward slashes
		// (FileSystem::GetWin32Path in common/FileSystem.cpp). Build tools pass
		// forward slashes, so every path is put in the platform's form here.
		for (std::string* path : {&o.boot_path, &o.bios_path, &o.data_dir, &o.out_path, &o.times_path, &o.log_path, &o.capture_path,
				 &o.bios_capture_path, &o.trace_path, &o.trace_against_path, &o.sync_path, &o.sync_against_path, &o.frame_path, &o.pad_path,
				 &o.profile_path, &o.vu_report_path, &o.iop_report_path, &o.vu_programs_path, &o.vu_capture_path, &o.iop_capture_path,
				 &o.record_dir, &o.sessions_dir, &o.input_path, &o.memcards_dir, &o.patch_archive})
			Path::ToNativePath(path);

		if (!o.input_path.empty())
		{
			// A session run again: its clock, as many frames as it had, its
			// patches and the EE cycle rate it was given, unless told
			// otherwise. (The overclock a patch's note asks for is not written
			// down: the note asks for it again, at the same moment.)
			ps2n::host::session::ReplayInfo info;
			if (!ps2n::host::session::LoadReplay(o.input_path, &info))
				return false;
			if (!o.clock_given && info.clock)
				o.clock_at = info.clock;
			if (!frames_given && info.frames)
				o.max_frames = *info.frames;
			if (!patches_given && info.patches && !ParsePatches(*info.patches, &o.patch_widescreen, &o.patch_fps))
			{
				std::fprintf(stderr, "ps2n-host: %s: patches '%s' are not ones -patches takes\n", o.input_path.c_str(),
					info.patches->c_str());
				return false;
			}
			if (!o.ee_cycle_rate_given && info.ee_cycle_rate)
			{
				if (*info.ee_cycle_rate < -3 || *info.ee_cycle_rate > 3)
				{
					std::fprintf(stderr, "ps2n-host: %s: ee-cycle-rate %d is not -3 to 3\n", o.input_path.c_str(), *info.ee_cycle_rate);
					return false;
				}
				o.ee_cycle_rate = *info.ee_cycle_rate;
				o.ee_cycle_rate_given = true;
			}
			// The images beside the BIOS make the machine another one: a session
			// run again with others would part from it at once.
			if (info.bios_roms)
			{
				const std::string here = ps2n::host::session::BiosRoms(o.bios_path);
				if (here != *info.bios_roms)
				{
					std::fprintf(stderr,
						"ps2n-host: %s was played with the BIOS's images %s beside it; this BIOS has %s (give it a BIOS image "
						"with the same beside it)\n",
						o.input_path.c_str(), info.bios_roms->c_str(), here.c_str());
					return false;
				}
			}
		}

		// An archive named that is not there (a game's folder made where PCSX2
		// was installed, played where it no longer is) is none.
		if (!o.patch_archive.empty() && !FileSystem::FileExists(o.patch_archive.c_str()))
		{
			std::fprintf(stderr, "ps2n-host: there is no archive of PCSX2's patches at %s\n", o.patch_archive.c_str());
			o.patch_archive.clear();
		}

		// The game's patches come from PCSX2's archive of them (-patch-archive;
		// PS2Native ships none, ADR 0028). Without one there is nothing to apply.
		// A window, which asks for them by default, then plays without them, and
		// writes the session down as it is played: with none. A run told to
		// apply them, or a session played with them run again, could not be
		// what it was asked to be.
		if ((o.patch_widescreen || o.patch_fps) && o.patch_archive.empty())
		{
			if (patches_given || !o.input_path.empty())
			{
				std::fputs("ps2n-host: the game's patches need PCSX2's archive of them (-patch-archive)\n", stderr);
				return false;
			}
			o.patch_widescreen = false;
			o.patch_fps = false;
			std::fputs("ps2n-host: no archive of PCSX2's patches (-patch-archive): the game is played without them\n", stderr);
		}

		// A session in a folder of its own (-sessions): what game.ps1 -Play does
		// around the host, done by the host, which in a game's folder nothing
		// else starts. The folder is named after the time the session began, and
		// what the session has to interpret is written there too; it is made,
		// with the memory cards and the console's NVRAM, once the machine can be
		// set up (MakeSessionFolder).
		if (!o.sessions_dir.empty() && o.record_dir.empty())
		{
			const std::time_t now = std::time(nullptr);
			std::tm local = {};
#ifdef _WIN32
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			char name[32];
			std::strftime(name, sizeof(name), "%Y%m%d-%H%M%S", &local);
			o.record_dir = Path::Combine(o.sessions_dir, name);
			o.make_session_folder = true;
			if (o.capture_path.empty() && o.ee == EEProvider::Ps2n)
				o.capture_path = Path::Combine(o.record_dir, "capture.toml");
			if (o.vu_capture_path.empty() && o.vu_ps2n != 0)
				o.vu_capture_path = Path::Combine(o.record_dir, "vu-capture.toml");
			if (o.iop_capture_path.empty() && o.iop_ps2n)
				o.iop_capture_path = Path::Combine(o.record_dir, "iop-capture.toml");
		}
		return true;
	}

	// The folder of a session under -sessions, made once the machine can be set
	// up, so that a start that stops before (an option the host does not take,
	// a BIOS or disc image that is not there) leaves none behind; and in it,
	// before the machine opens them, the memory cards and the console's NVRAM
	// as they are now (the core keeps the NVRAM beside the BIOS image:
	// <image>.nvm and .mec), as game.ps1 -Play puts them beside a record.
	bool MakeSessionFolder()
	{
		const Options& o = s_options;
		if (!FileSystem::CreateDirectoryPath(o.record_dir.c_str(), true))
		{
			std::fprintf(stderr, "ps2n-host: cannot make %s\n", o.record_dir.c_str());
			return false;
		}
		for (const char* card : {"Mcd001.ps2", "Mcd002.ps2"})
		{
			const std::string from = Path::Combine(Path::Combine(Path::Combine(o.data_dir, "PCSX2"), "memcards"), card);
			if (FileSystem::FileExists(from.c_str()))
				FileSystem::CopyFilePath(from.c_str(), Path::Combine(o.record_dir, card).c_str(), false);
		}
		for (const char* extension : {"nvm", "mec"})
		{
			const std::string from = Path::ReplaceExtension(o.bios_path, extension);
			if (FileSystem::FileExists(from.c_str()))
				FileSystem::CopyFilePath(from.c_str(), Path::Combine(o.record_dir, Path::GetFileName(from)).c_str(), false);
		}
		return true;
	}

	// The options, with the files -settings names read in the place of
	// `-settings <file>`: one option to a line, a line beginning with # left
	// out, %GAME% the file's folder. Started with no options at all, the host
	// reads settings.txt beside it, where there is one: in a game's folder it
	// then plays the game as the game's launcher has it play (host/launcher.cpp
	// gives -settings itself; docs/phase4-proposal.md, step P4.4).
	bool ExpandSettings(int argc, char* argv[], std::vector<std::string>* out)
	{
		out->emplace_back(argc > 0 ? argv[0] : "ps2n-host");
		std::vector<std::string> given(argv + std::min(argc, 1), argv + argc);
		if (given.empty())
		{
			const std::string beside = Path::Combine(Path::GetDirectory(FileSystem::GetProgramPath()), "settings.txt");
			if (FileSystem::FileExists(beside.c_str()))
				given = {"-settings", beside};
		}
		for (size_t i = 0; i < given.size(); i++)
		{
			if (given[i] != "-settings" || i + 1 >= given.size())
			{
				out->push_back(given[i]);
				continue;
			}
			std::string file = given[++i];
			if (!Path::IsAbsolute(file))
				file = Path::Canonicalize(Path::Combine(FileSystem::GetWorkingDirectory(), file));
			const std::optional<std::string> text = FileSystem::ReadFileToString(file.c_str());
			if (!text)
			{
				std::fprintf(stderr, "ps2n-host: cannot read %s\n", file.c_str());
				return false;
			}
			const std::string folder(Path::GetDirectory(file));
			for (std::string_view line : StringUtil::SplitString(*text, '\n', false))
			{
				line = StringUtil::StripWhitespace(line);
				if (line.empty() || line.front() == '#')
					continue;
				out->push_back(StringUtil::ReplaceAll(line, "%GAME%", folder));
			}
		}
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

		// The console's clock, which the IOP's modules read: two runs started a
		// minute apart are not the same machine unless it starts at the same
		// time in both. So a run that is not played in a window starts at one
		// fixed time (the one PCSX2 gives its input recordings: 2020-03-04,
		// midnight), unless -clock says otherwise; someone playing gets this
		// computer's time of day. The host sets it in every case, rather than
		// leave the time of day to the core, so that a session can write down
		// the time it started at, and a replay start at it again.
		if (o.clock_at)
		{
			s_clock_used = *o.clock_at;
		}
		else if (o.clock_fixed)
		{
			s_clock_used = {2020, 3, 4, 0, 0, 0};
		}
		else
		{
			const std::time_t now = std::time(nullptr);
			std::tm local = {};
#ifdef _WIN32
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			s_clock_used = {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
		}
		si.SetBoolValue("EmuCore", "ManuallySetRealTimeClock", true);
		si.SetIntValue("EmuCore", "RtcYear", s_clock_used[0] - 2000);
		si.SetIntValue("EmuCore", "RtcMonth", s_clock_used[1]);
		si.SetIntValue("EmuCore", "RtcDay", s_clock_used[2]);
		si.SetIntValue("EmuCore", "RtcHour", s_clock_used[3]);
		si.SetIntValue("EmuCore", "RtcMinute", s_clock_used[4]);
		si.SetIntValue("EmuCore", "RtcSecond", s_clock_used[5]);

		// The game's patches (ADR 0025): the widescreen group of its patch file,
		// which the host takes from PCSX2's archive when the game has booted.
		// With it, the game's films, made for the console's own picture, keep
		// their proportions in the middle of the wide one (the maintainer's
		// choice), as PCSX2 can show them; it changes only how the picture is
		// shown. 4:3 and not "Auto 4:3/3:2", which the renderer turns back into
		// the patch's own 16:9 (CalculateDrawDstRect in GSRenderer.cpp).
		si.SetBoolValue("EmuCore", "EnableWideScreenPatches", o.patch_widescreen);
		if (o.patch_widescreen)
			si.SetStringValue("EmuCore/GS", "FMVAspectRatioSwitch", "4:3");

		// PCSX2's EE cycle rate: its overclock, which a game's patch may ask for.
		si.SetIntValue("EmuCore/Speedhacks", "EECycleRate", o.ee_cycle_rate);

		si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(o.renderer));
		si.SetFloatValue("EmuCore/GS", "upscale_multiplier", static_cast<float>(o.upscale));

		// None of PCSX2's hotkeys, with a window or without: they open its own
		// interface, save states, change settings. A game has none of that.
		si.ClearSection("Hotkeys");

		if (o.window)
		{
			// Someone is playing. Everything else stays as PCSX2 sets it up by
			// default (VMManager::SetDefaultSettings above): the frame limiter
			// holds the machine to the console's speed, the sound goes to the
			// system's output, the pad in port 1 is on the keyboard, controllers
			// are looked for, and slot 1 holds a memory card, which the core
			// makes in the data directory when there is none.
			si.SetBoolValue("SPU2/Output", "OutputMuted", o.mute);

			// What the core can draw over the picture: none of it, unless asked
			// for (docs/adr/0012-ps2native-branding-not-pcsx2s.md).
			if (!o.osd)
			{
				si.SetIntValue("EmuCore/GS", "OsdMessagesPos", 0);
				si.SetIntValue("EmuCore/GS", "OsdPerformancePos", 0);
				for (const char* name : {"OsdShowSpeed", "OsdShowFPS", "OsdShowVPS", "OsdShowCPU", "OsdShowGPU", "OsdShowGPUDebug",
						 "OsdShowGPUStats", "OsdShowGPUShadersStats", "OsdShowResolution", "OsdShowGSStats", "OsdShowIndicators",
						 "OsdShowSettings", "OsdshowPatches", "OsdShowInputs", "OsdShowFrameTimes", "OsdShowVersion",
						 "OsdShowHardwareInfo", "OsdShowVideoCapture", "OsdShowInputRec", "OsdShowTextureReplacements"})
				{
					si.SetBoolValue("EmuCore/GS", name, false);
				}
			}
		}
		else
		{
			// Run as fast as the host allows, with nothing presented.
			si.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
			si.SetIntValue("EmuCore/GS", "VsyncEnable", 0);

			// No sound device, no input devices, and no memory cards unless a
			// folder of them is given (-memcards: a session run again has the
			// cards it began with). Then both slots hold one, as they always do
			// in a window: where the session began without a card, the core
			// made a new one when the machine started, and it makes one here
			// the same way (8 MB, unformatted).
			si.SetStringValue("SPU2/Output", "Backend", "Null");
			si.SetBoolValue("InputSources", "SDL", false);
			si.SetBoolValue("InputSources", "XInput", false);
			Pad::ClearPortBindings(si, 0);
			const bool cards = !o.memcards_dir.empty();
			if (cards)
				si.SetStringValue("Folders", "MemoryCards", o.memcards_dir.c_str());
			for (u32 i = 0; i < 2; i++)
			{
				const std::string name = fmt::format("Mcd{:03}.ps2", i + 1);
				si.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", i + 1).c_str(), cards);
				si.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", i + 1).c_str(), cards ? name.c_str() : "");
			}
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

	// What the window passes on (window.h). Each of these is called on the main
	// thread; what it has for the machine goes to the CPU thread, which takes it
	// up between two frames.
	void OnWindowKey(u32 code, bool pressed)
	{
		ps2n::host::RunOnCPUThread(
			[code, pressed]() { InputManager::InvokeEvents(InputManager::MakeHostKeyboardKey(code), pressed ? 1.0f : 0.0f); }, false);
	}

	void OnWindowResized(u32 width, u32 height, float scale)
	{
		ps2n::host::RunOnCPUThread(
			[width, height, scale]() {
				if (MTGS::IsOpen())
					MTGS::ResizeDisplayWindow(width, height, scale);
			},
			false);
	}

	void OnWindowFocusLost()
	{
		// The keys that are down will come up in some other window.
		ps2n::host::RunOnCPUThread([]() { InputManager::ClearBindStateFromSource(InputManager::MakeHostKeyboardKey(0)); }, false);
	}

	void OnWindowShotRequested(uint32_t number)
	{
		if (number != 0)
			s_shot_requested.store(number);
	}

	void OnWindowCloseRequested()
	{
		s_close_requested.store(true);
	}

	// Gives the first controller the pad in port 1, beside the keyboard: the
	// mapping PCSX2 would make for it automatically, added to the bindings that
	// are there. CPU thread, between two frames.
	void MapFirstController()
	{
		if (s_controller_mapped)
			return;

		const InputManager::GenericInputBindingMapping mapping = InputManager::GetGenericBindingMapping("SDL-0");
		const auto find = [&mapping](GenericInputBinding generic) -> const char* {
			for (const std::pair<GenericInputBinding, std::string>& entry : mapping)
			{
				if (entry.first == generic)
					return entry.second.c_str();
			}
			return nullptr;
		};

		u32 mapped = 0;
		{
			std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
			const std::string section = Pad::GetConfigSection(0);
			const Pad::ControllerInfo* info = Pad::GetConfigControllerType(s_settings, section.c_str(), 0);
			if (!info)
				return;

			for (const InputBindingInfo& binding : info->bindings)
			{
				if (binding.generic_mapping == GenericInputBinding::Unknown)
					continue;
				if (const char* source = find(binding.generic_mapping))
				{
					s_settings.AddToStringList(section.c_str(), binding.name, source);
					mapped++;
				}
			}
			if (info->vibration_caps == Pad::VibrationCapabilities::LargeSmallMotors)
			{
				if (const char* source = find(GenericInputBinding::SmallMotor))
					s_settings.SetStringValue(section.c_str(), "SmallMotor", source);
				if (const char* source = find(GenericInputBinding::LargeMotor))
					s_settings.SetStringValue(section.c_str(), "LargeMotor", source);
			}
		}
		if (mapped == 0)
			return;

		s_controller_mapped = true;
		VMManager::ReloadInputBindings(true);
		std::fprintf(stderr, "ps2n-host: the first controller has the pad in port 1 (%u inputs), beside the keyboard\n", mapped);
	}

	// Writes the picture the GS has on display, at the internal resolution,
	// for a check that asked for it through the window (window::kShotMessage):
	// shot-<number>.png beside the session's last frame. Any renderer: a
	// picture Vulkan presents cannot be photographed from the screen here.
	void WriteShot(u32 number)
	{
		if (s_options.renderer == GSRendererType::Null || s_options.frame_path.empty())
			return;
		u32 width = 0, height = 0;
		std::vector<u32> pixels;
		const std::string path = Path::Combine(Path::GetDirectory(s_options.frame_path), fmt::format("shot-{}.png", number));
		if (!MTGS::SaveMemorySnapshot(0, 0, false, false, &width, &height, &pixels) || pixels.empty())
		{
			std::fprintf(stderr, "ps2n-host: shot %u: nothing on display\n", number);
			return;
		}
		for (u32& pixel : pixels)
			pixel |= 0xFF000000u;
		// Written under another name first: the check waits for this one. (The
		// name given to SaveToFile only chooses the format.)
		const std::string part = path + ".part";
		const RGBA8Image image(width, height, pixels.data());
		std::FILE* file = FileSystem::OpenCFile(part.c_str(), "wb");
		const bool written = file && image.SaveToFile(path.c_str(), file);
		if (file)
			std::fclose(file);
		if (!written || !FileSystem::RenamePath(part.c_str(), path.c_str()))
			std::fprintf(stderr, "ps2n-host: cannot write %s\n", path.c_str());
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

	// Fingerprints the sound hardware, so that two runs can be held to making
	// the same sound, whether anything plays it or not. It covers the SPU2's
	// registers and its memory. That memory holds the samples the program gave
	// it, the working memory of its reverberation, and the last 512 samples of
	// everything it mixed: its mixer writes them there, as the console's does
	// (pcsx2/SPU2/Mixer.cpp). The SPU2 runs on the CPU thread, in the IOP's
	// time, so two runs that are at the same place at the same time and made
	// the same sound have the same fingerprint.
	void CaptureSound(bool along_the_way = false)
	{
		u64 hash = 14695981039346656037ull;
		const auto add = [&hash](const s16* words, size_t count) {
			for (size_t i = 0; i < count; i++)
				hash = (hash ^ static_cast<u16>(words[i])) * 1099511628211ull;
		};
		add(spu2regs, std::size(spu2regs));
		add(_spu2mem, std::size(_spu2mem));

		if (along_the_way)
			s_sound_lines.push_back(fmt::format("ps2n-host: sound at frame {}: fingerprint {:016X}", s_frames, hash));
		else
			s_sound_line = fmt::format("ps2n-host: sound at the last frame: fingerprint {:016X}", hash);
	}

	void ReportIop()
	{
		using ull = unsigned long long;
		const ps2n::iop::Stats st = ps2n::iop::GetStats();
		std::fprintf(stderr,
			"ps2n-host: PS2Native's IOP provider: %llu stretches of IOP time, %llu blocks begun, %llu instructions in them; %llu blocks "
			"formed, %llu thrown away because their memory was written, the whole record %llu times; %llu SYSCALL and BREAK "
			"instructions; %llu times a wait loop let time pass; %llu imports of modules for which the core has a function, which did "
			"%llu of them itself\n",
			ull(st.stretches), ull(st.runs), ull(st.instructions), ull(st.formed), ull(st.thrown), ull(st.resets), ull(st.exceptions),
			ull(st.waits), ull(st.imports), ull(st.importsDone));
		std::fprintf(stderr,
			"ps2n-host: the IOP's blocks: %llu different ones by their instructions (%llu instructions); %llu of them compiled ahead of "
			"time (this host has %zu), which ran %llu times; %llu interpreted, which ran %llu times\n",
			ull(st.shapes), ull(st.shapeWords), ull(st.shapesCompiled), ps2n::iop::CompiledBlockCount(),
			ull(st.runs - st.runsInterpreted), ull(st.shapesInterpreted), ull(st.runsInterpreted));
		if (st.unknown)
		{
			std::fprintf(stderr,
				"ps2n-host: PS2Native's IOP provider: the blocks formed held %llu instructions of encodings PCSX2's IOP recompiler has "
				"nothing for; they are carried out as it carries them out, which is not at all\n",
				ull(st.unknown));
		}
		if (const char* what = ps2n::iop::Unsupported())
			std::fprintf(stderr, "ps2n-host: PS2Native's IOP provider does not carry out: %s\n", what);
		// That PCSX2's IOP recompiler carried out nothing, by its own table
		// (which it has only where the core was set up with it).
		if (s_options.iop_jit)
		{
			std::fprintf(stderr, "ps2n-host: PCSX2's IOP recompiler holds %u blocks after this run\n",
				ps2n::host::iopreport::RecompilerBlocks());
		}
		if (!s_options.iop_capture_path.empty())
		{
			if (ps2n::iop::WriteCapture(s_options.iop_capture_path.c_str()))
				std::fprintf(stderr, "ps2n-host: the blocks of IOP code that were interpreted, for the recompiler: %s\n",
					s_options.iop_capture_path.c_str());
			else
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.iop_capture_path.c_str());
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
				" code nothing was compiled from %llu; times compiled code was told to run a block %llu (%llu of them with an answer"
				" kept from the time before), times the interpreter carried one out at a label instead %llu, times it was a timeout"
				" loop's turn %llu; calls passed on to a variant %llu; frames abandoned: %llu for another executable, %llu for stack"
				" depth; blocks finished by the interpreter %llu\n",
				stats.image->name, s_options.static_code ? "in use" : "switched off", static_cast<ull>(stats.static_entries),
				static_cast<ull>(stats.blocks_checked), static_cast<ull>(stats.blocks_unknown), static_cast<ull>(stats.blocks_compiled),
				static_cast<ull>(stats.blocks_compiled_known),
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
			"ps2n-host: interpreter: %llu instructions (%llu inside the executable's code); carried out by PCSX2's interpreter"
			" functions, for the interpreter or for compiled code (VCALLMS, VCALLMSR), %llu\n",
			static_cast<ull>(stats.interpreted), static_cast<ull>(stats.interpreted_in_image), static_cast<ull>(stats.delegated));
		std::fprintf(stderr,
			"ps2n-host: interpreted outside the executable's code: %llu instructions in the kernel's segments (the BIOS), %llu below"
			" 00100000 (the BIOS's program loader), %llu elsewhere in user memory (code the executable does not hold)\n",
			static_cast<ull>(stats.interpreted_kernel), static_cast<ull>(stats.interpreted_loader),
			static_cast<ull>(stats.interpreted_elsewhere));
		{
			// The BIOS's code, piece by piece (ps2n/romdir.h; docs/game-log.md, "G8").
			static constexpr const char* kPlaces[ps2n::ee::kBiosPlaces] = {"the ROM", "the kernel's file", "the rest of kernel memory",
				"RAM through its uncached mirror", "the idle thread's file", "the program loader's file", "the rest below 00100000"};
			std::string places;
			for (size_t p = 0; p < ps2n::ee::kBiosPlaces; p++)
			{
				const u64 boot = stats.bios_interpreted[0][p];
				const u64 after = stats.bios_interpreted[1][p];
				if (!boot && !after && !stats.bios_block_starts[p])
					continue;
				places += fmt::format("{}{} {} at boot and {} after ({} block starts)", places.empty() ? "" : "; ", kPlaces[p], boot,
					after, stats.bios_block_starts[p]);
			}
			std::fprintf(stderr, "ps2n-host: the BIOS's compiled code: %s\n",
				!stats.bios_images ? "none (none for this ROM in the host, or -bios-static off)" :
				s_options.static_code ?
									 fmt::format("{} images in use, {} functions", stats.bios_images, stats.bios_functions).c_str() :
									 fmt::format("{} images, switched off (-static off)", stats.bios_images).c_str());
			std::fprintf(stderr,
				"ps2n-host: the BIOS interpreted, by where its blocks begin, at boot (until the executable's entry point) and after:"
				" %s; instructions in blocks counted double %llu; runs of the program loader's hooked blocks %llu\n",
				places.empty() ? "nothing" : places.c_str(), static_cast<ull>(stats.interpreted_doubled),
				static_cast<ull>(stats.hooked_block_runs));

			const auto files = [](const std::vector<ps2n::ee::BiosFileCheck>& checks) {
				std::string text;
				for (const ps2n::ee::BiosFileCheck& c : checks)
				{
					text += fmt::format("{}{} at {:08X}, ", text.empty() ? "" : "; ", c.file, c.address);
					if (!c.words)
						text += "not in the ROM's directory";
					else if (!c.differing)
						text += fmt::format("{} words, all the same", c.words);
					else
						text += fmt::format("{} words, {} differ, from {:08X} to {:08X}", c.words, c.differing, c.first, c.last);
				}
				return text.empty() ? std::string("not compared") : text;
			};
			std::fprintf(stderr,
				"ps2n-host: the BIOS's files against memory: when the executable reached its entry point, %s; at the end, %s\n",
				files(ps2n::ee::GetBiosFilesAtEntry()).c_str(), files(ps2n::ee::CompareBiosFiles()).c_str());
		}
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
		if (stats.fixed_loop_skips)
		{
			std::fprintf(stderr,
				"ps2n-host: loops that had reached a fixed point: %llu times round not run, %llu EE cycles, at %llu event tests\n",
				static_cast<ull>(stats.fixed_loop_rounds), static_cast<ull>(stats.fixed_loop_cycles),
				static_cast<ull>(stats.fixed_loop_skips));
		}
		{
			// What compiled code still hands to the runtime of COP2: PS2N_COP2,
			// PS2N_VCALLMS, and PS2N_VU0 and PS2N_COP2_MOVE where the runtime has
			// something to do about VU0 first (PS2N_COP2_ASKS).
			std::string line;
			uint64_t total = 0;
			for (size_t k = 0; k < ps2n::ee::kCop2Kinds; k++)
			{
				if (!stats.cop2_runtime[k])
					continue;
				total += stats.cop2_runtime[k];
				line += fmt::format("{}{} {} ({} with VU0 running)", line.empty() ? "" : ", ", ps2n::ee::kCop2KindNames[k],
					stats.cop2_runtime[k], stats.cop2_runtime_vu0_running[k]);
			}
			if (total)
				std::fprintf(stderr, "ps2n-host: COP2 instructions compiled code handed to the runtime: %llu: %s\n",
					static_cast<ull>(total), line.c_str());
		}
		if (stats.console_followed_abs_neg || stats.console_followed_max_min)
		{
			// Where a register may differ from a stock run's on purpose (ADR 0005).
			std::fprintf(stderr,
				"ps2n-host: the console was followed where stock PCSX2 may leave something else in an FPU register: max.s and"
				" min.s of +0 and -0 or of bits PCSX2 clamps %llu times, abs.s and neg.s of such bits %llu times\n",
				static_cast<ull>(stats.console_followed_max_min), static_cast<ull>(stats.console_followed_abs_neg));
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
		const std::vector<ps2n::ee::CapturedCode> outside = ps2n::ee::GetCodeOutside();
		if (stats.image && s_options.static_code)
		{
			const bool anything = !missing.empty() || !captured.empty() || !outside.empty();
			std::fprintf(stderr,
				"ps2n-host: %zu block starts inside the executable's code had to be interpreted; in the place of %zu functions memory"
				" held code that nothing was compiled from; %zu blocks of the program's ran outside its executable and the BIOS's"
				" files, where nothing was compiled%s\n",
				missing.size(), captured.size(), outside.size(),
				(anything && s_options.capture_path.empty()) ? " (pass -capture <file> to record them for the recompiler)" : "");
		}
		const std::vector<ps2n::ee::LoadedProgram> loaded = ps2n::ee::GetLoadedPrograms();
		if (!loaded.empty() && s_options.static_code)
		{
			std::string line;
			for (const ps2n::ee::LoadedProgram& p : loaded)
			{
				line += fmt::format("{}{} (CRC {:08X}) put in use {} times, {} block starts in its code interpreted", line.empty() ? "" : "; ",
					p.image->name, p.image->program_crc, p.uses, p.missing.size());
			}
			std::fprintf(stderr, "ps2n-host: the executables it loads itself: %s\n", line.c_str());
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

				// The same in each executable it loaded itself (ps2n-recomp
				// compile --loaded-by), by that executable's CRC.
				for (const ps2n::ee::LoadedProgram& p : loaded)
				{
					if (p.missing.empty())
						continue;
					std::fprintf(fp,
						"\n# The same in %s, which it loaded itself.\n"
						"[[program]]\n"
						"crc = 0x%08x\n"
						"entries = [\n",
						p.image->name, p.image->program_crc);
					for (const uint32_t pc : p.missing)
						std::fprintf(fp, "  0x%08x,\n", pc);
					std::fputs("]\n", fp);
				}

				for (const ps2n::ee::CapturedCode& code : captured)
				{
					std::fprintf(fp,
						"\n# Code found in memory in the place of the function at 0x%08x.\n"
						"[[code]]\n",
						code.start);
					if (code.program)
						std::fprintf(fp, "program = 0x%08x\n", code.program);
					std::fprintf(fp,
						"start = 0x%08x\n"
						"words = [",
						code.start);
					for (size_t i = 0; i < code.words.size(); i++)
						std::fprintf(fp, "%s0x%08x,", (i % 8) ? " " : "\n  ", code.words[i]);
					std::fputs("\n]\n", fp);
				}

				// The program's code that ran outside its executable and the
				// BIOS's files (ps2n-recomp compile --outside).
				for (const ps2n::ee::CapturedCode& code : outside)
				{
					std::fprintf(fp,
						"\n# A block of the program's run at 0x%08x, outside its executable, where nothing was compiled.\n"
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
		if (!s_options.bios_capture_path.empty())
		{
			if (std::FILE* fp = FileSystem::OpenCFile(s_options.bios_capture_path.c_str(), "wb"))
			{
				std::fputs("# The addresses in the BIOS at which PS2Native's interpreter formed a block.\n"
						   "entries = [\n",
					fp);
				for (const uint32_t pc : ps2n::ee::GetBiosBlockStarts())
					std::fprintf(fp, "  0x%08x,\n", pc);
				std::fputs("]\n", fp);

				// The words of the BIOS's files that memory held otherwise, for whoever
				// reads this file; the recompiler takes them for comments.
				const auto words = [fp](const char* when, const std::vector<ps2n::ee::BiosFileCheck>& checks) {
					for (const ps2n::ee::BiosFileCheck& c : checks)
					{
						if (!c.differing)
							continue;
						std::fprintf(fp, "\n# %s, %u words of %s (at %08X) differed from the ROM's file; the first %zu:\n", when,
							c.differing, c.file, c.address, c.sample.size());
						for (const ps2n::ee::BiosFileCheck::Word& w : c.sample)
							std::fprintf(fp, "#   %08X  ROM %08X  memory %08X\n", w.address, w.rom, w.memory);
					}
				};
				words("When the executable reached its entry point", ps2n::ee::GetBiosFilesAtEntry());
				words("At the end", ps2n::ee::CompareBiosFiles());
				std::fclose(fp);
			}
			else
			{
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.bios_capture_path.c_str());
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
				ps2n::vu::SetCompiledBlocksEnabled(s_options.vu_static);
				ps2n::vu::SetImages(ps2n_vu_images, ps2n_vu_image_count);
				ps2n::iop::SetCompiledBlocksEnabled(s_options.iop_static);
				ps2n::iop::SetImages(ps2n_iop_images, ps2n_iop_image_count);
				// The self-test's own micro programs compiled, as a build with their
				// capture has them (the image "selftest"); and its IOP programs'
				// blocks, likewise.
				bool compiled_selftest = false;
				for (size_t i = 0; i < ps2n_vu_image_count; i++)
					compiled_selftest = compiled_selftest || !std::strcmp(ps2n_vu_images[i]->id, "selftest");
				bool compiled_iop_selftest = false;
				for (size_t i = 0; i < ps2n_iop_image_count; i++)
					compiled_iop_selftest = compiled_iop_selftest || !std::strcmp(ps2n_iop_images[i]->id, "selftest");
				int failures = 0;
				if (s_options.vu_require_compiled && !compiled_selftest)
				{
					std::fputs("PS2N_SKIP: this host was not built with the self-test's micro programs compiled "
							   "(scripts/windows/vu-selftest.ps1 builds them in)\n",
						stderr);
				}
				else if (s_options.iop_require_compiled && !compiled_iop_selftest)
				{
					std::fputs("PS2N_SKIP: this host was not built with the blocks of the self-test's IOP programs compiled "
							   "(scripts/windows/iop-selftest.ps1 builds them in)\n",
						stderr);
				}
				else
				{
					failures = ps2n::selftest::Run(s_options.selftest_options);
					if (!s_options.selftest_options.vu_only)
					{
						const ps2n::iop::Stats st = ps2n::iop::GetStats();
						std::fprintf(stderr,
							"ps2n-host: the IOP's blocks: %llu different ones by their instructions (%llu instructions); %llu of them "
							"compiled ahead of time (this host has %zu), which ran %llu times; %llu interpreted, which ran %llu times\n",
							static_cast<unsigned long long>(st.shapes), static_cast<unsigned long long>(st.shapeWords),
							static_cast<unsigned long long>(st.shapesCompiled), ps2n::iop::CompiledBlockCount(),
							static_cast<unsigned long long>(st.runs - st.runsInterpreted),
							static_cast<unsigned long long>(st.shapesInterpreted), static_cast<unsigned long long>(st.runsInterpreted));
						if (s_options.iop_require_compiled && st.shapesInterpreted)
						{
							std::fprintf(stderr,
								"ps2n-host: %llu blocks of the self-test's IOP programs had to be interpreted: the compiled ones this host "
								"has are of other programs. Make them again (scripts/windows/iop-selftest.ps1).\n",
								static_cast<unsigned long long>(st.shapesInterpreted));
							failures++;
						}
						if (!s_options.iop_capture_path.empty() && !ps2n::iop::WriteCapture(s_options.iop_capture_path.c_str()))
						{
							std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.iop_capture_path.c_str());
							failures++;
						}
					}
					ReportVuPlans(3);
					const u64 interpreted = ps2n::vu::Plans(0).interpreted + ps2n::vu::Plans(1).interpreted;
					if (s_options.vu_require_compiled && interpreted)
					{
						std::fprintf(stderr,
							"ps2n-host: %llu blocks of the self-test's micro programs had to be interpreted: the compiled ones this host has "
							"are of other plans. Make them again (scripts/windows/vu-selftest.ps1).\n",
							static_cast<unsigned long long>(interpreted));
						failures++;
					}
				}
				if (!s_options.vu_capture_path.empty() && !ps2n::vu::WriteCapture(s_options.vu_capture_path.c_str()))
				{
					std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.vu_capture_path.c_str());
					failures++;
				}
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
					// Of the executables' images, all of them or the one -image
					// names, with the image of the code it ran outside its own
					// and those of the executables it loads itself
					// (<id>_loads_<name>); the BIOS's, unless -bios-static off.
					static std::vector<const ps2n::ee::ImageInfo*> chosen;
					bool found = s_options.image_id.empty();
					for (size_t i = 0; i < ps2n_image_count; i++)
					{
						const ps2n::ee::ImageInfo* image = ps2n_images[i];
						if (image->kind == ps2n::ee::kImageBios)
						{
							if (s_options.bios_static)
								chosen.push_back(image);
						}
						else if (s_options.image_id.empty() || s_options.image_id == image->id)
						{
							chosen.push_back(image);
							found = true;
						}
						else if (image->kind == ps2n::ee::kImageOutside && image->id == s_options.image_id + "_outside")
						{
							chosen.push_back(image);
						}
						else if (image->kind == ps2n::ee::kImageLoaded && std::string_view(image->id).starts_with(s_options.image_id + "_loads_"))
						{
							chosen.push_back(image);
						}
					}
					if (!found)
					{
						std::fprintf(stderr, "ps2n-host: this host has no compiled image '%s'\n", s_options.image_id.c_str());
						trace_ok = false;
					}
					ps2n::ee::SetImages(chosen.data(), chosen.size());
					ps2n::ee::SetStaticCodeEnabled(s_options.static_code);
					ps2n::ee::SetCallSitesEnabled(s_options.call_sites);
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

				// PS2Native's IOP provider in the place of the core's (-iop-ps2n).
				// Nothing of the IOP's has run yet.
				if (s_options.iop_ps2n)
				{
					ps2n::iop::SetCompiledBlocksEnabled(s_options.iop_static);
					ps2n::iop::SetImages(ps2n_iop_images, ps2n_iop_image_count);
					psxCpu = ps2n::iop::Provider();
					psxCpu->Reset();
				}

				// What the IOP runs, in any configuration: its provider watched,
				// not changed (iop_report.h). Before the record of sync points,
				// which then stands in front of the watch.
				if (!s_options.iop_report_path.empty())
				{
					if (ps2n::host::iopreport::Start(s_options.iop_report_path.c_str()))
					{
						ps2n::host::iopreport::Install();
					}
					else
					{
						std::fprintf(stderr, "ps2n-host: cannot open the files of %s\n", s_options.iop_report_path.c_str());
						trace_ok = false;
					}
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

				// PS2Native's VU interpreter in the place of the core's VU providers
				// (-vu-ps2n). The core chose its providers with the machine, and
				// chooses again only when a recompiler is switched on or off.
				ps2n::vu::SetCompiledBlocksEnabled(s_options.vu_static);
				ps2n::vu::SetImages(ps2n_vu_images, ps2n_vu_image_count);
				for (int vu = 0; vu < 2; vu++)
				{
					if (!(s_options.vu_ps2n & (1u << vu)))
						continue;
					BaseVUmicroCPU* ours = ps2n::vu::Interpreter(vu);
					ours->Reset();
					(vu ? CpuVU1 : CpuVU0) = ours;
					if (!s_options.vu_programs_path.empty())
					{
						const std::string path = vu ? s_options.vu_programs_path + "1" : s_options.vu_programs_path + "0";
						if (!ps2n::vu::LogPrograms(vu, path.c_str()))
						{
							std::fprintf(stderr, "ps2n-host: cannot write %s\n", path.c_str());
							trace_ok = false;
						}
					}
				}

				// What the vector units run, in any configuration: their providers
				// watched, not changed (vu_report.h).
				if (!s_options.vu_report_path.empty())
				{
					if (ps2n::host::vureport::Start(s_options.vu_report_path.c_str()))
					{
						ps2n::host::vureport::Install();
					}
					else
					{
						std::fprintf(stderr, "ps2n-host: cannot open the files of %s\n", s_options.vu_report_path.c_str());
						trace_ok = false;
					}
				}

				// As fast as the host goes: the core otherwise holds the machine to
				// the console's speed, however little work a frame is. With a
				// window someone is playing, and the console's speed is the point.
				VMManager::SetState(VMState::Running);
				VMManager::SetLimiterMode(s_options.window ? LimiterModeType::Nominal : LimiterModeType::Unlimited);
				while (trace_ok && VMManager::GetState() == VMState::Running)
					VMManager::Execute();

				if (s_options.ee == EEProvider::Ps2n)
					ps2n::ee::StopTrace();
				ps2n::host::StopSyncTrace();
				if (!s_options.iop_report_path.empty())
				{
					if (ps2n::host::iopreport::Stop(s_frames))
						std::fprintf(stderr, "ps2n-host: what the IOP ran: %s\n", s_options.iop_report_path.c_str());
					else
						std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.iop_report_path.c_str());
				}
				if (!s_options.vu_report_path.empty())
				{
					if (ps2n::host::vureport::Stop(s_frames))
						std::fprintf(stderr, "ps2n-host: what the vector units ran: %s\n", s_options.vu_report_path.c_str());
					else
						std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.vu_report_path.c_str());
				}

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
					reason = "an error: the core put a provider of its own back where the host had put one";
				}
				else if (s_iop_unsupported)
				{
					code = EXIT_ERROR;
					reason = "an error: PS2Native's IOP provider met what it does not carry out";
				}
				else if (s_vu_unsupported)
				{
					code = EXIT_ERROR;
					reason = "an error: PS2Native's VU interpreter met what it does not carry out yet";
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
				else if (s_close_requested.load())
				{
					code = EXIT_MARKER;
					reason = "the closing of the window";
				}
				else if (s_time_limit_passed.load())
				{
					code = EXIT_TIME_LIMIT;
					reason = "time limit";
				}
				std::fprintf(stderr, "ps2n-host: stopped at %s after %u frames, EE cycle %llu, EE %s, IOP %s, VU %s\n", reason,
					s_frames, static_cast<unsigned long long>(cpuRegs.cycle), EEProviderName(s_options.ee),
					s_options.iop_ps2n ? "ps2n" : s_options.iop_jit ? "jit" : "interp",
					s_options.vu_ps2n == 3 ? "ps2n" :
					s_options.vu_ps2n == 1 ? "ps2n for VU0" :
					s_options.vu_ps2n == 2 ? "ps2n for VU1" :
					s_options.vu_jit ? "jit" :
										 "interp");
				std::fprintf(stderr, "ps2n-host: EE COP0 Config %08x (double issue %s), Status %08x, EE cycle rate setting %d\n",
					cpuRegs.CP0.n.Config, (cpuRegs.CP0.n.Config >> 18) & 1 ? "on" : "off", cpuRegs.CP0.n.Status.val,
					static_cast<int>(EmuConfig.Speedhacks.EECycleRate));
				for (int vu = 0; vu < 2; vu++)
				{
					if (!(s_options.vu_ps2n & (1u << vu)))
						continue;
					const ps2n::vu::InterpreterStats& st = ps2n::vu::Stats(vu);
					std::fprintf(stderr,
						"ps2n-host: PS2Native's VU%d provider: %llu runs, %llu blocks, %llu instruction pairs; %llu blocks analysed, "
						"%llu programs recorded, %llu indirect jumps, %llu runs stopped for their cycles; %llu times the program at a "
						"start or a jump had to be looked for, %llu recorded programs compared with micro memory for that, and %llu "
						"times it had not: nothing was written where the program found last was compiled from\n",
						vu, static_cast<unsigned long long>(st.runs), static_cast<unsigned long long>(st.blocks),
						static_cast<unsigned long long>(st.pairs), static_cast<unsigned long long>(st.compiles),
						static_cast<unsigned long long>(st.programs), static_cast<unsigned long long>(st.jumps),
						static_cast<unsigned long long>(st.pauses), static_cast<unsigned long long>(st.searches),
						static_cast<unsigned long long>(st.compares), static_cast<unsigned long long>(st.spared));
					if (st.unknown)
					{
						std::fprintf(stderr,
							"ps2n-host: PS2Native's VU%d provider: the blocks analysed held %llu instructions of encodings PCSX2's VU "
							"recompiler knows nothing of; they are carried out as it carries them out, which is not at all\n",
							vu, static_cast<unsigned long long>(st.unknown));
					}
					if (const char* what = ps2n::vu::Unsupported(vu))
						std::fprintf(stderr, "ps2n-host: PS2Native's VU%d interpreter does not carry out yet: %s\n", vu, what);
					ps2n::vu::LogPrograms(vu, nullptr);
				}
				if (s_options.iop_ps2n)
					ReportIop();
				ReportVuPlans(s_options.vu_ps2n);
				if (s_options.vu_ps2n && !s_options.vu_capture_path.empty())
				{
					if (ps2n::vu::WriteCapture(s_options.vu_capture_path.c_str()))
						std::fprintf(stderr, "ps2n-host: the blocks of micro code that were interpreted, for the recompiler: %s\n",
							s_options.vu_capture_path.c_str());
					else
						std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.vu_capture_path.c_str());
				}
				if (s_options.window && s_frames > 1)
				{
					// The pace the session kept, against the console's. A machine that
					// cannot keep up shows fewer frames a second than the console.
					const double seconds = std::chrono::duration<double>(s_latest_frame_time - s_first_frame_time).count();
					std::fprintf(stderr, "ps2n-host: in the window, %u frames in %.1f s: %.2f a second, where the console shows %.2f\n",
						s_frames - 1, seconds, seconds > 0.0 ? static_cast<double>(s_frames - 1) / seconds : 0.0,
						static_cast<double>(GetVerticalFrequency()));
				}
				if (!s_options.marks.empty() && s_frames > 0)
				{
					// The first frame, those asked for, and the last, each with its
					// wall-clock time since the host started: what lies between two of
					// them is the time of that part of the run.
					std::string line;
					const auto add = [&line](u32 frame, double seconds) {
						line += fmt::format("{}frame {} at {:.3f} s", line.empty() ? "" : ", ", frame, seconds);
					};
					for (const auto& [frame, seconds] : s_frame_marks)
						add(frame, seconds);
					if (s_frame_marks.empty() || s_frame_marks.back().first != s_frames)
						add(s_frames, std::chrono::duration<double>(s_latest_frame_time - s_host_start).count());
					std::fprintf(stderr, "ps2n-host: wall-clock time since the host started: %s\n", line.c_str());
				}
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
						else if (!time)
							std::fputs("ps2n-host: sync: place and time are the same as the reference run's at every sync point\n", stderr);
						if (state)
							std::fprintf(stderr, "ps2n-host: sync: registers first differ %s\n", state);
						for (int kind = 0; kind < ps2n::host::kSyncRegisterKinds; kind++)
						{
							const char* name = nullptr;
							const char* first = nullptr;
							if (const u64 differing = ps2n::host::GetSyncRegisterDifferences(kind, &name, &first))
							{
								std::fprintf(stderr, "ps2n-host: sync: %s differ at %llu sync points, first %s\n", name,
									static_cast<unsigned long long>(differing), first);
							}
						}
						if (time)
							std::fprintf(stderr, "ps2n-host: sync: place or time first differs %s\n", time);
					}
				}
				for (const std::string& line : s_bench_lines)
					std::fprintf(stderr, "%s\n", line.c_str());
				for (const std::string& line : ps2n::host::session::Finish(s_frames))
					std::fprintf(stderr, "%s\n", line.c_str());
				if (!s_patch_line.empty())
					std::fprintf(stderr, "%s\n", s_patch_line.c_str());
				for (const std::string& line : s_frame_lines)
					std::fprintf(stderr, "%s\n", line.c_str());
				if (!s_frame_line.empty())
					std::fprintf(stderr, "%s\n", s_frame_line.c_str());
				for (const std::string& line : s_sound_lines)
					std::fprintf(stderr, "%s\n", line.c_str());
				if (!s_sound_line.empty())
					std::fprintf(stderr, "%s\n", s_sound_line.c_str());

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

	// Keeps the process on the processor's fastest cores (-fast-cores). A
	// processor with performance and efficiency cores runs the same work about
	// twice as fast on the first, and the system moves a process that is not in
	// front between them as it sees fit: the same run of a game took between 75
	// and 150 s (docs/game-log.md, "G5"). A processor whose cores are all alike
	// is left as it is.
	void UseFastCores()
	{
#ifdef _WIN32
		ULONG length = 0;
		GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
		std::vector<u8> buffer(length);
		if (length == 0 ||
			!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), length, &length, GetCurrentProcess(), 0))
		{
			std::fputs("ps2n-host: -fast-cores: the system does not say which cores it has; the run goes where it is put\n", stderr);
			return;
		}

		// A core with a higher efficiency class is the faster one.
		const auto each = [&buffer, length](const auto& function) {
			for (ULONG offset = 0; offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= length;)
			{
				const SYSTEM_CPU_SET_INFORMATION* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
				if (info->Size == 0)
					break;
				if (info->Type == CpuSetInformation)
					function(info->CpuSet.Id, info->CpuSet.EfficiencyClass);
				offset += info->Size;
			}
		};
		BYTE fastest = 0;
		size_t all = 0;
		each([&fastest, &all](ULONG id, BYTE efficiency_class) {
			fastest = std::max(fastest, efficiency_class);
			all++;
		});
		std::vector<ULONG> ids;
		each([&ids, fastest](ULONG id, BYTE efficiency_class) {
			if (efficiency_class == fastest)
				ids.push_back(id);
		});

		if (ids.size() == all)
		{
			std::fprintf(stderr, "ps2n-host: -fast-cores: the %zu logical processors are all alike\n", all);
			return;
		}
		if (SetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), static_cast<ULONG>(ids.size())))
			std::fprintf(stderr, "ps2n-host: kept to the processor's fastest cores: %zu of %zu logical processors\n", ids.size(), all);
		else
			std::fputs("ps2n-host: -fast-cores: the system refused; the run goes where it is put\n", stderr);
#else
		std::fputs("ps2n-host: -fast-cores is not available on this platform yet; the run goes where it is put\n", stderr);
#endif
	}

	// Ends the process if the machine does not stop on its own: a guest that
	// never reaches a frame boundary cannot be stopped from the frame callback.
	void WatchdogMain()
	{
		const auto start = std::chrono::steady_clock::now();
		const auto limit = std::chrono::seconds(s_options.max_seconds);
		const auto grace = std::chrono::seconds(20);
		std::optional<std::chrono::steady_clock::time_point> close_seen;

		while (!s_cpu_thread_done.load())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			const auto now = std::chrono::steady_clock::now();
			const auto elapsed = now - start;

			// No limit when someone is playing in a window (0 seconds).
			if (s_options.max_seconds != 0)
			{
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

			// A closed window must not leave a process behind either.
			if (s_close_requested.load())
			{
				if (!close_seen)
					close_seen = now;
				else if (now - *close_seen >= grace)
				{
					std::fputs("ps2n-host: the machine did not stop after the window was closed; ending the process\n", stderr);
					WriteGuestOutput();
					std::fflush(nullptr);
					std::_Exit(EXIT_ERROR);
				}
			}
		}
	}

	int Run(int argc, char* argv[])
	{
		// PS2Native's description of PCSX2's structures, which a module of
		// compiled code is built against (ps2n/pcsx2_layout.h): what a
		// compiler could not compare with PCSX2's when the runtime was built.
		if (const std::string differs = ps2n::pcsx2::CheckLayout(); !differs.empty())
		{
			std::fprintf(stderr, "ps2n-host: PS2Native's description of PCSX2's structures differs from PCSX2's: %s\n",
				differs.c_str());
			return EXIT_ERROR;
		}

		// The options as given, or as a game's folder has them (-settings,
		// settings.txt).
		std::vector<std::string> arguments;
		if (!ExpandSettings(argc, argv, &arguments))
			return EXIT_USAGE;
		std::vector<char*> pointers;
		for (std::string& argument : arguments)
			pointers.push_back(argument.data());
		pointers.push_back(nullptr);
		if (!ParseCommandLine(static_cast<int>(arguments.size()), pointers.data()))
		{
			PrintUsage();
			return EXIT_USAGE;
		}

		// How much host code the compiled images became: no machine needed.
		if (!s_options.code_size_path.empty())
		{
			std::vector<ps2n::host::profile::ImageCode> images;
			for (size_t i = 0; i < ps2n_image_count; i++)
			{
				const ps2n::ee::ImageInfo& image = *ps2n_images[i];
				u64 instructions = 0;
				for (u32 f = 0; f < image.function_count; f++)
					instructions += (image.functions[f].end - image.functions[f].start) / 4;
				images.push_back({image.id, instructions});
			}
			if (!ps2n::host::profile::WriteCodeSize(s_options.code_size_path.c_str(), images))
			{
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.code_size_path.c_str());
				return EXIT_ERROR;
			}
			std::fprintf(stderr, "ps2n-host: how much host code the compiled images became: %s\n", s_options.code_size_path.c_str());
			return EXIT_MARKER;
		}

		CrashHandler::Install();

		// Before any other thread exists: they all take the choice over.
		if (s_options.fast_cores)
			UseFastCores();

		if (!InitializeConfig())
			return EXIT_ERROR;
		if (s_options.make_session_folder && !MakeSessionFolder())
			return EXIT_ERROR;
		if (!s_options.record_dir.empty() &&
			!ps2n::host::session::StartRecording(s_options.record_dir, s_clock_used,
				PatchesText(s_options.patch_widescreen, s_options.patch_fps),
				s_options.ee_cycle_rate_given ? std::optional<int>(s_options.ee_cycle_rate) : std::nullopt,
				ps2n::host::session::BiosRoms(s_options.bios_path)))
			return EXIT_ERROR;
		if (!s_options.pad_path.empty() && !LoadPadScript(s_options.pad_path))
			return EXIT_ERROR;

		// The window belongs to this thread, which serves its messages below
		// while the machine runs on its own.
		if (s_options.window)
		{
			const ps2n::host::window::Listener listener = {
				&OnWindowKey, &OnWindowResized, &OnWindowFocusLost, &OnWindowCloseRequested, &OnWindowShotRequested};
			if (!ps2n::host::window::Create(s_options.window_title.empty() ? std::string(kWindowTitle) : s_options.window_title,
					s_options.window_width, s_options.window_height, listener))
			{
				std::fputs("ps2n-host: cannot make the window\n", stderr);
				return EXIT_ERROR;
			}
		}

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
			ps2n::host::window::Destroy();
			return EXIT_ERROR;
		}

#ifdef _WIN32
		// The game's window, when there is one, and windows that components of
		// the core may create are owned by this thread; keep its message queue
		// served while the machine runs.
		HANDLE handle = static_cast<HANDLE>(static_cast<void*>(cpu_thread));
		if (!s_options.profile_path.empty() && !ps2n::host::profile::Start(handle))
			std::fputs("ps2n-host: cannot look at the CPU thread: there will be no profile\n", stderr);
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
		ps2n::host::window::Destroy();

		if (!s_options.profile_path.empty())
		{
			// The CPU thread has ended: what it kept for itself can be read here.
			ps2n::host::profile::Stop();
			// From the frame at which it began looking to the last.
			const bool looked = s_frames >= s_options.profile_from;
			const double seconds = looked ? std::chrono::duration<double>(s_latest_frame_time - s_profile_start_time).count() : 0.0;
			const u32 frames = looked ? s_frames - s_options.profile_from : 0;
			if (ps2n::host::profile::Write(s_options.profile_path.c_str(), s_options.profile_from, frames, seconds))
				std::fprintf(stderr, "ps2n-host: where the CPU thread's time went: %s\n", s_options.profile_path.c_str());
			else
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_options.profile_path.c_str());
		}

		if (!WriteGuestOutput())
			return EXIT_ERROR;
		return s_exit_code.load();
	}
} // namespace

namespace
{
	// The EE overclock a group of a patch file asks for in its note, as
	// PCSX2's archive words it ("Might need EE Overclock (130%).", "Might need
	// EE overclock at 180%.", "set \"EE Cyclerate\" to 2"; ADR 0025): PCSX2's
	// EE cycle rate setting, 1 to 3 for 130%, 180% and 300%, or 0 where the
	// note asks for none. A note that names no figure asks for the mildest,
	// 130%; one that names two ("130-180%"), for the first.
	int OverclockAsked(std::string_view lines)
	{
		int asked = 0;
		for (const std::string_view line : StringUtil::SplitString(lines, '\n', true))
		{
			const std::string lower = StringUtil::toLower(line);
			const bool cycle_rate = lower.find("cycle rate") != std::string::npos || lower.find("cyclerate") != std::string::npos;
			if (!cycle_rate && lower.find("overclock") == std::string::npos)
				continue;
			int rate = 1;
			size_t first = std::string::npos;
			static constexpr std::pair<std::string_view, int> kFigures[] = {{"130", 1}, {"180", 2}, {"300", 3}};
			for (const auto& [figure, value] : kFigures)
			{
				const size_t at = lower.find(figure);
				if (at < first)
				{
					first = at;
					rate = value;
				}
			}
			if (first == std::string::npos && cycle_rate)
			{
				// The setting itself: "to 2".
				const size_t to = lower.find(" to ");
				if (to != std::string::npos && to + 4 < lower.size() && lower[to + 4] >= '1' && lower[to + 4] <= '3' &&
					(to + 5 == lower.size() || !std::isdigit(static_cast<unsigned char>(lower[to + 5]))))
				{
					rate = lower[to + 4] - '0';
				}
			}
			asked = std::max(asked, rate);
		}
		return asked;
	}

	// Takes the booted game's patch file out of PCSX2's archive into the folder
	// where the core looks for patch files (ADR 0025): the core loads it just
	// after this, and applies the groups asked for. Only that one file is
	// copied, for this run; the archive stays where it is.
	void TakePatchFile(std::string_view serial, u32 crc)
	{
		const std::string names[2] = {fmt::format("{}_{:08X}.pnach", serial, crc), fmt::format("{:08X}.pnach", crc)};
		zip_error_t error = {};
		const auto archive = zip_open_managed(s_options.patch_archive.c_str(), ZIP_RDONLY, &error);
		if (!archive)
		{
			s_patch_line = fmt::format("ps2n-host: patches: cannot open {}", s_options.patch_archive);
			return;
		}
		for (const std::string& name : names)
		{
			if (serial.empty() && &name == &names[0])
				continue;
			const std::optional<std::string> text = ReadFileInZipToString(archive.get(), name.c_str());
			if (!text)
				continue;
			const std::string path = Path::Combine(EmuFolders::Patches, name);
			if (!FileSystem::EnsureDirectoryExists(EmuFolders::Patches.c_str(), false) ||
				!FileSystem::WriteStringToFile(path.c_str(), *text))
			{
				s_patch_line = fmt::format("ps2n-host: patches: cannot write {}", path);
				return;
			}

			// The widescreen group is PCSX2's own setting: the group named
			// "Widescreen 16:9", read when the machine started. A frame-rate
			// group has no name PCSX2 knows ("50/60 FPS", "60 FPS"): the one
			// group whose name says FPS is put in the list of enabled patches,
			// which the core reads again here. Where several say FPS they are
			// choices, not one patch ("60 FPS" beside "50 FPS", "Onscreen FPS
			// Display", "Slow Motion (60 fps)"), and none of them is applied.
			std::vector<std::string> groups;
			std::map<std::string, std::string> group_lines; // each group's lines, its note among them
			std::string* current = nullptr;
			for (std::string_view line : StringUtil::SplitString(*text, '\n', true))
			{
				line = StringUtil::StripWhitespace(line);
				if (line.size() > 2 && line.front() == '[' && line.back() == ']')
				{
					std::string group(line.substr(1, line.size() - 2));
					if (std::find(groups.begin(), groups.end(), group) == groups.end())
						groups.push_back(group);
					current = &group_lines[std::move(group)];
				}
				else if (current)
				{
					current->append(line);
					current->push_back('\n');
				}
			}
			std::vector<std::string> applied;
			std::string left_out;
			if (s_options.patch_widescreen && std::find(groups.begin(), groups.end(), "Widescreen 16:9") != groups.end())
				applied.emplace_back("Widescreen 16:9");
			if (s_options.patch_fps)
			{
				std::vector<std::string> fps;
				for (const std::string& group : groups)
				{
					if (StringUtil::toLower(group).find("fps") != std::string::npos)
						fps.push_back(group);
				}
				if (fps.size() == 1)
				{
					{
						std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
						s_settings.AddToStringList("Patches", "Enable", fps.front().c_str());
					}
					VMManager::ReloadPatches(true, true, false, false);
					applied.push_back(fps.front());
				}
				else if (fps.size() > 1)
				{
					left_out = fmt::format("; {} groups say FPS, so none of them is applied: {}", fps.size(),
						StringUtil::JoinString(fps.begin(), fps.end(), ", "));
				}
			}

			// What a group applied asks for in its note is applied with it (the
			// maintainer's answer, ADR 0025): the EE overclock. The core reads
			// it with the game's other settings just after this
			// (VMManager::HandleELFChange), and throws every block away for it,
			// as its recompiler and PS2Native's EE provider both do then.
			std::string overclock;
			int asked = 0;
			for (const std::string& group : applied)
				asked = std::max(asked, OverclockAsked(group_lines[group]));
			if (asked != 0)
			{
				static constexpr int kPercent[] = {100, 130, 180, 300};
				if (s_options.ee_cycle_rate_given)
				{
					overclock = fmt::format("; the EE overclock asked for ({}%) left out: -ee-cycle-rate {} given", kPercent[asked],
						s_options.ee_cycle_rate);
				}
				else
				{
					{
						std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
						s_settings.SetIntValue("EmuCore/Speedhacks", "EECycleRate", asked);
					}
					overclock = fmt::format("; with the EE overclock asked for, {}% (EE cycle rate {})", kPercent[asked], asked);
				}
			}

			s_patch_line = fmt::format("ps2n-host: patches: {} taken from {} for this run; groups applied: {}{}{}", name,
				s_options.patch_archive, applied.empty() ? std::string("none") : StringUtil::JoinString(applied.begin(), applied.end(), ", "),
				left_out, overclock);
			return;
		}
		s_patch_line = fmt::format("ps2n-host: patches: {} has none for {} {:08X}", s_options.patch_archive, serial, crc);
	}
} // namespace

void ps2n::host::OnGameChanged(std::string_view title, std::string_view serial, uint32_t crc)
{
	// A game's folder names its window itself (-title).
	if (s_options.window && s_options.window_title.empty())
		window::SetTitle(title.empty() ? std::string(kWindowTitle) : fmt::format("{} - {}", title, kWindowTitle));
	if (crc != 0 && (s_options.patch_widescreen || s_options.patch_fps) && !s_options.patch_archive.empty())
		TakePatchFile(serial, crc);
}

void ps2n::host::OnInputDeviceConnected(std::string_view identifier)
{
	// Only the first controller, and only when someone is playing. Not at
	// once: the core may be holding its settings. Between two frames.
	if (!s_options.window || identifier != "SDL-0")
		return;

	std::lock_guard lock(s_queue_mutex);
	s_queue.push_back([]() { MapFirstController(); });
}

void ps2n::host::OnGuestFrame()
{
	s_frames++;
	const bool profiling = !s_options.profile_path.empty();
	s_latest_frame_time = std::chrono::steady_clock::now();
	if (s_frames == 1)
	{
		// From the first frame on: the machine exists now.
		s_first_frame_time = s_latest_frame_time;
	}
	if (profiling && s_frames == s_options.profile_from)
	{
		s_profile_start_time = s_latest_frame_time;
		ps2n::host::profile::Enable(true);
	}
	if (!s_options.marks.empty() &&
		(s_frames == 1 || std::find(s_options.marks.begin(), s_options.marks.end(), s_frames) != s_options.marks.end()))
	{
		s_frame_marks.emplace_back(s_frames, std::chrono::duration<double>(s_latest_frame_time - s_host_start).count());
	}
	// A session being written down, or run again (-record, -input): before
	// this frame's input is taken in.
	ps2n::host::session::FrameStart(s_frames);
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

	// A session run again: the controller as the session had it from this frame on.
	ps2n::host::session::ApplyInput(s_frames);

	// What the vector units did during the frame (-vu-report), and the IOP
	// (-iop-report).
	ps2n::host::vureport::Frame(s_frames);
	ps2n::host::iopreport::Frame(s_frames);

	// Never run on with a provider other than the one that was asked for. The
	// VU report's watch stands in front of the VU interpreter when both are
	// asked for, and checks itself.
	const bool vuWatched = !s_options.vu_report_path.empty();
	if ((s_options.ee == EEProvider::Ps2n && Cpu != ps2n::ee::Provider()) || !ps2n::host::SyncTraceInstalled() ||
		!ps2n::host::vureport::Installed() || !ps2n::host::iopreport::Installed() ||
		(s_options.iop_ps2n && ps2n::host::iopreport::Under(ps2n::host::SyncTraceUnder(psxCpu)) != ps2n::iop::Provider()) ||
		(!vuWatched && (s_options.vu_ps2n & 1) && CpuVU0 != ps2n::vu::Interpreter(0)) ||
		(!vuWatched && (s_options.vu_ps2n & 2) && CpuVU1 != ps2n::vu::Interpreter(1)))
		s_provider_replaced = true;

	// PS2Native's VU interpreter stops a VU where it meets what it does not
	// carry out yet; the run stops with it.
	if (((s_options.vu_ps2n & 1) && ps2n::vu::Unsupported(0)) || ((s_options.vu_ps2n & 2) && ps2n::vu::Unsupported(1)))
		s_vu_unsupported = true;

	// So does PS2Native's IOP provider.
	if (s_options.iop_ps2n && ps2n::iop::Unsupported())
		s_iop_unsupported = true;

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

	// A check asked for the picture on display (window-check.ps1).
	if (const u32 shot = s_shot_requested.exchange(0))
		WriteShot(shot);

	// 0 frames: no limit (someone is playing in a window).
	const bool frames_done = s_options.max_frames != 0 && s_frames >= s_options.max_frames;

	if (s_provider_replaced || s_vu_unsupported || s_iop_unsupported || parted || marker || s_time_limit_passed.load() || frames_done ||
		s_close_requested.load())
	{
		if (profiling)
			ps2n::host::profile::Enable(false);
		CaptureFrame();
		CaptureSound();
		s_stop_requested = true;
		VMManager::SetState(VMState::Stopping);
	}
	else if (s_options.frame_every != 0 && s_frames % s_options.frame_every == 0)
	{
		CaptureFrame(true);
		CaptureSound(true);
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

#if defined(PS2N_GAME_MODULE)
// Loads the modules of compiled code beside the host's executable and puts
// their lists together. False, with what went wrong on stderr, when one is
// missing or was not built for this host.
static bool LoadGameModules()
{
	std::wstring directory(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(nullptr, directory.data(), static_cast<DWORD>(directory.size()));
		if (length == 0)
		{
			std::fprintf(stderr, "ps2n-host: cannot find its own executable (error %lu)\n", GetLastError());
			return false;
		}
		if (length < directory.size())
		{
			directory.resize(length);
			break;
		}
		directory.resize(directory.size() * 2);
	}
	directory.erase(directory.find_last_of(L"\\/") + 1);

	for (const wchar_t* file : ps2n::kGameModuleFiles)
	{
		const std::wstring path = directory + file;
		const std::string shown = StringUtil::WideStringToUTF8String(path);
		const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module)
		{
			std::fprintf(stderr, "ps2n-host: cannot load the module of compiled code %s (error %lu)\n", shown.c_str(),
				GetLastError());
			return false;
		}
		const auto entry = reinterpret_cast<ps2n::GameModuleEntry>(GetProcAddress(module, ps2n::kGameModuleEntry));
		if (!entry)
		{
			std::fprintf(stderr, "ps2n-host: %s is not a module of compiled code: it has no %s\n", shown.c_str(),
				ps2n::kGameModuleEntry);
			return false;
		}
		ps2n::GameModuleImages images{};
		entry(&images);
		if (images.version != ps2n::kGameModuleVersion)
		{
			std::fprintf(stderr, "ps2n-host: %s was built for another host (version %u, this host's %u)\n", shown.c_str(),
				images.version, ps2n::kGameModuleVersion);
			return false;
		}
		s_module_images.insert(s_module_images.end(), images.ee, images.ee + images.ee_count);
		s_module_vu_images.insert(s_module_vu_images.end(), images.vu, images.vu + images.vu_count);
		s_module_iop_images.insert(s_module_iop_images.end(), images.iop, images.iop + images.iop_count);
	}

	ps2n_image_count = s_module_images.size();
	ps2n_vu_image_count = s_module_vu_images.size();
	ps2n_iop_image_count = s_module_iop_images.size();
	s_module_images.push_back(nullptr);
	s_module_vu_images.push_back(nullptr);
	s_module_iop_images.push_back(nullptr);
	ps2n_images = s_module_images.data();
	ps2n_vu_images = s_module_vu_images.data();
	ps2n_iop_images = s_module_iop_images.data();
	return true;
}
#endif

// File names are only handled correctly on Windows when they arrive as UTF-16.
int wmain(int argc, wchar_t** argv)
{
#if defined(PS2N_GAME_MODULE)
	if (!LoadGameModules())
		return EXIT_ERROR;
#endif

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
