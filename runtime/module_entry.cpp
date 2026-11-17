// SPDX-License-Identifier: GPL-3.0-or-later
//
// The entry of a module of compiled code (runtime/game_module.h): hands the
// host that loads it the lists of the compiled code the module holds. Built
// into each module, which has no C runtime (tests/tests.cmake), and so
// defines here the one thing the compiler asks of one.

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
