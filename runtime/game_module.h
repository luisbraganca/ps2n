// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a game module gives the host that loads it (docs/phase4-proposal.md,
// step P4.1). With PS2N_GAME_MODULE the compiled code is not linked into
// the host but built into a module of its own, a DLL beside the host's
// executable, which the host loads when it starts. The module reaches the
// machine and the runtime through what the host exports
// (runtime/include/ps2n/module.h); the host reaches the module's compiled
// code through the one function the module exports, which hands it the
// lists the recompiler wrote (registry.cpp) that the host was linked with
// before.
#pragma once

#include <cstddef>
#include <cstdint>

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
	// Changes whenever what a module and the host share changes.
	inline constexpr uint32_t kGameModuleVersion = 1;

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

	// The module's file, beside the host's executable.
	inline constexpr wchar_t kGameModuleFile[] = L"ps2n-game.dll";
} // namespace ps2n
