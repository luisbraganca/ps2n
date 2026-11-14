// SPDX-License-Identifier: GPL-3.0-or-later
//
// PCSX2's VU recompiler (microVU) as a model: what it decides about a block
// of micro code before it generates any host code. PS2Native's VU
// interpreter follows it (docs/adr/0022-vu-micro-programs-compiled-as-step-g11.md),
// and so will the translator, so that a VU program takes the time it takes
// under stock PCSX2 and its results are microVU's (ADR 0005, ADR 0011).
//
// microVU compiles a block in two passes. The first, ported here, goes over
// the block's instruction pairs from the pipeline state the block is entered
// with and decides everything that is not a value: each pair's stall, which
// of four instances of the status, MAC and clip flags it writes and reads,
// which Q and P instance, whether the lower instruction runs before the
// upper or not at all, how a branch reads a VI register written just before
// it, where the block ends and how many cycles it takes. The second pass
// (the interpreter's, runtime/vu_interp.cpp) carries the instructions out
// with that.
//
// Derived from PCSX2 v2.9.170: pcsx2/x86/microVU_Compile.inl (mVUcompile's
// first pass, mVUsetCycles, mVUincCycles, mVUoptimizePipeState),
// microVU_Analyze.inl, microVU_Flags.inl (mVUsetFlags, mVUflagPass,
// mVUsetFlagInfo), the first-pass parts of microVU_Upper.inl and
// microVU_Lower.inl, the structures of microVU_IR.h. Copyright 2002-2026
// PCSX2 Dev Team, GPL-3.0+. The names in the comments are microVU's, so
// that each piece can be read beside its original.
//
// Nothing here depends on PCSX2: the settings that change the analysis
// come in Settings, micro memory as words.
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace ps2n::vu::mvu {

// ---- the pipeline state a block is entered with (microRegInfo) -----------------------

// Cycles until each component of a VF register is written (regCycleInfo; four
// bits each in microVU, and never more than 4 here).
struct RegCycles
{
	uint8_t x = 0, y = 0, z = 0, w = 0;
	bool operator==(const RegCycles&) const = default;
};

struct PipeState
{
	// The part a simple block search compares ("quick64").
	uint8_t needExactMatch = 0; // 1 status, 2 MAC, 4 clip: the next block needs the flags exact; 8: the flag pass found a flag write
	uint8_t flagInfo = 0; // the flag instances to go on from: clip << 6 | MAC << 4 | status << 2
	uint8_t q = 0; // cycles until the Q result
	uint8_t p = 0; // cycles until the P result
	uint8_t xgkick = 0; // cycles until a pending XGKICK
	uint8_t viBackUp = 0; // the VI register written in a branch's delay slot
	uint8_t blockType = 0; // 0 normal; 1 an E bit in a delay slot; 2 a branch in a delay slot
	uint8_t r = 0; // cycles until R is written
	// The rest, compared only when the state asks for an exact match.
	uint32_t xgkickcycles = 0;
	uint8_t unused = 0;
	uint8_t vi15v = 0; // vi15's constant valid (constant propagation, which microVU leaves off)
	uint16_t vi15 = 0;
	uint8_t VI[16] = {};
	RegCycles VF[32] = {};

	// Every byte of it is a value (it has no padding): compared as memory.
	bool operator==(const PipeState& other) const { return std::memcmp(this, &other, sizeof(PipeState)) == 0; }

	// The first eight bytes as microVU packs them, in the order above.
	uint64_t quick64() const
	{
		return static_cast<uint64_t>(needExactMatch) | static_cast<uint64_t>(flagInfo) << 8 | static_cast<uint64_t>(q) << 16 |
			static_cast<uint64_t>(p) << 24 | static_cast<uint64_t>(xgkick) << 32 | static_cast<uint64_t>(viBackUp) << 40 |
			static_cast<uint64_t>(blockType) << 48 | static_cast<uint64_t>(r) << 56;
	}
};

static_assert(std::has_unique_object_representations_v<PipeState> && sizeof(PipeState) == 160,
              "PipeState is compared as memory: it may hold no padding");

// ---- what the first pass decides for one instruction pair (microOp) -----------------

struct VFReg // microVFreg
{
	uint8_t reg = 0, x = 0, y = 0, z = 0, w = 0;
};

struct VIReg // microVIreg
{
	uint8_t reg = 0, used = 0;
};

struct ConstInfo // microConstInfo
{
	uint8_t isValid = 0;
	uint32_t regValue = 0;
};

struct UpperInfo // microUpperOp
{
	bool eBit = false, iBit = false, mBit = false, tBit = false, dBit = false;
	VFReg VF_write;
	VFReg VF_read[2];
};

struct LowerInfo // microLowerOp
{
	VFReg VF_write;
	VFReg VF_read[2];
	VIReg VI_write;
	VIReg VI_read[2];
	ConstInfo constJump;
	uint32_t branch = 0; // 0 none, 1 B, 2 BAL, 3-8 the conditional branches, 9 JR, 10 JALR
	uint32_t kickcycles = 0;
	bool badBranch = false; // a branch with another in its delay slot
	bool evilBranch = false; // the branch in that delay slot
	bool isNOP = false;
	bool isFSSET = false;
	bool noWriteVF = false; // the upper instruction writes the same VF register: the lower's result is dropped
	bool backupVI = false; // the VI register's value before this instruction is kept for a branch
	bool memReadIs = false; // the branch reads is from that kept value
	bool memReadIt = false; // and it
	bool readFlags = false;
	bool isMemWrite = false;
	bool isKick = false;
};

struct FlagInst // microFlagInst
{
	bool doFlag = false; // this instruction updates the flag
	bool doNonSticky = false; // and the status flag's non-sticky bits
	uint8_t write = 0; // the instance it writes
	uint8_t lastWrite = 0; // the instance written last before it
	uint8_t read = 0; // the instance a lower instruction reads
};

struct OpInfo // microOp
{
	uint8_t stall = 0;
	bool isBadOp = false;
	bool isEOB = false; // the block's last pair
	bool isBdelay = false; // in a branch's delay slot
	bool swapOps = false; // the lower instruction runs first
	bool backupVF = false; // and the upper reads the VF register it writes
	bool doXGKICK = false;
	uint32_t XGKICKPC = 0;
	bool doDivFlag = false;
	int readQ = 0, writeQ = 0, readP = 0, writeP = 0;
	FlagInst sFlag, mFlag, cFlag;
	UpperInfo uOp;
	LowerInfo lOp;
};

// When each flag instance was last written, in the block's cycles (microFlagCycles).
struct FlagCycles
{
	int xStatus[4] = {};
	int xMac[4] = {};
	int xClip[4] = {};
	int cycles = 0;
};

// What changes the analysis: game fixes and settings of PCSX2's, read by
// the caller from its configuration.
struct Settings
{
	bool flagHack = true; // CHECK_VU_FLAGHACK (Speedhacks.vuFlagHack): status flags left out where nothing reads them
	bool ibitHack = false; // Gamefixes.IbitHack
	bool xgkickHack = false; // CHECK_XGKICKHACK (Gamefixes.XgKickHack)
	bool vuSyncHack = false; // Gamefixes.VUSyncHack
	bool fullVu0SyncHack = false; // Gamefixes.FullVU0SyncHack
	int eeCycleRate = 0; // Speedhacks.EECycleRate, which scales VU0's block cycles
};

// ---- the first pass over one block (mVUcompile's first loop) ------------------------

// The analysis of one block, kept for the second pass.
struct Block
{
	uint32_t startPC = 0; // byte address of its first pair
	uint32_t count = 0; // pairs (mVUcount)
	uint32_t cycles = 0; // mVUcycles, as mVUtestCycles counts them
	PipeState entry; // the state it was compiled for (mVUpBlock->pState)
	PipeState end; // mVUregs after the first pass: what its successors are entered with
	FlagCycles flagCycles; // mFC
	int endP = 0, endQ = 0; // mVU.p and mVU.q after the first pass
	std::vector<OpInfo> ops; // one per pair, in order
	// Each pair's words as they were when the block was compiled (lower |
	// upper << 32): microVU's code is generated from them, and runs as they
	// were even if memory changes where its program is not compared (with the
	// I-bit game fix, the immediates it reads from memory as it runs instead).
	std::vector<uint64_t> words;
};

// microVU keeps the analysis of every pair of micro memory in one array per
// VU (mVU.prog.IRinfo.info), cleared one pair at a time as a pass reaches
// it; a few checks read the pair before a block's first, which then holds
// what an earlier compile left. Keep one per VU and pass it to every
// analysis, as microVU does.
struct InfoArray
{
	std::vector<OpInfo> info; // memSize / 8 entries
};

// Ranges of micro memory a program's code was compiled from
// (mVUsetupRange); the shadow of microVU's program cache keeps them. The
// first pass reports the calls microVU makes there (with the I-bit game fix).
class RangeSink
{
public:
	virtual ~RangeSink() = default;
	virtual void setupRange(int32_t pc, bool isStartPC) = 0;
};

// Analyses the block that starts at `startPC` (a byte address) when entered
// with `pState`. `blockState` is the state as the block manager holds it
// (mVUpBlock->pState), which is `pState` as given. `micro` is the VU's micro
// memory, `memSize` its length in bytes (0x1000 for VU0, 0x4000 for VU1).
Block analyzeBlock(int vu, const uint32_t* micro, uint32_t memSize, uint32_t startPC, const PipeState& pState, const Settings& settings,
	InfoArray& infos, RangeSink* ranges);

} // namespace ps2n::vu::mvu
