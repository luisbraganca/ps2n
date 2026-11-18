// SPDX-License-Identifier: GPL-3.0-or-later
//
// See disc.h. The disc is opened as PCSX2's game list opens one to read its
// serial (GameList::GetIsoSerialAndCRC): its ISO reader, which picks the
// reader of the image's format and tells the size of its sectors itself, and
// each sector's 2048 bytes of data as PCSX2's own reader of a disc's files
// asks for them (IsoReader::ReadSector, CDVD_MODE_2048).

#include "disc.h"

#include "ps2n/iso9660.h"

#include "CDVD/CDVDcommon.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include <cstdio>

namespace
{
	constexpr int kDone = 0;
	constexpr int kFailed = 1; // as the runtime's EXIT_ERROR
	constexpr int kUsage = 4; // as its EXIT_USAGE

	// A disc image open through PCSX2's ISO reader, for as long as it is kept.
	class OpenDisc
	{
	public:
		~OpenDisc()
		{
			if (m_open)
				DoCDVDclose();
			if (m_locked)
				cdvdUnlock();
		}

		bool Open(const std::string& image, std::string* error)
		{
			Error e;
			if (!cdvdLock(&e))
			{
				*error = e.GetDescription();
				return false;
			}
			m_locked = true;
			CDVD = &CDVDapi_Iso;
			if (!CDVD->open(image, &e))
			{
				*error = e.GetDescription();
				return false;
			}
			m_open = true;
			return true;
		}

		static ps2n::iso::SectorReader Reader()
		{
			return [](uint32_t sector, uint8_t* out) { return DoCDVDreadSector(out, sector, CDVD_MODE_2048) == 0; };
		}

	private:
		bool m_locked = false;
		bool m_open = false;
	};

	int Fail(const std::string& image, const std::string& why)
	{
		std::fprintf(stderr, "ps2n-host: cannot read the disc image %s: %s\n", image.c_str(), why.c_str());
		return kFailed;
	}

	int Executables(const std::string& image)
	{
		OpenDisc disc;
		std::string error;
		if (!disc.Open(image, &error))
			return Fail(image, error);
		const ps2n::iso::SectorReader read = OpenDisc::Reader();
		const std::optional<std::string> boot = ps2n::iso::bootExecutable(read, &error);
		if (!error.empty())
			return Fail(image, error);
		const std::optional<std::vector<std::string>> paths = ps2n::iso::executables(read, &error);
		if (!paths)
			return Fail(image, error);
		for (const std::string& path : *paths)
			std::printf("%s %s\n", boot && path == *boot ? "boot" : "loads", path.c_str());
		return kDone;
	}

	int Extract(const std::string& image, const std::string& folder, std::vector<std::string> names)
	{
		OpenDisc disc;
		std::string error;
		if (!disc.Open(image, &error))
			return Fail(image, error);
		const ps2n::iso::SectorReader read = OpenDisc::Reader();
		if (names.empty())
		{
			const std::optional<std::string> boot = ps2n::iso::bootExecutable(read, &error);
			if (!error.empty())
				return Fail(image, error);
			if (!boot)
				return Fail(image, "the disc names no executable to boot (SYSTEM.CNF's BOOT2)");
			names.push_back(*boot);
		}

		Error e;
		if (!FileSystem::EnsureDirectoryExists(folder.c_str(), true, &e))
		{
			std::fprintf(stderr, "ps2n-host: cannot make the folder %s: %s\n", folder.c_str(), e.GetDescription().c_str());
			return kFailed;
		}
		for (const std::string& name : names)
		{
			const std::optional<ps2n::iso::Entry> entry = ps2n::iso::find(read, name, &error);
			if (!error.empty())
				return Fail(image, error);
			if (!entry || entry->directory)
				return Fail(image, "the disc has no file " + name);
			const std::optional<std::vector<uint8_t>> data = ps2n::iso::readFile(read, *entry, &error);
			if (!data)
				return Fail(image, error);
			const std::string to = Path::Combine(folder, entry->name);
			// Left alone when it is the same, as the recompiler leaves what it
			// writes, so that nothing looks newer than it is.
			const std::optional<std::vector<u8>> before = FileSystem::ReadBinaryFile(to.c_str());
			if ((!before || *before != *data) && !FileSystem::WriteBinaryFile(to.c_str(), data->data(), data->size()))
			{
				std::fprintf(stderr, "ps2n-host: cannot write %s\n", to.c_str());
				return kFailed;
			}
			std::printf("%s %s\n", name.c_str(), to.c_str());
		}
		return kDone;
	}
} // namespace

std::optional<int> ps2n::host::disc::Run(const std::vector<std::string>& arguments)
{
	if (arguments.size() < 2 || (arguments[1] != "-disc-executables" && arguments[1] != "-disc-extract"))
		return std::nullopt;

	// The paths as the system writes them: PCSX2's files take no other on
	// Windows, and the wizard's are Qt's, with forward slashes.
	if (arguments[1] == "-disc-executables")
	{
		if (arguments.size() != 3)
		{
			std::fputs("ps2n-host: -disc-executables takes a disc image, and nothing else\n", stderr);
			return kUsage;
		}
		return Executables(Path::ToNativePath(arguments[2]));
	}

	if (arguments.size() < 4)
	{
		std::fputs("ps2n-host: -disc-extract takes a disc image, a folder, and the paths of files on the disc\n", stderr);
		return kUsage;
	}
	return Extract(Path::ToNativePath(arguments[2]), Path::ToNativePath(arguments[3]),
		std::vector<std::string>(arguments.begin() + 4, arguments.end()));
}
