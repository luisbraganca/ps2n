// SPDX-License-Identifier: GPL-3.0-or-later
//
// PCSX2 on Linux makes the GS's memory under one name for every process
// (GSAllocateWrappedMemory in pcsx2/GS/GS.cpp: shm_open("/GS.mem", O_CREAT |
// O_EXCL), and shm_unlink at once), and stops at an assertion when the name
// is taken: two runtimes setting their machines up at the same moment
// collide, as tests run side by side did (6 of 534 on 2026-11-19). Its other
// shared memory has the process's number in its name
// (HostSys::GetFileMappingName, common/Linux/LnxHostSys.cpp); this gives the
// GS's the same. shm_open and shm_unlink are defined here, in the
// executable, which the linker binds PCSX2's calls to; every other name goes
// to the C library's unchanged.

#if defined(__linux__)

#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace
{
	constexpr char kGsMemory[] = "/GS.mem";

	// "/GS.mem" with this process's number after it; any other name as it is.
	std::string Named(const char* name)
	{
		if (name && std::strcmp(name, kGsMemory) == 0)
			return std::string(kGsMemory) + "." + std::to_string(static_cast<unsigned long>(getpid()));
		return name ? std::string(name) : std::string();
	}
} // namespace

extern "C" int shm_open(const char* name, int flags, mode_t mode)
{
	using Function = int (*)(const char*, int, mode_t);
	static const Function c_library = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "shm_open"));
	return c_library(Named(name).c_str(), flags, mode);
}

extern "C" int shm_unlink(const char* name)
{
	using Function = int (*)(const char*);
	static const Function c_library = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "shm_unlink"));
	return c_library(Named(name).c_str());
}

#endif
