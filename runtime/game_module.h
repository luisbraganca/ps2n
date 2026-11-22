// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a module of compiled code gives the host that loads it
// (docs/phase4-proposal.md, steps P4.1 and P4.2). With PS2N_GAME_MODULE the
// compiled code is not linked into the host but built into modules of its
// own, DLLs (shared objects on Linux) beside the host's executable, one for
// each kind of code, which
// the host loads when it starts. A module reaches the machine and the
// runtime through what the host exports (runtime/include/ps2n/module.h); the
// host reaches a module's compiled code through the one function each
// exports, which hands it the lists the recompiler wrote for it (its
// registry), which the host was linked with before.
//
// Compiled into the modules too, so it needs nothing but the compiler's own
// headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ps2n::ee
{
	struct ImageInfo;
}
namespace ps2n::vu
{
	struct ImageInfo;
}
namespace ps2n::iop
{
	struct ImageInfo;
}

namespace ps2n
{
	// Changes whenever what a module and the host share changes: 2, the
	// machine's state as PS2Native describes it (ps2n/pcsx2_layout.h); 3, what
	// the recompiler decided of COP2 instructions (ee::ImageInfo::cop2, ADR
	// 0046); 4, where compiled micro code reads its VIF's TOP and ITOP and
	// FBRST (vu::Ctx, ADR 0051).
	inline constexpr uint32_t kGameModuleVersion = 4;

	// The lists of the compiled code a module holds, each ending with a null
	// entry, as the recompiler writes them.
	struct GameModuleImages
	{
		uint32_t version;
		const ee::ImageInfo* const* ee;
		size_t ee_count;
		const vu::ImageInfo* const* vu;
		size_t vu_count;
		const iop::ImageInfo* const* iop;
		size_t iop_count;
	};

	// The function a module exports, by this name.
	using GameModuleEntry = void (*)(GameModuleImages* images);
	inline constexpr char kGameModuleEntry[] = "ps2n_game_module";

	// The modules, beside the host's executable, in the order their lists are
	// put together: the programs' EE code, the BIOS's, the micro code, the
	// IOP's code.
#if defined(_WIN32)
	inline constexpr const wchar_t* kGameModuleFiles[] = {L"ps2n-game.dll", L"ps2n-bios.dll", L"ps2n-vu.dll", L"ps2n-iop.dll"};
#else
	inline constexpr const char* kGameModuleFiles[] = {"ps2n-game.so", "ps2n-bios.so", "ps2n-vu.so", "ps2n-iop.so"};
#endif
} // namespace ps2n
