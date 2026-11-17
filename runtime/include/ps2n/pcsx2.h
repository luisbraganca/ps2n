// SPDX-License-Identifier: GPL-3.0-or-later
//
// PCSX2's names, as the instruction semantics and generated code use them:
// the machine's state (cpuRegs, fpuRegs, VU0, VU1, psxRegs, iopMem, the
// address table, the settings), its types, and the core's functions they
// call. Every header of the semantics that reaches the core includes this
// one, and nothing of PCSX2's itself.
//
// In the runtime, built with PCSX2's headers, they are PCSX2's own.
//
// In a game's module (PS2N_MODULE), which is compiled with PS2Native's own
// headers and nothing else (docs/phase4-proposal.md, step P4.2), they are
// PS2Native's (ps2n/pcsx2_layout.h, which the runtime checks against
// PCSX2's), and the core's variables and functions are the runtime's
// executable's, imported: each declared under PCSX2's own symbol, the name
// its compiler gives it, which the executable exports (tests/tests.cmake).
// cpuRegs, fpuRegs, VU0, VU1, vif0Regs and vif1Regs are references in
// PCSX2, which a module cannot bind to an import without code that runs
// when it is loaded; here they are macros.
#pragma once

#include "ps2n/pcsx2_layout.h"

#if !defined(PS2N_MODULE)

// R5900.h first: it and R3000A.h each define the fields of an instruction
// (_Rs_, _PC_ and the like) for their processor, unless the other has, and
// a file of the runtime's that includes this one is the EE's more often
// than not. Nothing of PS2Native's uses them.
#include "R5900.h"

#include "COP0.h"
#include "Config.h"
#include "IopBios.h"
#include "IopDma.h"
#include "IopMem.h"
#include "MemoryTypes.h"
#include "R3000A.h"
#include "VU.h"
#include "Vif.h"
#include "vtlb.h"

#else // PS2N_MODULE

#if !defined(_WIN32)
#error "A game's module is built on Windows only so far (docs/phase4-proposal.md, P4.7 for Linux)."
#endif

// ---- types and numbers ---------------------------------------------------------------

using mem8_t = uint8_t;
using mem16_t = uint16_t;
using mem32_t = uint32_t;
using mem64_t = uint64_t;

using ps2n::pcsx2::irxHLE;
using ps2n::pcsx2::REG_VI;
using ps2n::pcsx2::VECTOR;
using ps2n::pcsx2::VURegs;

using ps2n::pcsx2::REG_CLIP_FLAG;
using ps2n::pcsx2::REG_FBRST;
using ps2n::pcsx2::REG_I;
using ps2n::pcsx2::REG_MAC_FLAG;
using ps2n::pcsx2::REG_P;
using ps2n::pcsx2::REG_Q;
using ps2n::pcsx2::REG_R;
using ps2n::pcsx2::REG_STATUS_FLAG;
using ps2n::pcsx2::REG_TPC;
using ps2n::pcsx2::REG_VPU_STAT;

#define VUFLAG_MFLAGSET (ps2n::pcsx2::kVuFlagMFlagSet)

namespace vtlb_private
{
	using ps2n::pcsx2::MapData;
	using ps2n::pcsx2::VTLB_PAGE_BITS;
	using ps2n::pcsx2::VTLBVirtual;
} // namespace vtlb_private

// A 128-bit value in an SSE register, and how PCSX2 hands one over
// (common/SingleRegisterTypes.h).
using r128 = __m128i;
#define RETURNS_R128 r128 PS2N_VECTORCALL
#define TAKES_R128 PS2N_VECTORCALL

[[gnu::always_inline]] inline r128 r128_load(const void* ptr)
{
	return _mm_load_si128(static_cast<const r128*>(ptr));
}
[[gnu::always_inline]] inline void r128_store(void* ptr, r128 value)
{
	_mm_store_si128(static_cast<r128*>(ptr), value);
}
[[gnu::always_inline]] inline void r128_store_unaligned(void* ptr, r128 value)
{
	_mm_storeu_si128(static_cast<r128*>(ptr), value);
}
[[gnu::always_inline]] inline uint64_t r128_to_u64(r128 value)
{
	return static_cast<uint64_t>(_mm_cvtsi128_si64(value));
}

// ---- the core's variables, imported ------------------------------------------------

__declspec(dllimport) extern ps2n::pcsx2::cpuRegistersPack _cpuRegistersPack __asm__("?_cpuRegistersPack@@3UcpuRegistersPack@@A");
__declspec(dllimport) extern ps2n::pcsx2::VURegs vuRegs[2] __asm__("?vuRegs@@3PAUVURegs@@A");
__declspec(dllimport) extern uint8_t eeHw[ps2n::pcsx2::Ps2MemSize::Hardware] __asm__("?eeHw@@3PAEA");
__declspec(dllimport) extern ps2n::pcsx2::IopVM_MemoryAllocMess* iopMem __asm__("?iopMem@@3PEAUIopVM_MemoryAllocMess@@EA");
__declspec(dllimport) extern ps2n::pcsx2::psxRegisters psxRegs __asm__("?psxRegs@@3UpsxRegisters@@A");
__declspec(dllimport) extern ps2n::pcsx2::Pcsx2Config EmuConfig __asm__("?EmuConfig@@3UPcsx2Config@@A");
namespace vtlb_private
{
	__declspec(dllimport) extern ps2n::pcsx2::MapData vtlbdata __asm__("?vtlbdata@vtlb_private@@3UMapData@1@A");
} // namespace vtlb_private

#define cpuRegs (_cpuRegistersPack.cpuRegs)
#define fpuRegs (_cpuRegistersPack.fpuRegs)
#define VU0 (vuRegs[0])
#define VU1 (vuRegs[1])
#define vif0Regs (*reinterpret_cast<ps2n::pcsx2::VIFregisters*>(&eeHw[ps2n::pcsx2::kVif0Regs]))
#define vif1Regs (*reinterpret_cast<ps2n::pcsx2::VIFregisters*>(&eeHw[ps2n::pcsx2::kVif1Regs]))

// ---- the core's functions, imported -------------------------------------------------
// Declared as PCSX2 declares them, so that each has PCSX2's symbol; their
// types are the ones the runtime checks (ps2n/pcsx2_layout.h).

template <typename T>
__declspec(dllimport) T vtlb_memRead(uint32_t mem);
template <typename T>
__declspec(dllimport) void vtlb_memWrite(uint32_t mem, T value);
__declspec(dllimport) __m128i PS2N_VECTORCALL vtlb_memRead128(uint32_t mem);
__declspec(dllimport) void PS2N_VECTORCALL vtlb_memWrite128(uint32_t mem, __m128i value);
__declspec(dllimport) void COP0_UpdatePCCR();
__declspec(dllimport) void COP0_DiagnosticPCCR();
__declspec(dllimport) void WriteCP0Status(uint32_t value);
__declspec(dllimport) void WriteCP0Config(uint32_t value);
__declspec(dllimport) void iopEventTest();
__declspec(dllimport) void iopTestIntc();
__declspec(dllimport) uint8_t iopMemRead8(uint32_t mem);
__declspec(dllimport) uint16_t iopMemRead16(uint32_t mem);
__declspec(dllimport) uint32_t iopMemRead32(uint32_t mem);
__declspec(dllimport) void iopMemWrite8(uint32_t mem, uint8_t value);
__declspec(dllimport) void iopMemWrite16(uint32_t mem, uint16_t value);
__declspec(dllimport) void iopMemWrite32(uint32_t mem, uint32_t value);
__declspec(dllimport) void psxException(uint32_t code, uint32_t bd);

static_assert(__is_same(decltype(vtlb_memRead<uint32_t>), ps2n::pcsx2::vtlb_memRead_t<uint32_t>));
static_assert(__is_same(decltype(vtlb_memRead<uint64_t>), ps2n::pcsx2::vtlb_memRead_t<uint64_t>));
static_assert(__is_same(decltype(vtlb_memWrite<uint32_t>), ps2n::pcsx2::vtlb_memWrite_t<uint32_t>));
static_assert(__is_same(decltype(vtlb_memWrite<uint64_t>), ps2n::pcsx2::vtlb_memWrite_t<uint64_t>));
static_assert(__is_same(decltype(vtlb_memRead128), ps2n::pcsx2::vtlb_memRead128_t));
static_assert(__is_same(decltype(vtlb_memWrite128), ps2n::pcsx2::vtlb_memWrite128_t));
static_assert(__is_same(decltype(COP0_UpdatePCCR), ps2n::pcsx2::COP0_UpdatePCCR_t));
static_assert(__is_same(decltype(COP0_DiagnosticPCCR), ps2n::pcsx2::COP0_DiagnosticPCCR_t));
static_assert(__is_same(decltype(WriteCP0Status), ps2n::pcsx2::WriteCP0Status_t));
static_assert(__is_same(decltype(WriteCP0Config), ps2n::pcsx2::WriteCP0Config_t));
static_assert(__is_same(decltype(iopEventTest), ps2n::pcsx2::iopEventTest_t));
static_assert(__is_same(decltype(iopTestIntc), ps2n::pcsx2::iopTestIntc_t));
static_assert(__is_same(decltype(iopMemRead8), ps2n::pcsx2::iopMemRead8_t));
static_assert(__is_same(decltype(iopMemRead16), ps2n::pcsx2::iopMemRead16_t));
static_assert(__is_same(decltype(iopMemRead32), ps2n::pcsx2::iopMemRead32_t));
static_assert(__is_same(decltype(iopMemWrite8), ps2n::pcsx2::iopMemWrite8_t));
static_assert(__is_same(decltype(iopMemWrite16), ps2n::pcsx2::iopMemWrite16_t));
static_assert(__is_same(decltype(iopMemWrite32), ps2n::pcsx2::iopMemWrite32_t));
static_assert(__is_same(decltype(psxException), ps2n::pcsx2::psxException_t));

#endif // PS2N_MODULE
