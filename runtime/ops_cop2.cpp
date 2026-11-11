// SPDX-License-Identifier: GPL-3.0-or-later
//
// CFC2 and CTC2. ps2n/ops_cop2.h says which behavior each case reproduces and
// why; the comments here only name the side followed.

#include "ps2n/ops_cop2.h"

#include "ps2n/ee_state.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/VUmicro.h"

// Defined in pcsx2/VU0.cpp; PCSX2 declares them in no header.
void vu0Sync(); // runs VU0 until it has caught up with the EE
void _vu0WaitMicro(); // runs VU0's micro program until it ends or stops at an M bit

void ps2n::ee::ops::CFC2(uint32_t rt, uint32_t fs, bool interlock)
{
	vu0Sync();
	if (interlock)
		_vu0FinishMicro();

	if (rt == 0)
		return;

	// Bits 0 to 63 of the general register; bits 64 to 127 stay.
	GPR_reg& reg = cpuRegs.GPR.r[rt];
	if (fs == 0)
		reg.UD[0] = 0;
	else if (fs == REG_R)
		reg.UD[0] = VU0.VI[REG_R].UL & 0x7FFFFFu; // the recompiler
	else if (fs >= REG_STATUS_FLAG)
		reg.SD[0] = VU0.VI[fs].SL;
	else
		reg.UD[0] = VU0.VI[fs].US[0]; // the recompiler and the console
}

void ps2n::ee::ops::CTC2(uint32_t rt, uint32_t fs, bool interlock)
{
	vu0Sync();
	if (interlock)
		_vu0WaitMicro();

	if (fs == 0)
		return;

	const u32 value = cpuRegs.GPR.r[rt].UL[0];
	switch (fs)
	{
		case REG_MAC_FLAG:
		case REG_TPC:
		case REG_VPU_STAT:
			// Read-only.
			break;

		case REG_R:
			VU0.VI[REG_R].UL = (value & 0x7FFFFFu) | 0x3F800000u;
			break;

		case REG_STATUS_FLAG:
		{
			// The recompiler and the console: the sticky flags are written, the
			// current ones stay.
			u32& status = VU0.VI[REG_STATUS_FLAG].UL;
			status = (status & 0x3Fu) | (value & 0xFC0u);

			// PCSX2's VU recompiler keeps four copies of a micro program's status
			// flags in a layout of its own (mVUallocSFLAGd, x86/microVU_Alloc.inl).
			// The EE recompiler rewrites them here, so this does.
			const u32 micro = ((status >> 3) & 0x18u) | ((status << 11) & 0x1800u) | ((status << 14) & 0x3CF0000u);
			for (u32& copy : VU0.micro_statusflags)
				copy = micro;
			break;
		}

		case REG_CLIP_FLAG:
			// The interpreter: the copy its VU0 macro functions work on.
			VU0.clipflag = value;
			VU0.VI[REG_CLIP_FLAG].UL = value;
			break;

		case REG_FBRST:
			// The interpreter and the console: stored first, then the resets,
			// each of which clears its VU's half of the register again.
			VU0.VI[REG_FBRST].UL = value & 0x0C0Cu;
			if (value & 0x002u)
				vu0ResetRegs();
			if (value & 0x200u)
				vu1ResetRegs();
			break;

		case REG_CMSAR1:
			// Starts a VU1 micro program. The recompiler: all 32 bits of the
			// value are the address, so that -1 means "where VU1 stopped".
			vu1Finish(true);
			vu1ExecMicro(value);
			break;

		default:
			if (fs < REG_STATUS_FLAG)
				VU0.VI[fs].US[0] = static_cast<u16>(value); // the recompiler and the console
			else
				VU0.VI[fs].UL = value;
			break;
	}
}
