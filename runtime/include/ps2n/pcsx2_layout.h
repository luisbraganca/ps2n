// SPDX-License-Identifier: GPL-3.0-or-later
//
// What compiled code reaches of PCSX2's core, described by PS2Native itself:
// the layout of the structures it reads and writes, the numbers it uses and
// the types of the functions of the core it calls. Transcribed from PCSX2
// v2.9.189 (pcsx2/R5900.h, VU.h, R3000A.h, MemoryTypes.h, vtlb.h, Vif.h,
// IopBios.h, Config.h, common/FPControl.h; Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+), field for field, with nothing of the core's code.
//
// A game's module is compiled against these instead of PCSX2's headers
// (ps2n/pcsx2.h), with a compiler and PS2Native's own headers and nothing
// else (docs/phase4-proposal.md, step P4.2). The runtime is built with
// PCSX2's headers, and compares everything here with PCSX2's own when it is
// built (runtime/pcsx2_check.cpp: every structure's size and alignment, the
// place and size of every field, every number, the type of every function),
// and, each time it starts, the bits of the settings, which a compiler
// cannot compare. A change to one of PCSX2's structures stops the runtime's
// build: it never reaches a game's module unnoticed.
//
// Everything here is in ps2n::pcsx2, so that the runtime's check can see
// these and PCSX2's side by side.
#pragma once

#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>

// PCSX2's calling convention for its 128-bit values (RETURNS_R128,
// TAKES_R128 in common/SingleRegisterTypes.h): __vectorcall on Windows,
// the platform's own elsewhere.
#if defined(_WIN32)
#define PS2N_VECTORCALL __vectorcall
#else
#define PS2N_VECTORCALL
#endif

namespace ps2n::pcsx2
{
	// ---- the EE's registers (R5900.h) -----------------------------------------

	// A general register, 128 bits. (PCSX2's also has UQ and SQ, the same bits
	// as structures of two 64-bit halves, which compiled code does not use.)
	union GPR_reg
	{
		uint64_t UD[2];
		int64_t SD[2];
		uint32_t UL[4];
		int32_t SL[4];
		uint16_t US[8];
		int16_t SS[8];
		uint8_t UC[16];
		int8_t SC[16];
	};

	// (PCSX2's also names each register.)
	union GPRregs
	{
		GPR_reg r[32];
	};

	// The performance counters. (PCSX2's also names the bits of pccr.)
	union PERFregs
	{
		struct
		{
			union
			{
				uint32_t val;
			} pccr;
			uint32_t pcr0, pcr1, pad;
		} n;
		uint32_t r[4];
	};

	// COP0's registers. (PCSX2's also names the bits of Status.)
	union CP0regs
	{
		struct
		{
			uint32_t Index, Random, EntryLo0, EntryLo1,
				Context, PageMask, Wired, Reserved0,
				BadVAddr, Count, EntryHi, Compare;
			union
			{
				uint32_t val;
			} Status;
			uint32_t Cause, EPC, PRid,
				Config, LLAddr, WatchLO, WatchHI,
				XContext, Reserved1, Reserved2, Debug,
				DEPC, PerfCnt, ErrCtl, CacheErr,
				TagLo, TagHi, ErrorEPC, DESAVE;
		} n;
		uint32_t r[32];
	};

	struct cpuRegisters
	{
		GPRregs GPR;
		GPR_reg HI;
		GPR_reg LO;
		CP0regs CP0;
		uint32_t sa;
		uint32_t IsDelaySlot;
		uint32_t pc;
		uint32_t code;
		PERFregs PERF;
		uint32_t eCycle[32];
		uint64_t sCycle[32];
		uint64_t cycle;
		uint32_t interrupt;
		int branch;
		int opmode;
		uint32_t tempcycles;
		uint32_t dmastall;
		uint32_t pcWriteback;
		uint64_t nextEventCycle;
		uint64_t lastEventCycle;
		uint64_t lastCOP0Cycle;
		uint64_t lastPERFCycle[2];
	};

	union FPRreg
	{
		float f;
		uint32_t UL;
		int32_t SL;
	};

	struct fpuRegisters
	{
		FPRreg fpr[32];
		uint32_t fprc[32];
		FPRreg ACC;
		uint32_t ACCflag;
	};

	// The EE's registers and the FPU's, as the core keeps them together
	// (_cpuRegistersPack, of which cpuRegs and fpuRegs are the two parts).
	struct cpuRegistersPack
	{
		alignas(16) cpuRegisters cpuRegs;
		alignas(16) fpuRegisters fpuRegs;
	};

	// ---- the vector units' registers (VU.h) ---------------------------------------

	// The numbers of VI registers with a meaning of their own.
	enum VURegFlags
	{
		REG_STATUS_FLAG = 16,
		REG_MAC_FLAG = 17,
		REG_CLIP_FLAG = 18,
		REG_ACC_FLAG = 19,
		REG_R = 20,
		REG_I = 21,
		REG_Q = 22,
		REG_P = 23,
		REG_VF0_FLAG = 24,
		REG_TPC = 26,
		REG_CMSAR0 = 27,
		REG_FBRST = 28,
		REG_VPU_STAT = 29,
		REG_CMSAR1 = 31
	};

	// A bit of VURegs::flags: the M bit was met (VUFLAG_MFLAGSET).
	inline constexpr uint32_t kVuFlagMFlagSet = 0x00000002;

	// A VF register. (PCSX2's also has UQ and SQ, as GPR_reg.)
	union VECTOR
	{
		struct
		{
			float x, y, z, w;
		} f;
		struct
		{
			uint32_t x, y, z, w;
		} i;
		float F[4];
		uint64_t UD[2];
		int64_t SD[2];
		uint32_t UL[4];
		int32_t SL[4];
		uint16_t US[8];
		int16_t SS[8];
		uint8_t UC[16];
		int8_t SC[16];
	};

	// A VI register, in sixteen bytes.
	struct REG_VI
	{
		union
		{
			float F;
			int32_t SL;
			uint32_t UL;
			int16_t SS[2];
			uint16_t US[2];
			int8_t SC[4];
			uint8_t UC[4];
		};
		uint32_t padding[3];
	};

	struct fdivPipe
	{
		int enable;
		REG_VI reg;
		uint64_t sCycle;
		uint32_t Cycle;
		uint32_t statusflag;
	};

	struct efuPipe
	{
		int enable;
		REG_VI reg;
		uint64_t sCycle;
		uint32_t Cycle;
	};

	struct fmacPipe
	{
		uint32_t regupper;
		uint32_t reglower;
		int flagreg;
		uint32_t xyzwupper;
		uint32_t xyzwlower;
		uint64_t sCycle;
		uint32_t Cycle;
		uint32_t macflag;
		uint32_t statusflag;
		uint32_t clipflag;
	};

	struct ialuPipe
	{
		int reg;
		uint64_t sCycle;
		uint32_t Cycle;
	};

	struct alignas(16) VURegs
	{
		VECTOR VF[32];
		REG_VI VI[32];
		VECTOR ACC;
		REG_VI q;
		REG_VI p;
		unsigned int idx;
		uint64_t cycle;
		uint32_t flags;
		uint32_t code;
		uint32_t start_pc;
		uint32_t branch;
		uint32_t branchpc;
		uint32_t delaybranchpc;
		bool takedelaybranch;
		uint32_t ebit;
		uint32_t pending_q;
		uint32_t pending_p;
		alignas(16) uint32_t micro_macflags[4];
		alignas(16) uint32_t micro_clipflags[4];
		alignas(16) uint32_t micro_statusflags[4];
		uint32_t macflag;
		uint32_t statusflag;
		uint32_t clipflag;
		int32_t nextBlockCycles; // 32 bits since PCSX2 v2.9.174, four bytes of padding after it
		uint8_t* Mem;
		uint8_t* Micro;
		uint32_t xgkickaddr;
		uint32_t xgkickdiff;
		uint32_t xgkicksizeremaining;
		uint64_t xgkicklastcycle;
		uint32_t xgkickcyclecount;
		uint32_t xgkickenable;
		uint32_t xgkickendpacket;
		uint8_t VIBackupCycles;
		uint32_t VIOldValue;
		uint32_t VIRegNumber;
		fmacPipe fmac[4];
		uint32_t fmacreadpos;
		uint32_t fmacwritepos;
		uint32_t fmaccount;
		fdivPipe fdiv;
		efuPipe efu;
		ialuPipe ialu[4];
		uint32_t ialureadpos;
		uint32_t ialuwritepos;
		uint32_t ialucount;
	};

	// ---- the VIFs' registers (Vif.h) ------------------------------------------------

	// Each in sixteen bytes. (PCSX2's stat, err and cycle are words of named
	// bits.)
	struct VIFregisters
	{
		uint32_t stat;
		uint32_t _pad0[3];
		uint32_t fbrst;
		uint32_t _pad1[3];
		uint32_t err;
		uint32_t _pad2[3];
		uint32_t mark;
		uint32_t _pad3[3];
		uint32_t cycle;
		uint32_t _pad4[3];
		uint32_t mode;
		uint32_t _pad5[3];
		uint32_t num;
		uint32_t _pad6[3];
		uint32_t mask;
		uint32_t _pad7[3];
		uint32_t code;
		uint32_t _pad8[3];
		uint32_t itops;
		uint32_t _pad9[3];
		uint32_t base;
		uint32_t _pad10[3];
		uint32_t ofst;
		uint32_t _pad11[3];
		uint32_t tops;
		uint32_t _pad12[3];
		uint32_t itop;
		uint32_t _pad13[3];
		uint32_t top;
		uint32_t _pad14[3];
		uint32_t mskpath3;
		uint32_t _pad15[3];
		uint32_t r0;
		uint32_t _pad16[3];
		uint32_t r1;
		uint32_t _pad17[3];
		uint32_t r2;
		uint32_t _pad18[3];
		uint32_t r3;
		uint32_t _pad19[3];
		uint32_t c0;
		uint32_t _pad20[3];
		uint32_t c1;
		uint32_t _pad21[3];
		uint32_t c2;
		uint32_t _pad22[3];
		uint32_t c3;
		uint32_t _pad23[3];
		uint32_t offset;
		uint32_t addr;
	};

	// Where each VIF's registers are among the EE's hardware registers (eeHw):
	// vif0Regs and vif1Regs.
	inline constexpr uint32_t kVif0Regs = 0x3800;
	inline constexpr uint32_t kVif1Regs = 0x3C00;

	// ---- memory (MemoryTypes.h, vtlb.h) ----------------------------------------------

	namespace Ps2MemSize
	{
		inline constexpr uint32_t TotalRam = 128 * 1024 * 1024;
		inline constexpr uint32_t Hardware = 64 * 1024;
	} // namespace Ps2MemSize

	// The IOP's memory, as the core allocates it.
	struct IopVM_MemoryAllocMess
	{
		uint8_t Main[Ps2MemSize::TotalRam];
		uint8_t P[64 * 1024];
		uint8_t Sif[0x100];
	};

	// The address table (vtlb_private): for each page of the EE's address
	// space, an entry that is either where the page is in the host's memory,
	// less the page's address, or a handler of the core's.
	inline constexpr uint32_t VTLB_PAGE_BITS = 12;
	inline constexpr uint32_t VTLB_HANDLER_ITEMS = 128;
	inline constexpr uint32_t VTLB_PMAP_ITEMS = 512u * 1024 * 1024 / 4096;

	struct VTLBVirtual
	{
		uintptr_t value;

		// The page is a handler's, not memory.
		bool isHandler(uint32_t vaddr) const { return static_cast<intptr_t>(value + vaddr) < 0; }
		// Where `vaddr` is, for a page that is memory.
		uintptr_t assumePtr(uint32_t vaddr) const { return value + vaddr; }
	};

	struct MapData
	{
		void* RWFT[5][2][VTLB_HANDLER_ITEMS];
		intptr_t pmap[VTLB_PMAP_ITEMS];
		VTLBVirtual* vmap;
		uint32_t* ppmap;
		uintptr_t fastmem_base;
	};

	// ---- the IOP's registers (R3000A.h, IopBios.h) ------------------------------------

	// (PCSX2's also names each register.) hi is r[32], lo r[33].
	union GPRRegs
	{
		uint32_t r[34];
	};

	union CP0Regs
	{
		struct
		{
			uint32_t Index, Random, EntryLo0, EntryLo1,
				Context, PageMask, Wired, Reserved0,
				BadVAddr, Count, EntryHi, Compare,
				Status, Cause, EPC, PRid,
				Config, LLAddr, WatchLO, WatchHI,
				XContext, Reserved1, Reserved2, Reserved3,
				Reserved4, Reserved5, ECC, CacheErr,
				TagLo, TagHi, ErrorEPC, Reserved6;
		} n;
		uint32_t r[32];
	};

	// The GTE's registers. (PCSX2's also names them; compiled code leaves
	// them to the core.)
	union CP2Data
	{
		uint32_t r[32];
	};

	union CP2Ctrl
	{
		uint32_t r[32];
	};

	struct psxRegisters
	{
		GPRRegs GPR;
		CP0Regs CP0;
		CP2Data CP2D;
		CP2Ctrl CP2C;
		uint32_t pc;
		uint32_t code;
		uint64_t cycle;
		uint32_t interrupt;
		uint32_t pcWriteback;
		uint64_t iopNextEventCycle;
		int32_t iopBreak;
		int32_t iopCycleEE;
		uint32_t iopCycleEECarry;
		uint64_t sCycle[32];
		int32_t eCycle[32];
	};

	// A function of the core's for a module's import (irxImportHLE): 1 when
	// it handled the call itself.
	using irxHLE = int (*)();

	// ---- the settings (Config.h, common/FPControl.h) -----------------------------------

	// The host's SSE control register, as a setting.
	struct FPControlRegister
	{
		uint32_t bitmask;
	};

	// Its rounding control (FPControlRegister::SetRoundMode), and the mode
	// that rounds to nearest.
	inline constexpr uint32_t kRoundingControlBits = 3u << 13;
	inline constexpr uint32_t kRoundNearest = 0u << 13;

	// The settings are words of named bits (BITFIELD32), declared here as
	// PCSX2 declares them, in the same order, so that each bit is where
	// PCSX2's is; the runtime checks the ones compiled code reads when it
	// starts (pcsx2_check.cpp).
	struct RecompilerOptions
	{
		union
		{
			uint32_t bitset;
			struct
			{
				bool EnableEE : 1, EnableIOP : 1, EnableVU0 : 1, EnableVU1 : 1;
				bool vu0Overflow : 1, vu0ExtraOverflow : 1, vu0SignOverflow : 1, vu0Underflow : 1;
				bool vu1Overflow : 1, vu1ExtraOverflow : 1, vu1SignOverflow : 1, vu1Underflow : 1;
				bool fpuOverflow : 1, fpuExtraOverflow : 1, fpuFullMode : 1;
				bool EnableEECache : 1;
				bool EnableFastmem : 1;
				bool PauseOnTLBMiss : 1;
			};
		};
	};

	struct CpuOptions
	{
		union
		{
			uint32_t bitset;
			struct
			{
				bool ExtraMemory : 1;
			};
		};
		RecompilerOptions Recompiler;
		FPControlRegister FPUFPCR;
		FPControlRegister FPUDivFPCR;
		FPControlRegister VU0FPCR;
		FPControlRegister VU1FPCR;
	};

	struct GamefixOptions
	{
		union
		{
			uint32_t bitset;
			struct
			{
				bool FpuMulHack : 1, GoemonTlbHack : 1, SoftwareRendererFMVHack : 1, SkipMPEGHack : 1,
					OPHFlagHack : 1, EETimingHack : 1, InstantDMAHack : 1, DMABusyHack : 1,
					GIFFIFOHack : 1, VIFFIFOHack : 1, VIF1StallHack : 1, VuAddSubHack : 1,
					IbitHack : 1, VUSyncHack : 1, VUOverflowHack : 1, XgKickHack : 1,
					BlitInternalFPSHack : 1, FullVU0SyncHack : 1;
			};
		};
	};

	// Where the CPU options and the game fixes are in PCSX2's settings
	// (Pcsx2Config, EmuConfig), which hold much else that compiled code does
	// not read, and are not described here.
	inline constexpr size_t kConfigCpu = 4;
	inline constexpr size_t kConfigGamefixes = 552;

	struct Pcsx2Config
	{
		uint8_t before_cpu[kConfigCpu];
		CpuOptions Cpu;
		uint8_t before_gamefixes[kConfigGamefixes - kConfigCpu - sizeof(CpuOptions)];
		GamefixOptions Gamefixes;
	};

	// ---- the core's functions compiled code calls, by their types ----------------

	template <typename T>
	using vtlb_memRead_t = T(uint32_t mem);
	template <typename T>
	using vtlb_memWrite_t = void(uint32_t mem, T value);
	using vtlb_memRead128_t = __m128i PS2N_VECTORCALL(uint32_t mem);
	using vtlb_memWrite128_t = void PS2N_VECTORCALL(uint32_t mem, __m128i value);
	using COP0_UpdatePCCR_t = void();
	using COP0_DiagnosticPCCR_t = void();
	using WriteCP0Status_t = void(uint32_t value);
	using WriteCP0Config_t = void(uint32_t value);
	using iopEventTest_t = void();
	using iopTestIntc_t = void();
	using iopMemRead8_t = uint8_t(uint32_t mem);
	using iopMemRead16_t = uint16_t(uint32_t mem);
	using iopMemRead32_t = uint32_t(uint32_t mem);
	using iopMemWrite8_t = void(uint32_t mem, uint8_t value);
	using iopMemWrite16_t = void(uint32_t mem, uint16_t value);
	using iopMemWrite32_t = void(uint32_t mem, uint32_t value);
	using psxException_t = void(uint32_t code, uint32_t bd);
} // namespace ps2n::pcsx2
