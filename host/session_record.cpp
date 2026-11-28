// SPDX-License-Identifier: GPL-3.0-or-later

#include "session_record.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/GS.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/Recording/PadData.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"

#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <utility>

namespace ps2n::host::session
{
	namespace
	{
		// How often the registers' fingerprint is written down, in frames.
		constexpr uint32_t kFingerprintEvery = 50;

		// The controller ports written down: port 1, and port 2 where a second
		// player had a controller put in (docs/adr/0064). Each port's lines are
		// called by its word: "pad", "pad2".
		constexpr uint32_t kPorts = 2;
		constexpr const char* kPadWords[kPorts] = {"pad", "pad2"};

		// The bytes PCSX2's input recording keeps of a DualShock 2, in its order
		// (pcsx2/Recording/PadData.cpp): the buttons (pressed is 0), the right
		// stick, the left stick, and the pressure of right, left, up, down,
		// triangle, circle, cross, square, L1, R1, L2 and R2.
		using PadBytes = std::array<u8, 18>;

		std::FILE* s_record = nullptr;
		std::string s_record_path;
		Clock s_record_clock{};
		std::array<PadBytes, kPorts> s_last{};
		std::array<bool, kPorts> s_have_last{};
		std::array<uint32_t, kPorts> s_changes{};
		std::optional<uint32_t> s_connected_at; // the frame a controller was put into port 2 at
		uint32_t s_written_fingerprints = 0;
		uint32_t s_written_reboots = 0;
		// How many of the session's frames the console showed at 50 a second
		// (its PAL modes) and at 60 (every other mode); those before the BIOS
		// has set a mode are neither.
		uint32_t s_frames_at_50 = 0;
		uint32_t s_frames_at_60 = 0;

		bool s_replaying = false;
		// The session run again was played with VU1 on PCSX2's VU thread, where
		// a game's run need not repeat (its record says so: "vu-thread on").
		bool s_replay_vu_thread = false;
		std::string s_replay_path;
		struct Input
		{
			uint32_t frame;
			uint32_t port; // 0 for port 1
			PadBytes bytes;
		};
		std::vector<Input> s_inputs;
		size_t s_next_input = 0;
		std::vector<uint32_t> s_reboots; // the frames the session rebooted the game at, in order
		size_t s_next_reboot = 0;
		std::vector<std::pair<uint32_t, uint32_t>> s_connects; // the frames controllers were put in at, and the ports, in order
		size_t s_next_connect = 0;
		std::map<uint32_t, uint64_t> s_expected;
		uint32_t s_compared = 0;
		std::optional<uint32_t> s_parted_at;
		uint64_t s_parted_expected = 0;
		uint64_t s_parted_found = 0;

		bool ReadPad(uint32_t port, PadBytes* bytes)
		{
			PadBase* pad = Pad::GetPad(static_cast<u8>(port));
			if (!pad || pad->GetType() != Pad::ControllerType::DualShock2)
				return false;
			const u32 buttons = pad->GetButtons();
			(*bytes)[0] = static_cast<u8>((buttons & 0xFF00u) >> 8);
			(*bytes)[1] = static_cast<u8>(buttons & 0xFFu);
			const auto [rx, ry] = pad->GetRawRightAnalog();
			const auto [lx, ly] = pad->GetRawLeftAnalog();
			(*bytes)[2] = rx;
			(*bytes)[3] = ry;
			(*bytes)[4] = lx;
			(*bytes)[5] = ly;
			constexpr u32 kPressures[12] = {PadDualshock2::Inputs::PAD_RIGHT, PadDualshock2::Inputs::PAD_LEFT,
				PadDualshock2::Inputs::PAD_UP, PadDualshock2::Inputs::PAD_DOWN, PadDualshock2::Inputs::PAD_TRIANGLE,
				PadDualshock2::Inputs::PAD_CIRCLE, PadDualshock2::Inputs::PAD_CROSS, PadDualshock2::Inputs::PAD_SQUARE,
				PadDualshock2::Inputs::PAD_L1, PadDualshock2::Inputs::PAD_R1, PadDualshock2::Inputs::PAD_L2,
				PadDualshock2::Inputs::PAD_R2};
			for (size_t i = 0; i < 12; i++)
				(*bytes)[6 + i] = pad->GetRawInput(kPressures[i]);
			return true;
		}

		// The EE's general registers, place and cycle count, and the IOP's: what
		// two runs that are the same machine have alike at every frame.
		uint64_t Fingerprint()
		{
			uint64_t hash = 14695981039346656037ull;
			const auto add = [&hash](const void* data, size_t size) {
				const u8* bytes = static_cast<const u8*>(data);
				for (size_t i = 0; i < size; i++)
					hash = (hash ^ bytes[i]) * 1099511628211ull;
			};
			add(cpuRegs.GPR.r, sizeof(cpuRegs.GPR.r));
			add(&cpuRegs.pc, sizeof(cpuRegs.pc));
			add(&cpuRegs.cycle, sizeof(cpuRegs.cycle));
			add(psxRegs.GPR.r, sizeof(psxRegs.GPR.r));
			add(&psxRegs.pc, sizeof(psxRegs.pc));
			add(&psxRegs.cycle, sizeof(psxRegs.cycle));
			return hash;
		}

		std::string Hex(const PadBytes& bytes)
		{
			std::string text;
			for (const u8 byte : bytes)
				text += fmt::format("{:02X}", byte);
			return text;
		}

		bool ParseHex(std::string_view text, PadBytes* bytes)
		{
			if (text.size() != bytes->size() * 2)
				return false;
			for (size_t i = 0; i < bytes->size(); i++)
			{
				const std::optional<u32> byte = StringUtil::FromChars<u32>(text.substr(i * 2, 2), 16);
				if (!byte)
					return false;
				(*bytes)[i] = static_cast<u8>(*byte);
			}
			return true;
		}
	} // namespace

	std::optional<Clock> ParseClock(const std::string& text)
	{
		Clock clock{};
		char separator = 0;
		if (std::sscanf(text.c_str(), "%d-%d-%d%c%d:%d:%d", &clock[0], &clock[1], &clock[2], &separator, &clock[3], &clock[4],
				&clock[5]) != 7 ||
			(separator != 'T' && separator != ' '))
		{
			return std::nullopt;
		}
		if (clock[0] < 2000 || clock[0] > 2099 || clock[1] < 1 || clock[1] > 12 || clock[2] < 1 || clock[2] > 31 || clock[3] < 0 ||
			clock[3] > 23 || clock[4] < 0 || clock[4] > 59 || clock[5] < 0 || clock[5] > 59)
		{
			return std::nullopt;
		}
		return clock;
	}

	std::string FormatClock(const Clock& clock)
	{
		return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", clock[0], clock[1], clock[2], clock[3], clock[4], clock[5]);
	}

	std::string BiosRoms(const std::string& bios_path)
	{
		std::string found;
		for (const char* extension : {"rom1", "rom2"})
		{
			const std::string beside = bios_path + "." + extension;
			const std::string instead = Path::ReplaceExtension(bios_path, extension);
			if (FileSystem::GetPathFileSize(beside.c_str()) > 0 || FileSystem::GetPathFileSize(instead.c_str()) > 0)
				found += (found.empty() ? "" : ",") + std::string(extension);
		}
		return found.empty() ? std::string("none") : found;
	}

	bool StartRecording(const std::string& directory, const Clock& clock, const std::string& patches,
		std::optional<int> ee_cycle_rate, const std::string& bios_roms, bool packaged)
	{
		s_record_path = Path::Combine(directory, "input.txt");
		s_record = FileSystem::OpenCFile(s_record_path.c_str(), "wb");
		if (!s_record)
		{
			std::fprintf(stderr, "ps2n-host: cannot write %s\n", s_record_path.c_str());
			return false;
		}
		s_record_clock = clock;
		std::fprintf(s_record,
			"# A session in the window, written down to be run again (ps2n-host -record; host/session_record.h).\n"
			"# <frame> pad <18 bytes>: what the controller in port 1 held from that frame on, as PCSX2's input\n"
			"# recording keeps it; <frame> pad2 <18 bytes>, the same of the controller in port 2. <frame> connect 2:\n"
			"# a controller was put into port 2 there, for a second player (empty until then, and throughout where\n"
			"# no such line is). <frame> state <fingerprint>: the EE's and the IOP's registers at the start of\n"
			"# that frame. <frame> reboot: the player rebooted the game there (the menu's Reboot), the machine\n"
			"# reset once that frame's event test was over. The memory cards and the console's NVRAM as they\n"
			"# were when the session began are beside this file. patches: the game's patches it was played\n"
			"# with, with the EE overclock their note asks for; ee-cycle-rate: PCSX2's EE cycle rate, where the\n"
			"# session was given one; bios-roms: which of the images the core loads from beside the BIOS image it\n"
			"# had; packaged: played from a game's folder the wizard made, its own settings file and patch file\n"
			"# beside this file; vu-thread on, at the end: played with VU1 on PCSX2's VU thread, where a run need\n"
			"# not repeat.\n"
			"clock %s\n"
			"patches %s\n",
			FormatClock(clock).c_str(), patches.c_str());
		if (ee_cycle_rate)
			std::fprintf(s_record, "ee-cycle-rate %d\n", *ee_cycle_rate);
		std::fprintf(s_record, "bios-roms %s\n", bios_roms.c_str());
		if (packaged)
			std::fputs("packaged\n", s_record);
		return true;
	}

	bool LoadReplay(const std::string& path, ReplayInfo* info)
	{
		const std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str());
		if (!text)
		{
			std::fprintf(stderr, "ps2n-host: cannot read %s\n", path.c_str());
			return false;
		}

		uint32_t line_number = 0;
		for (std::string_view line : StringUtil::SplitString(*text, '\n', false))
		{
			line_number++;
			if (const size_t hash = line.find('#'); hash != std::string_view::npos)
				line = line.substr(0, hash);
			const std::vector<std::string_view> words = StringUtil::SplitString(StringUtil::StripWhitespace(line), ' ', true);
			if (words.empty())
				continue;

			bool good = false;
			if (words[0] == "clock" && words.size() == 2)
			{
				info->clock = ParseClock(std::string(words[1]));
				good = info->clock.has_value();
			}
			else if (words[0] == "end" && words.size() == 2)
			{
				info->frames = StringUtil::FromChars<u32>(words[1]);
				good = info->frames.has_value();
			}
			else if (words[0] == "patches" && words.size() == 2)
			{
				// Checked by the host, as -patches is.
				info->patches = std::string(words[1]);
				good = true;
			}
			else if (words[0] == "ee-cycle-rate" && words.size() == 2)
			{
				info->ee_cycle_rate = StringUtil::FromChars<s32>(words[1]);
				good = info->ee_cycle_rate.has_value();
			}
			else if (words[0] == "bios-roms" && words.size() == 2)
			{
				// Compared by the host with the images beside the BIOS it is given.
				info->bios_roms = std::string(words[1]);
				good = true;
			}
			else if (words[0] == "packaged" && words.size() == 1)
			{
				info->packaged = true;
				good = true;
			}
			else if (words[0] == "vu-thread" && words.size() == 2)
			{
				// For the report of a run of it again that parts from it.
				s_replay_vu_thread = words[1] == "on";
				good = words[1] == "on" || words[1] == "off";
			}
			else if (words[0] == "frames-a-second" && words.size() == 2)
			{
				// How long the session was, for a game's folder (ps2n-remake): a
				// replay has no use for it.
				good = StringUtil::FromChars<u32>(words[1]).has_value();
			}
			else if (words.size() == 2 && words[1] == "reboot")
			{
				const std::optional<u32> frame = StringUtil::FromChars<u32>(words[0]);
				good = frame.has_value();
				if (good)
					s_reboots.push_back(*frame);
			}
			else if (words.size() == 3)
			{
				const std::optional<u32> frame = StringUtil::FromChars<u32>(words[0]);
				const auto port = std::find(std::begin(kPadWords), std::end(kPadWords), words[1]);
				if (frame && port != std::end(kPadWords))
				{
					PadBytes bytes;
					good = ParseHex(words[2], &bytes);
					if (good)
						s_inputs.push_back({*frame, static_cast<uint32_t>(port - std::begin(kPadWords)), bytes});
				}
				else if (frame && words[1] == "connect")
				{
					// Port 2 only: port 1 always has its controller.
					good = words[2] == "2";
					if (good)
						s_connects.emplace_back(*frame, 1u);
				}
				else if (frame && words[1] == "state")
				{
					const std::optional<u64> fingerprint = StringUtil::FromChars<u64>(words[2], 16);
					good = fingerprint.has_value();
					if (good)
						s_expected[*frame] = *fingerprint;
				}
			}
			if (!good)
			{
				std::fprintf(stderr, "ps2n-host: %s, line %u: not a line of a session written down (host/session_record.h)\n",
					path.c_str(), line_number);
				return false;
			}
		}

		std::stable_sort(s_inputs.begin(), s_inputs.end(), [](const Input& a, const Input& b) { return a.frame < b.frame; });
		std::sort(s_reboots.begin(), s_reboots.end());
		std::sort(s_connects.begin(), s_connects.end());
		s_replay_path = path;
		s_replaying = true;
		return true;
	}

	void FrameStart(uint32_t frame)
	{
		if (s_record)
		{
			// The console's frames a second in the mode the game has it in (the
			// rates of pcsx2/Counters.cpp, GetVerticalFrequency, as the console
			// has them, whatever the user's own frame-rate settings).
			switch (gsVideoMode)
			{
				case GS_VideoMode::Uninitialized:
				case GS_VideoMode::Unknown:
					break;
				case GS_VideoMode::PAL:
				case GS_VideoMode::DVD_PAL:
				case GS_VideoMode::SDTV_576P:
					s_frames_at_50++;
					break;
				default:
					s_frames_at_60++;
					break;
			}

			// Taken in at the previous frame boundary, read by the game since: of
			// each port that has a controller.
			for (uint32_t port = 0; port < kPorts; port++)
			{
				PadBytes now;
				if (ReadPad(port, &now) && (!s_have_last[port] || now != s_last[port]))
				{
					std::fprintf(s_record, "%u %s %s\n", frame - 1, kPadWords[port], Hex(now).c_str());
					s_last[port] = now;
					s_have_last[port] = true;
					s_changes[port]++;
				}
			}
			if (frame % kFingerprintEvery == 0)
			{
				std::fprintf(s_record, "%u state %016llX\n", frame, static_cast<unsigned long long>(Fingerprint()));
				s_written_fingerprints++;
			}
		}
		else if (s_replaying)
		{
			const auto expected = s_expected.find(frame);
			if (expected != s_expected.end() && !s_parted_at)
			{
				const uint64_t found = Fingerprint();
				if (found == expected->second)
				{
					s_compared++;
				}
				else
				{
					s_parted_at = frame;
					s_parted_expected = expected->second;
					s_parted_found = found;
				}
			}
		}
	}

	void ApplyInput(uint32_t frame)
	{
		while (s_replaying && s_next_input < s_inputs.size() && s_inputs[s_next_input].frame <= frame)
		{
			const Input& input = s_inputs[s_next_input];
			// (A port with no DualShock 2 in it has nothing to take them: only a
			// file not written by a session would give it some.)
			const PadBase* pad = Pad::GetPad(static_cast<u8>(input.port));
			if (pad && pad->GetType() == Pad::ControllerType::DualShock2)
				PadData(static_cast<int>(input.port), 0, input.bytes).OverrideActualController();
			s_next_input++;
		}
	}

	void NoteConnect(uint32_t frame, uint32_t port)
	{
		if (!s_record)
			return;
		std::fprintf(s_record, "%u connect %u\n", frame, port + 1);
		if (port == 1 && !s_connected_at)
			s_connected_at = frame;
	}

	std::optional<uint32_t> ConnectAt(uint32_t frame)
	{
		if (!s_replaying || s_next_connect >= s_connects.size() || s_connects[s_next_connect].first > frame)
			return std::nullopt;
		return s_connects[s_next_connect++].second;
	}

	void NoteReboot(uint32_t frame)
	{
		if (!s_record)
			return;
		std::fprintf(s_record, "%u reboot\n", frame);
		s_written_reboots++;
	}

	bool RebootAt(uint32_t frame)
	{
		if (!s_replaying || s_next_reboot >= s_reboots.size() || s_reboots[s_next_reboot] != frame)
			return false;
		s_next_reboot++;
		return true;
	}

	std::vector<std::string> Finish(uint32_t frames)
	{
		std::vector<std::string> lines;
		if (s_record)
		{
			// What most of its frames were shown at: how long the session was
			// in the game's own time (a game's folder counts its profile after
			// one of 3 minutes, docs/adr/0047).
			if (s_frames_at_50 != 0 || s_frames_at_60 != 0)
				std::fprintf(s_record, "frames-a-second %d\n", s_frames_at_50 > s_frames_at_60 ? 50 : 60);
			if (THREAD_VU1)
				std::fputs("vu-thread on\n", s_record);
			std::fprintf(s_record, "end %u\n", frames);
			std::fclose(s_record);
			s_record = nullptr;
			lines.push_back(fmt::format("ps2n-host: the session written down, to be run again: {} changes of the controller in "
										"port 1{} and {} fingerprints of the registers over {} frames{}, the console's clock from {} ({})",
				s_changes[0],
				s_connected_at ? fmt::format(", {} of the one in port 2, put in at frame {},", s_changes[1], *s_connected_at) : std::string(),
				s_written_fingerprints, frames,
				s_written_reboots ? fmt::format(", {} reboots", s_written_reboots) : std::string(), FormatClock(s_record_clock),
				s_record_path));
		}
		if (s_replaying)
		{
			// The controller in port 2, where the session had one put in.
			const std::string second = s_connects.empty() ?
										   std::string() :
										   fmt::format("s, the one in port 2 put in at frame {} as the session had it,", s_connects.front().first);
			if (s_parted_at)
			{
				lines.push_back(fmt::format("ps2n-host: the replay of {}: {} states of the controller{} given back; the run is the "
											"session at {} fingerprints and parts from it at frame {} (fingerprint {:016X}, the "
											"session's {:016X}){}",
					s_replay_path, s_next_input, second, s_compared, *s_parted_at, s_parted_found, s_parted_expected,
					s_replay_vu_thread ? "; the session was played with VU1 on PCSX2's VU thread, where a run need not repeat" :
					THREAD_VU1         ? "; this run has VU1 on PCSX2's VU thread, where a run need not repeat" :
										 ""));
			}
			else
			{
				lines.push_back(fmt::format("ps2n-host: the replay of {}: {} states of the controller{} given back{}; the run is the "
											"session at every one of {} fingerprints compared, of the {} written down",
					s_replay_path, s_next_input, second,
					s_reboots.empty() ? std::string() : fmt::format(", {} of {} reboots", s_next_reboot, s_reboots.size()),
					s_compared, s_expected.size()));
			}
		}
		return lines;
	}
} // namespace ps2n::host::session
