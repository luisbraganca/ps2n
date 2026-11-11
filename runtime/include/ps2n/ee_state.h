// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Emotion Engine's registers, as PS2Native's instruction semantics see
// them.
//
// There is no state of PS2Native's own here: these are accessors over the
// register structures of the PCSX2 core (`cpuRegs`, `fpuRegs`), which the
// core's hardware model, its scheduler and the BIOS kernel's exception
// handling all read and write. Statically compiled code, the fallback
// interpreter and PCSX2's own functions therefore always agree on where a
// register is.
//
// PCSX2's R5900.h is included directly rather than mirrored: it is small and
// self-contained, and a change to it upstream then shows as a compile error
// or a failing test here, never as a silent layout mismatch.
#pragma once

#include "R5900.h"

#include <cstdint>

namespace ps2n::ee
{
	// Register numbers for generated code ("ADDIU(SP, SP, -32)").
	namespace regs
	{
		enum : uint32_t
		{
			ZERO = 0, AT, V0, V1, A0, A1, A2, A3,
			T0, T1, T2, T3, T4, T5, T6, T7,
			S0, S1, S2, S3, S4, S5, S6, S7,
			T8, T9, K0, K1, GP, SP, FP, RA,
		};
	} // namespace regs

	// General-purpose registers are 128 bits wide. Ordinary instructions see
	// bits 0-63 (UD[0]); only the multimedia instructions, LQ/SQ and the
	// quadword moves touch bits 64-127 (UD[1]).
	inline uint64_t ru64(uint32_t r) { return cpuRegs.GPR.r[r].UD[0]; }
	inline int64_t rs64(uint32_t r) { return cpuRegs.GPR.r[r].SD[0]; }
	inline uint32_t ru32(uint32_t r) { return cpuRegs.GPR.r[r].UL[0]; }
	inline int32_t rs32(uint32_t r) { return cpuRegs.GPR.r[r].SL[0]; }
	inline uint64_t rhi64(uint32_t r) { return cpuRegs.GPR.r[r].UD[1]; }

	// Writes to $zero are discarded. With a constant register number the test
	// folds away.
	inline void setU64(uint32_t r, uint64_t v)
	{
		if (r)
			cpuRegs.GPR.r[r].UD[0] = v;
	}
	// A 32-bit result is sign-extended into bits 32-63, as the EE does.
	inline void setS32(uint32_t r, int32_t v)
	{
		if (r)
			cpuRegs.GPR.r[r].SD[0] = v;
	}
	inline void setU32(uint32_t r, uint32_t v) { setS32(r, static_cast<int32_t>(v)); }
	inline void set128(uint32_t r, uint64_t lo, uint64_t hi)
	{
		if (r)
		{
			cpuRegs.GPR.r[r].UD[0] = lo;
			cpuRegs.GPR.r[r].UD[1] = hi;
		}
	}

	inline uint64_t sext32(uint32_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); }
} // namespace ps2n::ee
