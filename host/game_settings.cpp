// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_settings.h"

#include "common/SettingsInterface.h"
#include "common/StringUtil.h"
#include "common/ZipHelpers.h"

#include "pcsx2/Config.h"
#include "pcsx2/VMManager.h"

#include "fmt/format.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ps2n::game
{
	void SetDefaults(SettingsInterface& si)
	{
		VMManager::SetDefaultSettings(si, true, true, true, true, true);

		si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::VK));
		si.SetFloatValue("EmuCore/GS", "upscale_multiplier", 3.0f);

		// The game's films, made for the console's own picture, keep their
		// proportions in the middle of the wide one (the maintainer's choice,
		// G15). 4:3 and not "Auto 4:3/3:2", which the renderer turns back into
		// the patch's own 16:9 (CalculateDrawDstRect in GSRenderer.cpp).
		si.SetBoolValue("EmuCore", "EnableWideScreenPatches", true);
		si.SetStringValue("EmuCore/GS", "FMVAspectRatioSwitch", "4:3");

		// The picture 16:9 for every game, at the maintainer's word (2026-11-18, ADR 0032), in the place of
		// PCSX2's "Auto 4:3/3:2", which only a patch that asks for 16:9 makes wide
		// (ApplyPatchSettingOverrides in Patch.cpp): a game drawn wide without one is shown wide too.
		si.SetStringValue("EmuCore/GS", "AspectRatio", "16:9");

		si.SetIntValue("EmuCore/GS", "OsdMessagesPos", 0);
		si.SetIntValue("EmuCore/GS", "OsdPerformancePos", 0);
		for (const char* name : {"OsdShowSpeed", "OsdShowFPS", "OsdShowVPS", "OsdShowCPU", "OsdShowGPU", "OsdShowGPUDebug",
				 "OsdShowGPUStats", "OsdShowGPUShadersStats", "OsdShowResolution", "OsdShowGSStats", "OsdShowIndicators",
				 "OsdShowSettings", "OsdshowPatches", "OsdShowInputs", "OsdShowFrameTimes", "OsdShowVersion",
				 "OsdShowHardwareInfo", "OsdShowVideoCapture", "OsdShowInputRec", "OsdShowTextureReplacements"})
		{
			si.SetBoolValue("EmuCore/GS", name, false);
		}

		si.ClearSection("Hotkeys");
	}

	std::optional<PatchFile> ReadPatchFile(const std::string& archive, std::string_view serial, u32 crc, std::string* error)
	{
		const std::string names[2] = {fmt::format("{}_{:08X}.pnach", serial, crc), fmt::format("{:08X}.pnach", crc)};
		zip_error_t zip_error = {};
		const auto zip = zip_open_managed(archive.c_str(), ZIP_RDONLY, &zip_error);
		if (!zip)
		{
			if (error)
				*error = fmt::format("cannot open {}", archive);
			return std::nullopt;
		}
		for (const std::string& name : names)
		{
			if (serial.empty() && &name == &names[0])
				continue;
			if (std::optional<std::string> text = ReadFileInZipToString(zip.get(), name.c_str()))
				return ParsePatchFile(name, std::move(*text));
		}
		return std::nullopt;
	}

	PatchFile ParsePatchFile(std::string name, std::string text)
	{
		PatchFile file;
		file.name = std::move(name);
		file.text = std::move(text);
		std::string* current = nullptr;
		for (std::string_view line : StringUtil::SplitString(file.text, '\n', true))
		{
			line = StringUtil::StripWhitespace(line);
			if (line.size() > 2 && line.front() == '[' && line.back() == ']')
			{
				std::string group(line.substr(1, line.size() - 2));
				if (std::find(file.groups.begin(), file.groups.end(), group) == file.groups.end())
					file.groups.push_back(group);
				current = &file.group_lines[std::move(group)];
			}
			else if (current)
			{
				current->append(line);
				current->push_back('\n');
			}
		}
		return file;
	}

	std::optional<std::string> FrameRateGroup(const PatchFile& file, std::vector<std::string>* fps_groups)
	{
		std::vector<std::string> fps;
		for (const std::string& group : file.groups)
		{
			if (StringUtil::toLower(group).find("fps") != std::string::npos)
				fps.push_back(group);
		}
		std::optional<std::string> chosen;
		if (fps.size() == 1)
			chosen = fps.front();
		if (fps_groups)
			*fps_groups = std::move(fps);
		return chosen;
	}

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
} // namespace ps2n::game
