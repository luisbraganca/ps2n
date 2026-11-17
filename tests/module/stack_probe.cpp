// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stack probe a module defines for itself (__chkstk, runtime/module_entry.cpp), which no test
// program's code calls: a program built as a module is (freestanding, no C runtime, tests/tests.cmake),
// whose functions' locals take from one page to fifty and a bit, run on a thread of its own, whose
// stack Windows commits a page at a time as it grows. Without the probe touching each page in order,
// the deepest of them stops the program (an access violation); with it, the thread's stack grows
// through all of them. Exit code 0 when the sums are right and the stack grew as far as the deepest
// frame, 1 or 2 when not.

#include "runtime/game_module.h"

#include <intrin.h>
#include <stddef.h>

// The lists module_entry.cpp hands the host, empty here, as a module's registry_<kind>.cpp defines them.
extern const ps2n::ee::ImageInfo* const ps2n_images[];
extern const size_t ps2n_image_count;
const ps2n::ee::ImageInfo* const ps2n_images[] = {nullptr};
const size_t ps2n_image_count = 0;
extern const ps2n::vu::ImageInfo* const ps2n_vu_images[];
extern const size_t ps2n_vu_image_count;
const ps2n::vu::ImageInfo* const ps2n_vu_images[] = {nullptr};
const size_t ps2n_vu_image_count = 0;
extern const ps2n::iop::ImageInfo* const ps2n_iop_images[];
extern const size_t ps2n_iop_image_count;
const ps2n::iop::ImageInfo* const ps2n_iop_images[] = {nullptr};
const size_t ps2n_iop_image_count = 0;

extern "C" {
__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);
__declspec(dllimport) void* __stdcall CreateThread(void* attributes, size_t stack, unsigned long(__stdcall* start)(void*),
	void* parameter, unsigned long flags, unsigned long* id);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* handle, unsigned long milliseconds);
__declspec(dllimport) int __stdcall GetExitCodeThread(void* handle, unsigned long* code);
}

namespace
{
	// Reached through a pointer, so that each array is there whole: of an array only touched in
	// place, the compiler keeps what is touched.
	__declspec(noinline) int Touch(volatile char* p, size_t n, int v)
	{
		p[n - 1] = static_cast<char>(v);
		p[0] = static_cast<char>(v);
		return p[0] + p[n - 1];
	}

	template <size_t N>
	__declspec(noinline) int Deep(int n)
	{
		char locals[N];
		return Touch(locals, N, n);
	}

	unsigned long __stdcall Worker(void*)
	{
		// The thread's stack: where it begins, and how far down it is committed (its TEB).
		const unsigned long long base = __readgsqword(8);
		// 2 + 4 + 6 + 8 + 10 = 30, then the deepest once more, from a deeper stack.
		int sum = Deep<1000>(1) + Deep<4100>(2) + Deep<8200>(3) + Deep<205000>(4) + Deep<12288>(5);
		sum += Deep<205000>(sum - 26) - 8;
		const unsigned long long grown = base - __readgsqword(16);
		return sum == 30 ? (grown >= 205000 ? 0 : 2) : 1;
	}
} // namespace

extern "C" void Entry()
{
	unsigned long code = 3;
	// A megabyte reserved (STACK_SIZE_PARAM_IS_A_RESERVATION); committed as the program's header says,
	// a page.
	if (void* thread = CreateThread(nullptr, 1u << 20, Worker, nullptr, 0x10000, nullptr))
	{
		WaitForSingleObject(thread, 0xFFFFFFFF);
		GetExitCodeThread(thread, &code);
	}
	ExitProcess(code);
}
