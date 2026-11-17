// SPDX-License-Identifier: GPL-3.0-or-later
//
// PS2Native's description of PCSX2's structures, compared with PCSX2's own
// (see pcsx2_check.h). A game's module is compiled against the description
// alone (ps2n/pcsx2_layout.h), so a difference here would be one between
// what compiled code and the core take the machine to be. Everything a
// compiler can compare is compared below, when the runtime is built: every
// structure's size and alignment, the place, size and type of every field,
// every number, the type of every function and variable; CheckLayout()
// compares the bits of the settings, when the host starts.

#include "runtime/pcsx2_check.h"

#include "ps2n/pcsx2.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace
{
	namespace m = ps2n::pcsx2;

	// A field of PCSX2's and the description's: the same type, where it is a
	// number or an array of numbers; the same size otherwise (a structure of
	// PCSX2's, described field for field by a structure of its own).
	template <typename A, typename B>
	constexpr bool SameType()
	{
		using a = std::remove_all_extents_t<A>;
		if constexpr (std::is_arithmetic_v<a> || std::is_enum_v<a>)
			return std::is_same_v<A, B>;
		else
			return sizeof(A) == sizeof(B) && std::rank_v<A> == std::rank_v<B> && std::extent_v<A> == std::extent_v<B>;
	}
} // namespace

#define PS2N_SAME_STRUCT(P, M) \
	static_assert(sizeof(P) == sizeof(M), #P ": not the size ps2n/pcsx2_layout.h has"); \
	static_assert(alignof(P) == alignof(M), #P ": not aligned as ps2n/pcsx2_layout.h has it")

#define PS2N_SAME_FIELD(P, M, f) \
	static_assert(offsetof(P, f) == offsetof(M, f), #P "::" #f ": not where ps2n/pcsx2_layout.h has it"); \
	static_assert(SameType<decltype(std::declval<P&>().f), decltype(std::declval<M&>().f)>(), \
		#P "::" #f ": not of the type ps2n/pcsx2_layout.h has")

// ---- the EE's registers ---------------------------------------------------------------

PS2N_SAME_STRUCT(GPR_reg, m::GPR_reg);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, UD);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, SD);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, UL);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, SL);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, US);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, SS);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, UC);
PS2N_SAME_FIELD(GPR_reg, m::GPR_reg, SC);

PS2N_SAME_STRUCT(GPRregs, m::GPRregs);
PS2N_SAME_FIELD(GPRregs, m::GPRregs, r);

PS2N_SAME_STRUCT(PERFregs, m::PERFregs);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, n.pccr);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, n.pccr.val);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, n.pcr0);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, n.pcr1);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, n.pad);
PS2N_SAME_FIELD(PERFregs, m::PERFregs, r);

PS2N_SAME_STRUCT(CP0regs, m::CP0regs);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Index);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Random);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.EntryLo0);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.EntryLo1);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Context);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.PageMask);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Wired);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Reserved0);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.BadVAddr);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Count);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.EntryHi);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Compare);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Status);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Status.val);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Cause);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.EPC);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.PRid);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Config);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.LLAddr);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.WatchLO);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.WatchHI);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.XContext);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Reserved1);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Reserved2);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.Debug);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.DEPC);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.PerfCnt);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.ErrCtl);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.CacheErr);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.TagLo);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.TagHi);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.ErrorEPC);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, n.DESAVE);
PS2N_SAME_FIELD(CP0regs, m::CP0regs, r);

PS2N_SAME_STRUCT(cpuRegisters, m::cpuRegisters);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, GPR);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, HI);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, LO);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, CP0);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, sa);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, IsDelaySlot);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, pc);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, code);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, PERF);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, eCycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, sCycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, cycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, interrupt);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, branch);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, opmode);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, tempcycles);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, dmastall);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, pcWriteback);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, nextEventCycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, lastEventCycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, lastCOP0Cycle);
PS2N_SAME_FIELD(cpuRegisters, m::cpuRegisters, lastPERFCycle);

PS2N_SAME_STRUCT(FPRreg, m::FPRreg);
PS2N_SAME_FIELD(FPRreg, m::FPRreg, f);
PS2N_SAME_FIELD(FPRreg, m::FPRreg, UL);
PS2N_SAME_FIELD(FPRreg, m::FPRreg, SL);

PS2N_SAME_STRUCT(fpuRegisters, m::fpuRegisters);
PS2N_SAME_FIELD(fpuRegisters, m::fpuRegisters, fpr);
PS2N_SAME_FIELD(fpuRegisters, m::fpuRegisters, fprc);
PS2N_SAME_FIELD(fpuRegisters, m::fpuRegisters, ACC);
PS2N_SAME_FIELD(fpuRegisters, m::fpuRegisters, ACCflag);

PS2N_SAME_STRUCT(cpuRegistersPack, m::cpuRegistersPack);
PS2N_SAME_FIELD(cpuRegistersPack, m::cpuRegistersPack, cpuRegs);
PS2N_SAME_FIELD(cpuRegistersPack, m::cpuRegistersPack, fpuRegs);

static_assert(std::is_same_v<decltype(_cpuRegistersPack), cpuRegistersPack>);
static_assert(&cpuRegs == &_cpuRegistersPack.cpuRegs && &fpuRegs == &_cpuRegistersPack.fpuRegs,
	"cpuRegs and fpuRegs: not the two parts of _cpuRegistersPack, as ps2n/pcsx2.h has them");

// ---- the vector units' registers ------------------------------------------------------

namespace
{
	// Two numbers of two enumerations, as numbers.
	constexpr bool SameNumber(long long a, long long b)
	{
		return a == b;
	}
} // namespace
static_assert(SameNumber(m::REG_STATUS_FLAG, REG_STATUS_FLAG) && SameNumber(m::REG_MAC_FLAG, REG_MAC_FLAG) &&
	SameNumber(m::REG_CLIP_FLAG, REG_CLIP_FLAG) && SameNumber(m::REG_ACC_FLAG, REG_ACC_FLAG) &&
	SameNumber(m::REG_R, REG_R) && SameNumber(m::REG_I, REG_I) && SameNumber(m::REG_Q, REG_Q) &&
	SameNumber(m::REG_P, REG_P) && SameNumber(m::REG_VF0_FLAG, REG_VF0_FLAG) && SameNumber(m::REG_TPC, REG_TPC) &&
	SameNumber(m::REG_CMSAR0, REG_CMSAR0) && SameNumber(m::REG_FBRST, REG_FBRST) &&
	SameNumber(m::REG_VPU_STAT, REG_VPU_STAT) && SameNumber(m::REG_CMSAR1, REG_CMSAR1),
	"VURegFlags: not the numbers ps2n/pcsx2_layout.h has");
static_assert(m::kVuFlagMFlagSet == VUFLAG_MFLAGSET);

PS2N_SAME_STRUCT(VECTOR, m::VECTOR);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, f);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, f.x);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, f.y);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, f.z);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, f.w);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, i);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, i.x);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, i.y);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, i.z);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, i.w);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, F);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, UD);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, SD);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, UL);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, SL);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, US);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, SS);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, UC);
PS2N_SAME_FIELD(VECTOR, m::VECTOR, SC);

PS2N_SAME_STRUCT(REG_VI, m::REG_VI);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, F);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, SL);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, UL);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, SS);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, US);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, SC);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, UC);
PS2N_SAME_FIELD(REG_VI, m::REG_VI, padding);

PS2N_SAME_STRUCT(fdivPipe, m::fdivPipe);
PS2N_SAME_FIELD(fdivPipe, m::fdivPipe, enable);
PS2N_SAME_FIELD(fdivPipe, m::fdivPipe, reg);
PS2N_SAME_FIELD(fdivPipe, m::fdivPipe, sCycle);
PS2N_SAME_FIELD(fdivPipe, m::fdivPipe, Cycle);
PS2N_SAME_FIELD(fdivPipe, m::fdivPipe, statusflag);

PS2N_SAME_STRUCT(efuPipe, m::efuPipe);
PS2N_SAME_FIELD(efuPipe, m::efuPipe, enable);
PS2N_SAME_FIELD(efuPipe, m::efuPipe, reg);
PS2N_SAME_FIELD(efuPipe, m::efuPipe, sCycle);
PS2N_SAME_FIELD(efuPipe, m::efuPipe, Cycle);

PS2N_SAME_STRUCT(fmacPipe, m::fmacPipe);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, regupper);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, reglower);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, flagreg);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, xyzwupper);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, xyzwlower);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, sCycle);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, Cycle);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, macflag);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, statusflag);
PS2N_SAME_FIELD(fmacPipe, m::fmacPipe, clipflag);

PS2N_SAME_STRUCT(ialuPipe, m::ialuPipe);
PS2N_SAME_FIELD(ialuPipe, m::ialuPipe, reg);
PS2N_SAME_FIELD(ialuPipe, m::ialuPipe, sCycle);
PS2N_SAME_FIELD(ialuPipe, m::ialuPipe, Cycle);

PS2N_SAME_STRUCT(VURegs, m::VURegs);
PS2N_SAME_FIELD(VURegs, m::VURegs, VF);
PS2N_SAME_FIELD(VURegs, m::VURegs, VI);
PS2N_SAME_FIELD(VURegs, m::VURegs, ACC);
PS2N_SAME_FIELD(VURegs, m::VURegs, q);
PS2N_SAME_FIELD(VURegs, m::VURegs, p);
PS2N_SAME_FIELD(VURegs, m::VURegs, idx);
PS2N_SAME_FIELD(VURegs, m::VURegs, cycle);
PS2N_SAME_FIELD(VURegs, m::VURegs, flags);
PS2N_SAME_FIELD(VURegs, m::VURegs, code);
PS2N_SAME_FIELD(VURegs, m::VURegs, start_pc);
PS2N_SAME_FIELD(VURegs, m::VURegs, branch);
PS2N_SAME_FIELD(VURegs, m::VURegs, branchpc);
PS2N_SAME_FIELD(VURegs, m::VURegs, delaybranchpc);
PS2N_SAME_FIELD(VURegs, m::VURegs, takedelaybranch);
PS2N_SAME_FIELD(VURegs, m::VURegs, ebit);
PS2N_SAME_FIELD(VURegs, m::VURegs, pending_q);
PS2N_SAME_FIELD(VURegs, m::VURegs, pending_p);
PS2N_SAME_FIELD(VURegs, m::VURegs, micro_macflags);
PS2N_SAME_FIELD(VURegs, m::VURegs, micro_clipflags);
PS2N_SAME_FIELD(VURegs, m::VURegs, micro_statusflags);
PS2N_SAME_FIELD(VURegs, m::VURegs, macflag);
PS2N_SAME_FIELD(VURegs, m::VURegs, statusflag);
PS2N_SAME_FIELD(VURegs, m::VURegs, clipflag);
PS2N_SAME_FIELD(VURegs, m::VURegs, nextBlockCycles);
PS2N_SAME_FIELD(VURegs, m::VURegs, Mem);
PS2N_SAME_FIELD(VURegs, m::VURegs, Micro);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkickaddr);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkickdiff);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkicksizeremaining);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkicklastcycle);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkickcyclecount);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkickenable);
PS2N_SAME_FIELD(VURegs, m::VURegs, xgkickendpacket);
PS2N_SAME_FIELD(VURegs, m::VURegs, VIBackupCycles);
PS2N_SAME_FIELD(VURegs, m::VURegs, VIOldValue);
PS2N_SAME_FIELD(VURegs, m::VURegs, VIRegNumber);
PS2N_SAME_FIELD(VURegs, m::VURegs, fmac);
PS2N_SAME_FIELD(VURegs, m::VURegs, fmacreadpos);
PS2N_SAME_FIELD(VURegs, m::VURegs, fmacwritepos);
PS2N_SAME_FIELD(VURegs, m::VURegs, fmaccount);
PS2N_SAME_FIELD(VURegs, m::VURegs, fdiv);
PS2N_SAME_FIELD(VURegs, m::VURegs, efu);
PS2N_SAME_FIELD(VURegs, m::VURegs, ialu);
PS2N_SAME_FIELD(VURegs, m::VURegs, ialureadpos);
PS2N_SAME_FIELD(VURegs, m::VURegs, ialuwritepos);
PS2N_SAME_FIELD(VURegs, m::VURegs, ialucount);

static_assert(std::is_same_v<decltype(vuRegs), VURegs[2]>);
static_assert(&VU0 == &vuRegs[0] && &VU1 == &vuRegs[1], "VU0 and VU1: not vuRegs[0] and vuRegs[1], as ps2n/pcsx2.h has them");

// ---- the VIFs' registers --------------------------------------------------------------

PS2N_SAME_STRUCT(VIFregisters, m::VIFregisters);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, stat);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, fbrst);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, err);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, mark);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, cycle);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, mode);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, num);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, mask);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, code);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, itops);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, base);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, ofst);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, tops);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, itop);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, top);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, mskpath3);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, r0);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, r1);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, r2);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, r3);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, c0);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, c1);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, c2);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, c3);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, offset);
PS2N_SAME_FIELD(VIFregisters, m::VIFregisters, addr);

// ---- memory ---------------------------------------------------------------------------

static_assert(m::Ps2MemSize::TotalRam == Ps2MemSize::TotalRam && m::Ps2MemSize::Hardware == Ps2MemSize::Hardware);
static_assert(std::is_same_v<decltype(eeHw), uint8_t[m::Ps2MemSize::Hardware]>);

PS2N_SAME_STRUCT(IopVM_MemoryAllocMess, m::IopVM_MemoryAllocMess);
PS2N_SAME_FIELD(IopVM_MemoryAllocMess, m::IopVM_MemoryAllocMess, Main);
PS2N_SAME_FIELD(IopVM_MemoryAllocMess, m::IopVM_MemoryAllocMess, P);
PS2N_SAME_FIELD(IopVM_MemoryAllocMess, m::IopVM_MemoryAllocMess, Sif);
static_assert(std::is_same_v<decltype(iopMem), IopVM_MemoryAllocMess*>);

static_assert(m::VTLB_PAGE_BITS == vtlb_private::VTLB_PAGE_BITS &&
	m::VTLB_HANDLER_ITEMS == vtlb_private::VTLB_HANDLER_ITEMS &&
	m::VTLB_PMAP_ITEMS == vtlb_private::VTLB_PMAP_ITEMS);
// One word, which isHandler() and assumePtr() read as PCSX2's do.
PS2N_SAME_STRUCT(vtlb_private::VTLBVirtual, m::VTLBVirtual);
static_assert(sizeof(vtlb_private::VTLBVirtual) == sizeof(uptr));
PS2N_SAME_STRUCT(vtlb_private::VTLBPhysical, intptr_t);

PS2N_SAME_STRUCT(vtlb_private::MapData, m::MapData);
PS2N_SAME_FIELD(vtlb_private::MapData, m::MapData, RWFT);
PS2N_SAME_FIELD(vtlb_private::MapData, m::MapData, pmap);
PS2N_SAME_FIELD(vtlb_private::MapData, m::MapData, vmap);
PS2N_SAME_FIELD(vtlb_private::MapData, m::MapData, ppmap);
PS2N_SAME_FIELD(vtlb_private::MapData, m::MapData, fastmem_base);
static_assert(std::is_same_v<decltype(vtlb_private::vtlbdata), vtlb_private::MapData>);
static_assert(std::is_same_v<decltype(vtlb_private::MapData::vmap), vtlb_private::VTLBVirtual*>);

// ---- the IOP's registers --------------------------------------------------------------

PS2N_SAME_STRUCT(GPRRegs, m::GPRRegs);
PS2N_SAME_FIELD(GPRRegs, m::GPRRegs, r);

PS2N_SAME_STRUCT(CP0Regs, m::CP0Regs);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Index);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Random);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.EntryLo0);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.EntryLo1);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Context);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.PageMask);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Wired);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved0);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.BadVAddr);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Count);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.EntryHi);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Compare);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Status);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Cause);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.EPC);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.PRid);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Config);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.LLAddr);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.WatchLO);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.WatchHI);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.XContext);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved1);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved2);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved3);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved4);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved5);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.ECC);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.CacheErr);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.TagLo);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.TagHi);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.ErrorEPC);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, n.Reserved6);
PS2N_SAME_FIELD(CP0Regs, m::CP0Regs, r);

PS2N_SAME_STRUCT(CP2Data, m::CP2Data);
PS2N_SAME_FIELD(CP2Data, m::CP2Data, r);
PS2N_SAME_STRUCT(CP2Ctrl, m::CP2Ctrl);
PS2N_SAME_FIELD(CP2Ctrl, m::CP2Ctrl, r);

PS2N_SAME_STRUCT(psxRegisters, m::psxRegisters);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, GPR);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, CP0);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, CP2D);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, CP2C);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, pc);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, code);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, cycle);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, interrupt);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, pcWriteback);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, iopNextEventCycle);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, iopBreak);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, iopCycleEE);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, iopCycleEECarry);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, sCycle);
PS2N_SAME_FIELD(psxRegisters, m::psxRegisters, eCycle);
static_assert(std::is_same_v<decltype(psxRegs), psxRegisters>);

static_assert(std::is_same_v<irxHLE, m::irxHLE>);

// ---- the settings ---------------------------------------------------------------------

PS2N_SAME_STRUCT(FPControlRegister, m::FPControlRegister);
PS2N_SAME_FIELD(FPControlRegister, m::FPControlRegister, bitmask);
static_assert(m::kRoundingControlBits == FPControlRegister::ROUNDING_CONTROL_BITS);
static_assert(m::kRoundNearest == (static_cast<u32>(FPRoundMode::Nearest) << FPControlRegister::ROUNDING_CONTROL_SHIFT));

PS2N_SAME_STRUCT(Pcsx2Config::RecompilerOptions, m::RecompilerOptions);
PS2N_SAME_FIELD(Pcsx2Config::RecompilerOptions, m::RecompilerOptions, bitset);

PS2N_SAME_STRUCT(Pcsx2Config::CpuOptions, m::CpuOptions);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, bitset);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, Recompiler);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, FPUFPCR);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, FPUDivFPCR);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, VU0FPCR);
PS2N_SAME_FIELD(Pcsx2Config::CpuOptions, m::CpuOptions, VU1FPCR);

PS2N_SAME_STRUCT(Pcsx2Config::GamefixOptions, m::GamefixOptions);
PS2N_SAME_FIELD(Pcsx2Config::GamefixOptions, m::GamefixOptions, bitset);

// PCSX2's settings hold much else besides (strings, among them), so they
// are not a structure whose fields' places the language defines; clang
// gives them all the same.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(Pcsx2Config, Cpu) == m::kConfigCpu, "Pcsx2Config::Cpu: not where ps2n/pcsx2_layout.h has it (kConfigCpu)");
static_assert(offsetof(Pcsx2Config, Gamefixes) == m::kConfigGamefixes,
	"Pcsx2Config::Gamefixes: not where ps2n/pcsx2_layout.h has it (kConfigGamefixes)");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
static_assert(offsetof(m::Pcsx2Config, Cpu) == m::kConfigCpu && offsetof(m::Pcsx2Config, Gamefixes) == m::kConfigGamefixes);
static_assert(std::is_same_v<decltype(EmuConfig), Pcsx2Config>);

// ---- the core's functions -------------------------------------------------------------

static_assert(std::is_same_v<decltype(vtlb_memRead<mem32_t>), m::vtlb_memRead_t<uint32_t>>);
static_assert(std::is_same_v<decltype(vtlb_memRead<mem64_t>), m::vtlb_memRead_t<uint64_t>>);
static_assert(std::is_same_v<decltype(vtlb_memWrite<mem32_t>), m::vtlb_memWrite_t<uint32_t>>);
static_assert(std::is_same_v<decltype(vtlb_memWrite<mem64_t>), m::vtlb_memWrite_t<uint64_t>>);
static_assert(std::is_same_v<decltype(vtlb_memRead128), m::vtlb_memRead128_t>);
static_assert(std::is_same_v<decltype(vtlb_memWrite128), m::vtlb_memWrite128_t>);
static_assert(std::is_same_v<decltype(COP0_UpdatePCCR), m::COP0_UpdatePCCR_t>);
static_assert(std::is_same_v<decltype(COP0_DiagnosticPCCR), m::COP0_DiagnosticPCCR_t>);
static_assert(std::is_same_v<decltype(WriteCP0Status), m::WriteCP0Status_t>);
static_assert(std::is_same_v<decltype(WriteCP0Config), m::WriteCP0Config_t>);
static_assert(std::is_same_v<decltype(iopEventTest), m::iopEventTest_t>);
static_assert(std::is_same_v<decltype(iopTestIntc), m::iopTestIntc_t>);
static_assert(std::is_same_v<decltype(iopMemRead8), m::iopMemRead8_t>);
static_assert(std::is_same_v<decltype(iopMemRead16), m::iopMemRead16_t>);
static_assert(std::is_same_v<decltype(iopMemRead32), m::iopMemRead32_t>);
static_assert(std::is_same_v<decltype(iopMemWrite8), m::iopMemWrite8_t>);
static_assert(std::is_same_v<decltype(iopMemWrite16), m::iopMemWrite16_t>);
static_assert(std::is_same_v<decltype(iopMemWrite32), m::iopMemWrite32_t>);
static_assert(std::is_same_v<decltype(psxException), m::psxException_t>);

// ---- what only a run can compare ------------------------------------------------------

namespace
{
	// A bit of a word of settings, set by its name in PCSX2's structure and
	// in the description: the same bit.
	template <typename P, typename M>
	void SameBit(std::string& failures, const char* name, void (*setP)(P&), void (*setM)(M&))
	{
		P p;
		p.bitset = 0;
		setP(p);
		M d{};
		d.bitset = 0;
		setM(d);
		if (p.bitset != d.bitset)
		{
			failures += failures.empty() ? "" : ", ";
			failures += name;
		}
	}
} // namespace

#define PS2N_SAME_BIT(P, M, f) \
	SameBit<P, M>(failures, #P "::" #f, [](P& p) { p.f = true; }, [](M& d) { d.f = true; })

std::string ps2n::pcsx2::CheckLayout()
{
	std::string failures;

	// The settings compiled code reads (ps2n/ops_fpu.h, ps2n/ops_vu0.h).
	// (Here, in ps2n::pcsx2, PCSX2's own names need their "::".)
	PS2N_SAME_BIT(::Pcsx2Config::RecompilerOptions, m::RecompilerOptions, vu0Overflow);
	PS2N_SAME_BIT(::Pcsx2Config::RecompilerOptions, m::RecompilerOptions, vu0ExtraOverflow);
	PS2N_SAME_BIT(::Pcsx2Config::RecompilerOptions, m::RecompilerOptions, vu0SignOverflow);
	PS2N_SAME_BIT(::Pcsx2Config::RecompilerOptions, m::RecompilerOptions, fpuOverflow);
	PS2N_SAME_BIT(::Pcsx2Config::RecompilerOptions, m::RecompilerOptions, fpuExtraOverflow);
	PS2N_SAME_BIT(::Pcsx2Config::GamefixOptions, m::GamefixOptions, FpuMulHack);
	PS2N_SAME_BIT(::Pcsx2Config::GamefixOptions, m::GamefixOptions, VuAddSubHack);
	PS2N_SAME_BIT(::Pcsx2Config::GamefixOptions, m::GamefixOptions, IbitHack);

	// The two VIFs' registers, among the hardware registers.
	if (reinterpret_cast<const uint8_t*>(&vif0Regs) != &eeHw[m::kVif0Regs])
		failures += failures.empty() ? "vif0Regs" : ", vif0Regs";
	if (reinterpret_cast<const uint8_t*>(&vif1Regs) != &eeHw[m::kVif1Regs])
		failures += failures.empty() ? "vif1Regs" : ", vif1Regs";

	return failures;
}
