// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compiled code in a module of its own (docs/phase4-proposal.md, step P4.1).
//
// The compiled code of a game is built into the host, or, with
// PS2N_GAME_MODULE (tests/tests.cmake), into a module of its own, a DLL
// that the host loads when it starts: what a user's computer is to build
// (ADR 0028). In a module, compiled code reaches the machine's state and the
// runtime across the module's boundary, through what the host exports:
//
//  - the runtime's variables and functions that compiled code uses are
//    declared PS2N_API: exported by the host, imported by the module;
//  - PCSX2's variables that it uses are exported by the host under names of
//    PS2Native's own (host/targets.cmake). Below, a module declares them
//    imported and puts them in the place of PCSX2's names, so that the
//    headers of the semantics and the generated code reach them unchanged;
//  - PCSX2's functions that it calls are exported under their own names,
//    and reached through the import library's thunks.
//
// Every header of the semantics that reaches PCSX2's state includes this one
// first. Built into the host, as compiled code has been, it changes nothing.
// This is the prototype that measures what the boundary costs; the
// generated code that needs neither PCSX2's headers nor a standard library
// is step P4.2.
#pragma once

#if defined(_WIN32) && defined(PS2N_MODULE)
#define PS2N_API __declspec(dllimport)
#elif defined(_WIN32) && defined(PS2N_EXPORTS)
#define PS2N_API __declspec(dllexport)
#else
#define PS2N_API
#endif

#if defined(PS2N_MODULE)

#if !defined(_WIN32)
#error "A game module is built on Windows only so far (docs/phase4-proposal.md, P4.1)."
#endif

// Every PCSX2 header the semantics include, before any of PCSX2's names is
// replaced: a header read after the replacement would declare the name it
// replaces again.
#include "COP0.h"
#include "Config.h"
#include "IopBios.h"
#include "IopDma.h"
#include "IopMem.h"
#include "MemoryTypes.h"
#include "R3000A.h"
#include "R5900.h"
#include "VU.h"
#include "Vif.h"
#include "vtlb.h"

// PCSX2's variables, as the host exports them.
extern "C"
{
	__declspec(dllimport) extern cpuRegistersPack ps2n_imp_cpuRegistersPack;
	__declspec(dllimport) extern VURegs ps2n_imp_vuRegs[2];
	__declspec(dllimport) extern u8 ps2n_imp_eeHw[Ps2MemSize::Hardware];
	__declspec(dllimport) extern IopVM_MemoryAllocMess* ps2n_imp_iopMem;
	__declspec(dllimport) extern psxRegisters ps2n_imp_psxRegs;
	__declspec(dllimport) extern Pcsx2Config ps2n_imp_EmuConfig;
}
namespace vtlb_private
{
	extern "C" __declspec(dllimport) MapData ps2n_imp_vtlbdata;
}

// In the place of PCSX2's names. cpuRegs, fpuRegs, VU0, VU1, vif0Regs and
// vif1Regs are references PCSX2's headers bind, in every file that includes
// them, to the variables above, which a module cannot reach by name.
#define cpuRegs (ps2n_imp_cpuRegistersPack.cpuRegs)
#define fpuRegs (ps2n_imp_cpuRegistersPack.fpuRegs)
#define vuRegs ps2n_imp_vuRegs
#define VU0 (ps2n_imp_vuRegs[0])
#define VU1 (ps2n_imp_vuRegs[1])
#define eeHw ps2n_imp_eeHw
#define vif0Regs (*reinterpret_cast<VIFregisters*>(&ps2n_imp_eeHw[0x3800]))
#define vif1Regs (*reinterpret_cast<VIFregisters*>(&ps2n_imp_eeHw[0x3C00]))
#define iopMem ps2n_imp_iopMem
#define psxRegs ps2n_imp_psxRegs
#define EmuConfig ps2n_imp_EmuConfig
#define vtlbdata ps2n_imp_vtlbdata

#endif // PS2N_MODULE
