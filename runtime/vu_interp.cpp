// SPDX-License-Identifier: GPL-3.0-or-later
//
// See vu_interp.h. The names in the comments are microVU's (PCSX2 v2.9.170,
// pcsx2/x86/microVU*.{cpp,h,inl}, Copyright 2002-2026 PCSX2 Dev Team,
// GPL-3.0+), so that each piece can be read beside the code it follows.

#include "vu_interp.h"

#include "ps2n/ops_vu0.h"
#include "ps2n/vu_mvu.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Config.h"
#include "pcsx2/Dmac.h"
#include "pcsx2/VU.h"
#include "pcsx2/VUmicro.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <smmintrin.h>

namespace x86Emitter
{
extern bool use_avx; // PCSX2's emitter (common/emitter/x86types.h)
}

// PCSX2's own functions for an XGKICK, in microVU (microVU_Lower.inl).
void mVU_XGKICK_(u32 addr);
void _vuXGKICKTransfermVU(bool flush);

namespace ps2n::vu {
namespace {

using mvu::Block;
using mvu::OpInfo;
using mvu::PipeState;

namespace d = ps2n::ee::vu0::detail;

constexpr uint32_t kIbit = 1u << 31;
constexpr uint32_t kEbit = 1u << 30;

// ---- the record of programs and blocks (microVU.cpp, microVU.h) ---------------------------

struct Range // microRange
{
	int32_t start;
	int32_t end;
};

struct Program;
struct ShadowBlock;

struct JumpCacheEntry // microJumpCache
{
	Program* prog = nullptr;
	ShadowBlock* block = nullptr;
};

// How a block's code ends: what mVUcompile's second pass generates last.
enum class Exit : uint8_t
{
	EBit, // mVUendProgram(isEbit 1): the program ends
	MBit, // VU0 stops after a pair with the M bit, its state kept for the next run
	Branch, // B, BAL (normBranch)
	BranchEBit, // B, BAL with the E bit: the program ends at the target
	CondBranch, // IBEQ ... (condBranch)
	CondBranchEBit, // a conditional branch with the E bit
	Jump, // JR, JALR (normJump, normJumpCompile)
	JumpEBit, // JR, JALR with the E bit
	EvilJump, // a block of one pair after a branch in a delay slot: on to that branch's target
	EvilJumpEBit, // the same pair with the E bit
	Unsupported, // something not carried out yet
};

// What an end of a program decided when it was compiled (mVUendProgram's
// mVUincCycles(100)): which Q and P instances are the registers', and
// whether a division's flags or an XGKICK are still to come.
struct EndInfo
{
	int qInst = 0;
	int pInst = 0;
	bool doDivFlag = false;
	bool doXGKICK = false;
};

// A pair with the T bit (mVUDoTBit): if the T bit is enabled in FBRST, the
// program ends after it, raising the VU's interrupt.
struct TBitEnd
{
	uint32_t pair = 0; // its index in the block
	uint32_t tpc = 0; // where the program would go on
	EndInfo end;
};

struct ShadowBlock // microBlock, with what the second pass decided
{
	PipeState pState; // the state it was compiled for
	PipeState pStateEnd; // the state at its end, for an indirect jump
	Block a; // the first pass
	Exit exit = Exit::EBit;
	uint32_t exitPC = 0; // TPC when the program ends here
	uint32_t target = 0; // the branch's target
	uint32_t notTakenPC = 0;
	ShadowBlock* taken = nullptr;
	ShadowBlock* notTaken = nullptr;
	int linkP = 0, linkQ = 0; // mVU.p, mVU.q where the block links on (shufflePQ)
	EndInfo end; // at an end with the E bit
	std::vector<TBitEnd> tBits;
	std::unique_ptr<JumpCacheEntry[]> jumpCache;
	std::string unsupported;
	std::vector<int8_t> preload; // what mvuPreloadRegisters loads (PreloadOf): VI n, or 16 + VF n
};

class BlockManager // microBlockManager
{
public:
	ShadowBlock* search(const PipeState& s, bool flagHack)
	{
		if (s.needExactMatch)
		{
			for (size_t i = 0; i < fBlockList_.size(); i++)
			{
				if (fBlockList_[i]->pState == s)
				{
					ShadowBlock* b = fBlockList_[i];
					if (i != 0)
					{
						fBlockList_.erase(fBlockList_.begin() + static_cast<std::ptrdiff_t>(i));
						fBlockList_.insert(fBlockList_.begin(), b);
					}
					return b;
				}
			}
		}
		else
		{
			const uint64_t quick = s.quick64();
			for (const Ref& ref : quickLookup_)
			{
				if (flagHack)
				{
					if ((ref.quick & ~0x3002ull) != (quick & ~0x3002ull))
						continue;
				}
				else if (ref.quick != quick)
				{
					continue;
				}
				return ref.block;
			}
		}
		return nullptr;
	}

	ShadowBlock* add(const PipeState& s, bool flagHack)
	{
		if (ShadowBlock* found = search(s, flagHack))
			return found;
		blocks_.push_back(std::make_unique<ShadowBlock>());
		ShadowBlock* b = blocks_.back().get();
		b->pState = s;
		if (s.needExactMatch)
			fBlockList_.push_back(b);
		quickLookup_.push_back({b, s.quick64()});
		return b;
	}

private:
	struct Ref
	{
		ShadowBlock* block;
		uint64_t quick;
	};
	std::vector<ShadowBlock*> fBlockList_;
	std::vector<Ref> quickLookup_;
	std::vector<std::unique_ptr<ShadowBlock>> blocks_;
};

struct Program // microProgram
{
	int idx = 0;
	std::deque<Range> ranges;
	std::vector<uint8_t> data; // micro memory as compiled, where its ranges are
	uint32_t startPC = 0; // pair index
	std::vector<std::unique_ptr<BlockManager>> block; // per pair
};

// ---- one VU: microVU's struct, its program manager and its run-time variables ---------------

struct Vu
{
	int index = 0;
	uint32_t memSize = 0; // micro memory, bytes
	VURegs* R = nullptr;

	// microProgManager
	std::vector<std::deque<Program*>> prog; // per pair: the programs started there, most recently found first
	struct Quick
	{
		BlockManager* block = nullptr;
		Program* prog = nullptr;
	};
	std::vector<Quick> quick;
	Program* cur = nullptr;
	int cleared = 1;
	int isSame = -1;
	int total = 0;
	PipeState lpState;
	std::vector<std::unique_ptr<Program>> owned;
	mvu::InfoArray infos;
	mvu::Settings settings;

	// microVU's variables
	uint32_t divFlag = 0;
	uint32_t VIbackup = 0;
	uint32_t VIxgkick = 0;
	uint32_t branch = 0;
	uint32_t badBranch = 0;
	uint32_t evilBranch = 0;
	uint32_t evilevilBranch = 0;
	uint32_t totalCycles = 0;
	int32_t cycles = 0;

	// What microVU keeps in host registers while it runs: four instances of
	// each flag (the status flag in microVU's layout) and P and Q (xmmPQ:
	// Q, its other instance, P, its other instance).
	uint32_t statFlag[4] = {};
	uint32_t macFlag[4] = {};
	uint32_t clipFlag[4] = {};
	uint32_t pq[4] = {};

	InterpreterStats stats;
	std::string unsupported;
	std::FILE* log = nullptr;
};

Vu s_vu[2];

void ResetVu(Vu& V) // mVUreset
{
	V.R = V.index ? &VU1 : &VU0;
	V.memSize = V.index ? 0x4000 : 0x1000;
	const uint32_t pairs = V.memSize / 8;
	V.prog.assign(pairs, {});
	V.quick.assign(pairs, {});
	V.owned.clear();
	V.cur = nullptr;
	V.cleared = 1;
	V.isSame = -1;
	V.total = 0;
	V.lpState = PipeState{};
	V.R->nextBlockCycles = 0;
	V.infos.info.assign(pairs, OpInfo{});
}

void ClearVu(Vu& V) // mVUclear
{
	if (!V.cleared)
	{
		V.cleared = 1;
		V.lpState = PipeState{};
		for (Vu::Quick& q : V.quick)
			q = {};
	}
}

const uint8_t* Micro(const Vu& V) { return V.R->Micro; }
const uint32_t* MicroWords(const Vu& V) { return reinterpret_cast<const uint32_t*>(V.R->Micro); }

void CacheProg(Vu& V, Program& prog) // mVUcacheProg
{
	const Range& r = V.cur->ranges.front();
	if (r.end > r.start)
		std::memcpy(prog.data.data() + r.start, Micro(V) + r.start, static_cast<size_t>(r.end - r.start));
}

// mVUsetupRange
void SetupRange(Vu& V, int32_t pc, bool isStartPC)
{
	std::deque<Range>& ranges = V.cur->ranges;
	const int32_t memSize = static_cast<int32_t>(V.memSize);
	if (pc > memSize)
		return;
	const int32_t curPC = (!isStartPC && !ranges.empty() && ranges.front().start > pc && pc == 0) ? memSize : pc;
	if (isStartPC)
	{
		for (auto it = ranges.begin(); it != ranges.end(); ++it)
		{
			if (curPC >= it->start && curPC <= it->end && it->start != it->end)
			{
				const Range r = *it;
				ranges.erase(it);
				ranges.push_front(r);
				return;
			}
		}
		ranges.push_front({curPC, -1});
		return;
	}
	if (ranges.front().end >= curPC)
		return;
	Range& front = ranges.front();
	if (front.start <= curPC)
	{
		front.end = curPC;
		int32_t rStart = front.start;
		int32_t rEnd = front.end;
		for (auto it = ranges.begin() + 1; it != ranges.end();)
		{
			if ((it->start >= rStart && it->start <= rEnd) || (it->end >= rStart && it->end <= rEnd))
			{
				ranges.front().start = rStart = std::min(it->start, rStart);
				ranges.front().end = rEnd = std::max(it->end, rEnd);
				it = ranges.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	else
	{
		front.end = memSize;
		ranges.push_front({0, curPC});
	}
	CacheProg(V, *V.cur);
}

class Ranges final : public mvu::RangeSink
{
public:
	explicit Ranges(Vu& V)
		: V_(V)
	{
	}
	void setupRange(int32_t pc, bool isStartPC) override { SetupRange(V_, pc, isStartPC); }

private:
	Vu& V_;
};

bool CmpProg(Vu& V, Program& prog) // mVUcmpProg
{
	for (const Range& r : prog.ranges)
	{
		if (r.end < r.start)
			return false; // a range left open; microVU would read past it
		if (std::memcmp(prog.data.data() + r.start, Micro(V) + r.start, static_cast<size_t>(r.end - r.start)) != 0)
			return false;
	}
	V.cleared = 0;
	V.cur = &prog;
	V.isSame = -1;
	return true;
}

Program* CreateProg(Vu& V, uint32_t startPC) // mVUcreateProg; startPC: pair index
{
	auto prog = std::make_unique<Program>();
	prog->idx = V.total++;
	prog->startPC = startPC;
	prog->data.assign(V.memSize, 0);
	prog->block.resize(V.memSize / 8);
	if (V.log)
	{
		std::fprintf(V.log, "microVU%d: Cached Prog = [%03d] [PC=%04x] [List=%02d]\n", V.index, prog->idx, startPC * 8,
			static_cast<int>(V.prog[startPC].size()) + 1);
	}
	V.stats.programs++;
	Program* p = prog.get();
	V.owned.push_back(std::move(prog));
	return p;
}

ShadowBlock* Compile(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp);

BlockManager& BlocksAt(Vu& V, uint32_t pair) // blockCreate
{
	std::unique_ptr<BlockManager>& bm = V.cur->block[pair];
	if (!bm)
		bm = std::make_unique<BlockManager>();
	return *bm;
}

ShadowBlock* EntryGet(Vu& V, BlockManager& bm, uint32_t startPC, const PipeState& pState, bool fromLp) // mVUentryGet
{
	if (ShadowBlock* b = bm.search(pState, V.settings.flagHack))
		return b;
	return Compile(V, startPC, pState, fromLp);
}

ShadowBlock* BlockFetch(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp) // mVUblockFetch
{
	startPC &= V.memSize - 8;
	return EntryGet(V, BlocksAt(V, startPC / 8), startPC, pState, fromLp);
}

ShadowBlock* SearchProg(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp) // mVUsearchProg
{
	const uint32_t startPair = V.R->start_pc / 8;
	Vu::Quick& quick = V.quick[startPair];
	std::deque<Program*>& list = V.prog[startPair];
	if (!quick.prog)
	{
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if (CmpProg(V, **it))
			{
				Program* found = *it;
				quick.block = found->block[startPC / 8].get();
				quick.prog = found;
				list.erase(it);
				list.push_front(found);
				if (!quick.block)
					return BlockFetch(V, startPC, pState, fromLp);
				return EntryGet(V, *quick.block, startPC, pState, fromLp);
			}
		}
		V.cleared = 0;
		V.isSame = 1;
		V.cur = CreateProg(V, startPair);
		ShadowBlock* entry = BlockFetch(V, startPC, pState, fromLp);
		quick.block = V.cur->block[startPC / 8].get();
		quick.prog = V.cur;
		list.push_front(V.cur);
		return entry;
	}
	V.isSame = -1;
	V.cur = quick.prog;
	quick.block = V.cur->block[startPC / 8].get();
	if (!quick.block)
		return BlockFetch(V, startPC, pState, fromLp);
	return EntryGet(V, *quick.block, startPC, pState, fromLp);
}

// normBranchCompile
ShadowBlock* BranchCompile(Vu& V, uint32_t branchPC, const PipeState& regs)
{
	BlockManager& bm = BlocksAt(V, branchPC / 8);
	if (ShadowBlock* b = bm.search(regs, V.settings.flagHack))
		return b;
	return Compile(V, branchPC, regs, false);
}

// The host registers microVU keeps VI registers in (microRegAlloc's
// constructor and reset): not eax and ecx (gprT1, gprT2), esp, r12 to r15
// (the status flag's instances), nor rbx (RTEXTPTR: microVU sets the
// emitter's text pointer before it compiles); rbp (RFASTMEMBASE) in micro
// mode, yes.
constexpr bool kGprUsable[16] = {false, false, true, false, false, true, true, true, true, true, true, true, false, false, false, false};
// Those a call may change (xRegisterBase::IsCallerSaved).
#ifdef _WIN32
constexpr bool kGprCallerSaved[16] = {true, true, true, false, false, false, false, false, true, true, true, true, false, false, false, false};
#else
constexpr bool kGprCallerSaved[16] = {true, true, true, false, false, false, true, true, true, true, true, true, false, false, false, false};
#endif
// getFreeGPRCount, as microVU counts it: the registers it may not use.
constexpr int kFreeGprCount = 8;
// The XMM registers a call may change (xRegisterSSE::IsCallerSaved).
#ifdef _WIN32
constexpr bool kXmmCallerSaved[15] = {true, true, true, true, true, true, false, false, false, false, false, false, false, false, false};
#else
constexpr bool kXmmCallerSaved[15] = {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true};
#endif
// Whether PCSX2's emitter writes AVX (VMManager sets it from the processor).
bool UseAvx()
{
	return ::x86Emitter::use_avx;
}

// Instructions whose result under microVU depends on what its allocator holds
// in XMM registers, which the interpreter does not follow: allocReg loads a
// register it does not hold as the instruction's field asks (one component
// alone in the first lane, VF00 without w as zeros) and copies or shuffles
// a whole one it holds. OPMULA, ESADD, ERSADD, ELENG, ERLENG and ESUM load
// their operand so and read its other lanes too (OPMSUB and the arctangents
// load theirs whole). Assemblers write xyz in their field (xyzw for ESUM),
// where all of this agrees.
const char* QuirkOf(int vu, uint32_t upper, uint32_t lower)
{
	auto single = [](uint32_t dest) { return dest == 8 || dest == 4 || dest == 2 || dest == 1; };
	if ((upper & 0x7ff) == 0x2fe && single((upper >> 21) & 0xf))
		return "OPMULA with one component in its field";
	if (vu == 0 || (upper & kIbit) || (lower >> 25) != 0x40 || (lower & 0x3c) != 0x3c)
		return nullptr;
	const uint32_t table = lower & 3, row = (lower >> 6) & 0x1f, dest = (lower >> 21) & 0xf;
	if ((row == 28 || (row == 29 && table == 2)) && single(dest))
		return "an EFU sum with one component in its field";
	if (row == 29 && table == 2 && ((lower >> 11) & 31) == 0 && !(dest & 1))
		return "ESUM of VF00 without w in its field";
	return nullptr;
}

// mvuPreloadRegisters: the VF and VI registers microVU loads at a block's
// start, from the record of the pairs from there on as it is when the block
// is compiled (beyond the block's end too: the scan stops at a branch or a
// pending XGKICK). 16 + n for VF register n, else the VI register, in order.
// canPreload() always holds: the counts never go below what it asks.
std::vector<int8_t> PreloadOf(const Vu& V, uint32_t startPC, bool onePair)
{
	std::vector<int8_t> seq;
	const uint32_t endCount = onePair ? 1 : V.memSize / 8;
	const uint32_t progMemMask = V.memSize / 4 - 1;
	uint32_t vfsLoaded = 0, visLoaded = 0;
	int freeRegs = 15, freeGprs = kFreeGprCount;
	auto preloadVF = [&](uint8_t reg) {
		if (freeRegs <= 3 || reg == 0 || (vfsLoaded & (1u << (reg & 31))) != 0)
			return;
		seq.push_back(static_cast<int8_t>(16 + reg));
		vfsLoaded |= 1u << (reg & 31);
		freeRegs--;
	};
	auto preloadVI = [&](uint8_t reg) {
		if (freeGprs <= 1 || reg == 0 || (visLoaded & (1u << reg)) != 0)
			return;
		seq.push_back(static_cast<int8_t>(reg));
		visLoaded |= 1u << reg;
		freeGprs--;
	};
	uint32_t iPC = startPC / 4;
	for (uint32_t x = 0; x < endCount; x++)
	{
		iPC = (iPC + 1) & progMemMask; // incPC(1): a pair is looked at twice, its words one at a time
		const OpInfo& info = V.infos.info[iPC / 2];
		if (info.doXGKICK)
			break;
		for (int i = 0; i < 2; i++)
		{
			preloadVF(info.uOp.VF_read[i].reg);
			preloadVF(info.lOp.VF_read[i].reg);
			if (info.lOp.VI_read[i].used)
				preloadVI(info.lOp.VI_read[i].reg);
		}
		const mvu::VFReg& u = info.uOp.VF_write;
		if (u.reg != 0 && (!u.x || !u.y || !u.z || !u.w))
			preloadVF(u.reg);
		const mvu::VFReg& l = info.lOp.VF_write;
		if (l.reg != 0 && (!l.x || !l.y || !l.z || !l.w))
			preloadVF(l.reg);
		if (info.lOp.branch)
			break;
	}
	return seq;
}

int FindFlagInst(const int* fFlag, int cycles)
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

// What mVUendProgram and mVUDTendProgram leave on the record of the pair
// they compile at (iPC), on top of what is there: mVUincCycles(100)'s marks
// from the state at the block's end, and, with the division flag to set,
// sFLAG.doFlag and sFLAG.write at the end's status instance. The rest of the
// block (after a T bit) runs with them, and later compiles read them.
void MarkEnd(OpInfo& info, const Block& a, uint32_t pairPC)
{
	if (a.end.q > 4)
		info.doDivFlag = true;
	if (a.end.xgkick)
	{
		info.doXGKICK = true;
		info.XGKICKPC = pairPC;
	}
	if (info.doDivFlag)
	{
		info.sFlag.doFlag = true;
		info.sFlag.write = FindFlagInst(a.flagCycles.xStatus, 0x7fffffff);
	}
}

// What mVUendProgram does to the record of the pair it compiles at: clears
// it (std::memset), then marks it.
void ClearAndMarkEnd(Vu& V, const Block& a, uint32_t pair)
{
	V.infos.info[pair] = OpInfo{};
	MarkEnd(V.infos.info[pair], a, pair * 8);
}

// What mVUendProgram's mVUincCycles(100) leaves, from the state at the end
// of the first pass.
EndInfo EndOf(const Block& a, int q, int p, bool vu1)
{
	EndInfo e;
	const PipeState& s = a.end;
	e.qInst = q;
	e.pInst = p;
	if (s.q)
	{
		if (s.q > 4)
			e.doDivFlag = true;
		e.qInst ^= 1;
	}
	if (s.p)
		e.pInst ^= 1;
	e.doXGKICK = s.xgkick != 0;
	if (!vu1)
		e.pInst = 0;
	return e;
}

uint32_t BranchTarget(const Vu& V, uint32_t branchPC, uint32_t lower) // branchAddr
{
	const int32_t imm11 = (lower & 0x400) ? static_cast<int32_t>(0xfffffc00u | (lower & 0x3ff)) : static_cast<int32_t>(lower & 0x3ff);
	const uint32_t mask = V.memSize / 4 - 1;
	return (((branchPC / 4 + 2) + static_cast<uint32_t>(imm11 * 2)) & mask) * 4;
}

// mVUcompile: the first pass (ps2n/vu_mvu.h), then what the second pass
// does to the record: the ranges, and the blocks a branch leads to.
ShadowBlock* Compile(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp)
{
	V.stats.compiles++;
	SetupRange(V, static_cast<int32_t>(startPC), true);
	if (!fromLp)
		V.lpState = pState; // mVUinitFirstPass
	BlockManager& bm = BlocksAt(V, startPC / 8);
	ShadowBlock* b = bm.add(pState, V.settings.flagHack);
	Ranges ranges(V);
	b->a = mvu::analyzeBlock(V.index, MicroWords(V), V.memSize, startPC, pState, V.settings, V.infos, &ranges);
	const Block& a = b->a;
	const bool vu1 = V.index != 0;
	const bool isEvilBlock = b->pState.blockType == 2;
	int p = a.endP, q = a.endQ;

	auto unsupported = [&](const char* what, uint32_t pc) {
		b->exit = Exit::Unsupported;
		char text[160];
		std::snprintf(text, sizeof(text), "VU%d: %s at %04x", V.index, what, pc);
		b->unsupported = text;
	};
	// What the interpreter does not follow, noted for the run to stop at
	// while the compile goes on as microVU's does.
	auto note = [&](const char* what, uint32_t pc) {
		if (!b->unsupported.empty())
			return;
		char text[160];
		std::snprintf(text, sizeof(text), "VU%d: %s at %04x", V.index, what, pc);
		b->unsupported = text;
	};

	// The second pass begins with the loads of mvuPreloadRegisters.
	b->preload = PreloadOf(V, startPC, pState.blockType != 0);

	for (uint32_t i = 0; i < a.count; i++)
	{
		const OpInfo& op = a.ops[i];
		const uint32_t pc = (startPC + i * 8) & (V.memSize - 8);
		const uint32_t upperWord = static_cast<uint32_t>(a.words[i] >> 32), lowerWord = static_cast<uint32_t>(a.words[i]);
		if (const char* what = QuirkOf(V.index, upperWord, lowerWord))
			note(what, pc);
		// mVU_XGKICK takes a pending XGKICK on itself and clears the mark.
		if (op.lOp.isKick && !op.lOp.isNOP)
			V.infos.info[pc / 8].doXGKICK = false;
		if (!op.isBdelay && !op.lOp.branch)
		{
			// The D bit microVU leaves alone (doDBitHandling is off).
			if (op.uOp.tBit)
			{
				// mVUDoTBit, mVUDTendProgram: compiled from the state at the
				// block's end, with mVUincCycles(100) on it. That turns P and Q
				// as the rest of the block compiles, and leaves its marks on
				// the next pair's analysis, which the rest of the block runs
				// with.
				const uint32_t nextPair = (pc / 8 + 1) % (V.memSize / 8);
				OpInfo& next = V.infos.info[nextPair];
				MarkEnd(next, a, nextPair * 8);
				if (a.end.q)
					q ^= 1;
				if (a.end.p)
					p ^= 1;
				if (i + 1 < a.count)
				{
					OpInfo& after = b->a.ops[i + 1];
					after.doDivFlag = next.doDivFlag;
					after.doXGKICK = next.doXGKICK;
					after.XGKICKPC = next.XGKICKPC;
					after.sFlag.doFlag = next.sFlag.doFlag;
					after.sFlag.write = next.sFlag.write;
				}
				TBitEnd t;
				t.pair = i;
				t.tpc = nextPair * 8;
				t.end.qInst = q;
				t.end.pInst = vu1 ? p : 0;
				t.end.doDivFlag = next.doDivFlag;
				t.end.doXGKICK = next.doXGKICK && nextPair * 8 >= next.XGKICKPC;
				// There mVU_XGKICK_DELAY empties the host registers a call may
				// change while the block compiles, on the path that ends: the
				// rest of the block, which does not end there, runs without
				// what it held in them.
				if (t.end.doXGKICK)
					note("the T bit with an XGKICK still to come", pc);
				b->tBits.push_back(t);
			}
			if (op.uOp.mBit && !op.uOp.eBit && !op.isEOB)
			{
				// The program stops after this pair; the next block is compiled
				// all the same.
				b->exit = Exit::MBit;
				b->exitPC = (pc + 8) & (V.memSize - 8);
				b->linkP = p;
				b->linkQ = q;
				b->pStateEnd = a.end;
				SetupRange(V, static_cast<int32_t>(b->exitPC), false);
				BranchCompile(V, b->exitPC, a.end);
				return b;
			}
		}
		if (isEvilBlock)
		{
			// normJumpCompile(isEvilJump): on to where the branch in the delay
			// slot goes (mVU.evilBranch), after linking as a jump does.
			SetupRange(V, static_cast<int32_t>(pc + 8), false);
			b->pStateEnd = a.end;
			b->linkP = p;
			b->linkQ = q;
			if (op.uOp.eBit)
			{
				b->exit = Exit::EvilJumpEBit;
				ClearAndMarkEnd(V, a, pc / 8);
				b->end = EndOf(a, 0, 0, vu1);
				return b;
			}
			b->exit = Exit::EvilJump;
			b->jumpCache = std::make_unique<JumpCacheEntry[]>(V.memSize / 8);
			return b;
		}
		if (!op.isBdelay)
		{
			if (pc + 8 == V.memSize)
			{
				SetupRange(V, static_cast<int32_t>(pc + 8), false);
				SetupRange(V, 0, true);
			}
			continue;
		}

		// The delay slot: the block ends at its branch (xPC after incPC(1), wrapped).
		SetupRange(V, static_cast<int32_t>((pc + 8) & (V.memSize - 8)), false);
		const uint32_t branchPC = (pc - 8) & (V.memSize - 8);
		const OpInfo& br = a.ops[i - 1];
		const uint32_t lower = static_cast<uint32_t>(a.words[i - 1]);
		// A branch in this delay slot: the blocks compiled from here are of one
		// pair each (the state at the end asks for that, blockType 2).
		if (br.uOp.tBit || br.uOp.mBit)
		{
			unsupported("the T or M bit on a branch", branchPC);
			return b;
		}
		// The D bit on a branch microVU leaves alone too, and a T bit in a
		// delay slot (only the branch's is looked at).
		b->linkP = p;
		b->linkQ = q;
		switch (br.lOp.branch)
		{
			case 1: // B
			case 2: // BAL (normBranch)
				b->target = BranchTarget(V, branchPC, lower);
				if (br.uOp.eBit)
				{
					b->exit = Exit::BranchEBit;
					ClearAndMarkEnd(V, a, b->target / 8); // mVUendProgram clears the pair it stands on
					b->exitPC = b->target;
					b->end = EndOf(a, q, p, vu1);
					return b;
				}
				b->exit = Exit::Branch;
				b->taken = BranchCompile(V, b->target, a.end);
				return b;
			case 9: // JR
			case 10: // JALR (normJump)
				b->pStateEnd = a.end;
				if (br.uOp.eBit)
				{
					b->exit = Exit::JumpEBit;
					ClearAndMarkEnd(V, a, branchPC / 8);
					b->end = EndOf(a, q, p, vu1);
					return b;
				}
				b->exit = Exit::Jump;
				b->jumpCache = std::make_unique<JumpCacheEntry[]>(V.memSize / 8);
				return b;
			default: // the conditional branches (condBranch)
			{
				b->target = BranchTarget(V, branchPC, lower);
				b->notTakenPC = (branchPC + 16) & (V.memSize - 8);
				if (br.uOp.eBit)
				{
					// condBranch links first (mVUsetupBranch: P and Q at their
					// first instances), then ends the program.
					b->exit = Exit::CondBranchEBit;
					ClearAndMarkEnd(V, a, branchPC / 8);
					b->end = EndOf(a, 0, 0, vu1);
					return b;
				}
				b->exit = Exit::CondBranch;
				const PipeState regs = a.end;
				if (ShadowBlock* nt = BlocksAt(V, b->notTakenPC / 8).search(regs, V.settings.flagHack))
				{
					b->notTaken = nt;
					b->taken = BranchCompile(V, b->target, regs);
				}
				else
				{
					b->notTaken = Compile(V, b->notTakenPC, regs, false);
					b->taken = BlockFetch(V, b->target, regs, false);
				}
				return b;
			}
		}
	}

	// The E bit's end.
	const uint32_t endPC = (startPC + a.count * 8) & (V.memSize - 8);
	SetupRange(V, static_cast<int32_t>(endPC), false); // xPC, wrapped; mVUsetupRange takes 0 for the end of memory
	b->exit = Exit::EBit;
	ClearAndMarkEnd(V, a, endPC / 8); // mVUendProgram clears the pair after the last
	b->exitPC = endPC;
	b->end = EndOf(a, q, p, vu1);
	return b;
}

// ---- carrying the instructions out (the second pass) ---------------------------------------

constexpr uint32_t kDivI = 0x1040000u;
constexpr uint32_t kDivD = 0x2080000u;

// The flag instances a block leaves for the code after it (getLastFlagInst).
int LastFlagInst(const PipeState& entry, const int* xFlag, int flagType, bool isEbit)
{
	if (isEbit)
		return FindFlagInst(xFlag, 0x7fffffff);
	if (entry.needExactMatch & (1 << flagType))
		return 3;
	return (((entry.flagInfo >> (2 * flagType + 2)) & 3) - 1) & 3;
}

void SortFlag(const int* fFlag, int* bFlag, int cycles) // sortFlag
{
	for (int i = 0; i < 4; i++)
	{
		bFlag[i] = FindFlagInst(fFlag, cycles);
		cycles++;
	}
}

class Runner
{
public:
	explicit Runner(Vu& V)
		: V(V)
		, R(*V.R)
		, vu1(V.index != 0)
	{
	}

	void Run(uint32_t startPC, uint32_t cycles);

private:
	// ---- registers -----------------------------------------------------------------------
	__m128 VF(uint32_t r) const { return d::Float(_mm_load_si128(reinterpret_cast<const __m128i*>(R.VF[r].UL))); }
	__m128 ACC() const { return d::Float(_mm_load_si128(reinterpret_cast<const __m128i*>(R.ACC.UL))); }
	void WriteVF(uint32_t r, uint32_t dest, __m128 v)
	{
		if (r == 0)
			return;
		__m128i* reg = reinterpret_cast<__m128i*>(R.VF[r].UL);
		_mm_store_si128(reg, d::Merge(_mm_load_si128(reg), d::Int(v), dest));
	}
	void WriteACC(uint32_t dest, __m128 v)
	{
		__m128i* reg = reinterpret_cast<__m128i*>(R.ACC.UL);
		_mm_store_si128(reg, d::Merge(_mm_load_si128(reg), d::Int(v), dest));
	}
	uint32_t VI(uint32_t r) const { return R.VI[r & 15].US[0]; }

	// ---- VI registers in host registers (microRegAlloc's GPR side, microVU_IR.h) -----------
	//
	// Inside a block microVU keeps VI registers in host registers, all 32 bits
	// of them, and writes the low half back to VURegs at the block's end. What
	// it does to the high half mostly does not show: branches compare 16 bits
	// and addresses are masked. Not all of it: FMEQ compares the whole register,
	// ISW and ISWR store the whole of one MFIR sign-extended in place without
	// saying so, and the integer instructions carry the high half on. So the
	// interpreter holds what each host register holds, and which VI register
	// is in which is decided as microVU decides it, from its allocations in the
	// order its second pass makes them: the least recently used register given
	// up first, the loads at the block's start (mvuPreloadRegisters), the
	// registers a call may change emptied before an XGKICK. R.VI always holds
	// a register's value (the low half of a host register holding it): microVU
	// reads VURegs only for a register no host register holds.
	struct HostGpr // microMapGPR, and the register's contents
	{
		int vi = -1; // VIreg: -1 nothing (a temporary)
		int count = 0;
		bool needed = false;
		bool dirty = false;
		bool zext = false; // isZeroExtended
		uint32_t value = 0;
	};
	HostGpr gpr_[16];
	int counter_ = 0;

	void ResetGprs() // reset(false), at the block's compile
	{
		for (HostGpr& g : gpr_)
			g = HostGpr{};
		counter_ = 0;
	}
	// mvuPreloadRegisters, as the block's compile found it (ShadowBlock::preload).
	void Preload(const ShadowBlock& b)
	{
		for (const int8_t e : b.preload)
		{
			if (e >= 16)
				DoneXmm(AllocXmm(e - 16));
			else
				Done(AllocGpr(e));
		}
	}
	int FindFreeGprRec(int start)
	{
		for (int i = start; i < 16; i++)
		{
			if (kGprUsable[i] && !gpr_[i].needed)
			{
				const int x = FindFreeGprRec(i + 1);
				if (x == -1)
					return i;
				return gpr_[i].count < gpr_[x].count ? i : x;
			}
		}
		return -1;
	}
	int FindFreeGpr()
	{
		for (int i = 0; i < 16; i++)
		{
			if (kGprUsable[i] && !gpr_[i].needed && gpr_[i].vi < 0)
				return i;
		}
		return FindFreeGprRec(0);
	}
	// writeBackReg: VURegs holds the value already.
	void WriteBackGpr(int i, bool clearDirty)
	{
		if (gpr_[i].dirty && clearDirty)
			gpr_[i].dirty = false;
	}
	void ClearGpr(int i) // clearGPR
	{
		HostGpr& g = gpr_[i];
		g.vi = -1;
		g.count = 0;
		g.needed = false;
		g.dirty = false;
		g.zext = false;
	}
	void UnbindVi(int vi, bool& backup) // unbindAnyVIAllocations
	{
		for (int i = 0; i < 16; i++)
		{
			HostGpr& g = gpr_[i];
			if (g.vi != vi)
				continue;
			if (backup)
			{
				V.VIbackup = g.value; // writeVIBackup
				backup = false;
			}
			if (g.needed)
			{
				g.vi = -1;
				g.dirty = false;
				g.zext = false;
			}
			else
			{
				ClearGpr(i);
			}
			break;
		}
	}
	// allocGPR: the host register for a VI register read (load), written
	// (write), both, or neither (a temporary); with backup, the value the
	// written register had kept for a branch (mVU.VIbackup, all 32 bits).
	int AllocGpr(int load = -1, int write = -1, bool backup = false, bool zextIfDirty = false)
	{
		const int thisCounter = counter_++;
		if (write == 0)
		{
			// Writing VI00: a temporary holding zero, thrown away.
			const int x = FindFreeGpr();
			WriteBackGpr(x, true);
			HostGpr& g = gpr_[x];
			g.value = 0;
			g.vi = -1;
			g.dirty = false;
			g.count = thisCounter;
			g.needed = true;
			g.zext = true;
			return x;
		}
		if (load >= 0)
		{
			for (int i = 0; i < 16; i++)
			{
				if (gpr_[i].vi != load)
					continue;
				gpr_[i].count = thisCounter;
				if (write >= 0)
				{
					if (load != write)
					{
						UnbindVi(write, backup);
						int x = FindFreeGpr();
						WriteBackGpr(x, true);
						if (backup && gpr_[x].vi != write)
						{
							gpr_[x].value = VI(write);
							V.VIbackup = gpr_[x].value;
							backup = false;
						}
						gpr_[x].value = zextIfDirty ? (gpr_[i].value & 0xffff) : gpr_[i].value;
						gpr_[x].zext = zextIfDirty;
						std::swap(x, i);
					}
					else
					{
						gpr_[i].zext = false;
					}
					gpr_[i].vi = write;
					gpr_[i].dirty = true;
				}
				else if (zextIfDirty && !gpr_[i].zext)
				{
					gpr_[i].value &= 0xffff;
					gpr_[i].zext = true;
				}
				gpr_[i].needed = true;
				if (backup)
					V.VIbackup = gpr_[i].value;
				return i;
			}
		}
		if (write >= 0)
			UnbindVi(write, backup);
		const int x = FindFreeGpr();
		WriteBackGpr(x, true);
		HostGpr& g = gpr_[x];
		if (backup && load >= 0 && write > 0 && load != write)
		{
			g.value = VI(write);
			V.VIbackup = g.value;
			backup = false;
		}
		if (load > 0)
			g.value = VI(load);
		else if (load == 0)
			g.value = 0;
		g.vi = load;
		g.zext = true;
		if (write >= 0)
		{
			g.vi = write;
			g.dirty = true;
			g.zext = false;
			if (backup)
			{
				if (load < 0 && write > 0)
					g.value = VI(write);
				V.VIbackup = g.value;
			}
		}
		g.count = thisCounter;
		g.needed = true;
		return x;
	}
	void Done(int i) { gpr_[i].needed = false; } // clearNeeded
	// moveVIToGPR: a VI register into a temporary, the high half from the low.
	uint32_t MoveVi(uint32_t vi, bool signExtend = false)
	{
		if (vi == 0)
			return 0;
		const int s = AllocGpr(static_cast<int>(vi));
		const uint32_t v = gpr_[s].value;
		Done(s);
		return signExtend ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v))) : (v & 0xffff);
	}
	// The result of an instruction in the host register it was given: the VI
	// register's value is its low half.
	void Put(int i, uint32_t value)
	{
		gpr_[i].value = value;
		if (gpr_[i].vi > 0)
			R.VI[gpr_[i].vi].US[0] = static_cast<u16>(value);
	}
	// The VI register written: a host register for it alone (allocGPR(-1, vi)),
	// given the value.
	void WriteVi(const OpInfo& op, uint32_t vi, uint32_t value)
	{
		const int t = AllocGpr(-1, static_cast<int>(vi), op.lOp.backupVI);
		Put(t, value);
		Done(t);
	}
	void FlushCallerSavedGprs() // flushCallerSavedRegisters, its VI half
	{
		for (int i = 0; i < 16; i++)
		{
			if (!kGprCallerSaved[i])
				continue;
			WriteBackGpr(i, true);
			if (!gpr_[i].needed)
				ClearGpr(i);
		}
	}

	// ---- VF registers in host registers (microRegAlloc's XMM side) --------------------------
	//
	// Which XMM register microVU gives each operand matters for one thing:
	// with AVX, PCSX2's emitter swaps the two sources of a commutative
	// instruction (ADDPS, MULPS) when the first is in xmm0 to xmm7 and the
	// second in xmm8 to xmm15, for a shorter encoding, and where both sources
	// are NaN x86 gives the first one's. So the allocation is followed as the
	// VI registers' is, in the same order (the two share microVU's count),
	// without the values: the values are the VF registers'. xmm15 is P and Q.
	struct HostXmm // microMapXMM
	{
		int vf = -1; // VFreg: -1 a temporary, 0 VF00 (or a register written as it), 32 ACC
		int xyzw = 0; // the components written, to be written back
		int count = 0;
		bool needed = false;
		bool isZero = false;
	};
	static constexpr int kXmmTotal = 15;
	static constexpr int kXmmPQ = 15;
	HostXmm xmm_[kXmmTotal];

	void ResetXmms()
	{
		for (HostXmm& x : xmm_)
			x = HostXmm{};
	}
	int FindFreeXmmRec(int start)
	{
		for (int i = start; i < kXmmTotal; i++)
		{
			if (!xmm_[i].needed)
			{
				const int x = FindFreeXmmRec(i + 1);
				if (x == -1)
					return i;
				return xmm_[i].count < xmm_[x].count ? i : x;
			}
		}
		return -1;
	}
	int FindFreeXmm()
	{
		for (int i = 0; i < kXmmTotal; i++)
		{
			if (!xmm_[i].needed && xmm_[i].vf < 0)
				return i;
		}
		return FindFreeXmmRec(0);
	}
	void ClearXmm(int i) { xmm_[i] = HostXmm{}; } // clearReg
	void WriteBackXmm(int i, bool invalidateRegs = true) // writeBackReg
	{
		HostXmm& x = xmm_[i];
		if (x.vf > 0 && x.xyzw)
		{
			if (invalidateRegs)
			{
				for (int j = 0; j < kXmmTotal; j++)
				{
					if (j == i || xmm_[j].needed)
						continue;
					if (xmm_[j].vf == x.vf)
						ClearXmm(j);
				}
			}
			if (x.xyzw == 0xf) // cached whole
			{
				x.count = counter_;
				x.xyzw = 0;
				x.needed = false;
				return;
			}
			ClearXmm(i);
		}
		else if (x.xyzw)
		{
			ClearXmm(i);
		}
	}
	void DoneXmm(int i) // clearNeeded
	{
		if (i < 0 || i >= kXmmTotal)
			return; // P and Q
		HostXmm& c = xmm_[i];
		c.needed = false;
		if (!c.xyzw)
			return;
		if (c.vf <= 0)
		{
			ClearXmm(i);
			return;
		}
		int mergeRegs = c.xyzw < 0xf ? 1 : 0;
		for (int j = 0; j < kXmmTotal; j++)
		{
			if (j == i || xmm_[j].vf != c.vf)
				continue;
			if (mergeRegs == 1)
			{
				xmm_[j].xyzw = 0xf;
				xmm_[j].count = counter_;
				mergeRegs = 2;
			}
			else
			{
				ClearXmm(j);
			}
		}
		if (mergeRegs == 2)
			ClearXmm(i);
		else if (mergeRegs == 1)
			WriteBackXmm(i);
	}
	// allocReg: load VF `load` (32 ACC, 33 I), to be written as `write` (0: a
	// temporary written as VF00), components `xyzw`; without either, a
	// temporary.
	int AllocXmm(int load = -1, int write = -1, int xyzw = 0, bool cloneWrite = true)
	{
		counter_++;
		if (load >= 0)
		{
			for (int i = 0; i < kXmmTotal; i++)
			{
				HostXmm& m = xmm_[i];
				if (m.vf == load && (!m.xyzw || (m.vf && m.xyzw == 0xf)))
				{
					int z = i;
					if (write >= 0)
					{
						if (cloneWrite)
						{
							z = FindFreeXmm();
							WriteBackXmm(z);
							m.count = counter_;
						}
						else if (load != write || xyzw != 0xf)
						{
							WriteBackXmm(i);
						}
						xmm_[z].vf = write;
						xmm_[z].xyzw = xyzw;
						xmm_[z].isZero = load == 0;
					}
					xmm_[z].count = counter_;
					xmm_[z].needed = true;
					return z;
				}
			}
		}
		const int x = FindFreeXmm();
		WriteBackXmm(x);
		if (write >= 0)
		{
			xmm_[x].vf = write;
			xmm_[x].xyzw = xyzw;
		}
		else
		{
			xmm_[x].vf = load;
			xmm_[x].xyzw = 0;
		}
		xmm_[x].isZero = load == 0;
		xmm_[x].count = counter_;
		xmm_[x].needed = true;
		return x;
	}
	void FlushCallerSavedXmms() // flushCallerSavedRegisters, its VF half
	{
		for (int i = 0; i < kXmmTotal; i++)
		{
			if (!kXmmCallerSaved[i])
				continue;
			WriteBackXmm(i);
			if (!xmm_[i].needed)
				ClearXmm(i);
		}
	}
	// Whether the emitter swaps the sources of a commutative instruction on
	// these two registers (EmitSIMDImpl: a two-byte VEX prefix needs the
	// second source in xmm0 to xmm7, the first may be any).
	static bool Swapped(int first, int second) { return UseAvx() && second >= 8 && first < 8; }
	// The XMM allocations of the fourth table's lower instructions that have no
	// VI register, in microVU's order (microVU_Lower.inl); the others make
	// theirs among their VI allocations. ESUM's addition is a commutative one.
	bool esumSwap_ = false;
	void LowerXmm(const OpInfo& op, uint32_t code, int table, uint32_t row)
	{
		const int ft = static_cast<int>((code >> 16) & 31), fs = static_cast<int>((code >> 11) & 31);
		const int dest = static_cast<int>((code >> 21) & 0xF);
		const int fsf = static_cast<int>((code >> 21) & 3), ftf = static_cast<int>((code >> 23) & 3);
		auto one = [](int f) { return 1 << (3 - f); };
		auto threeTemps = [&](int first) {
			const int t1 = AllocXmm();
			const int t2 = AllocXmm();
			DoneXmm(first);
			DoneXmm(t1);
			DoneXmm(t2);
		};
		switch ((table << 5) | static_cast<int>(row))
		{
			case (0 << 5) | 12: // MOVE
				DoneXmm(AllocXmm(fs, ft, dest));
				break;
			case (0 << 5) | 14: // DIV
			{
				const int t = ftf ? AllocXmm(ft, 0, one(ftf)) : AllocXmm(ft);
				const int s = AllocXmm(fs, 0, one(fsf));
				const int t1 = AllocXmm();
				DoneXmm(s);
				DoneXmm(t);
				DoneXmm(t1);
				break;
			}
			case (1 << 5) | 14: // SQRT
				DoneXmm(AllocXmm(ft, 0, one(ftf)));
				break;
			case (2 << 5) | 14: // RSQRT
			{
				const int s = AllocXmm(fs, 0, one(fsf));
				const int t = AllocXmm(ft, 0, one(ftf));
				const int t1 = AllocXmm();
				DoneXmm(s);
				DoneXmm(t);
				DoneXmm(t1);
				break;
			}
			case (0 << 5) | 25: // MFP
				DoneXmm(AllocXmm(-1, ft, dest));
				break;
			case (1 << 5) | 12: // MR32
			{
				const int s = AllocXmm(fs);
				const int t = AllocXmm(-1, ft, dest);
				DoneXmm(t);
				DoneXmm(s);
				break;
			}
			case (1 << 5) | 16: // RGET
				if (!op.lOp.noWriteVF)
					DoneXmm(AllocXmm(-1, ft, dest));
				break;
			case (2 << 5) | 16: // RINIT
			case (3 << 5) | 16: // RXOR
				if (fs || fsf == 3)
					DoneXmm(AllocXmm(fs, 0, one(fsf)));
				break;
			case (0 << 5) | 28: // ESADD
			case (1 << 5) | 28: // ERSADD
			case (2 << 5) | 28: // ELENG
			case (3 << 5) | 28: // ERLENG
				DoneXmm(AllocXmm(fs, 0, dest));
				break;
			case (0 << 5) | 29: // EATANxy
			case (1 << 5) | 29: // EATANxz
			{
				const int whole = AllocXmm(fs, 0, 0xf);
				const int f = AllocXmm();
				const int t2 = AllocXmm();
				DoneXmm(f);
				DoneXmm(whole);
				DoneXmm(t2);
				break;
			}
			case (2 << 5) | 29: // ESUM
			{
				const int f = AllocXmm(fs, 0, dest);
				const int t1 = AllocXmm();
				esumSwap_ = Swapped(f, t1);
				DoneXmm(f);
				DoneXmm(t1);
				break;
			}
			case (0 << 5) | 30: // ESQRT
			case (1 << 5) | 30: // ERSQRT
			case (2 << 5) | 30: // ERCPR
				DoneXmm(AllocXmm(fs, 0, one(fsf)));
				break;
			case (0 << 5) | 31: // ESIN
			case (1 << 5) | 31: // EATAN
			case (2 << 5) | 31: // EEXP
				threeTemps(AllocXmm(fs, 0, one(fsf)));
				break;
			default:
				break; // with VI registers, or none
		}
	}

	// The temporaries of mVUupdateFlags, where the caller gives none.
	void FlagTemps(const OpInfo& op, uint32_t dest, bool t1Given, bool t2Given, bool modXYZW = true)
	{
		if (!op.sFlag.doFlag && !op.mFlag.doFlag)
			return;
		const int t1 = t1Given ? -1 : AllocXmm();
		const int t2 = (op.mFlag.doFlag && !(d::Single(dest) && modXYZW) && !t2Given) ? AllocXmm() : -1;
		if (t1 >= 0)
			DoneXmm(t1);
		if (t2 >= 0)
			DoneXmm(t2);
	}
	d::Clamp ClampSettings() const
	{
		const auto& rec = EmuConfig.Cpu.Recompiler;
		return vu1 ? d::Clamp{rec.vu1Overflow, rec.vu1ExtraOverflow, rec.vu1SignOverflow}
				   : d::Clamp{rec.vu0Overflow, rec.vu0ExtraOverflow, rec.vu0SignOverflow};
	}
	uint32_t Live(uint32_t pc) const { return MicroWords(V)[pc / 4]; } // ptr32[&curI] as the code runs

	// ---- flags (mVUupdateFlags, mVUdivSet) -------------------------------------------------
	void UpdateFlags(const OpInfo& op, __m128 result, uint32_t dest, bool modXYZW = true)
	{
		static constexpr uint16_t flipMask[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
		if (!op.sFlag.doFlag && !op.mFlag.doFlag)
			return;
		const bool single = (dest == 8 || dest == 4 || dest == 2 || dest == 1) && modXYZW;
		__m128 t2 = result;
		if (op.mFlag.doFlag && !single)
			t2 = d::Float(_mm_shuffle_epi32(d::Int(result), 0x1B));
		uint32_t sReg = 0;
		if (op.sFlag.doFlag)
		{
			sReg = V.statFlag[op.sFlag.lastWrite];
			if (op.sFlag.doNonSticky)
				sReg &= 0xfffc00ffu;
		}
		// One component: microVU computes it in the first lane and takes its flags
		// there; here it is in its own lane.
		const int shiftDown = single ? ((dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3))) : 0;
		const uint32_t andXYZW = single ? 1u : (op.mFlag.doFlag ? dest : flipMask[dest]);
		uint32_t mReg = (static_cast<uint32_t>(_mm_movemask_ps(t2)) >> shiftDown) & andXYZW;
		mReg <<= 4;
		mReg |= (static_cast<uint32_t>(_mm_movemask_ps(d::CMPEQPS(_mm_setzero_ps(), t2))) >> shiftDown) & andXYZW;
		if (op.sFlag.doFlag && EmuConfig.Gamefixes.VUOverflowHack)
		{
			const __m128 mag = _mm_and_ps(t2, d::All(0x7fffffffu));
			const uint32_t big = (static_cast<uint32_t>(_mm_movemask_ps(d::CMPNLTPS(mag, d::All(0x7f7fffffu)))) >> shiftDown) & andXYZW;
			if (big)
			{
				sReg |= 0x820000u;
				if (op.mFlag.doFlag)
					mReg |= big << 12;
			}
		}
		if (op.mFlag.doFlag)
		{
			if (single && !(dest & 1))
				mReg <<= (dest & 8) ? 3 : ((dest & 4) ? 2 : 1);
			V.macFlag[op.mFlag.write] = mReg;
		}
		if (op.sFlag.doFlag)
		{
			mReg &= 0xff;
			sReg |= mReg;
			if (op.sFlag.doNonSticky)
				sReg |= mReg << 8;
			V.statFlag[op.sFlag.write] = sReg;
		}
	}

	void DivSet(const OpInfo& op) // mVUdivSet
	{
		if (!op.doDivFlag)
			return;
		if (!op.sFlag.doFlag)
			V.statFlag[op.sFlag.write] = V.statFlag[op.sFlag.lastWrite];
		V.statFlag[op.sFlag.write] = (V.statFlag[op.sFlag.write] & 0xfff3ffffu) | V.divFlag;
	}

	// ---- the upper instructions (microVU_Upper.inl) -----------------------------------------
	enum class Second
	{
		Vector,
		Bc,
		I,
		Q,
	};
	struct Operand
	{
		__m128 v;
		bool clampable;
	};
	Operand Fetch(const OpInfo& op, Second k, uint32_t ft, uint32_t bc) const
	{
		switch (k)
		{
			case Second::Vector: return {VF(ft), ft != 0};
			case Second::Bc: return {d::All(R.VF[ft].UL[bc]), true};
			// setupFtReg loads I as allocReg(33, 0, xyzw): a register written as
			// VF00's (VFreg 0, isZero false), which checkVFClamp clamps.
			case Second::I: return {d::All(R.VI[REG_I].UL), true};
			case Second::Q: break;
		}
		return {d::All(V.pq[op.readQ]), true};
	}

	enum class Kind
	{
		Add,
		Sub,
		Mul,
		Max,
		Min,
		AddI,
	};

	// setupFtReg's allocations: Ft's host register, and tempFt's (-1: none).
	struct FtRegs
	{
		int ft;
		int tempFt;
	};
	FtRegs SetupFt(const OpInfo& op, Second k, uint32_t clamp, uint32_t dest, uint32_t ft)
	{
		const d::Clamp c = ClampSettings();
		const bool single = d::Single(dest);
		switch (k)
		{
			case Second::Vector:
			{
				const bool willClamp = c.extra || ((clamp & d::cFt) && !c.extra && (c.overflow || c.sign));
				if (single && dest != 8) // _XYZW_SS2
				{
					const int r = AllocXmm(static_cast<int>(ft), 0, static_cast<int>(dest));
					return {r, r};
				}
				if (willClamp)
				{
					const int r = AllocXmm(static_cast<int>(ft), 0, 0xf);
					return {r, r};
				}
				return {AllocXmm(static_cast<int>(ft)), -1};
			}
			case Second::Bc:
			{
				const int whole = AllocXmm(static_cast<int>(ft));
				const int r = AllocXmm();
				DoneXmm(whole);
				return {r, r};
			}
			case Second::I:
			{
				const int r = AllocXmm(33, 0, static_cast<int>(dest));
				return {r, r};
			}
			case Second::Q:
				break;
		}
		if (!c.extra && single && !op.readQ)
			return {kXmmPQ, -1};
		const int r = AllocXmm();
		return {r, r};
	}
	// MIN_MAX_SS and MIN_MAX_PS: their temporaries.
	void MinMaxTemps(bool single)
	{
		const int t1 = AllocXmm();
		const int t2 = single ? -1 : AllocXmm();
		DoneXmm(t1);
		if (t2 >= 0)
			DoneXmm(t2);
	}

	void FMACa(const OpInfo& op, Kind kind, Second k, bool toAcc, uint32_t clamp, uint32_t code)
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
		const bool single = d::Single(dest);
		if (kind == Kind::Sub && k == Second::Vector && fs == ft) // doSafeSub
		{
			const int r = AllocXmm(-1, toAcc ? 32 : static_cast<int>(fd), static_cast<int>(dest));
			FlagTemps(op, dest, false, false);
			DoneXmm(r);
			const __m128 zero = _mm_setzero_ps();
			if (toAcc)
				WriteACC(dest, zero);
			else
				WriteVF(fd, dest, zero);
			UpdateFlags(op, zero, dest);
			return;
		}
		// The host registers, as mVU_FMACa allocates them.
		const FtRegs f = SetupFt(op, k, clamp, dest, ft);
		int sReg, accReg = -1;
		if (toAcc)
		{
			sReg = AllocXmm(static_cast<int>(fs), 0, static_cast<int>(dest));
			accReg = AllocXmm(dest == 0xf ? -1 : 32, 32, 0xf, false);
		}
		else
		{
			sReg = AllocXmm(static_cast<int>(fs), static_cast<int>(fd), static_cast<int>(dest));
		}
		if (kind == Kind::Max || kind == Kind::Min)
			MinMaxTemps(single);
		const bool swap = !single && (kind == Kind::Add || kind == Kind::Mul || kind == Kind::AddI) && Swapped(sReg, f.ft);
		if (toAcc)
		{
			FlagTemps(op, dest, true, f.tempFt >= 0);
			DoneXmm(accReg);
		}
		else if (kind != Kind::Max && kind != Kind::Min)
		{
			FlagTemps(op, dest, f.tempFt >= 0, false);
		}
		DoneXmm(sReg);
		DoneXmm(f.ft);

		const d::Clamp c = ClampSettings();
		Operand t = Fetch(op, k, ft, bc);
		__m128 s = VF(fs);
		const bool sClampable = fs != 0;
		if (clamp & d::cFt)
			t.v = d::Clamp2(t.v, c, t.clampable, false);
		if (clamp & d::cFs)
			s = d::Clamp2(s, c, sClampable, false);
		__m128 r;
		switch (kind)
		{
			case Kind::Add: r = d::Compute(d::Arith::Add, s, sClampable, t.v, t.clampable, c, swap); break;
			case Kind::Sub: r = d::Compute(d::Arith::Sub, s, sClampable, t.v, t.clampable, c); break;
			case Kind::Mul: r = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swap); break;
			case Kind::Max: r = d::MinMax(s, t.v, false); break;
			case Kind::Min: r = d::MinMax(s, t.v, true); break;
			default:
				if (single && EmuConfig.Gamefixes.VuAddSubHack) [[unlikely]]
					r = d::TriAceAdd(s, t.v, dest);
				else
					r = d::Compute(d::Arith::Add, s, sClampable, t.v, t.clampable, c, swap);
				break;
		}
		if (toAcc)
			WriteACC(dest, r);
		else
			WriteVF(fd, dest, r);
		if (kind != Kind::Max && kind != Kind::Min)
			UpdateFlags(op, r, dest);
	}

	void FMACb(const OpInfo& op, bool subtract, Second k, uint32_t clamp, uint32_t code) // MADDA, MSUBA
	{
		const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clamp, dest, ft);
		const int sReg = AllocXmm(static_cast<int>(fs), 0, static_cast<int>(dest));
		const int accReg = AllocXmm(32, 32, 0xf, false);
		const bool swapMul = !single && Swapped(sReg, f.ft);
		bool swapAdd = false;
		if (single || dest == 0xf)
		{
			swapAdd = !single && !subtract && Swapped(accReg, sReg);
			FlagTemps(op, dest, true, f.tempFt >= 0);
		}
		else
		{
			const int tempAcc = AllocXmm();
			swapAdd = !subtract && Swapped(tempAcc, sReg);
			FlagTemps(op, dest, true, f.tempFt >= 0);
			DoneXmm(tempAcc);
		}
		DoneXmm(accReg);
		DoneXmm(sReg);
		DoneXmm(f.ft);

		const d::Clamp c = ClampSettings();
		Operand t = Fetch(op, k, ft, bc);
		__m128 s = VF(fs);
		const bool sClampable = fs != 0;
		if (clamp & d::cFt)
			t.v = d::Clamp2(t.v, c, t.clampable, false);
		if (clamp & d::cFs)
			s = d::Clamp2(s, c, sClampable, false);
		const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
		const __m128 r = d::Compute(subtract ? d::Arith::Sub : d::Arith::Add, ACC(), true, product, sClampable, c, swapAdd);
		WriteACC(dest, r);
		UpdateFlags(op, r, dest);
	}

	void FMACc(const OpInfo& op, Second k, uint32_t clamp, uint32_t code) // MADD
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clamp, dest, ft);
		const int accReg = AllocXmm(32);
		const int sReg = AllocXmm(static_cast<int>(fs), static_cast<int>(fd), static_cast<int>(dest));
		const bool swapMul = !single && Swapped(sReg, f.ft);
		const bool swapAdd = !single && Swapped(sReg, accReg);
		FlagTemps(op, dest, f.tempFt >= 0, false);
		DoneXmm(sReg);
		DoneXmm(f.ft);
		DoneXmm(accReg);

		const d::Clamp c = ClampSettings();
		Operand t = Fetch(op, k, ft, bc);
		__m128 acc = ACC();
		__m128 s = VF(fs);
		const bool sClampable = fs != 0;
		if (clamp & d::cFt)
			t.v = d::Clamp2(t.v, c, t.clampable, false);
		if (clamp & d::cFs)
			s = d::Clamp2(s, c, sClampable, false);
		if (clamp & d::cACC)
			acc = d::Clamp2(acc, c, true, false);
		const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
		const __m128 r = d::Compute(d::Arith::Add, product, sClampable, acc, true, c, swapAdd);
		WriteVF(fd, dest, r);
		UpdateFlags(op, r, dest);
	}

	void FMACd(const OpInfo& op, Second k, uint32_t clamp, uint32_t code) // MSUB
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clamp, dest, ft);
		const int sReg = AllocXmm(static_cast<int>(fs), 0, static_cast<int>(dest));
		const int dReg = AllocXmm(32, static_cast<int>(fd), static_cast<int>(dest));
		const bool swapMul = !single && Swapped(sReg, f.ft);
		FlagTemps(op, dest, true, f.tempFt >= 0);
		DoneXmm(dReg);
		DoneXmm(f.ft);
		DoneXmm(sReg);

		const d::Clamp c = ClampSettings();
		Operand t = Fetch(op, k, ft, bc);
		__m128 s = VF(fs);
		__m128 acc = ACC();
		const bool sClampable = fs != 0;
		if (clamp & d::cFt)
			t.v = d::Clamp2(t.v, c, t.clampable, false);
		if (clamp & d::cFs)
			s = d::Clamp2(s, c, sClampable, false);
		if (clamp & d::cACC)
			acc = d::Clamp2(acc, c, true, false);
		const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
		const __m128 r = d::Compute(d::Arith::Sub, acc, true, product, sClampable, c);
		WriteVF(fd, dest, r);
		UpdateFlags(op, r, dest);
	}

	// Whether a conditional branch with this word goes, its value compared as
	// 16 bits (condBranch's CMP of mVU.branch).
	static bool Taken(uint32_t lower, uint32_t value)
	{
		const int16_t v = static_cast<int16_t>(value);
		switch (lower >> 25)
		{
			case 0x28: return v == 0; // IBEQ
			case 0x29: return v != 0; // IBNE
			case 0x2C: return v < 0; // IBLTZ
			case 0x2D: return v > 0; // IBGTZ
			case 0x2E: return v <= 0; // IBLEZ
			case 0x2F: return v >= 0; // IBGEZ
		}
		return false;
	}
	// A block of one pair, run after a branch whose delay slot held another
	// (isEvilBlock).
	bool EvilBlock() const { return block_ && block_->pState.blockType == 2; }
	const ShadowBlock* block_ = nullptr;

	bool Upper(const OpInfo& op, uint32_t code, uint32_t pc);
	bool Lower(const OpInfo& op, uint32_t code, uint32_t pc);
	bool LowerT3(const OpInfo& op, uint32_t code, uint32_t pc, int table);
	bool Execute(const OpInfo& op, uint64_t words, uint32_t pc);
	void Ibit(uint32_t code, uint32_t pc);

	bool Fail(const char* what, uint32_t code, uint32_t pc)
	{
		if (V.unsupported.empty())
		{
			char text[160];
			std::snprintf(text, sizeof(text), "VU%d: %s (%08x) at %04x", V.index, what, code, pc);
			V.unsupported = text;
		}
		return false;
	}

	// ---- the ends of blocks and programs (microVU_Branch.inl) --------------------------------
	void SetupFlags(const ShadowBlock& b)
	{
		const mvu::FlagCycles& mFC = b.a.flagCycles;
		const uint8_t exact = b.a.end.needExactMatch;
		if (exact & 1)
		{
			int bStatus[4];
			SortFlag(mFC.xStatus, bStatus, mFC.cycles);
			const uint32_t s[4] = {V.statFlag[0], V.statFlag[1], V.statFlag[2], V.statFlag[3]};
			for (int i = 0; i < 4; i++)
				V.statFlag[i] = s[bStatus[i]];
		}
		if (exact & 2)
		{
			int bMac[4];
			SortFlag(mFC.xMac, bMac, mFC.cycles);
			const uint32_t m[4] = {V.macFlag[0], V.macFlag[1], V.macFlag[2], V.macFlag[3]};
			for (int i = 0; i < 4; i++)
				V.macFlag[i] = m[bMac[i]];
		}
		if (exact & 4)
		{
			int bClip[4];
			SortFlag(mFC.xClip, bClip, mFC.cycles);
			const uint32_t cl[4] = {V.clipFlag[0], V.clipFlag[1], V.clipFlag[2], V.clipFlag[3]};
			for (int i = 0; i < 4; i++)
				V.clipFlag[i] = cl[bClip[i]];
		}
	}

	void SetupBranch(const ShadowBlock& b) // mVUsetupBranch
	{
		SetupFlags(b);
		if (b.linkP || b.linkQ)
		{
			if (b.linkQ)
				std::swap(V.pq[0], V.pq[1]);
			if (b.linkP)
				std::swap(V.pq[2], V.pq[3]);
		}
	}

	// mVUendProgram; isEbit: 0 the program pauses, 1 or 2 it ends. `e`: what
	// the end was compiled with, when not the block's own end (mVUDTendProgram).
	void EndProgram(const ShadowBlock& b, int isEbit, uint32_t tpc, const EndInfo* e = nullptr)
	{
		if (!e)
			e = &b.end;
		const bool ebit = isEbit != 0;
		const mvu::FlagCycles& mFC = b.a.flagCycles;
		const int fStatus = LastFlagInst(b.pState, mFC.xStatus, 0, ebit);
		const int fMac = LastFlagInst(b.pState, mFC.xMac, 1, ebit);
		const int fClip = LastFlagInst(b.pState, mFC.xClip, 2, ebit);
		int qInst = 0, pInst = 0;
		if (ebit)
		{
			qInst = e->qInst;
			pInst = e->pInst;
			if (e->doDivFlag)
				V.statFlag[fStatus] = (V.statFlag[fStatus] & 0xfff3ffffu) | V.divFlag;
			if (e->doXGKICK)
				XgkickDelay();
			if (vu1 && EmuConfig.Gamefixes.XgKickHack)
				Fail("the XGKICK game fix", 0, tpc);
			if (!V.cleared)
				V.lpState = PipeState{}; // mVU0clearlpStateJIT, mVU1clearlpStateJIT
		}
		// P and Q
		R.VI[REG_Q].UL = V.pq[qInst ? 1 : 0];
		R.pending_q = V.pq[qInst ? 0 : 1];
		if (vu1)
		{
			R.VI[REG_P].UL = V.pq[pInst ? 3 : 2];
			R.pending_p = V.pq[pInst ? 2 : 3];
		}
		// The flags
		R.VI[REG_STATUS_FLAG].UL = d::Normalize(V.statFlag[fStatus]);
		R.VI[REG_MAC_FLAG].UL = V.macFlag[fMac] & 0xffff;
		R.VI[REG_CLIP_FLAG].UL = V.clipFlag[fClip];
		if (!ebit)
		{
			std::memcpy(R.micro_macflags, V.macFlag, sizeof(V.macFlag));
			std::memcpy(R.micro_clipflags, V.clipFlag, sizeof(V.clipFlag));
			std::memcpy(R.micro_statusflags, V.statFlag, sizeof(V.statFlag));
		}
		else
		{
			const uint32_t clip = R.VI[REG_CLIP_FLAG].UL, mac = R.VI[REG_MAC_FLAG].UL, status = V.statFlag[fStatus];
			for (int i = 0; i < 4; i++)
			{
				R.micro_clipflags[i] = clip;
				R.micro_macflags[i] = mac;
				R.micro_statusflags[i] = status;
			}
		}
		if (ebit && (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack))
			R.nextBlockCycles = 0;
		R.VI[REG_TPC].UL = tpc;
		if (ebit)
			VU0.VI[REG_VPU_STAT].UL &= vu1 ? ~0x100u : ~0x001u;
	}

	void XgkickDelay() // mVU_XGKICK_DELAY
	{
		FlushCallerSavedXmms();
		FlushCallerSavedGprs();
		mVU_XGKICK_(V.VIxgkick);
	}

	// mVUcompileJIT: an indirect jump's target, as the start of a program of
	// its own, through the jump cache of the block it leaves.
	ShadowBlock* JumpTo(ShadowBlock* from, uint32_t target)
	{
		V.stats.jumps++;
		R.start_pc = target;
		JumpCacheEntry& jc = from->jumpCache[target / 8];
		if (jc.prog && jc.prog == V.quick[target / 8].prog)
			return jc.block;
		ShadowBlock* to = SearchProg(V, target, from->pStateEnd, false);
		jc.prog = V.quick[target / 8].prog;
		jc.block = to;
		return to;
	}

	// ---- the EFU's arithmetic (microVU_Lower.inl) --------------------------------------------
	static __m128 DPPS71(__m128 a)
	{
		__asm__("dpps $0x71, %0, %0" : "+x"(a));
		return a;
	}
	__m128 SumXYZ(uint32_t reg) const { return DPPS71(VF(reg)); } // mVU_sumXYZ

	// 1 / x: one loaded into its own register, then SSE_DIVSS.
	__m128 Rcp(__m128 x) const { return d::Compute(d::Arith::Div, d::Scalar(0x3f800000u), true, x, true, ClampSettings()); }

	// mVU_EATAN_: the series of `t` in odd powers, plus pi / 4.
	__m128 Atan(__m128 t) const
	{
		static constexpr uint32_t kT[7] = {0xbeaaa61cu, 0x3e4c40a6u, 0xbe0e6c63u, 0x3dc577dfu, 0xbd6501c4u, 0x3cb31652u, 0xbb84d7e7u};
		const d::Clamp c = ClampSettings();
		__m128 pq = d::MULPS(t, d::Scalar(0x3f7ffff5u)); // T1
		__m128 t2 = t;
		for (uint32_t k : kT)
		{
			t2 = d::Compute(d::Arith::Mul, t2, true, t, true, c);
			t2 = d::Compute(d::Arith::Mul, t2, true, t, true, c);
			const __m128 t1 = d::MULPS(t2, d::Scalar(k));
			pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
		}
		return d::ADDSS(pq, d::Scalar(0x3f490fdbu)); // pi / 4
	}

	Vu& V;
	VURegs& R;
	const bool vu1;
};

// I, from the lower word of an I-bit pair (doIbit).
void Runner::Ibit(uint32_t code, uint32_t pc)
{
	if (EmuConfig.Gamefixes.IbitHack)
	{
		R.VI[REG_I].UL = Live(pc);
	}
	else
	{
		uint32_t i = code;
		if (vu1 ? EmuConfig.Cpu.Recompiler.vu1Overflow : EmuConfig.Cpu.Recompiler.vu0Overflow)
		{
			if ((i & 0x7fffffffu) >= 0x7f800000u)
				i = (0x80000000u & i) | 0x7f7fffffu;
		}
		R.VI[REG_I].UL = i;
	}
}

bool Runner::Upper(const OpInfo& op, uint32_t code, uint32_t pc)
{
	using d::cACC;
	using d::cFs;
	using d::cFt;
	const uint32_t dest = (code >> 21) & 0xF;
	const bool ps = dest == 0xF;
	const uint32_t op6 = code & 0x3f;
	const uint32_t fs = (code >> 11) & 31, ft = (code >> 16) & 31;
	if (op6 < 0x30)
	{
		const uint32_t col = op6 & 3;
		switch (op6 >> 2)
		{
			case 0x0: FMACa(op, Kind::Add, Second::Bc, false, 0, code); return true; // ADDbc
			case 0x1: FMACa(op, Kind::Sub, Second::Bc, false, ps ? (cFs | cFt) : 0, code); return true; // SUBbc
			case 0x2: FMACc(op, Second::Bc, cFs, code); return true; // MADDbc (micro mode: MADDw too)
			case 0x3: FMACd(op, Second::Bc, 0, code); return true; // MSUBbc
			case 0x4: FMACa(op, Kind::Max, Second::Bc, false, 0, code); return true; // MAXbc
			case 0x5: FMACa(op, Kind::Min, Second::Bc, false, 0, code); return true; // MINIbc
			case 0x6: FMACa(op, Kind::Mul, Second::Bc, false, ps ? (cFs | cFt) : cFs, code); return true; // MULbc
			case 0x7:
				switch (col)
				{
					case 0: FMACa(op, Kind::Mul, Second::Q, false, ps ? (cFs | cFt) : cFs, code); return true; // MULq
					case 1: FMACa(op, Kind::Max, Second::I, false, 0, code); return true; // MAXi
					case 2: FMACa(op, Kind::Mul, Second::I, false, ps ? (cFs | cFt) : cFs, code); return true; // MULi
					default: FMACa(op, Kind::Min, Second::I, false, 0, code); return true; // MINIi
				}
			case 0x8:
				switch (col)
				{
					case 0: FMACa(op, Kind::Add, Second::Q, false, 0, code); return true; // ADDq
					case 1: FMACc(op, Second::Q, 0, code); return true; // MADDq
					case 2: FMACa(op, Kind::AddI, Second::I, false, 0, code); return true; // ADDi
					default: FMACc(op, Second::I, 0, code); return true; // MADDi
				}
			case 0x9:
				switch (col)
				{
					case 0: FMACa(op, Kind::Sub, Second::Q, false, ps ? (cFs | cFt) : 0, code); return true; // SUBq
					case 1: FMACd(op, Second::Q, 0, code); return true; // MSUBq
					case 2: FMACa(op, Kind::Sub, Second::I, false, ps ? (cFs | cFt) : 0, code); return true; // SUBi
					default: FMACd(op, Second::I, 0, code); return true; // MSUBi
				}
			case 0xA:
				switch (col)
				{
					case 0: FMACa(op, Kind::Add, Second::Vector, false, 0, code); return true; // ADD
					case 1: FMACc(op, Second::Vector, 0, code); return true; // MADD
					case 2: FMACa(op, Kind::Mul, Second::Vector, false, ps ? (cFs | cFt) : cFs, code); return true; // MUL
					default: FMACa(op, Kind::Max, Second::Vector, false, 0, code); return true; // MAX
				}
			default:
				switch (col)
				{
					case 0: FMACa(op, Kind::Sub, Second::Vector, false, ps ? (cFs | cFt) : 0, code); return true; // SUB
					case 1: FMACd(op, Second::Vector, 0, code); return true; // MSUB (micro mode: no clamp)
					case 2: // OPMSUB
					{
						const uint32_t fd = (code >> 6) & 31;
						const int tReg = AllocXmm(static_cast<int>(ft), 0, 0xf);
						const int sReg = AllocXmm(static_cast<int>(fs), 0, 0xf);
						const int accReg = AllocXmm(32, static_cast<int>(fd), static_cast<int>(dest));
						const bool swap = Swapped(sReg, tReg);
						DoneXmm(sReg);
						DoneXmm(tReg);
						FlagTemps(op, dest, false, false);
						DoneXmm(accReg);
						const d::Clamp c = ClampSettings();
						const __m128 t = d::Float(_mm_shuffle_epi32(d::Int(VF(ft)), 0xD2));
						const __m128 s = d::Float(_mm_shuffle_epi32(d::Int(VF(fs)), 0xC9));
						__m128 product = d::Compute(d::Arith::Mul, s, fs != 0, t, ft != 0, c, swap);
						if (d::Single(dest))
							product = d::Float(_mm_shuffle_epi32(d::Int(product), 0x00));
						const __m128 r = d::Compute(d::Arith::Sub, ACC(), true, product, fs != 0, c);
						WriteVF(fd, dest, r);
						UpdateFlags(op, r, dest);
						return true;
					}
					default: FMACa(op, Kind::Min, Second::Vector, false, 0, code); return true; // MINI
				}
		}
	}
	if (op6 < 0x3C)
		return Fail("an unknown upper instruction", code, pc);

	const uint32_t row = (code >> 6) & 0x1f;
	const uint32_t col = op6 & 3;
	switch (row)
	{
		case 0: FMACa(op, Kind::Add, Second::Bc, true, 0, code); return true; // ADDAbc
		case 1: FMACa(op, Kind::Sub, Second::Bc, true, 0, code); return true; // SUBAbc
		case 2: FMACb(op, false, Second::Bc, cFs, code); return true; // MADDAbc
		case 3: FMACb(op, true, Second::Bc, 0, code); return true; // MSUBAbc
		case 4: // ITOF0/4/12/15
		{
			if (ft == 0)
				return true;
			DoneXmm(AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf)));
			static constexpr uint32_t kScale[4] = {0, 0x3D800000u, 0x39800000u, 0x38000000u};
			__m128 v = d::CVTDQ2PS(d::Int(VF(fs)));
			if (kScale[col])
				v = d::MULPS(v, d::All(kScale[col]));
			WriteVF(ft, dest, v);
			return true;
		}
		case 5: // FTOI0/4/12/15
		{
			if (ft == 0)
				return true;
			{
				const int sReg = AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf));
				const int t1 = AllocXmm();
				DoneXmm(sReg);
				DoneXmm(t1);
			}
			static constexpr uint32_t kScale[4] = {0, 0x41800000u, 0x45800000u, 0x47000000u};
			__m128 v = VF(fs);
			if (kScale[col])
				v = d::MULPS(v, d::All(kScale[col]));
			const __m128i positiveOverflow = _mm_cmpgt_epi32(d::Int(v), _mm_set1_epi32(0x4EFFFFFF));
			WriteVF(ft, dest, d::Float(_mm_xor_si128(d::CVTTPS2DQ(v), positiveOverflow)));
			return true;
		}
		case 6: // MULAbc
			FMACa(op, Kind::Mul, Second::Bc, true, col == 3 ? (ps ? (cFs | cFt) : cFs) : cFs, code);
			return true;
		default: break;
	}
	switch (col)
	{
		case 0:
			switch (row)
			{
				case 7: FMACa(op, Kind::Mul, Second::Q, true, 0, code); return true; // MULAq
				case 8: FMACa(op, Kind::Add, Second::Q, true, 0, code); return true; // ADDAq
				case 9: FMACa(op, Kind::Sub, Second::Q, true, 0, code); return true; // SUBAq
				case 10: FMACa(op, Kind::Add, Second::Vector, true, 0, code); return true; // ADDA
				case 11: FMACa(op, Kind::Sub, Second::Vector, true, 0, code); return true; // SUBA
			}
			break;
		case 1:
			switch (row)
			{
				case 7: // ABS
					if (ft)
					{
						DoneXmm(AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf)));
						WriteVF(ft, dest, _mm_and_ps(VF(fs), d::All(0x7FFFFFFFu)));
					}
					return true;
				case 8: FMACb(op, false, Second::Q, 0, code); return true; // MADDAq
				case 9: FMACb(op, true, Second::Q, 0, code); return true; // MSUBAq
				case 10: FMACb(op, false, Second::Vector, 0, code); return true; // MADDA
				case 11: FMACb(op, true, Second::Vector, 0, code); return true; // MSUBA
			}
			break;
		case 2:
			switch (row)
			{
				case 7: FMACa(op, Kind::Mul, Second::I, true, 0, code); return true; // MULAi
				case 8: FMACa(op, Kind::Add, Second::I, true, 0, code); return true; // ADDAi
				case 9: FMACa(op, Kind::Sub, Second::I, true, 0, code); return true; // SUBAi
				case 10: FMACa(op, Kind::Mul, Second::Vector, true, 0, code); return true; // MULA
				case 11: // OPMULA
				{
					const int tReg = AllocXmm(static_cast<int>(ft), 0, static_cast<int>(dest));
					const int sReg = AllocXmm(static_cast<int>(fs), 32, static_cast<int>(dest));
					const bool swap = Swapped(sReg, tReg);
					DoneXmm(tReg);
					FlagTemps(op, dest, false, false);
					DoneXmm(sReg);
					const d::Clamp c = ClampSettings();
					__m128 s, t;
					if (d::Single(dest))
					{
						s = _mm_setzero_ps();
						t = _mm_setzero_ps();
					}
					else
					{
						s = d::Float(_mm_shuffle_epi32(d::Int(VF(fs)), 0xC9));
						t = d::Float(_mm_shuffle_epi32(d::Int(VF(ft)), 0xD2));
					}
					const __m128 r = d::Compute(d::Arith::Mul, s, fs != 0, t, ft != 0, c, swap);
					WriteACC(dest, r);
					UpdateFlags(op, r, dest);
					return true;
				}
			}
			break;
		default:
			switch (row)
			{
				case 7: // CLIP
				{
					{
						const int sReg = AllocXmm(static_cast<int>(fs), 0, 0xf);
						const int tReg = AllocXmm(static_cast<int>(ft), 0, 0x1);
						const int t1 = AllocXmm();
						const int t2 = AllocXmm();
						DoneXmm(sReg);
						DoneXmm(tReg);
						DoneXmm(t1);
						DoneXmm(t2);
					}
					const __m128i fsv = d::Int(VF(fs));
					__m128i ftv = _mm_set1_epi32(static_cast<int>(R.VF[ft].UL[3]));
					uint32_t prev = V.clipFlag[op.cFlag.lastWrite] << 6;
					const __m128i expo = _mm_and_si128(fsv, _mm_set1_epi32(0x7f800000));
					__m128i t1 = _mm_cmpeq_epi32(expo, _mm_setzero_si128());
					t1 = _mm_andnot_si128(t1, fsv); // a denormal component reads as +0
					ftv = _mm_and_si128(ftv, _mm_set1_epi32(0x7fffffff));
					__m128i neg = _mm_xor_si128(t1, _mm_set1_epi32(static_cast<int>(0x80000000u)));
					const __m128i gtPos = _mm_cmpgt_epi32(t1, ftv);
					const __m128i gtNeg = _mm_cmpgt_epi32(neg, ftv);
					__m128i packed = _mm_blend_epi16(gtNeg, gtPos, 0x55);
					packed = _mm_packs_epi16(packed, packed);
					const uint32_t bits = static_cast<uint32_t>(_mm_movemask_epi8(packed)) & 0x3f;
					V.clipFlag[op.cFlag.write] = (prev & 0xffffff) | bits;
					return true;
				}
				case 8: FMACb(op, false, Second::I, 0, code); return true; // MADDAi
				case 9: FMACb(op, true, Second::I, 0, code); return true; // MSUBAi
				case 11: return true; // NOP
			}
			break;
	}
	return Fail("an unknown upper instruction", code, pc);
}

bool Runner::Lower(const OpInfo& op, uint32_t code, uint32_t pc)
{
	const uint32_t it = (code >> 16) & 0xF, is = (code >> 11) & 0xF, id = (code >> 6) & 0xF;
	const uint32_t ft = (code >> 16) & 31, fs = (code >> 11) & 31;
	const uint32_t dest = (code >> 21) & 0xF;
	const bool ibitHack = EmuConfig.Gamefixes.IbitHack;
	auto imm11 = [&]() -> int32_t {
		const uint32_t w = ibitHack ? Live(pc) : code;
		return static_cast<int32_t>(w << 21) >> 21;
	};
	auto imm15 = [&]() -> uint32_t {
		const uint32_t w = ibitHack ? Live(pc) : code;
		return ((w >> 10) & 0x7800) | (w & 0x7ff);
	};
	// mVUaddrFix: a quadword address in data memory, in bytes.
	auto addr = [&](uint32_t a) -> uint8_t* {
		if (vu1)
			return VU1.Mem + ((a & 0x3ff) << 4);
		if (a & 0x400)
			return reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4);
		return VU0.Mem + ((a & 0xff) << 4);
	};
	auto loadReg = [&](uint32_t reg, const uint8_t* p) {
		if (op.lOp.noWriteVF)
			return;
		WriteVF(reg, dest, d::Float(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))));
	};
	auto saveReg = [&](uint8_t* p) {
		alignas(16) uint32_t v[4];
		_mm_store_si128(reinterpret_cast<__m128i*>(v), d::Int(VF(fs)));
		uint32_t* m = reinterpret_cast<uint32_t*>(p);
		if (dest & 8) m[0] = v[0];
		if (dest & 4) m[1] = v[1];
		if (dest & 2) m[2] = v[2];
		if (dest & 1) m[3] = v[3];
	};

	switch (code >> 25)
	{
		// The address registers through moveVIToGPR (none for VI00); ILW and ISW
		// with VI00 take a constant address (mVUoptimizeConstantAddr), the same.
		case 0x00: // LQ
		{
			const uint32_t base = MoveVi(is);
			DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
			loadReg(ft, addr(base + static_cast<uint32_t>(imm11())));
			return true;
		}
		case 0x01: // SQ
		{
			const uint32_t base = MoveVi(it);
			DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
			saveReg(addr(base + static_cast<uint32_t>(imm11())));
			return true;
		}
		case 0x04: // ILW
		{
			const uint8_t* p = addr(MoveVi(is) + static_cast<uint32_t>(imm11()));
			const int lane = (dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3));
			uint16_t v;
			std::memcpy(&v, p + lane * 4, 2);
			WriteVi(op, it, v);
			return true;
		}
		case 0x05: // ISW: all 32 bits of the host register (zero-extended if written in the block)
		{
			uint8_t* p = addr(MoveVi(is) + static_cast<uint32_t>(imm11()));
			const int t = AllocGpr(static_cast<int>(it), -1, false, true);
			const uint32_t v = gpr_[t].value;
			Done(t);
			if (dest & 8) std::memcpy(p + 0, &v, 4);
			if (dest & 4) std::memcpy(p + 4, &v, 4);
			if (dest & 2) std::memcpy(p + 8, &v, 4);
			if (dest & 1) std::memcpy(p + 12, &v, 4);
			return true;
		}
		case 0x08: // IADDIU
		case 0x09: // ISUBIU
		{
			const bool add = (code >> 25) == 0x08;
			if (add && is == 0)
			{
				WriteVi(op, it, imm15());
				return true;
			}
			const int s = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
			Put(s, add ? gpr_[s].value + imm15() : gpr_[s].value - imm15());
			Done(s);
			return true;
		}
		case 0x10: // FCEQ
		{
			const uint32_t v = V.clipFlag[op.cFlag.read] ^ (code & 0xffffff);
			WriteVi(op, 1, (v - 1) >> 31);
			return true;
		}
		case 0x11: V.clipFlag[op.cFlag.write] = code & 0xffffff; return true; // FCSET
		case 0x12: // FCAND
		{
			const uint32_t v = (V.clipFlag[op.cFlag.read] & (code & 0xffffff)) + 0xffffff;
			WriteVi(op, 1, v >> 24);
			return true;
		}
		case 0x13: // FCOR
		{
			const uint32_t v = (V.clipFlag[op.cFlag.read] | (code & 0xffffff)) + 1;
			WriteVi(op, 1, v >> 24);
			return true;
		}
		case 0x1C: WriteVi(op, it, V.clipFlag[op.cFlag.read] & 0xfff); return true; // FCGET
		case 0x18: // FMEQ: all 32 bits of is's host register against the MAC flag
		case 0x1A: // FMAND
		case 0x1B: // FMOR
		{
			const uint32_t mac = V.macFlag[op.mFlag.read] & 0xffff;
			const int t = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
			const uint32_t v = gpr_[t].value;
			Put(t, (code >> 25) == 0x18 ? ((v ^ mac) - 1) >> 31 : (code >> 25) == 0x1A ? (v & mac) : (v | mac));
			Done(t);
			return true;
		}
		case 0x14: // FSEQ: the status flag, its groups set whole, compared with the immediate
		{
			const uint32_t imm12 = (((code >> 21) & 1) << 11) | (code & 0x7ff);
			uint32_t imm = 0;
			if (imm12 & 0x0001) imm |= 0x0000f00; // Z
			if (imm12 & 0x0002) imm |= 0x000f000; // S
			if (imm12 & 0x0004) imm |= 0x0010000; // U
			if (imm12 & 0x0008) imm |= 0x0020000; // O
			if (imm12 & 0x0010) imm |= 0x0040000; // I
			if (imm12 & 0x0020) imm |= 0x0080000; // D
			if (imm12 & 0x0040) imm |= 0x000000f; // ZS
			if (imm12 & 0x0080) imm |= 0x00000f0; // SS
			if (imm12 & 0x0100) imm |= 0x0400000; // US
			if (imm12 & 0x0200) imm |= 0x0800000; // OS
			if (imm12 & 0x0400) imm |= 0x1000000; // IS
			if (imm12 & 0x0800) imm |= 0x2000000; // DS
			uint32_t s = V.statFlag[op.sFlag.read];
			for (uint32_t group : {0x0f00u, 0xf000u, 0x000fu, 0x00f0u})
			{
				if (s & group)
					s |= group;
			}
			WriteVi(op, it, ((s ^ imm) - 1) >> 31);
			return true;
		}
		case 0x15: // FSSET: the sticky bits from the immediate
		{
			const uint32_t imm12 = (((code >> 21) & 1) << 11) | (code & 0x7ff);
			uint32_t imm = 0;
			if (imm12 & 0x0040) imm |= 0x000000f; // ZS
			if (imm12 & 0x0080) imm |= 0x00000f0; // SS
			if (imm12 & 0x0100) imm |= 0x0400000; // US
			if (imm12 & 0x0200) imm |= 0x0800000; // OS
			if (imm12 & 0x0400) imm |= 0x1000000; // IS
			if (imm12 & 0x0800) imm |= 0x2000000; // DS
			if (!(op.sFlag.doFlag || op.doDivFlag))
				V.statFlag[op.sFlag.write] = V.statFlag[op.sFlag.lastWrite];
			V.statFlag[op.sFlag.write] = (V.statFlag[op.sFlag.write] & 0xfff00u) | imm;
			return true;
		}
		case 0x16: // FSAND
		case 0x17: // FSOR
		{
			const uint32_t imm12 = (((code >> 21) & 1) << 11) | (code & 0x7ff);
			const uint32_t s = d::Normalize(V.statFlag[op.sFlag.read]);
			WriteVi(op, it, (code >> 25) == 0x16 ? (s & imm12) : (s | imm12));
			return true;
		}
		case 0x20: // B: the block's end does it, but where a branch is in another's delay slot
		case 0x21: // BAL
		{
			const uint32_t target = BranchTarget(V, pc, code);
			if ((code >> 25) == 0x21)
			{
				if (!op.lOp.evilBranch)
					WriteVi(op, it, ((pc + 16) & (V.memSize - 8)) / 8);
				else
					WriteVi(op, it, ((EvilBlock() ? V.evilBranch : V.badBranch) + 8) >> 3);
			}
			if (op.lOp.badBranch)
				V.badBranch = target;
			if (op.lOp.evilBranch)
				(EvilBlock() ? V.evilevilBranch : V.evilBranch) = target;
			return true;
		}
		case 0x24: // JR
		case 0x25: // JALR
		{
			// normJumpPass2
			const uint32_t target = (MoveVi(is) << 3) & (V.memSize - 8);
			if (!op.lOp.evilBranch)
				V.branch = target;
			else
				(EvilBlock() ? V.evilevilBranch : V.evilBranch) = target;
			if (op.lOp.badBranch)
				V.badBranch = target;
			if ((code >> 25) == 0x25)
			{
				if (!op.lOp.evilBranch)
					WriteVi(op, it, ((pc + 16) & (V.memSize - 8)) / 8);
				else
					WriteVi(op, it, ((EvilBlock() ? V.evilBranch : V.badBranch) + 8) >> 3);
			}
			return true;
		}
		case 0x28: // IBEQ
		case 0x29: // IBNE
		case 0x2C: // IBLTZ
		case 0x2D: // IBGTZ
		case 0x2E: // IBLEZ
		case 0x2F: // IBGEZ
		{
			// Compared as 16 bits at the block's end (condBranch), so the high
			// halves do not matter; the allocations do.
			uint32_t v = op.lOp.memReadIs ? V.VIbackup : MoveVi(is);
			if ((code >> 25) <= 0x29)
			{
				if (op.lOp.memReadIt)
				{
					v ^= V.VIbackup;
				}
				else
				{
					const int t = AllocGpr(static_cast<int>(it));
					v ^= gpr_[t].value;
					Done(t);
				}
			}
			if (!op.lOp.badBranch && !op.lOp.evilBranch)
			{
				V.branch = v;
				return true;
			}
			// condEvilBranch: where each of the two branches goes.
			const uint32_t target = BranchTarget(V, pc, code);
			const bool taken = Taken(code, v);
			if (op.lOp.badBranch)
			{
				V.branch = v;
				V.badBranch = taken ? target : ((pc + 16) & (V.memSize - 8));
			}
			else if (EvilBlock())
			{
				V.evilevilBranch = taken ? target : V.evilBranch + 8;
			}
			else
			{
				V.evilBranch = taken ? target : V.badBranch + 8;
			}
			return true;
		}
		case 0x40:
			switch (code & 0x3f)
			{
				case 0x30: // IADD
					if (is == 0 || it == 0)
					{
						const int s = AllocGpr(static_cast<int>(is ? is : it), -1);
						const int d = AllocGpr(-1, static_cast<int>(id), op.lOp.backupVI);
						Put(d, gpr_[s].value);
						Done(d);
						Done(s);
						return true;
					}
					[[fallthrough]];
				case 0x34: // IAND
				case 0x35: // IOR
				{
					const int t = AllocGpr(static_cast<int>(it), -1);
					const int s = AllocGpr(static_cast<int>(is), static_cast<int>(id), op.lOp.backupVI);
					const uint32_t a = gpr_[s].value, b = gpr_[t].value;
					const uint32_t low = code & 0x3f;
					Put(s, low == 0x30 ? a + b : (it == is) ? a : (low == 0x34 ? (a & b) : (a | b)));
					Done(s);
					Done(t);
					return true;
				}
				case 0x31: // ISUB
					if (it != is)
					{
						const int t = AllocGpr(static_cast<int>(it), -1);
						const int s = AllocGpr(static_cast<int>(is), static_cast<int>(id), op.lOp.backupVI);
						Put(s, gpr_[s].value - gpr_[t].value);
						Done(s);
						Done(t);
					}
					else
					{
						WriteVi(op, id, 0);
					}
					return true;
				case 0x32: // IADDI: the immediate sign-extended to 32 bits
				{
					const uint32_t w = ibitHack ? Live(pc) : code;
					const uint32_t imm5 = static_cast<uint32_t>(static_cast<int32_t>(w << 21) >> 27);
					if (is == 0)
					{
						WriteVi(op, it, imm5);
						return true;
					}
					const int s = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
					Put(s, gpr_[s].value + imm5);
					Done(s);
					return true;
				}
				case 0x3C:
				case 0x3D:
				case 0x3E:
				case 0x3F:
				{
					const int table = static_cast<int>(code & 3);
					LowerXmm(op, code, table, (code >> 6) & 0x1f);
					return LowerT3(op, code, pc, table);
				}
			}
			break;
	}
	return Fail("an unknown lower instruction", code, pc);
}

bool Runner::LowerT3(const OpInfo& op, uint32_t code, uint32_t pc, int table)
{
	const uint32_t row = (code >> 6) & 0x1f;
	const uint32_t it = (code >> 16) & 0xF, is = (code >> 11) & 0xF;
	const uint32_t ft = (code >> 16) & 31, fs = (code >> 11) & 31;
	const uint32_t dest = (code >> 21) & 0xF;
	const uint32_t fsf = (code >> 21) & 3, ftf = (code >> 23) & 3;
	const d::Clamp c = ClampSettings();

	// The FDIV and EFU instructions: Q to its instance now, P to its own;
	// DIV, SQRT and RSQRT also set the division flags (divFlag).
	auto writeP = [&](__m128 v) { V.pq[2 + (op.writeP ? 1 : 0)] = d::Bits(v); };
	auto component = [&](uint32_t reg, uint32_t f) { return d::Scalar(R.VF[reg].UL[f]); };
	auto isZero = [&](__m128 v) { return (_mm_movemask_ps(d::CMPEQPS(_mm_setzero_ps(), v)) & 1) != 0; };
	auto clamp1 = [&](__m128 v, bool clampable) { return d::Clamp1(v, c, clampable, true); }; // mVUclamp1(..., 8, true)
	auto sumXYZ = [&](uint32_t reg) { return SumXYZ(reg); };
	auto efu = [&](auto&& compute) {
		if (!vu1)
			return true; // VU0 has no EFU
		writeP(compute());
		return true;
	};

	switch (table)
	{
		case 0:
			switch (row)
			{
				case 12: WriteVF(ft, dest, VF(fs)); return true; // MOVE
				case 13: // LQI
				{
					uint32_t a = 0;
					if (is)
					{
						const int r = AllocGpr(static_cast<int>(is), static_cast<int>(is), op.lOp.backupVI);
						a = static_cast<uint32_t>(static_cast<int16_t>(gpr_[r].value));
						Put(r, gpr_[r].value + 1);
						Done(r);
					}
					if (!op.lOp.noWriteVF)
					{
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
						const uint8_t* p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
											   : ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
						WriteVF(ft, dest, d::Float(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))));
					}
					return true;
				}
				case 14: // DIV
				{
					__m128 s = component(fs, fsf);
					const __m128 t = component(ft, ftf);
					if (isZero(t))
					{
						V.divFlag = isZero(s) ? kDivI : kDivD;
						s = _mm_xor_ps(s, t);
						s = _mm_and_ps(s, d::All(0x80000000u));
						s = _mm_or_ps(s, d::All(0x7f7fffffu));
					}
					else
					{
						V.divFlag = 0;
						s = d::Compute(d::Arith::Div, s, fs != 0, t, ft != 0, c);
						s = clamp1(s, fs != 0);
					}
					V.pq[op.writeQ ? 1 : 0] = d::Bits(s);
					return true;
				}
				case 15: // MTIR: the whole float in the host register
				{
					const int s = AllocXmm(static_cast<int>(fs), 0, 1 << (3 - fsf));
					WriteVi(op, it, R.VF[fs].UL[fsf]);
					DoneXmm(s);
					return true;
				}
				case 16: // RNEXT: the next of R's sequence
				{
					const int temp = AllocGpr(); // a temporary for it
					uint32_t r = R.VI[REG_R].UL;
					const uint32_t bit = ((r >> 4) & 1) ^ ((r >> 22) & 1);
					r = (((r << 1) ^ bit) & 0x007fffffu) | 0x3f800000u;
					R.VI[REG_R].UL = r;
					if (!op.lOp.noWriteVF)
					{
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
						WriteVF(ft, dest, d::All(r));
					}
					Done(temp);
					return true;
				}
				case 25: // MFP
					if (vu1)
						WriteVF(ft, dest, d::All(V.pq[2 + (op.readP ? 1 : 0)]));
					return true;
				case 26: // XTOP
					if (vu1)
						WriteVi(op, it, R.GetVifRegs().top);
					return true;
				case 27: // XGKICK
					if (!vu1)
						return true;
					if (EmuConfig.Gamefixes.XgKickHack)
						return Fail("the XGKICK game fix", code, pc);
					if (op.doXGKICK)
						XgkickDelay();
					{
						const int r = AllocGpr(static_cast<int>(is), -1);
						V.VIxgkick = gpr_[r].value;
						Done(r);
					}
					return true;
				case 28: return efu([&] { return sumXYZ(fs); }); // ESADD
				case 29: // EATANxy
					return efu([&] {
						const __m128 v = VF(fs);
						const __m128 x = v;
						const __m128 y = d::Float(_mm_shuffle_epi32(d::Int(v), 0x01));
						__m128 num = d::Compute(d::Arith::Sub, y, true, x, fs != 0, c);
						const __m128 den = d::Compute(d::Arith::Add, x, fs != 0, y, true, c);
						num = d::Compute(d::Arith::Div, num, true, den, true, c);
						return Atan(num);
					});
				case 30: // ESQRT
					return efu([&] { return d::SQRTSS(_mm_and_ps(component(fs, fsf), d::All(0x7fffffffu))); });
				case 31: // ESIN: x + s2 x^3 + s3 x^5 + s4 x^7 + s5 x^9
					return efu([&] {
						const __m128 x = component(fs, fsf);
						__m128 pq = x;
						__m128 f = d::Compute(d::Arith::Mul, x, fs != 0, x, fs != 0, c); // x^2
						const __m128 t1 = f;
						f = d::Compute(d::Arith::Mul, f, fs != 0, pq, true, c); // x^3
						__m128 t2 = f;
						f = d::MULPS(f, d::Scalar(0xbe2aaaa4u)); // S2
						pq = d::Compute(d::Arith::Add, pq, true, f, fs != 0, c);
						for (uint32_t k : {0x3c08873eu, 0xb94fb21fu}) // S3, S4
						{
							t2 = d::Compute(d::Arith::Mul, t2, true, t1, true, c);
							f = d::MULPS(t2, d::Scalar(k));
							pq = d::Compute(d::Arith::Add, pq, true, f, fs != 0, c);
						}
						t2 = d::Compute(d::Arith::Mul, t2, true, t1, true, c);
						t2 = d::MULPS(t2, d::Scalar(0x362e9c14u)); // S5
						return d::Compute(d::Arith::Add, pq, true, t2, true, c);
					});
			}
			break;
		case 1:
			switch (row)
			{
				case 12: WriteVF(ft, dest, d::Float(_mm_shuffle_epi32(d::Int(VF(fs)), 0x39))); return true; // MR32
				case 13: // SQI
				{
					uint32_t a = 0;
					if (it)
					{
						const int r = AllocGpr(static_cast<int>(it), static_cast<int>(it), op.lOp.backupVI);
						a = gpr_[r].value & 0xffff;
						Put(r, gpr_[r].value + 1);
						Done(r);
					}
					DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
					uint8_t* p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
									 : ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
					alignas(16) uint32_t v[4];
					_mm_store_si128(reinterpret_cast<__m128i*>(v), d::Int(VF(fs)));
					uint32_t* m = reinterpret_cast<uint32_t*>(p);
					if (dest & 8) m[0] = v[0];
					if (dest & 4) m[1] = v[1];
					if (dest & 2) m[2] = v[2];
					if (dest & 1) m[3] = v[3];
					return true;
				}
				case 14: // SQRT
				{
					__m128 t = component(ft, ftf);
					V.divFlag = 0;
					if (static_cast<int32_t>(d::Bits(t)) < 0) // testNeg: invalid, and the magnitude's root
					{
						V.divFlag = kDivI;
						t = _mm_and_ps(t, d::All(0x7fffffffu));
					}
					if (vu1 ? EmuConfig.Cpu.Recompiler.vu1Overflow : EmuConfig.Cpu.Recompiler.vu0Overflow)
						t = d::MINSS(t, d::Scalar(0x7f7fffffu));
					V.pq[op.writeQ ? 1 : 0] = d::Bits(d::SQRTSS(t));
					return true;
				}
				case 15: // MFIR: sign-extends the host register in place, saying nothing
				{
					const int t = AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest));
					int32_t v = 0;
					if (is)
					{
						const int r = AllocGpr(static_cast<int>(is), -1);
						gpr_[r].value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(gpr_[r].value)));
						v = static_cast<int32_t>(gpr_[r].value);
						Done(r);
					}
					DoneXmm(t);
					WriteVF(ft, dest, d::Float(_mm_set1_epi32(v)));
					return true;
				}
				case 16: // RGET
					if (!op.lOp.noWriteVF)
						WriteVF(ft, dest, d::All(R.VI[REG_R].UL));
					return true;
				case 26: // XITOP
					WriteVi(op, it, R.GetVifRegs().itop & (vu1 ? 0x3ff : 0xff));
					return true;
				case 28: return efu([&] { return Rcp(sumXYZ(fs)); }); // ERSADD
				case 29: // EATANxz
					return efu([&] {
						const __m128 v = VF(fs);
						const __m128 x = v;
						const __m128 z = d::Float(_mm_shuffle_epi32(d::Int(v), 0x02));
						__m128 num = d::Compute(d::Arith::Sub, z, true, x, fs != 0, c);
						const __m128 den = d::Compute(d::Arith::Add, x, fs != 0, z, true, c);
						num = d::Compute(d::Arith::Div, num, true, den, true, c);
						return Atan(num);
					});
				case 30: // ERSQRT
					return efu([&] { return Rcp(d::SQRTSS(_mm_and_ps(component(fs, fsf), d::All(0x7fffffffu)))); });
				case 31: // EATAN: of (x - 1) / (x + 1)
					return efu([&] {
						const __m128 x = component(fs, fsf);
						const __m128 one = d::Scalar(0x3f800000u);
						__m128 f = d::SUBPS(x, one); // the first lanes: SUBSS
						const __m128 pq = d::ADDSS(x, one);
						f = d::Compute(d::Arith::Div, f, fs != 0, pq, true, c);
						return Atan(f);
					});
			}
			break;
		case 2:
			switch (row)
			{
				case 13: // LQD: the address register decremented first
				{
					const uint8_t* p;
					if (is || !vu1)
					{
						const int r = AllocGpr(static_cast<int>(is), static_cast<int>(is), op.lOp.backupVI); // VU0's VI00: a temporary
						Put(r, gpr_[r].value - 1);
						const uint32_t a = static_cast<uint32_t>(static_cast<int16_t>(gpr_[r].value));
						Done(r);
						p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
								: ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
					}
					else
					{
						p = VU1.Mem + (0xffff & (V.memSize - 8)); // as microVU addresses it
					}
					if (!op.lOp.noWriteVF)
					{
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
						WriteVF(ft, dest, d::Float(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))));
					}
					return true;
				}
				case 14: // RSQRT
				{
					__m128 s = component(fs, fsf);
					__m128 t = component(ft, ftf);
					V.divFlag = 0;
					if (static_cast<int32_t>(d::Bits(t)) < 0)
					{
						V.divFlag = kDivI;
						t = _mm_and_ps(t, d::All(0x7fffffffu));
					}
					t = d::SQRTSS(t);
					if (isZero(t))
					{
						V.divFlag = isZero(s) ? kDivI : kDivD;
						s = _mm_and_ps(s, d::All(0x80000000u));
						s = _mm_or_ps(s, d::All(0x7f7fffffu));
					}
					else
					{
						s = d::Compute(d::Arith::Div, s, fs != 0, t, true, c);
						s = clamp1(s, fs != 0);
					}
					V.pq[op.writeQ ? 1 : 0] = d::Bits(s);
					return true;
				}
				case 15: // ILWR
				{
					const int lane = (dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3));
					const uint32_t a = MoveVi(is);
					const uint8_t* p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
										   : ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
					uint16_t v;
					std::memcpy(&v, p + lane * 4, 2);
					WriteVi(op, it, v);
					return true;
				}
				case 16: // RINIT
					if (fs || fsf == 3)
						R.VI[REG_R].UL = (R.VF[fs].UL[fsf] & 0x007fffffu) | 0x3f800000u;
					else
						R.VI[REG_R].UL = 0x3f800000u;
					return true;
				case 28: return efu([&] { return d::SQRTSS(sumXYZ(fs)); }); // ELENG
				case 29: // ESUM: x + y + z + w, as (x + w) + (y + z)
					return efu([&] {
						__m128 f = VF(fs);
						__m128 t1 = d::Float(_mm_shuffle_epi32(d::Int(f), 0x1b));
						f = d::Compute(d::Arith::Add, f, fs != 0, t1, true, c, esumSwap_);
						t1 = d::Float(_mm_shuffle_epi32(d::Int(f), 0x01));
						return d::Compute(d::Arith::Add, f, fs != 0, t1, true, c);
					});
				case 30: return efu([&] { return Rcp(component(fs, fsf)); }); // ERCPR
				case 31: // EEXP: 1 / (1 + e1 x + e2 x^2 + ... + e6 x^6)^4
					return efu([&] {
						const __m128 x = component(fs, fsf);
						__m128 pq = d::MULPS(x, d::Scalar(0x3e7fffa8u)); // E1
						pq = d::ADDSS(pq, d::Scalar(0x3f800000u));
						__m128 t1 = d::Compute(d::Arith::Mul, x, fs != 0, x, fs != 0, c); // x^2
						__m128 t2 = t1;
						t1 = d::MULPS(t1, d::Scalar(0x3d0007f4u)); // E2
						pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
						for (uint32_t k : {0x3b29d3ffu, 0x3933e553u, 0x36b63510u}) // E3, E4, E5
						{
							t2 = d::Compute(d::Arith::Mul, t2, true, x, fs != 0, c);
							t1 = d::MULPS(t2, d::Scalar(k));
							pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
						}
						t2 = d::Compute(d::Arith::Mul, t2, true, x, fs != 0, c);
						t2 = d::MULPS(t2, d::Scalar(0x353961acu)); // E6
						pq = d::Compute(d::Arith::Add, pq, true, t2, true, c);
						pq = d::Compute(d::Arith::Mul, pq, true, pq, true, c);
						pq = d::Compute(d::Arith::Mul, pq, true, pq, true, c);
						return Rcp(pq);
					});
			}
			break;
		default:
			switch (row)
			{
				case 13: // SQD: the address register decremented first
				{
					uint8_t* p;
					if (it || !vu1)
					{
						const int r = AllocGpr(static_cast<int>(it), static_cast<int>(it), op.lOp.backupVI); // VU0's VI00: a temporary
						Put(r, gpr_[r].value - 1);
						const uint32_t a = gpr_[r].value & 0xffff;
						Done(r);
						p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
								: ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
					}
					else
					{
						p = VU1.Mem + (0xffff & (V.memSize - 8)); // as microVU addresses it
					}
					DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
					alignas(16) uint32_t v[4];
					_mm_store_si128(reinterpret_cast<__m128i*>(v), d::Int(VF(fs)));
					uint32_t w[4];
					std::memcpy(w, p, 16);
					if (dest & 8) w[0] = v[0];
					if (dest & 4) w[1] = v[1];
					if (dest & 2) w[2] = v[2];
					if (dest & 1) w[3] = v[3];
					std::memcpy(p, w, 16);
					return true;
				}
				case 14: return true; // WAITQ: its stall is the analysis'
				case 15: // ISWR
				{
					const uint32_t a = MoveVi(is);
					uint8_t* p = vu1 ? VU1.Mem + ((a & 0x3ff) << 4)
									 : ((a & 0x400) ? reinterpret_cast<uint8_t*>(VU1.VF) + ((a & 0x3f) << 4) : VU0.Mem + ((a & 0xff) << 4));
					const int r = AllocGpr(static_cast<int>(it), -1, false, true); // all 32 bits, as ISW
					const uint32_t v = gpr_[r].value;
					Done(r);
					if (dest & 8) std::memcpy(p + 0, &v, 4);
					if (dest & 4) std::memcpy(p + 4, &v, 4);
					if (dest & 2) std::memcpy(p + 8, &v, 4);
					if (dest & 1) std::memcpy(p + 12, &v, 4);
					return true;
				}
				case 16: // RXOR
					if (fs || fsf == 3)
						R.VI[REG_R].UL ^= R.VF[fs].UL[fsf] & 0x7fffffu;
					return true;
				case 28: return efu([&] { return Rcp(d::SQRTSS(sumXYZ(fs))); }); // ERLENG
				case 30: return true; // WAITP
			}
			break;
	}
	return Fail("an unknown lower instruction", code, pc);
}

bool Runner::Execute(const OpInfo& op, uint64_t words, uint32_t pc) // mVUexecuteInstruction
{
	const uint32_t lower = static_cast<uint32_t>(words);
	const uint32_t upper = static_cast<uint32_t>(words >> 32);
	if (op.lOp.isNOP)
	{
		if (!Upper(op, upper, pc))
			return false;
		DivSet(op);
		if (op.uOp.iBit)
			Ibit(lower, pc);
		return true;
	}
	if (!op.swapOps)
	{
		if (!Upper(op, upper, pc))
			return false;
		DivSet(op);
		return Lower(op, lower, pc);
	}
	if (op.backupVF && !op.lOp.noWriteVF)
	{
		// doSwapOp: the lower instruction first, the upper reading the register
		// as it was, then the lower's result written over it whole.
		const uint32_t reg = op.lOp.VF_write.reg;
		const int t1 = AllocXmm(static_cast<int>(reg));
		const int t2 = AllocXmm();
		DoneXmm(t1);
		const __m128i before = _mm_load_si128(reinterpret_cast<const __m128i*>(R.VF[reg].UL));
		if (!Lower(op, lower, pc))
			return false;
		DoneXmm(AllocXmm(static_cast<int>(reg), static_cast<int>(reg), 0xf, false));
		const __m128i after = _mm_load_si128(reinterpret_cast<const __m128i*>(R.VF[reg].UL));
		_mm_store_si128(reinterpret_cast<__m128i*>(R.VF[reg].UL), before);
		if (!Upper(op, upper, pc))
			return false;
		DivSet(op);
		DoneXmm(AllocXmm(-1, static_cast<int>(reg), 0xf));
		DoneXmm(t2);
		if (reg)
			_mm_store_si128(reinterpret_cast<__m128i*>(R.VF[reg].UL), after);
		return true;
	}
	if (!Lower(op, lower, pc))
		return false;
	if (!Upper(op, upper, pc))
		return false;
	DivSet(op);
	return true;
}

void Runner::Run(uint32_t startPC, uint32_t cycles)
{
	V.stats.runs++;
	// mVUexecute
	V.cycles = static_cast<int32_t>(cycles);
	V.totalCycles = cycles;
	const uint32_t vuLimit = vu1 ? 0x3ff8 : 0xff8;
	ShadowBlock* b = SearchProg(V, startPC & vuLimit, V.lpState, true);

	// mVUdispatcherAB: the control register, P and Q, the flags.
	const uint32_t vuFPCR = vu1 ? EmuConfig.Cpu.VU1FPCR.bitmask : EmuConfig.Cpu.VU0FPCR.bitmask;
	const bool fpcr = EmuConfig.Cpu.FPUFPCR.bitmask != vuFPCR;
	if (fpcr)
		_mm_setcsr(vuFPCR);
	V.pq[0] = R.VI[REG_Q].UL;
	V.pq[1] = R.pending_q;
	V.pq[2] = R.VI[REG_P].UL;
	V.pq[3] = vu1 ? R.pending_p : R.VI[REG_P].UL;
	std::memcpy(V.macFlag, R.micro_macflags, sizeof(V.macFlag));
	std::memcpy(V.clipFlag, R.micro_clipflags, sizeof(V.clipFlag));
	std::memcpy(V.statFlag, R.micro_statusflags, sizeof(V.statFlag));

	const bool syncHack = EmuConfig.Gamefixes.VUSyncHack;
	while (b)
	{
		if (b->exit == Exit::Unsupported || !b->unsupported.empty())
		{
			if (V.unsupported.empty())
				V.unsupported = b->unsupported;
			break;
		}
		// mVUtestCycles
		const int32_t left = syncHack ? V.cycles - static_cast<int32_t>(b->a.cycles) : V.cycles - 1;
		if (left < 0)
		{
			V.lpState = b->pState; // copyPLState
			if (syncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
				R.nextBlockCycles = b->a.cycles;
			EndProgram(*b, 0, b->a.startPC);
			V.stats.pauses++;
			break;
		}
		V.cycles -= static_cast<int32_t>(b->a.cycles);
		V.stats.blocks++;
		// The block's VI registers in host registers, from none (reset) and the
		// loads it begins with.
		ResetGprs();
		ResetXmms();
		Preload(*b);

		const Block& a = b->a;
		block_ = b;
		bool ok = true;
		bool ended = false;
		for (uint32_t i = 0; i < a.count && ok && !ended; i++)
		{
			const OpInfo& op = a.ops[i];
			const uint32_t pc = (a.startPC + i * 8) & (V.memSize - 8);
			if (op.uOp.mBit)
				R.flags |= VUFLAG_MFLAGSET;
			if (vu1 && op.lOp.kickcycles && EmuConfig.Gamefixes.XgKickHack)
				ok = Fail("the XGKICK game fix", 0, pc);
			if (ok)
				ok = Execute(op, a.words[i], pc);
			V.stats.pairs++;
			if (ok && op.uOp.tBit && !op.isBdelay && !op.lOp.branch && (VU0.VI[REG_FBRST].UL & (vu1 ? 0x800u : 0x8u)))
			{
				// mVUDoTBit: the T bit enabled, the program ends here.
				for (const TBitEnd& t : b->tBits)
				{
					if (t.pair != i)
						continue;
					VU0.VI[REG_VPU_STAT].UL |= vu1 ? 0x400u : 0x4u;
					R.flags |= VUFLAG_INTCINTERRUPT;
					EndProgram(*b, 1, t.tpc, &t.end);
					ended = true;
					break;
				}
				if (ended)
					break;
			}
			if (ok && op.doXGKICK && !(op.lOp.isKick))
				XgkickDelay();
		}
		if (!ok)
			break;
		if (ended)
		{
			b = nullptr;
			continue;
		}

		switch (b->exit)
		{
			case Exit::EBit:
				EndProgram(*b, 1, b->exitPC);
				b = nullptr;
				break;
			case Exit::MBit:
				SetupBranch(*b);
				V.lpState = b->pStateEnd;
				if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
					R.nextBlockCycles = 0;
				EndProgram(*b, 0, b->exitPC);
				b = nullptr;
				break;
			case Exit::Branch:
				SetupBranch(*b);
				b = b->taken;
				break;
			case Exit::BranchEBit:
				EndProgram(*b, 1, b->exitPC);
				b = nullptr;
				break;
			case Exit::CondBranch:
			case Exit::CondBranchEBit:
			{
				const int16_t v = static_cast<int16_t>(V.branch);
				const uint32_t lower = static_cast<uint32_t>(a.words[a.count - 2]);
				bool taken = false;
				switch (lower >> 25)
				{
					case 0x28: taken = v == 0; break; // IBEQ
					case 0x29: taken = v != 0; break; // IBNE
					case 0x2C: taken = v < 0; break; // IBLTZ
					case 0x2D: taken = v > 0; break; // IBGTZ
					case 0x2E: taken = v <= 0; break; // IBLEZ
					case 0x2F: taken = v >= 0; break; // IBGEZ
				}
				if (b->exit == Exit::CondBranchEBit)
				{
					SetupBranch(*b);
					EndProgram(*b, 2, taken ? b->target : b->notTakenPC);
					b = nullptr;
					break;
				}
				SetupBranch(*b);
				b = taken ? b->taken : b->notTaken;
				break;
			}
			case Exit::Jump:
				SetupBranch(*b);
				b = JumpTo(b, V.branch);
				break;
			case Exit::JumpEBit:
				EndProgram(*b, 2, V.branch);
				b = nullptr;
				break;
			case Exit::EvilJump:
			case Exit::EvilJumpEBit:
			{
				SetupBranch(*b);
				const uint32_t target = V.evilBranch;
				V.evilBranch = V.evilevilBranch;
				if (b->exit == Exit::EvilJumpEBit)
				{
					EndProgram(*b, 2, target);
					// normJumpCompile stores arg1regd in TPC after mVUendProgram,
					// which has used that register (ecx, gprT2, on Windows) for
					// the clip flag last. (On Linux it is edi, which the call to
					// mVU1clearlpStateJIT leaves as it may: nothing to follow.)
					R.VI[REG_TPC].UL = R.VI[REG_CLIP_FLAG].UL;
					b = nullptr;
					break;
				}
				b = JumpTo(b, target);
				break;
			}
			case Exit::Unsupported:
				b = nullptr;
				break;
		}
	}

	if (fpcr)
		_mm_setcsr(EmuConfig.Cpu.FPUFPCR.bitmask);

	// mVUcleanUp
	V.cycles = static_cast<int32_t>(V.totalCycles) - std::max(0, V.cycles);
	R.cycle += static_cast<uint32_t>(V.cycles);
	const uint32_t passed = static_cast<uint32_t>(std::min(V.cycles, 3000)) * static_cast<uint32_t>(EmuConfig.Speedhacks.EECycleSkip);
	if (passed > 0)
	{
		const s64 vu0Offset = static_cast<s64>(VU0.cycle - cpuRegs.cycle);
		cpuRegs.cycle += passed;
		if (!vu1)
			VU0.cycle = cpuRegs.cycle + vu0Offset;
		else
			VU0.cycle += passed;
	}
}

// ---- the provider -------------------------------------------------------------------------

mvu::Settings ReadSettings()
{
	mvu::Settings s;
	s.flagHack = EmuConfig.Speedhacks.vuFlagHack;
	s.ibitHack = EmuConfig.Gamefixes.IbitHack;
	s.xgkickHack = EmuConfig.Gamefixes.XgKickHack;
	s.vuSyncHack = EmuConfig.Gamefixes.VUSyncHack;
	s.fullVu0SyncHack = EmuConfig.Gamefixes.FullVU0SyncHack;
	s.eeCycleRate = EmuConfig.Speedhacks.EECycleRate;
	return s;
}

class Provider final : public BaseVUmicroCPU
{
public:
	explicit Provider(int index)
	{
		m_Idx = index;
		IsInterpreter = false;
		s_vu[index].index = index;
	}
	const char* GetShortName() const override { return m_Idx ? "ps2nVU1" : "ps2nVU0"; }
	const char* GetLongName() const override { return m_Idx ? "PS2Native VU1 interpreter" : "PS2Native VU0 interpreter"; }
	void Shutdown() override {}
	void Reset() override
	{
		Vu& V = s_vu[m_Idx];
		V.index = m_Idx;
		ResetVu(V);
	}
	void Step() override {}
	void SetStartPC(u32 startPC) override { (m_Idx ? VU1 : VU0).start_pc = startPC; }
	void Execute(u32 cycles) override
	{
		Vu& V = s_vu[m_Idx];
		if (!V.R)
			Reset();
		VURegs& R = *V.R;
		if (m_Idx == 0)
			VU0.flags &= ~VUFLAG_MFLAGSET;
		if (!(VU0.VI[REG_VPU_STAT].UL & (m_Idx ? 0x100u : 0x1u)))
			return;
		if (!V.unsupported.empty())
			return; // stopped at what it does not carry out
		V.settings = ReadSettings();
		R.VI[REG_TPC].UL <<= 3;
		Runner(V).Run(R.VI[REG_TPC].UL, cycles);
		R.VI[REG_TPC].UL >>= 3;
		if (R.flags & VUFLAG_INTCINTERRUPT)
		{
			R.flags &= ~VUFLAG_INTCINTERRUPT;
			hwIntcIrq(m_Idx ? 7 : 6);
		}
	}
	void Clear(u32, u32) override { ClearVu(s_vu[m_Idx]); }
	void ResumeXGkick() override {}
};

Provider s_provider0(0);
Provider s_provider1(1);

} // namespace

BaseVUmicroCPU* Interpreter(int vu)
{
	return vu ? static_cast<BaseVUmicroCPU*>(&s_provider1) : static_cast<BaseVUmicroCPU*>(&s_provider0);
}

const InterpreterStats& Stats(int vu)
{
	return s_vu[vu ? 1 : 0].stats;
}

const char* Unsupported(int vu)
{
	const Vu& V = s_vu[vu ? 1 : 0];
	return V.unsupported.empty() ? nullptr : V.unsupported.c_str();
}

void ClearUnsupported(int vu)
{
	s_vu[vu ? 1 : 0].unsupported.clear();
}

bool LogPrograms(int vu, const char* path)
{
	Vu& V = s_vu[vu ? 1 : 0];
	if (V.log)
	{
		std::fclose(V.log);
		V.log = nullptr;
	}
	if (!path)
		return true;
	V.log = std::fopen(path, "wb");
	return V.log != nullptr;
}

} // namespace ps2n::vu
