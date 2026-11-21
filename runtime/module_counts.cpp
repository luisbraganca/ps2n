// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built into a module of compiled code made to count what of its code runs
// (docs/adr/0045): its sources compiled with clang's -fprofile-generate, the
// module linked with LLVM's profile runtime and this. A game's folder makes
// such modules once, runs a session of its user's again with them, and
// compiles its modules again with the counts (wizard/remake_main.cpp).
//
// The host writes the counts when the run is over (ps2n-host -module-counts):
// a module has no C runtime of its own to start the profile runtime with or to
// write a file at exit with. So this keeps the profile runtime's own start out,
// hands the host the counts as LLVM's raw profile, a piece at a time, and gives
// the profile runtime, on Windows, the little it asks of a C runtime, which a
// module does not have: on Linux the process's C library gives it.

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define PS2N_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define PS2N_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C" {
// Defined, it keeps the profile runtime's start (InstrProfilingRuntime), which
// would register a write of the counts at exit, out of the module.
int __llvm_profile_runtime = 0;

// The profile runtime's own writer of a raw profile, which its file and its
// buffer are written with (compiler-rt/lib/profile/InstrProfilingInternal.h, as
// LLVM 22.1.3 has it: see there when LLVM changes). Given its reader of what
// was counted at calls through a pointer, it writes that too, which its
// buffer leaves out, and without which llvm-profdata takes the raw profile of
// a function that has such calls for one cut short.
struct ProfDataIOVec
{
	const void* Data;
	size_t ElmSize;
	size_t NumElm;
	int UseZeroPadding;
};
struct ProfDataWriter;
typedef uint32_t (*WriterCallback)(ProfDataWriter* writer, ProfDataIOVec* pieces, uint32_t count);
struct ProfDataWriter
{
	WriterCallback Write;
	void* WriterCtx;
};
struct VPDataReaderType;
int lprofWriteData(ProfDataWriter* writer, VPDataReaderType* reader, int skip_names);
VPDataReaderType* lprofGetVPDataReader();
}

namespace
{
	// The host's: each piece of the raw profile in turn, `data` null for as
	// many bytes of zeros.
	using Put = void (*)(void* context, const void* data, size_t size);
	struct Host
	{
		Put put;
		void* context;
	};

	uint32_t Write(ProfDataWriter* writer, ProfDataIOVec* pieces, uint32_t count)
	{
		const Host* host = static_cast<const Host*>(writer->WriterCtx);
		for (uint32_t i = 0; i < count; i++)
			host->put(host->context, pieces[i].Data, pieces[i].ElmSize * pieces[i].NumElm);
		return 0;
	}
} // namespace

// What the module counted, as LLVM's raw profile (what llvm-profdata merges),
// given to the host a piece at a time. 0 when it is all given.
PS2N_MODULE_EXPORT int ps2n_module_counts_write(Put put, void* context)
{
	Host host{put, context};
	ProfDataWriter writer{Write, &host};
	return lprofWriteData(&writer, lprofGetVPDataReader(), 0);
}

#if defined(_WIN32)

extern "C" {

// What the profile runtime asks of Microsoft's C runtime. Memory for the
// counts of the targets of calls through a pointer: from an arena in the
// module, zeroed as every page of it starts, never given back (the run is
// short, and what it counts is kept to its end). Out of it, the profile
// runtime does without (it counts no more targets).
alignas(16) static unsigned char s_arena[64u << 20];
static size_t s_arena_used = 0;

__attribute__((no_builtin)) void* calloc(size_t count, size_t size)
{
	if (size != 0 && count > (sizeof(s_arena) / size))
		return nullptr;
	const size_t bytes = (count * size + 15) & ~static_cast<size_t>(15);
	const size_t at = __atomic_fetch_add(&s_arena_used, bytes, __ATOMIC_RELAXED);
	if (at + bytes > sizeof(s_arena))
		return nullptr;
	return s_arena + at;
}

__attribute__((no_builtin)) void* malloc(size_t size)
{
	return calloc(1, size);
}

__attribute__((no_builtin)) void free(void*)
{
}

__attribute__((no_builtin)) void* memcpy(void* to, const void* from, size_t size)
{
	unsigned char* d = static_cast<unsigned char*>(to);
	const unsigned char* s = static_cast<const unsigned char*>(from);
	for (size_t i = 0; i < size; i++)
		d[i] = s[i];
	return to;
}

__attribute__((no_builtin)) void* memset(void* to, int value, size_t size)
{
	unsigned char* d = static_cast<unsigned char*>(to);
	for (size_t i = 0; i < size; i++)
		d[i] = static_cast<unsigned char>(value);
	return to;
}

// The profile runtime is built with Microsoft's stack checks: a cookie, and
// its check, which a module has no use for.
uintptr_t __security_cookie = 0x2B992DDFA232ull;
void __security_check_cookie(uintptr_t)
{
}

// What it reads of the environment (the most targets counted at a call
// through a pointer, LLVM_VP_MAX_NUM_VALS_PER_SITE): nothing, its default.
char* getenv(const char*)
{
	return nullptr;
}

int atoi(const char* text)
{
	int value = 0;
	while (*text >= '0' && *text <= '9')
		value = value * 10 + (*text++ - '0');
	return value;
}

// Its warnings (fprintf to stderr, as Microsoft's C runtime has them): none.
void* __acrt_iob_func(unsigned)
{
	return nullptr;
}

int __stdio_common_vfprintf(unsigned long long, void*, const char*, void*, char*)
{
	return 0;
}

} // extern "C"

#endif // _WIN32
