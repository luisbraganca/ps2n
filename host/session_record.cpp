// SPDX-License-Identifier: GPL-3.0-or-later

#include "session_record.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
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
#include <map>
#include <utility>

namespace ps2n::host::session
{
	namespace
	{
		// How often the registers' fingerprint is written down, in frames.
		constexpr uint32_t kFingerprintEvery = 50;

		// The bytes PCSX2's input recording keeps of a DualShock 2, in its order
		// (pcsx2/Recording/PadData.cpp): the buttons (pressed is 0), the right
		// stick, the left stick, and the pressure of right, left, up, down,
		// triangle, circle, cross, square, L1, R1, L2 and R2.
		using PadBytes = std::array<u8, 18>;

		std::FILE* s_record = nullptr;
		std::string s_record_path;
		Clock s_record_clock{};
		PadBytes s_last{};
		bool s_have_last = false;
		uint32_t s_changes = 0;
		uint32_t s_written_fingerprints = 0;

		bool s_replaying = false;
		std::string s_replay_path;
		std::vector<std::pair<uint32_t, PadBytes>> s_inputs;
		size_t s_next_input = 0;
		std::map<uint32_t, uint64_t> s_expected;
		uint32_t s_compared = 0;
		std::optional<uint32_t> s_parted_at;
		uint64_t s_parted_expected = 0;
		uint64_t s_parted_found = 0;

		bool ReadPad(PadBytes* bytes)
		{
			PadBase* pad = Pad::GetPad(0);
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

	bool StartRecording(const std::string& directory, const Clock& clock)
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
			"# recording keeps it. <frame> state <fingerprint>: the EE's and the IOP's registers at the start of\n"
			"# that frame. The memory cards and the console's NVRAM as they were when the session began are\n"
			"# beside this file.\n"
			"clock %s\n",
			FormatClock(clock).c_str());
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
			else if (words.size() == 3)
			{
				const std::optional<u32> frame = StringUtil::FromChars<u32>(words[0]);
				if (frame && words[1] == "pad")
				{
					PadBytes bytes;
					good = ParseHex(words[2], &bytes);
					if (good)
						s_inputs.emplace_back(*frame, bytes);
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

		std::stable_sort(s_inputs.begin(), s_inputs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		s_replay_path = path;
		s_replaying = true;
		return true;
	}

	void FrameStart(uint32_t frame)
	{
		if (s_record)
		{
			// Taken in at the previous frame boundary, read by the game since.
			PadBytes now;
			if (ReadPad(&now) && (!s_have_last || now != s_last))
			{
				std::fprintf(s_record, "%u pad %s\n", frame - 1, Hex(now).c_str());
				s_last = now;
				s_have_last = true;
				s_changes++;
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
		while (s_replaying && s_next_input < s_inputs.size() && s_inputs[s_next_input].first <= frame)
		{
			const std::array<u8, 18>& bytes = s_inputs[s_next_input].second;
			PadData(0, 0, bytes).OverrideActualController();
			s_next_input++;
		}
	}

	std::vector<std::string> Finish(uint32_t frames)
	{
		std::vector<std::string> lines;
		if (s_record)
		{
			std::fprintf(s_record, "end %u\n", frames);
			std::fclose(s_record);
			s_record = nullptr;
			lines.push_back(fmt::format("ps2n-host: the session written down, to be run again: {} changes of the controller in "
										"port 1 and {} fingerprints of the registers over {} frames, the console's clock from {} ({})",
				s_changes, s_written_fingerprints, frames, FormatClock(s_record_clock), s_record_path));
		}
		if (s_replaying)
		{
			if (s_parted_at)
			{
				lines.push_back(fmt::format("ps2n-host: the replay of {}: {} states of the controller given back; the run is the "
											"session at {} fingerprints and parts from it at frame {} (fingerprint {:016X}, the "
											"session's {:016X})",
					s_replay_path, s_next_input, s_compared, *s_parted_at, s_parted_found, s_parted_expected));
			}
			else
			{
				lines.push_back(fmt::format("ps2n-host: the replay of {}: {} states of the controller given back; the run is the "
											"session at every one of {} fingerprints compared, of the {} written down",
					s_replay_path, s_next_input, s_compared, s_expected.size()));
			}
		}
		return lines;
	}
} // namespace ps2n::host::session
