// SPDX-License-Identifier: GPL-3.0-or-later
//
// See ps2n/vu_mvu.h. Each function here is microVU's of the same name, from
// PCSX2 v2.9.170 (Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+), with the
// host code it generates left out; the order of its steps is kept, because
// several of them read what the one before left.

#include "ps2n/vu_mvu.h"

#include <algorithm>

namespace ps2n::vu::mvu {
namespace {

constexpr uint32_t kIbit = 1u << 31;
constexpr uint32_t kEbit = 1u << 30;
constexpr uint32_t kMbit = 1u << 29;
constexpr uint32_t kDbit = 1u << 28;
constexpr uint32_t kTbit = 1u << 27;

// microVU's settings that are compile-time constants (microVU_Misc.h).
constexpr bool doSFlagInsts = true;
constexpr bool doMFlagInsts = true;
constexpr bool doCFlagInsts = true;
constexpr bool doBranchInDelaySlot = true;
constexpr bool doConstProp = false;
constexpr bool doDBitHandling = false;
constexpr bool noFlagOpts = false;

// microTempRegInfo
struct TempRegInfo
{
	RegCycles VF[2]; // [0] the upper instruction's result, [1] the lower's
	uint8_t VFreg[2] = {0, 0};
	uint8_t VI = 0;
	uint8_t VIreg = 0;
	uint8_t q = 0;
	uint8_t p = 0;
	uint8_t r = 0;
	uint8_t xgkick = 0;
};

uint8_t optimizeReg(uint8_t rState) { return (rState == 1) ? 0 : rState; }
uint8_t calcCycles(uint8_t reg, uint8_t x) { return (reg > x) ? static_cast<uint8_t>(reg - x) : 0; }
uint8_t tCycles(uint8_t dest, uint8_t src) { return std::max(dest, src); }

class Analyzer
{
public:
	Analyzer(int vu, const uint32_t* micro, uint32_t memSize, const Settings& settings, InfoArray& infos, RangeSink* ranges)
		: vu_(vu)
		, micro_(micro)
		, memSize_(memSize)
		, progMemMask_(memSize / 4 - 1)
		, settings_(settings)
		, infos_(infos)
		, ranges_(ranges)
	{
		if (infos_.info.size() != memSize / 8)
			infos_.info.assign(memSize / 8, OpInfo{});
	}

	Block compile(uint32_t startPC, const PipeState& pState);

private:
	// ---- microVU's macros ------------------------------------------------------------
	bool isVU0() const { return vu_ == 0; }
	bool isVU1() const { return vu_ != 0; }
	uint32_t curI() const { return micro_[iPC]; }
	void setCode() { code = curI(); }
	void incPC(int x)
	{
		iPC = (iPC + x) & progMemMask_;
		code = curI();
	}
	void incPC2(int x) { iPC = (iPC + x) & progMemMask_; }
	uint32_t xPC() const { return (iPC / 2) * 8; }
	OpInfo& info() { return infos_.info[iPC / 2]; } // mVUinfo
	uint8_t& stall() { return info().stall; } // mVUstall
	UpperInfo& up() { return info().uOp; } // mVUup
	LowerInfo& low() { return info().lOp; } // mVUlow
	FlagInst& sFLAG() { return info().sFlag; }
	FlagInst& mFLAG() { return info().mFlag; }
	FlagInst& cFLAG() { return info().cFlag; }
	bool isEvilBlock() const { return blockState.blockType == 2; }
	bool status() const { return (regs.needExactMatch & 1) != 0; } // __Status
	bool mac() const { return (regs.needExactMatch & 2) != 0; } // __Mac
	bool clip() const { return (regs.needExactMatch & 4) != 0; } // __Clip
	void setupRange(int32_t pc, bool isStartPC)
	{
		if (ranges_)
			ranges_->setupRange(pc, isStartPC);
	}

	uint32_t Ft() const { return (code >> 16) & 0x1F; }
	uint32_t Fs() const { return (code >> 11) & 0x1F; }
	uint32_t Fd() const { return (code >> 6) & 0x1F; }
	uint32_t It() const { return (code >> 16) & 0xF; }
	uint32_t Is() const { return (code >> 11) & 0xF; }
	uint32_t Id() const { return (code >> 6) & 0xF; }
	bool X() const { return (code >> 24) & 1; }
	bool Y() const { return (code >> 23) & 1; }
	bool Z() const { return (code >> 22) & 1; }
	bool W() const { return (code >> 21) & 1; }
	uint32_t bc() const { return code & 3; }
	uint32_t Fsf() const { return (code >> 21) & 3; }
	uint32_t Ftf() const { return (code >> 23) & 3; }
	int16_t Imm5() const { return static_cast<int16_t>(((code & 0x400) ? 0xfff0 : 0) | ((code >> 6) & 0xf)); }
	int32_t Imm11() const { return (code & 0x400) ? static_cast<int32_t>(0xfffffc00u | (code & 0x3ff)) : static_cast<int32_t>(code & 0x3ff); }
	uint32_t Imm15() const { return ((code >> 10) & 0x7800) | (code & 0x7ff); }
	uint32_t bSaveAddr() const { return ((xPC() + 16) & (memSize_ - 8)) / 8; }
	uint32_t branchAddr() const { return (((iPC + 2) + (Imm11() * 2)) & progMemMask_) * 4; }

	// ---- microVU_Analyze.inl ---------------------------------------------------------
	void analyzeReg1(uint32_t xReg, VFReg& vfRead) // read a VF register
	{
		if (!xReg)
			return;
		const RegCycles& c = regs.VF[xReg];
		if (X()) { stall() = std::max(stall(), c.x); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.x = 1; }
		if (Y()) { stall() = std::max(stall(), c.y); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.y = 1; }
		if (Z()) { stall() = std::max(stall(), c.z); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.z = 1; }
		if (W()) { stall() = std::max(stall(), c.w); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.w = 1; }
	}
	void analyzeReg2(uint32_t xReg, VFReg& vfWrite, bool isLowOp) // write a VF register
	{
		if (!xReg)
			return;
		const int y = isLowOp ? 1 : 0;
		auto b = [&]() -> RegCycles& {
			regsTemp.VFreg[y] = static_cast<uint8_t>(xReg);
			return regsTemp.VF[y];
		};
		if (X()) { b().x = 4; vfWrite.reg = static_cast<uint8_t>(xReg); vfWrite.x = 4; }
		if (Y()) { b().y = 4; vfWrite.reg = static_cast<uint8_t>(xReg); vfWrite.y = 4; }
		if (Z()) { b().z = 4; vfWrite.reg = static_cast<uint8_t>(xReg); vfWrite.z = 4; }
		if (W()) { b().w = 4; vfWrite.reg = static_cast<uint8_t>(xReg); vfWrite.w = 4; }
	}
	void analyzeReg3(uint32_t xReg, VFReg& vfRead) // read a VF register's broadcast component
	{
		if (!xReg)
			return;
		const RegCycles& c = regs.VF[xReg];
		vfRead.reg = static_cast<uint8_t>(xReg);
		switch (bc())
		{
			case 0: stall() = std::max(stall(), c.x); vfRead.x = 1; break;
			case 1: stall() = std::max(stall(), c.y); vfRead.y = 1; break;
			case 2: stall() = std::max(stall(), c.z); vfRead.z = 1; break;
			default: stall() = std::max(stall(), c.w); vfRead.w = 1; break;
		}
	}
	void analyzeReg4(uint32_t xReg, VFReg& vfRead) // CLIP's w
	{
		if (!xReg)
			return;
		stall() = std::max(stall(), regs.VF[xReg].w);
		vfRead.reg = static_cast<uint8_t>(xReg);
		vfRead.w = 1;
	}
	void analyzeReg5(uint32_t xReg, uint32_t fxf, VFReg& vfRead) // one component (fsf, ftf)
	{
		if (!xReg)
			return;
		const RegCycles& c = regs.VF[xReg];
		vfRead.reg = static_cast<uint8_t>(xReg);
		switch (fxf)
		{
			case 0: stall() = std::max(stall(), c.x); vfRead.x = 1; break;
			case 1: stall() = std::max(stall(), c.y); vfRead.y = 1; break;
			case 2: stall() = std::max(stall(), c.z); vfRead.z = 1; break;
			default: stall() = std::max(stall(), c.w); vfRead.w = 1; break;
		}
	}
	void analyzeReg6(uint32_t xReg, VFReg& vfRead) // MR32: xyzw read as yzwx
	{
		if (!xReg)
			return;
		const RegCycles& c = regs.VF[xReg];
		if (X()) { stall() = std::max(stall(), c.y); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.y = 1; }
		if (Y()) { stall() = std::max(stall(), c.z); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.z = 1; }
		if (Z()) { stall() = std::max(stall(), c.w); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.w = 1; }
		if (W()) { stall() = std::max(stall(), c.x); vfRead.reg = static_cast<uint8_t>(xReg); vfRead.x = 1; }
	}
	void analyzeVIreg1(uint32_t xReg, VIReg& viRead)
	{
		if (!xReg)
			return;
		stall() = std::max(stall(), regs.VI[xReg]);
		viRead.reg = static_cast<uint8_t>(xReg);
		viRead.used = 1;
	}
	void analyzeVIreg2(uint32_t xReg, VIReg& viWrite, int aCycles)
	{
		if (!xReg)
			return;
		constReg[xReg].isValid = 0;
		regsTemp.VIreg = static_cast<uint8_t>(xReg);
		regsTemp.VI = static_cast<uint8_t>(aCycles);
		viWrite.reg = static_cast<uint8_t>(xReg);
		viWrite.used = static_cast<uint8_t>(aCycles);
	}
	void analyzeQreg(int x)
	{
		regsTemp.q = static_cast<uint8_t>(x);
		stall() = std::max(stall(), regs.q);
	}
	void analyzePreg(int x)
	{
		regsTemp.p = static_cast<uint8_t>(x);
		stall() = std::max(stall(), static_cast<uint8_t>(regs.p ? regs.p - 1 : 0));
	}
	void analyzeRreg() { regsTemp.r = 1; }
	void analyzeXGkick1() { stall() = std::max(stall(), regs.xgkick); }
	void analyzeXGkick2(int x) { regsTemp.xgkick = static_cast<uint8_t>(x); }
	void setConstReg(uint32_t x, uint32_t v)
	{
		if (x)
		{
			constReg[x].isValid = 1;
			constReg[x].regValue = v;
		}
	}

	void analyzeFMAC1(uint32_t Fd, uint32_t Fs, uint32_t Ft)
	{
		sFLAG().doFlag = true;
		analyzeReg1(Fs, up().VF_read[0]);
		analyzeReg1(Ft, up().VF_read[1]);
		analyzeReg2(Fd, up().VF_write, false);
	}
	void analyzeFMAC2(uint32_t Fs, uint32_t Ft)
	{
		analyzeReg1(Fs, up().VF_read[0]);
		analyzeReg2(Ft, up().VF_write, false);
	}
	void analyzeFMAC3(uint32_t Fd, uint32_t Fs, uint32_t Ft)
	{
		sFLAG().doFlag = true;
		analyzeReg1(Fs, up().VF_read[0]);
		analyzeReg3(Ft, up().VF_read[1]);
		analyzeReg2(Fd, up().VF_write, false);
	}
	void analyzeFMAC4(uint32_t Fs, uint32_t Ft)
	{
		cFLAG().doFlag = true;
		analyzeReg1(Fs, up().VF_read[0]);
		analyzeReg4(Ft, up().VF_read[1]);
	}
	void analyzeIALU1(uint32_t Id, uint32_t Is, uint32_t It)
	{
		if (!Id)
			low().isNOP = true;
		analyzeVIreg1(Is, low().VI_read[0]);
		analyzeVIreg1(It, low().VI_read[1]);
		analyzeVIreg2(Id, low().VI_write, 1);
	}
	void analyzeIALU2(uint32_t Is, uint32_t It)
	{
		if (!It)
			low().isNOP = true;
		analyzeVIreg1(Is, low().VI_read[0]);
		analyzeVIreg2(It, low().VI_write, 1);
	}
	void analyzeIADDI(uint32_t Is, uint32_t It, int32_t imm)
	{
		analyzeIALU2(Is, It);
		if (!Is && !settings_.ibitHack)
			setConstReg(It, static_cast<uint32_t>(static_cast<int16_t>(imm)));
	}
	void analyzeMR32(uint32_t Fs, uint32_t Ft)
	{
		if (!Ft)
			low().isNOP = true;
		analyzeReg6(Fs, low().VF_read[0]);
		analyzeReg2(Ft, low().VF_write, true);
	}
	void analyzeFDIV(uint32_t Fs, uint32_t Fsf, uint32_t Ft, uint32_t Ftf, int xCycles)
	{
		analyzeReg5(Fs, Fsf, low().VF_read[0]);
		analyzeReg5(Ft, Ftf, low().VF_read[1]);
		analyzeQreg(xCycles);
	}
	void analyzeEFU1(uint32_t Fs, uint32_t Fsf, int xCycles)
	{
		analyzeReg5(Fs, Fsf, low().VF_read[0]);
		analyzePreg(xCycles);
	}
	void analyzeEFU2(uint32_t Fs, int xCycles)
	{
		analyzeReg1(Fs, low().VF_read[0]);
		analyzePreg(xCycles);
	}
	void analyzeMFP(uint32_t Ft)
	{
		if (!Ft)
			low().isNOP = true;
		analyzeReg2(Ft, low().VF_write, true);
	}
	void analyzeMOVE(uint32_t Fs, uint32_t Ft)
	{
		if (!Ft || Ft == Fs)
			low().isNOP = true;
		analyzeReg1(Fs, low().VF_read[0]);
		analyzeReg2(Ft, low().VF_write, true);
	}
	void analyzeLQ(uint32_t Ft, uint32_t Is, bool writeIs)
	{
		analyzeVIreg1(Is, low().VI_read[0]);
		analyzeReg2(Ft, low().VF_write, true);
		if (!Ft)
		{
			if (writeIs && Is)
				low().noWriteVF = true;
			else
				low().isNOP = true;
		}
		if (writeIs)
			analyzeVIreg2(Is, low().VI_write, 1);
	}
	void analyzeSQ(uint32_t Fs, uint32_t It, bool writeIt)
	{
		low().isMemWrite = true;
		analyzeReg1(Fs, low().VF_read[0]);
		analyzeVIreg1(It, low().VI_read[0]);
		if (writeIt)
			analyzeVIreg2(It, low().VI_write, 1);
	}
	void analyzeR1(uint32_t Fs, uint32_t Fsf)
	{
		analyzeReg5(Fs, Fsf, low().VF_read[0]);
		analyzeRreg();
	}
	void analyzeR2(uint32_t Ft, bool canBeNOP)
	{
		if (!Ft)
		{
			if (canBeNOP)
				low().isNOP = true;
			else
				low().noWriteVF = true;
		}
		analyzeReg2(Ft, low().VF_write, true);
		analyzeRreg();
	}

	// Which instructions before this one in the block update the status (and
	// MAC) flags, for an instruction that reads the status or MAC flag.
	void flagSet(bool setMacFlag)
	{
		const uint32_t curPC = iPC;
		int calcOPS = 0;
		for (int i = static_cast<int>(count), j = 0; i > 0; i--, j++)
		{
			j += stall();
			incPC(-2);
			if (calcOPS >= 4 && up().VF_write.reg)
				break;
			if (sFLAG().doFlag && j >= 3)
			{
				if (setMacFlag)
					mFLAG().doFlag = true;
				sFLAG().doNonSticky = true;
				calcOPS++;
			}
		}
		iPC = curPC;
		setCode();
	}
	void analyzeSflag(uint32_t It)
	{
		low().readFlags = true;
		analyzeVIreg2(It, low().VI_write, 1);
		if (!It)
		{
			low().isNOP = true;
		}
		else
		{
			info().swapOps = true;
			flagSet(false);
		}
	}
	void analyzeFSSET()
	{
		low().isFSSET = true;
		low().readFlags = true;
	}
	void analyzeMflag(uint32_t Is, uint32_t It)
	{
		low().readFlags = true;
		analyzeVIreg1(Is, low().VI_read[0]);
		analyzeVIreg2(It, low().VI_write, 1);
		if (!It)
		{
			low().isNOP = true;
		}
		else
		{
			info().swapOps = true;
			flagSet(true);
		}
	}
	void analyzeCflag(uint32_t It)
	{
		info().swapOps = true;
		low().readFlags = true;
		analyzeVIreg2(It, low().VI_write, 1);
	}
	void analyzeXGkick(uint32_t Fs, int xCycles)
	{
		low().isKick = true;
		regs.xgkickcycles = 0;
		low().kickcycles = 0;
		analyzeVIreg1(Fs, low().VI_read[0]);
		if (!settings_.xgkickHack)
		{
			analyzeXGkick1();
			analyzeXGkick2(xCycles);
		}
	}

	// A branch that tests a VI register written by the instructions just
	// before it reads the value it had before them.
	void analyzeBranchVI(uint32_t xReg, bool& infoVar)
	{
		if (!xReg)
			return;
		if (stall())
			return;
		int i;
		int j = 0;
		int cyc = 0;
		const int iEnd = 4;
		const uint32_t bPC = iPC;
		incPC2(-2);
		for (i = 0; i < iEnd && cyc < iEnd; i++)
		{
			if (i == static_cast<int>(count))
			{
				if (blockState.viBackUp == xReg)
				{
					infoVar = true;
					j = i;
					i++;
				}
				break;
			}
			if (low().VI_write.reg == xReg && low().VI_write.used)
			{
				if (low().readFlags)
					break;
				j = i;
			}
			else if (i == 0)
			{
				break;
			}
			cyc += stall() + 1;
			incPC2(-2);
		}
		if (i)
		{
			if (!infoVar)
			{
				iPC = bPC;
				incPC2(-2 * (j + 1));
				low().backupVI = true;
				infoVar = true;
			}
			iPC = bPC;
		}
		else
		{
			iPC = bPC;
		}
	}

	// A branch in a branch's delay slot.
	int branchCheck()
	{
		if (!count && !isEvilBlock())
			return 0;
		if (isEvilBlock())
		{
			low().evilBranch = true;
			regs.blockType = 2;
			regs.needExactMatch |= 7;
			regs.flagInfo = 0;
			return 1;
		}
		incPC(-2);
		if (low().branch)
		{
			if (doBranchInDelaySlot)
			{
				low().badBranch = true;
				incPC(2);
				low().evilBranch = true;
				regs.blockType = 2;
				regs.needExactMatch |= 7;
				regs.flagInfo = 0;
				return 1;
			}
			incPC(2);
			low().isNOP = true;
			return 0;
		}
		incPC(2);
		return 0;
	}
	void analyzeCondBranch1(uint32_t Is)
	{
		analyzeVIreg1(Is, low().VI_read[0]);
		if (!branchCheck())
			analyzeBranchVI(Is, low().memReadIs);
	}
	void analyzeCondBranch2(uint32_t Is, uint32_t It)
	{
		analyzeVIreg1(Is, low().VI_read[0]);
		analyzeVIreg1(It, low().VI_read[1]);
		if (!branchCheck())
		{
			analyzeBranchVI(Is, low().memReadIs);
			analyzeBranchVI(It, low().memReadIt);
		}
	}
	void analyzeNormBranch(uint32_t It, bool isBAL)
	{
		branchCheck();
		if (isBAL)
		{
			analyzeVIreg2(It, low().VI_write, 1);
			if (!low().evilBranch)
				setConstReg(It, bSaveAddr());
		}
	}
	void analyzeJump(uint32_t Is, uint32_t It, bool isJALR)
	{
		low().branch = isJALR ? 10 : 9;
		branchCheck();
		if (constReg[Is].isValid && doConstProp)
		{
			low().constJump.isValid = 1;
			low().constJump.regValue = constReg[Is].regValue;
		}
		analyzeVIreg1(Is, low().VI_read[0]);
		if (isJALR)
		{
			analyzeVIreg2(It, low().VI_write, 1);
			if (!low().evilBranch)
				setConstReg(It, bSaveAddr());
		}
	}

	// setBranchA (microVU_Lower.inl): run in every pass before the branch's own analysis.
	// Returns false where the pass stops at it.
	void setBranchA(int pass, int x, uint32_t xReg)
	{
		bool isBranchDelaySlot = false;
		incPC(-2);
		if (low().branch)
			isBranchDelaySlot = true;
		incPC(2);
		const bool optimized = Imm11() == 1 && !xReg && !isBranchDelaySlot;
		if (pass == 0)
		{
			if (optimized)
			{
				low().isNOP = true; // "Branch Optimization"
				return;
			}
			branch = static_cast<uint8_t>(x);
			low().branch = static_cast<uint32_t>(x);
		}
		else // the flag pass (pass4)
		{
			if (optimized)
				return;
			branch = static_cast<uint8_t>(x);
		}
	}

	// ---- the instructions' first pass (pass1) and flag pass (pass4) -----------------
	// pass 0: the analysis; pass 3: the flag pass (mVUopU/mVUopL with recPass 3).
	void upperOp(int pass);
	void lowerOp(int pass);
	void lowerOpT3(int pass, int table);
	void unknownOp(int pass)
	{
		if (pass == 0 && code != 0x8000033c)
			info().isBadOp = true;
	}

	// ---- microVU_Compile.inl -------------------------------------------------------
	void incCycles(int x);
	void setCycles();
	void optimizePipeState();
	void branchWarning();
	void eBitWarning();

	// ---- microVU_Flags.inl ---------------------------------------------------------
	void statusFlagOp();
	void setFlags(FlagCycles& mFC);
	void flagPass(uint32_t startPC, uint32_t sCount, uint32_t found, std::vector<uint32_t>& v);
	void setFlagInfo();

	const int vu_;
	const uint32_t* const micro_;
	const uint32_t memSize_;
	const uint32_t progMemMask_;
	const Settings& settings_;
	InfoArray& infos_;
	RangeSink* const ranges_;

	// microIR and microVU's working variables
	PipeState regs; // mVUregs
	PipeState blockState; // mVUpBlock->pState
	TempRegInfo regsTemp; // mVUregsTemp
	ConstInfo constReg[16]; // mVUconstReg
	uint8_t branch = 0; // mVUbranch
	uint32_t cycles = 0; // mVUcycles
	uint32_t count = 0; // mVUcount
	uint32_t iPC = 0; // in 32-bit words
	uint32_t startPCw = 0; // mVUstartPC, in 32-bit words
	uint32_t sFlagHack = 0; // mVUsFlagHack
	int p = 0, q = 0; // mVU.p, mVU.q
	uint32_t code = 0; // mVU.code
};

// ---- the upper instructions ------------------------------------------------------------

void Analyzer::upperOp(int pass)
{
	// The FMAC family (microVU_Upper.inl's mVU_FMACa/b/c/d through setupPass1):
	// opCase 1 the vector form, 2 a broadcast, 3 I, 4 Q; isACC: the result goes
	// to ACC; noFlag: MAX and MINI, which update no flag.
	auto fmac = [&](int opCase, bool isACC, bool noFlag) {
		if (pass == 0)
		{
			const uint32_t fd = isACC ? 0 : Fd();
			if (opCase == 1)
				analyzeFMAC1(fd, Fs(), Ft());
			else if (opCase == 2)
				analyzeFMAC3(fd, Fs(), Ft());
			else
				analyzeFMAC1(fd, Fs(), 0);
			if (noFlag)
				sFLAG().doFlag = false;
		}
		else if (!noFlag)
		{
			regs.needExactMatch |= 8;
		}
	};

	const uint32_t op = code & 0x3f;
	if (op < 0x30)
	{
		switch (op >> 2)
		{
			case 0x0: fmac(2, false, false); return; // ADDbc
			case 0x1: fmac(2, false, false); return; // SUBbc
			case 0x2: fmac(2, false, false); return; // MADDbc
			case 0x3: fmac(2, false, false); return; // MSUBbc
			case 0x4: fmac(2, false, true); return; // MAXbc
			case 0x5: fmac(2, false, true); return; // MINIbc
			case 0x6: fmac(2, false, false); return; // MULbc
			case 0x7:
				switch (op & 3)
				{
					case 0: fmac(4, false, false); return; // MULq
					case 1: fmac(3, false, true); return; // MAXi
					case 2: fmac(3, false, false); return; // MULi
					default: fmac(3, false, true); return; // MINIi
				}
			case 0x8: // ADDq MADDq ADDi MADDi
			case 0x9: // SUBq MSUBq SUBi MSUBi
				fmac((op & 2) ? 3 : 4, false, false);
				return;
			case 0xA: // ADD MADD MUL MAX
				fmac(1, false, (op & 3) == 3);
				return;
			case 0xB: // SUB MSUB OPMSUB MINI
				if ((op & 3) == 2)
				{
					// OPMSUB
					if (pass == 0)
						analyzeFMAC1(Fd(), Fs(), Ft());
					else
						regs.needExactMatch |= 8;
					return;
				}
				fmac(1, false, (op & 3) == 3);
				return;
		}
	}
	if (op < 0x3C)
	{
		unknownOp(pass);
		return;
	}

	// The four tables of the accumulator and conversion instructions.
	const uint32_t row = (code >> 6) & 0x1f;
	const uint32_t column = op & 3; // 0 x, 1 y, 2 z, 3 w
	if (row <= 3) // ADDAbc SUBAbc MADDAbc MSUBAbc
	{
		fmac(2, true, false);
		return;
	}
	if (row == 4 || row == 5) // ITOF0/4/12/15, FTOI0/4/12/15
	{
		if (pass == 0)
			analyzeFMAC2(Fs(), Ft());
		return;
	}
	if (row == 6) // MULAbc
	{
		fmac(2, true, false);
		return;
	}
	switch (column)
	{
		case 0:
			switch (row)
			{
				case 7: fmac(4, true, false); return; // MULAq
				case 8: fmac(4, true, false); return; // ADDAq
				case 9: fmac(4, true, false); return; // SUBAq
				case 10: fmac(1, true, false); return; // ADDA
				case 11: fmac(1, true, false); return; // SUBA
			}
			break;
		case 1:
			switch (row)
			{
				case 7: // ABS
					if (pass == 0)
						analyzeFMAC2(Fs(), Ft());
					return;
				case 8: fmac(4, true, false); return; // MADDAq
				case 9: fmac(4, true, false); return; // MSUBAq
				case 10: fmac(1, true, false); return; // MADDA
				case 11: fmac(1, true, false); return; // MSUBA
			}
			break;
		case 2:
			switch (row)
			{
				case 7: fmac(3, true, false); return; // MULAi
				case 8: fmac(3, true, false); return; // ADDAi
				case 9: fmac(3, true, false); return; // SUBAi
				case 10: fmac(1, true, false); return; // MULA
				case 11: // OPMULA
					if (pass == 0)
						analyzeFMAC1(0, Fs(), Ft());
					else
						regs.needExactMatch |= 8;
					return;
			}
			break;
		default:
			switch (row)
			{
				case 7: // CLIP
					if (pass == 0)
						analyzeFMAC4(Fs(), Ft());
					return;
				case 8: fmac(3, true, false); return; // MADDAi
				case 9: fmac(3, true, false); return; // MSUBAi
				case 11: return; // NOP
			}
			break;
	}
	unknownOp(pass);
}

// ---- the lower instructions ------------------------------------------------------------

void Analyzer::lowerOp(int pass)
{
	const bool p1 = pass == 0;
	switch (code >> 25)
	{
		case 0x00: if (p1) analyzeLQ(Ft(), Is(), false); return; // LQ
		case 0x01: if (p1) analyzeSQ(Fs(), It(), false); return; // SQ
		case 0x04: // ILW
			if (p1)
			{
				if (!It())
					low().isNOP = true;
				analyzeVIreg1(Is(), low().VI_read[0]);
				analyzeVIreg2(It(), low().VI_write, 4);
			}
			return;
		case 0x05: // ISW
			if (p1)
			{
				low().isMemWrite = true;
				analyzeVIreg1(Is(), low().VI_read[0]);
				analyzeVIreg1(It(), low().VI_read[1]);
			}
			return;
		case 0x08: if (p1) analyzeIADDI(Is(), It(), static_cast<int32_t>(Imm15())); return; // IADDIU
		case 0x09: if (p1) analyzeIALU2(Is(), It()); return; // ISUBIU
		case 0x10: // FCEQ
		case 0x12: // FCAND
		case 0x13: // FCOR
			if (p1)
				analyzeCflag(1);
			else
				regs.needExactMatch |= 4;
			return;
		case 0x11: if (p1) cFLAG().doFlag = true; return; // FCSET
		case 0x14: // FSEQ
		case 0x16: // FSAND
		case 0x17: // FSOR
			if (p1)
				analyzeSflag(It());
			else
				regs.needExactMatch |= 1;
			return;
		case 0x15: if (p1) analyzeFSSET(); return; // FSSET
		case 0x18: // FMEQ
		case 0x1A: // FMAND
		case 0x1B: // FMOR
			if (p1)
				analyzeMflag(Is(), It());
			else
				regs.needExactMatch |= 2;
			return;
		case 0x1C: // FCGET
			if (p1)
				analyzeCflag(It());
			else
				regs.needExactMatch |= 4;
			return;
		case 0x20: // B
			setBranchA(pass, 1, 0);
			if (p1)
				analyzeNormBranch(0, false);
			return;
		case 0x21: // BAL
			setBranchA(pass, 2, It());
			if (p1)
				analyzeNormBranch(It(), true);
			return;
		case 0x24: // JR
			branch = 9;
			if (p1)
				analyzeJump(Is(), 0, false);
			return;
		case 0x25: // JALR
			branch = 10;
			if (p1)
				analyzeJump(Is(), It(), true);
			return;
		case 0x28: setBranchA(pass, 3, 0); if (p1) analyzeCondBranch2(Is(), It()); return; // IBEQ
		case 0x29: setBranchA(pass, 8, 0); if (p1) analyzeCondBranch2(Is(), It()); return; // IBNE
		case 0x2C: setBranchA(pass, 7, 0); if (p1) analyzeCondBranch1(Is()); return; // IBLTZ
		case 0x2D: setBranchA(pass, 5, 0); if (p1) analyzeCondBranch1(Is()); return; // IBGTZ
		case 0x2E: setBranchA(pass, 6, 0); if (p1) analyzeCondBranch1(Is()); return; // IBLEZ
		case 0x2F: setBranchA(pass, 4, 0); if (p1) analyzeCondBranch1(Is()); return; // IBGEZ
		case 0x40:
			switch (code & 0x3f)
			{
				case 0x30: // IADD
				case 0x31: // ISUB
				case 0x34: // IAND
				case 0x35: // IOR
					if (p1)
						analyzeIALU1(Id(), Is(), It());
					return;
				case 0x32: if (p1) analyzeIADDI(Is(), It(), Imm5()); return; // IADDI
				case 0x3C: lowerOpT3(pass, 0); return;
				case 0x3D: lowerOpT3(pass, 1); return;
				case 0x3E: lowerOpT3(pass, 2); return;
				case 0x3F: lowerOpT3(pass, 3); return;
			}
			break;
	}
	unknownOp(pass);
}

void Analyzer::lowerOpT3(int pass, int table)
{
	if (pass != 0)
		return; // none of these has anything in the flag pass
	const uint32_t row = (code >> 6) & 0x1f;
	// The EFU's instructions: on VU0, which has none, nothing.
	auto efu1 = [&](int c) {
		if (isVU0())
		{
			low().isNOP = true;
			return;
		}
		analyzeEFU1(Fs(), Fsf(), c);
	};
	auto efu2 = [&](int c) {
		if (isVU0())
		{
			low().isNOP = true;
			return;
		}
		analyzeEFU2(Fs(), c);
	};
	switch (table)
	{
		case 0:
			switch (row)
			{
				case 12: analyzeMOVE(Fs(), Ft()); return; // MOVE
				case 13: analyzeLQ(Ft(), Is(), true); return; // LQI
				case 14: analyzeFDIV(Fs(), Fsf(), Ft(), Ftf(), 7); return; // DIV
				case 15: // MTIR
					if (!It())
						low().isNOP = true;
					analyzeReg5(Fs(), Fsf(), low().VF_read[0]);
					analyzeVIreg2(It(), low().VI_write, 1);
					return;
				case 16: analyzeR2(Ft(), false); return; // RNEXT
				case 25: // MFP
					if (isVU0())
					{
						low().isNOP = true;
						return;
					}
					analyzeMFP(Ft());
					return;
				case 26: // XTOP
					if (isVU0())
					{
						low().isNOP = true;
						return;
					}
					if (!It())
						low().isNOP = true;
					analyzeVIreg2(It(), low().VI_write, 1);
					return;
				case 27: // XGKICK
					if (isVU0())
					{
						low().isNOP = true;
						return;
					}
					analyzeXGkick(Is(), 1);
					return;
				case 28: efu2(11); return; // ESADD
				case 29: efu2(54); return; // EATANxy
				case 30: efu1(12); return; // ESQRT
				case 31: efu1(29); return; // ESIN
			}
			break;
		case 1:
			switch (row)
			{
				case 12: analyzeMR32(Fs(), Ft()); return; // MR32
				case 13: analyzeSQ(Fs(), It(), true); return; // SQI
				case 14: analyzeFDIV(0, 0, Ft(), Ftf(), 7); return; // SQRT
				case 15: // MFIR
					if (!Ft())
						low().isNOP = true;
					analyzeVIreg1(Is(), low().VI_read[0]);
					analyzeReg2(Ft(), low().VF_write, true);
					return;
				case 16: analyzeR2(Ft(), true); return; // RGET
				case 26: // XITOP
					if (!It())
						low().isNOP = true;
					analyzeVIreg2(It(), low().VI_write, 1);
					return;
				case 28: efu2(18); return; // ERSADD
				case 29: efu2(54); return; // EATANxz
				case 30: efu1(18); return; // ERSQRT
				case 31: efu1(54); return; // EATAN
			}
			break;
		case 2:
			switch (row)
			{
				case 13: analyzeLQ(Ft(), Is(), true); return; // LQD
				case 14: analyzeFDIV(Fs(), Fsf(), Ft(), Ftf(), 13); return; // RSQRT
				case 15: // ILWR
					if (!It())
						low().isNOP = true;
					analyzeVIreg1(Is(), low().VI_read[0]);
					analyzeVIreg2(It(), low().VI_write, 4);
					return;
				case 16: analyzeR1(Fs(), Fsf()); return; // RINIT
				case 28: efu2(18); return; // ELENG
				case 29: efu2(12); return; // ESUM
				case 30: efu1(12); return; // ERCPR
				case 31: efu1(44); return; // EEXP
			}
			break;
		default:
			switch (row)
			{
				case 13: analyzeSQ(Fs(), It(), true); return; // SQD
				case 14: stall() = std::max(stall(), regs.q); return; // WAITQ
				case 15: // ISWR
					low().isMemWrite = true;
					analyzeVIreg1(Is(), low().VI_read[0]);
					analyzeVIreg1(It(), low().VI_read[1]);
					return;
				case 16: analyzeR1(Fs(), Fsf()); return; // RXOR
				case 28: efu2(24); return; // ERLENG
				case 30: // WAITP
					if (isVU0())
					{
						low().isNOP = true;
						return;
					}
					stall() = std::max(stall(), static_cast<uint8_t>(regs.p ? regs.p - 1 : 0));
					return;
			}
			break;
	}
	unknownOp(pass);
}

// ---- cycles and the pipeline state (microVU_Compile.inl) -------------------------------

void Analyzer::incCycles(int xi)
{
	const uint8_t x = static_cast<uint8_t>(xi);
	cycles += static_cast<uint32_t>(xi);
	for (int z = 31; z > 0; z--)
	{
		RegCycles& c = regs.VF[z];
		c.x = calcCycles(c.x, x);
		c.y = calcCycles(c.y, x);
		c.z = calcCycles(c.z, x);
		c.w = calcCycles(c.w, x);
	}
	for (int z = 15; z > 0; z--)
		regs.VI[z] = calcCycles(regs.VI[z], x);
	if (regs.q)
	{
		if (regs.q > 4)
		{
			regs.q = calcCycles(regs.q, x);
			if (regs.q <= 4)
				info().doDivFlag = true;
		}
		else
		{
			regs.q = calcCycles(regs.q, x);
		}
		if (!regs.q)
			q ^= 1;
	}
	if (regs.p)
	{
		regs.p = calcCycles(regs.p, x);
		if (!regs.p || regsTemp.p)
			p ^= 1;
	}
	if (regs.xgkick)
	{
		regs.xgkick = calcCycles(regs.xgkick, x);
		if (!regs.xgkick)
		{
			info().doXGKICK = true;
			info().XGKICKPC = xPC();
		}
	}
	regs.r = calcCycles(regs.r, x);
}

namespace {
void cmpVFregs(const VFReg& a, const VFReg& b, bool& xVar)
{
	if (a.reg == b.reg && ((a.x && b.x) || (a.y && b.y) || (a.z && b.z) || (a.w && b.w)))
		xVar = true;
}
} // namespace

void Analyzer::setCycles()
{
	incCycles(stall());
	if (regsTemp.VFreg[0] == regsTemp.VFreg[1] && regsTemp.VFreg[0])
	{
		if (regsTemp.r || regsTemp.VI)
			low().noWriteVF = true;
		else
			low().isNOP = true;
	}
	if ((low().VF_read[0].reg || low().VF_read[1].reg) && up().VF_write.reg)
	{
		cmpVFregs(up().VF_write, low().VF_read[0], info().swapOps);
		cmpVFregs(up().VF_write, low().VF_read[1], info().swapOps);
	}
	if (info().swapOps && ((up().VF_read[0].reg || up().VF_read[1].reg) && low().VF_write.reg))
	{
		cmpVFregs(low().VF_write, up().VF_read[0], info().backupVF);
		cmpVFregs(low().VF_write, up().VF_read[1], info().backupVF);
	}
	for (int k = 0; k < 2; k++)
	{
		RegCycles& c = regs.VF[regsTemp.VFreg[k]];
		c.x = tCycles(c.x, regsTemp.VF[k].x);
		c.y = tCycles(c.y, regsTemp.VF[k].y);
		c.z = tCycles(c.z, regsTemp.VF[k].z);
		c.w = tCycles(c.w, regsTemp.VF[k].w);
	}
	regs.VI[regsTemp.VIreg] = tCycles(regs.VI[regsTemp.VIreg], regsTemp.VI);
	regs.q = tCycles(regs.q, regsTemp.q);
	regs.p = tCycles(regs.p, regsTemp.p);
	regs.r = tCycles(regs.r, regsTemp.r);
	regs.xgkick = tCycles(regs.xgkick, regsTemp.xgkick);
}

void Analyzer::optimizePipeState()
{
	for (int i = 0; i < 32; i++)
	{
		RegCycles& c = regs.VF[i];
		c.x = optimizeReg(c.x);
		c.y = optimizeReg(c.y);
		c.z = optimizeReg(c.z);
		c.w = optimizeReg(c.w);
	}
	for (int i = 0; i < 16; i++)
		regs.VI[i] = optimizeReg(regs.VI[i]);
	if (regs.q)
	{
		regs.q = optimizeReg(regs.q);
		if (!regs.q)
			q ^= 1;
	}
	if (regs.p)
	{
		regs.p = optimizeReg(regs.p);
		if (!regs.p)
			p ^= 1;
	}
	regs.r = 0;
}

void Analyzer::branchWarning()
{
	incPC(-2);
	if (up().eBit && branch)
	{
		incPC(2);
		low().isNOP = true;
	}
	else
	{
		incPC(2);
	}
	if (info().isBdelay && !low().evilBranch)
	{
		if (low().VI_write.reg && low().VI_write.used && !low().readFlags)
		{
			low().backupVI = true;
			regs.viBackUp = low().VI_write.reg;
		}
	}
}

void Analyzer::eBitWarning()
{
	incPC(2);
	if (curI() & kEbit)
		regs.blockType = 1;
	incPC(-2);
}

// ---- flags (microVU_Flags.inl) -----------------------------------------------------------

void Analyzer::statusFlagOp()
{
	const uint32_t curPC = iPC;
	int i = static_cast<int>(count);
	bool runLoop = true;
	if (sFLAG().doFlag)
	{
		sFLAG().doNonSticky = true;
	}
	else
	{
		for (; i > 0; i--)
		{
			incPC2(-2);
			if (sFLAG().doNonSticky)
			{
				runLoop = false;
				break;
			}
			else if (sFLAG().doFlag)
			{
				sFLAG().doNonSticky = true;
				break;
			}
		}
	}
	if (runLoop)
	{
		for (; i > 0; i--)
		{
			incPC2(-2);
			if (sFLAG().doNonSticky)
				break;
			sFLAG().doFlag = false;
		}
	}
	iPC = curPC;
}

namespace {
int findFlagInst(const int* fFlag, int cycles)
{
	int j = 0, jValue = -1;
	for (int i = 0; i < 4; i++)
	{
		if (fFlag[i] <= cycles && fFlag[i] > jValue)
		{
			j = i;
			jValue = fFlag[i];
		}
	}
	return j;
}
} // namespace

void Analyzer::setFlags(FlagCycles& mFC)
{
	const uint32_t endPC = iPC;
	uint32_t aCount = 0;
	for (int i = static_cast<int>(count); i > 0; i--, aCount++)
	{
		if (sFLAG().doFlag)
		{
			if (mac())
				mFLAG().doFlag = true;
			if (status())
				sFLAG().doNonSticky = true;
			if (aCount >= 3)
				break;
		}
		incPC2(-2);
	}

	int xS = 0, xM = 0, xC = 0;
	for (int i = 0; i < 4; i++)
	{
		mFC.xStatus[i] = i;
		mFC.xMac[i] = i;
		mFC.xClip[i] = i;
	}
	if (!(blockState.needExactMatch & 1))
	{
		xS = (blockState.flagInfo >> 2) & 3;
		for (int i = 0; i < 4; i++)
			mFC.xStatus[i] = -1;
		mFC.xStatus[(xS - 1) & 3] = 0;
	}
	if (!(blockState.needExactMatch & 2))
	{
		for (int i = 0; i < 4; i++)
			mFC.xMac[i] = -1;
	}
	if (!(blockState.needExactMatch & 4))
	{
		xC = (blockState.flagInfo >> 6) & 3;
		for (int i = 0; i < 4; i++)
			mFC.xClip[i] = -1;
		mFC.xClip[(xC - 1) & 3] = 0;
	}

	mFC.cycles = 0;
	const uint32_t xCount = count;
	iPC = startPCw;
	for (count = 0; count < xCount; count++)
	{
		if (low().isFSSET && !noFlagOpts)
		{
			if (status())
			{
				if ((xCount - count) > aCount)
					statusFlagOp();
			}
			else
			{
				statusFlagOp();
			}
		}
		mFC.cycles += stall();

		sFLAG().read = static_cast<uint8_t>(doSFlagInsts ? findFlagInst(mFC.xStatus, mFC.cycles) : 0);
		mFLAG().read = static_cast<uint8_t>(doMFlagInsts ? findFlagInst(mFC.xMac, mFC.cycles) : 0);
		cFLAG().read = static_cast<uint8_t>(doCFlagInsts ? findFlagInst(mFC.xClip, mFC.cycles) : 0);

		sFLAG().write = static_cast<uint8_t>(doSFlagInsts ? xS : 0);
		mFLAG().write = static_cast<uint8_t>(doMFlagInsts ? xM : 0);
		cFLAG().write = static_cast<uint8_t>(doCFlagInsts ? xC : 0);

		sFLAG().lastWrite = static_cast<uint8_t>(doSFlagInsts ? (xS - 1) & 3 : 0);
		mFLAG().lastWrite = static_cast<uint8_t>(doMFlagInsts ? (xM - 1) & 3 : 0);
		cFLAG().lastWrite = static_cast<uint8_t>(doCFlagInsts ? (xC - 1) & 3 : 0);

		if (sFlagHack && !sFLAG().doNonSticky) // sHackCond
			sFLAG().doFlag = false;

		if (sFLAG().doFlag || low().isFSSET || info().doDivFlag) // sFlagCond
		{
			mFC.xStatus[xS] = mFC.cycles + 4;
			xS = (xS + 1) & 3;
		}
		if (mFLAG().doFlag)
		{
			mFC.xMac[xM] = mFC.cycles + 4;
			xM = (xM + 1) & 3;
		}
		if (cFLAG().doFlag)
		{
			mFC.xClip[xC] = mFC.cycles + 4;
			xC = (xC + 1) & 3;
		}
		mFC.cycles++;
		incPC2(2);
	}

	regs.flagInfo |= static_cast<uint8_t>(status() ? 0 : (xS << 2));
	regs.flagInfo |= static_cast<uint8_t>(xM << 4);
	regs.flagInfo |= static_cast<uint8_t>(clip() ? 0 : (xC << 6));
	iPC = endPC;
}

void Analyzer::flagPass(uint32_t startPC, uint32_t sCount, uint32_t found, std::vector<uint32_t>& v)
{
	for (uint32_t pc : v)
	{
		if (pc == startPC)
			return; // already looked at: no endless recursion
	}
	v.push_back(startPC);
	const uint32_t oldPC = iPC;
	const uint8_t oldBranch = branch;
	uint32_t aBranchAddr = 0;
	iPC = startPC / 4;
	branch = 0;
	for (int b = 0; sCount < 4; sCount += found)
	{
		regs.needExactMatch &= 7;
		incPC(1);
		upperOp(3);
		found |= (regs.needExactMatch & 8) >> 3;
		regs.needExactMatch &= 7;
		if (curI() & kEbit)
			b = 1;
		if (curI() & kTbit)
			b = 6;
		if ((curI() & kDbit) && doDBitHandling)
			b = 6;
		if (!(curI() & kIbit))
		{
			incPC(-1);
			lowerOp(3);
			incPC(1);
		}

		if (b >= 2)
		{
			// shortBranch()
			if (b == 3 || b == 4) // branches
			{
				flagPass(aBranchAddr, sCount + found, found, v);
				if (b == 3) // a branch that is always taken
					break;
				b = 0;
			}
			else if (b == 5) // JR/JALR
			{
				if (sCount + found < 4)
					regs.needExactMatch |= 7;
				break;
			}
			else // the E bit's end
			{
				break;
			}
		}
		else if (b == 1)
		{
			b = 2;
		}
		if (branch)
		{
			b = (branch > 8) ? 5 : ((branch < 3) ? 3 : 4);
			incPC(-1);
			aBranchAddr = branchAddr();
			incPC(1);
			branch = 0;
		}

		incPC(1);
		if ((regs.needExactMatch & 7) == 7)
			break;
	}
	iPC = oldPC;
	branch = oldBranch;
	regs.needExactMatch &= 7;
	setCode();
}

void Analyzer::setFlagInfo()
{
	std::vector<uint32_t> v;
	if (branch <= 2) // B/BAL
	{
		incPC(-1);
		flagPass(branchAddr(), 0, 0, v);
		incPC(1);
		regs.needExactMatch &= 0x7;
	}
	else if (branch <= 8) // the conditional branches
	{
		incPC(-1); // taken
		flagPass(branchAddr(), 0, 0, v);
		const uint8_t backupFlagInfo = regs.needExactMatch;
		regs.needExactMatch = 0;
		incPC(4); // not taken
		v.clear();
		flagPass(xPC(), 0, 0, v);
		incPC(-3);
		regs.needExactMatch |= backupFlagInfo;
		regs.needExactMatch &= 0x7;
	}
	else // JR/JALR
	{
		if (!doConstProp || !low().constJump.isValid)
			regs.needExactMatch |= 0x7;
		regs.needExactMatch &= 0x7;
	}
}

// ---- mVUcompile's first pass ----------------------------------------------------------

Block Analyzer::compile(uint32_t startPC, const PipeState& pState)
{
	Block block;
	block.startPC = startPC;
	block.entry = pState;

	iPC = startPC / 4;

	// mVUinitFirstPass
	startPCw = iPC;
	branch = 0;
	count = 0;
	cycles = 0;
	p = 0;
	q = 0;
	regs = pState;
	blockState = pState;
	regs.needExactMatch = blockState.blockType ? 7 : 0;
	regs.blockType = 0;
	regs.viBackUp = 0;
	regs.flagInfo = 0;
	sFlagHack = settings_.flagHack ? 1 : 0;
	for (int i = 0; i < 16; i++)
		constReg[i] = ConstInfo{};
	constReg[15].isValid = regs.vi15v;
	constReg[15].regValue = regs.vi15v ? regs.vi15 : 0;

	const uint32_t endCount = pState.blockType ? 1 : memSize_ / 8;
	branch = 0;
	for (int b = 0; count < endCount;)
	{
		incPC(1);
		// startLoop
		info() = OpInfo{};
		regsTemp = TempRegInfo{};

		incCycles(1);
		upperOp(0);
		// mVUcheckBadOp
		if (info().isBadOp && code != 0x8000033c)
			info().isEOB = true;
		if (curI() & kEbit)
		{
			// eBitPass1
			if (regs.blockType != 1)
			{
				b = 1;
				up().eBit = true;
			}
			if (isVU0())
				regs.needExactMatch |= 7;
		}
		if ((curI() & kMbit) && isVU0())
		{
			if (xPC() > 0)
			{
				incPC(-2);
				if (!(curI() & kMbit))
				{
					incPC(2);
					up().mBit = true;
				}
				else
				{
					incPC(2);
				}
			}
			else
			{
				up().mBit = true;
			}
		}
		if (curI() & kIbit)
		{
			low().isNOP = true;
			up().iBit = true;
			if (settings_.ibitHack)
			{
				setupRange(static_cast<int32_t>(xPC()), false);
				if (b < 2)
					setupRange(static_cast<int32_t>(xPC() + 4), true);
			}
		}
		else
		{
			incPC(-1);
			if (settings_.ibitHack)
			{
				// Instructions that may hold a constant the I-bit game fix guards.
				const uint32_t upper = code >> 25;
				if (upper == 0x1 || upper == 0x0 || upper == 0x4 || upper == 0x5 || upper == 0x8 || upper == 0x9 ||
					(upper == 0x40 && (code & 0x3F) == 0x32))
				{
					incPC(1);
					setupRange(static_cast<int32_t>(xPC()), false);
					if (b < 2)
						setupRange(static_cast<int32_t>(xPC() + 2), true);
					incPC(-1);
				}
			}
			lowerOp(0);
			incPC(1);
		}
		if (curI() & kDbit)
			up().dBit = true;
		if (curI() & kTbit)
			up().tBit = true;
		setCycles();

		// XGKICK information
		if (!low().isKick)
		{
			regs.xgkickcycles += 1 + stall();
			if (low().isMemWrite)
			{
				low().kickcycles = regs.xgkickcycles;
				regs.xgkickcycles = 0;
			}
		}
		else
		{
			// The XGKICK counts as one cycle of the transfer.
			regs.xgkickcycles = 1;
			low().kickcycles = 0;
		}

		info().readQ = q;
		info().writeQ = !q;
		info().readP = p && isVU1();
		info().writeP = !p && isVU1();
		count++;

		if (b >= 2)
		{
			info().isEOB = true;
			if (b == 3)
				info().isBdelay = true;
			branchWarning();
			if (regs.xgkickcycles)
			{
				low().kickcycles = regs.xgkickcycles;
				regs.xgkickcycles = 0;
			}
			break;
		}
		else if (b == 1)
		{
			b = 2;
		}
		if (branch)
		{
			setFlagInfo();
			eBitWarning();
			b = 3;
			branch = 0;
		}
		if (up().mBit && !b && !up().eBit)
		{
			regs.needExactMatch |= 7;
			if (regs.xgkickcycles)
			{
				low().kickcycles = regs.xgkickcycles;
				regs.xgkickcycles = 0;
			}
			break;
		}
		if (info().isEOB)
		{
			if (regs.xgkickcycles)
			{
				low().kickcycles = regs.xgkickcycles;
				regs.xgkickcycles = 0;
			}
			break;
		}
		incPC(1);
	}

	regs.vi15 = 0; // without constant propagation
	regs.vi15v = 0;
	setFlags(block.flagCycles);
	optimizePipeState();

	// mVUtestCycles: VU0's cycles scaled by the EE cycle rate setting.
	if (isVU0() && settings_.eeCycleRate != 0 && (!settings_.vuSyncHack || settings_.eeCycleRate < 0))
	{
		switch (std::min(settings_.eeCycleRate, static_cast<int>(cycles)))
		{
			case -3: cycles = static_cast<uint32_t>(cycles * 2.0f); break;
			case -2: cycles = static_cast<uint32_t>(cycles * 1.6666667f); break;
			case -1: cycles = static_cast<uint32_t>(cycles * 1.3333333f); break;
			case 1: cycles = static_cast<uint32_t>(cycles / 1.3f); break;
			case 2: cycles = static_cast<uint32_t>(cycles / 1.8f); break;
			case 3: cycles = static_cast<uint32_t>(cycles / 3.0f); break;
			default: break;
		}
	}

	block.count = count;
	block.cycles = cycles;
	block.end = regs;
	block.endP = p;
	block.endQ = q;
	block.ops.resize(count);
	block.words.resize(count);
	const uint32_t pairs = memSize_ / 8;
	for (uint32_t i = 0; i < count; i++)
	{
		const uint32_t pair = (startPC / 8 + i) % pairs;
		block.ops[i] = infos_.info[pair];
		block.words[i] = static_cast<uint64_t>(micro_[pair * 2]) | static_cast<uint64_t>(micro_[pair * 2 + 1]) << 32;
	}
	return block;
}

} // namespace

Block analyzeBlock(int vu, const uint32_t* micro, uint32_t memSize, uint32_t startPC, const PipeState& pState, const Settings& settings,
	InfoArray& infos, RangeSink* ranges)
{
	Analyzer analyzer(vu, micro, memSize, settings, infos, ranges);
	return analyzer.compile(startPC, pState);
}

} // namespace ps2n::vu::mvu
