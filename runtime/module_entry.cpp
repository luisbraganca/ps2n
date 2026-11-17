// SPDX-License-Identifier: GPL-3.0-or-later
//
// The entry of a module of compiled code (runtime/game_module.h): hands the
// host that loads it the lists of the compiled code the module holds. Built
// into each module, which has no C runtime (tests/tests.cmake), and so
// defines here the two things the compiler asks of one.

#include "runtime/game_module.h"

#include <stddef.h>

// The lists the recompiler writes (registry_<kind>.cpp).
extern const ps2n::ee::ImageInfo* const ps2n_images[];
extern const size_t ps2n_image_count;
extern const ps2n::vu::ImageInfo* const ps2n_vu_images[];
extern const size_t ps2n_vu_image_count;
extern const ps2n::iop::ImageInfo* const ps2n_iop_images[];
extern const size_t ps2n_iop_image_count;

extern "C" __declspec(dllexport) void ps2n_game_module(ps2n::GameModuleImages* images)
{
	images->version = ps2n::kGameModuleVersion;
	images->ee = ps2n_images;
	images->ee_count = ps2n_image_count;
	images->vu = ps2n_vu_images;
	images->vu_count = ps2n_vu_image_count;
	images->iop = ps2n_iop_images;
	images->iop_count = ps2n_iop_image_count;
}

// A file of compiled code that does arithmetic on floating point names this
// symbol, for Microsoft's C runtime to set its floating point up; with no C
// runtime, nothing has to be set up, but the name has to be defined. 0x9875
// is the runtime's own value.
extern "C" int _fltused = 0x9875;

// A function whose locals take more than a page of the stack (4 KB) calls
// __chkstk first, with their size in RAX, and then moves the stack pointer
// down by that much itself. Windows commits a thread's stack a page at a
// time, as the page below the last one is touched; a function that went
// further down at once would touch a page that is not there. This touches a
// byte in each page from the caller's stack pointer down to the size asked,
// in order, and changes no register but R10, R11 and the flags, as the
// compiler expects of Microsoft's, which a module does not have. (Killzone's
// code has one such function, docs/app-log.md, "P4.4".)
__asm__(
	".intel_syntax noprefix\n"
	".text\n"
	".globl __chkstk\n"
	".def __chkstk; .scl 2; .type 32; .endef\n"
	"__chkstk:\n"
	"	lea r10, [rsp + 8]\n" // the caller's stack pointer, above the address to return to
	"	mov r11, rax\n" // what is still to be touched below it
	".Lchkstk_page:\n"
	"	cmp r11, 0x1000\n"
	"	jb .Lchkstk_last\n"
	"	sub r10, 0x1000\n"
	"	test byte ptr [r10], al\n"
	"	sub r11, 0x1000\n"
	"	jmp .Lchkstk_page\n"
	".Lchkstk_last:\n"
	"	sub r10, r11\n"
	"	test byte ptr [r10], al\n"
	"	ret\n");
