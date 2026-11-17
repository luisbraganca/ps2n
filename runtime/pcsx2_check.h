// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's description of PCSX2's structures (ps2n/pcsx2_layout.h), which
// a game's module is compiled against, compared with PCSX2's own: all of it
// that a compiler can compare when the runtime is built (pcsx2_check.cpp),
// and the rest, the bits of the settings, here.
#pragma once

#include <string>

namespace ps2n::pcsx2
{
	// What of the description differs from PCSX2's, one name after another;
	// empty when nothing does. The host asks before anything else, and
	// refuses to go on otherwise.
	std::string CheckLayout();
} // namespace ps2n::pcsx2
