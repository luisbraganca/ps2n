// SPDX-License-Identifier: GPL-3.0-or-later
//
// See vu_interp.h. The names in the comments are microVU's (PCSX2 v2.9.170,
// pcsx2/x86/microVU*.{cpp,h,inl}, Copyright 2002-2026 PCSX2 Dev Team,
// GPL-3.0+), so that each piece can be read beside the code it follows.

#include "vu_interp.h"

#include "ps2n/ops_vu0.h"
#include "ps2n/vu_mvu.h"
#include "ps2n/vu_ops.h"
#include "ps2n/vu_plan.h"
#include "ps2n/vu_recomp.h"

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
#include <unordered_map>
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

void Xgkick(uint32_t address)
{
	mVU_XGKICK_(address);
}

namespace {

using mvu::Block;
using mvu::OpInfo;
using mvu::PipeState;

namespace d = ps2n::ee::vu0::detail;

// ---- plans (ps2n/vu_plan.h) -----------------------------------------------------------------

// A plan, kept once however many blocks of the record have it: its steps,
// the compiled function a build has for it, if any, and how often it ran.
struct PlanRecord
{
	PlanKey key;
	std::vector<Step> steps;
	BlockFn fn = nullptr; // compiled ahead of time; null: the steps are interpreted
	uint64_t runs = 0;
	// Where it was first met and what its pairs were then, for a capture.
	uint32_t at = 0;
	std::vector<uint64_t> words;
	bool captured = false; // written to the capture file already
};

struct PlanKeyHash
{
	size_t operator()(const PlanKey& k) const { return static_cast<size_t>(k.a ^ (k.b * 0x9E3779B97F4A7C15ull)); }
};

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

// microBlock, with what the second pass decided. It begins with what the
// block's code is given of it (Link in ps2n/vu_recomp.h, filled in by
// FinishBlock): the rest is for compiling it and the blocks after it, and for
// the provider when the program stops there.
struct ShadowBlock : Link
{
	ShadowBlock()
		: Link{}
	{
	}

	bool stop = true; // the run stops at it: it holds something not carried out
	// Its plan's last step, Leave, for the interpreter: how the program goes
	// on from it, the opcode of the conditional branch it ends with, what its
	// link does first.
	uint8_t leave = 0;
	uint8_t condOp = 0;
	uint32_t setup = 0;
	// The instances the flag registers take when the program stops at this
	// block ([0]) or ends in it ([1]): status, MAC, clip (getLastFlagInst).
	uint8_t lastInst[2][3] = {};

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
	PlanRecord* plan = nullptr; // how its pairs are carried out (null with `unsupported`)
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

	template <class F>
	void forEach(F&& f)
	{
		for (const std::unique_ptr<ShadowBlock>& b : blocks_)
			f(*b);
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
	// What the last search at a start address found (microProgramQuick).
	// microVU clears all of them at every write into micro memory; here each
	// says which time round it is of (quickGen), and one of an earlier time
	// round counts as cleared (QuickAt). `last` and `foundAt` outlive that:
	// the program found there last, and the number of the last write into
	// micro memory before it was found (SearchProg).
	struct Quick
	{
		BlockManager* block = nullptr;
		Program* prog = nullptr;
		uint64_t gen = 0;
		Program* last = nullptr;
		uint64_t foundAt = 0;
	};
	std::vector<Quick> quick;
	uint64_t quickGen = 1;
	// The writes into micro memory, counted, and for each chunk of micro memory
	// (kWriteChunk bytes) the number of the last write into it.
	uint64_t writeGen = 0;
	std::vector<uint64_t> chunkWrite;
	Program* cur = nullptr;
	int cleared = 1;
	int isSame = -1;
	int total = 0;
	PipeState lpState;
	std::vector<std::unique_ptr<Program>> owned;
	mvu::InfoArray infos;
	mvu::Settings settings;
	// The settings a plan is made with, beside those: the VU clamp mode and
	// the game fixes that change what an instruction gives.
	d::Clamp clamp = {false, false, false};
	bool overflowHack = false; // Gamefixes.VUOverflowHack
	bool addSubHack = false; // Gamefixes.VuAddSubHack

	// microVU's variables, and what it keeps in host registers while it runs
	// (ps2n/vu_ops.h).
	Ctx c;
	uint32_t totalCycles = 0;
	int32_t cycles = 0;

	// The plans of the blocks met, each once, by their keys. A plan is not
	// thrown away with the record: the same code comes back.
	std::unordered_map<PlanKey, std::unique_ptr<PlanRecord>, PlanKeyHash> plans;

	InterpreterStats stats;
	std::string unsupported;
	std::FILE* log = nullptr;
};

Vu s_vu[2];

// The compiled blocks the host was linked with, by their plans' keys, and
// whether they are used.
std::unordered_map<PlanKey, BlockFn, PlanKeyHash> s_compiled[2];
bool s_useCompiled = true;

constexpr uint32_t kWriteChunk = 128; // sixteen pairs

void ResetVu(Vu& V) // mVUreset
{
	V.R = V.index ? &VU1 : &VU0;
	V.memSize = V.index ? 0x4000 : 0x1000;
	const uint32_t pairs = V.memSize / 8;
	V.prog.assign(pairs, {});
	V.quick.assign(pairs, {});
	V.writeGen = 0;
	V.chunkWrite.assign(V.memSize / kWriteChunk, 0);
	V.owned.clear();
	V.cur = nullptr;
	V.cleared = 1;
	V.isSame = -1;
	V.total = 0;
	V.lpState = PipeState{};
	V.R->nextBlockCycles = 0;
	V.infos.info.assign(pairs, OpInfo{});
	V.c.R = V.R;
	V.c.micro = reinterpret_cast<const uint32_t*>(V.R->Micro);
	V.c.memMask = V.memSize - 8;
	V.c.index = static_cast<uint32_t>(V.index);
}

void ClearVu(Vu& V) // mVUclear
{
	if (!V.cleared)
	{
		V.cleared = 1;
		V.lpState = PipeState{};
		V.quickGen++; // every quick reference cleared
	}
}

// The quick reference of a start address, cleared if it is from before the
// last write into micro memory.
Vu::Quick& QuickAt(Vu& V, uint32_t pair)
{
	Vu::Quick& q = V.quick[pair];
	if (q.gen != V.quickGen) [[unlikely]]
	{
		q.block = nullptr;
		q.prog = nullptr;
		q.gen = V.quickGen;
	}
	return q;
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

// Whether nothing was written, after the write numbered `since`, into the
// micro memory the program was compiled from, by chunks.
bool Unwritten(const Vu& V, const Program& prog, uint64_t since)
{
	for (const Range& r : prog.ranges)
	{
		if (r.end < r.start)
			return false; // a range left open
		if (r.end == r.start)
			continue;
		const size_t last = std::min(static_cast<size_t>(r.end - 1) / kWriteChunk, V.chunkWrite.size() - 1);
		for (size_t chunk = static_cast<size_t>(r.start) / kWriteChunk; chunk <= last; chunk++)
		{
			if (V.chunkWrite[chunk] > since)
				return false;
		}
	}
	return true;
}

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

// mVUsearchProg: the program micro memory holds at the start address
// (VURegs::start_pc), and its block at `startPC` for the state `pState`.
//
// After a write into micro memory microVU has forgotten which program is at
// each start (ClearVu), and compares the programs recorded for the start with
// micro memory, the one found last first, until one is equal. Where that one
// is still the first and nothing was written since into the memory it was
// compiled from, the comparison can only find it equal: it is not made, and
// what it would have done is done.
//
// `jumpProg`, `jumpBlock`: for an indirect jump, the program its block found
// here before and the block it found in it. In the same program the search
// for the block gives that block again (the blocks of a block manager are
// never taken away, and a search takes the first that fits), so it is not
// made.
ShadowBlock* SearchProg(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp, const Program* jumpProg = nullptr,
	ShadowBlock* jumpBlock = nullptr)
{
	const uint32_t startPair = V.R->start_pc / 8;
	Vu::Quick& quick = QuickAt(V, startPair);
	std::deque<Program*>& list = V.prog[startPair];
	if (!quick.prog)
	{
		Program* found = nullptr;
		if (quick.last && !list.empty() && list.front() == quick.last && Unwritten(V, *quick.last, quick.foundAt))
		{
			found = quick.last;
			V.stats.spared++;
			V.cleared = 0; // as mVUcmpProg leaves them
			V.cur = found;
			V.isSame = -1;
		}
		else
		{
			V.stats.searches++;
			for (auto it = list.begin(); it != list.end(); ++it)
			{
				V.stats.compares++;
				if (CmpProg(V, **it))
				{
					found = *it;
					list.erase(it);
					list.push_front(found);
					break;
				}
			}
		}
		if (!found)
		{
			V.cleared = 0;
			V.isSame = 1;
			V.cur = CreateProg(V, startPair);
			ShadowBlock* entry = BlockFetch(V, startPC, pState, fromLp);
			quick.block = V.cur->block[startPC / 8].get();
			quick.prog = V.cur;
			quick.last = V.cur;
			quick.foundAt = V.writeGen;
			list.push_front(V.cur);
			return entry;
		}
		quick.prog = found;
		quick.last = found;
		quick.foundAt = V.writeGen;
	}
	else
	{
		V.isSame = -1;
		V.cur = quick.prog;
	}
	quick.block = quick.prog->block[startPC / 8].get();
	if (jumpProg && jumpProg == quick.prog)
		return jumpBlock;
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

void PlanBlock(Vu& V, ShadowBlock& b);

// mVUcompile: the first pass (ps2n/vu_mvu.h), then what the second pass
// does to the record: the ranges, and the blocks a branch leads to.
ShadowBlock* CompileBlock(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp)
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

// ---- a block's plan: what the second pass decides ---------------------------------------------
//
// microVU's second pass generates a block's host code with a register
// allocator: which host register holds which VI or VF register at each
// point is decided as the pass goes. Two things about that show in results,
// so the allocation is followed here, without values, as microVU makes it:
// the same count of allocations, in the same order.
//
// VI registers: inside a block microVU keeps them in host registers, all 32
// bits of them, and writes the low half back to VURegs at the block's end.
// What it does to the high half mostly does not show: branches compare 16
// bits and addresses are masked. Not all of it: FMEQ compares the whole
// register, ISW and ISWR store the whole of one MFIR sign-extended in place
// without saying so, and the integer instructions carry the high half on. So
// a plan has what each host register holds (the steps Vi...), and which VI
// register is in which is decided as microVU decides it: the least recently
// used register given up first, the loads at the block's start
// (mvuPreloadRegisters), the registers a call may change emptied before an
// XGKICK. A VI register in VURegs always holds its value (the low half of a
// host register holding it; the steps write both): microVU reads VURegs only
// for a register no host register holds.
//
// VF registers: which XMM register microVU gives each operand matters for
// one thing. With AVX, PCSX2's emitter swaps the two sources of a
// commutative instruction (ADDPS, MULPS) when the first is in xmm0 to xmm7
// and the second in xmm8 to xmm15, for a shorter encoding, and where both
// sources are NaN x86 gives the first one's. So a step says where the
// emitter would swap (mode::kSwap), and the values are the VF registers'.
// xmm15 is P and Q.

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

// What a block's link does (mVUsetupBranch), for its plan's last step: which
// of the flags' instances are put in a new order and in which
// (mVUsetupFlags), and whether P and Q are exchanged with their other
// instances (shufflePQ). The bits are step::Setup's (ps2n/vu_ops.h).
uint32_t SetupOf(const ShadowBlock& b)
{
	const Block& a = b.a;
	const mvu::FlagCycles& mFC = a.flagCycles;
	const int* const flags[3] = {mFC.xStatus, mFC.xMac, mFC.xClip};
	uint32_t setup = 0;
	for (int k = 0; k < 3; k++)
	{
		if (!(a.end.needExactMatch & (1 << k)))
			continue;
		int order[4];
		SortFlag(flags[k], order, mFC.cycles);
		bool same = true;
		uint32_t bits = 0;
		for (int i = 0; i < 4; i++)
		{
			bits |= static_cast<uint32_t>(order[i]) << (2 * i);
			same = same && order[i] == i;
		}
		if (!same)
			setup |= (1u << k) | (bits << (8 + 8 * k));
	}
	if (b.linkQ)
		setup |= 8;
	if (b.linkP)
		setup |= 16;
	return setup;
}

class Planner
{
public:
	Planner(const Vu& V, const ShadowBlock& b, std::vector<Step>& out)
		: V(V)
		, b(b)
		, out(out)
		, vu1(V.index != 0)
		, evil(b.pState.blockType == 2)
		, ibitHack(V.settings.ibitHack)
		, clamp(V.clamp)
	{
	}

	// The block's plan, into `out`. False where the block holds something
	// that is not carried out; `what` says it.
	bool Run(std::string& what)
	{
		const Block& a = b.a;
		// What changes the code of every block, with the game fix: not followed.
		if (vu1 && V.settings.xgkickHack)
			return Fail("the XGKICK game fix", 0, 0, what);
		// The block's registers in host registers, from none (reset) and the
		// loads it begins with.
		ResetGprs();
		ResetXmms();
		Preload();
		for (uint32_t i = 0; i < a.count; i++)
		{
			const OpInfo& op = a.ops[i];
			if (op.uOp.mBit)
				Emit(StepOp::MFlag);
			if (!Execute(op, a.words[i], i))
			{
				what = what_;
				return false;
			}
			if (op.uOp.tBit && !op.isBdelay && !op.lOp.branch)
			{
				// mVUDoTBit: with the T bit enabled, the program ends here.
				for (size_t k = 0; k < b.tBits.size(); k++)
				{
					if (b.tBits[k].pair == i)
					{
						Emit(StepOp::TBit, static_cast<uint32_t>(k), vu1 ? 1 : 0);
						break;
					}
				}
			}
			if (op.doXGKICK && !op.lOp.isKick)
				XgkickDelay();
		}
		// How the program goes on from the block (the step Leave), and what
		// its link does first where it has one (mVUsetupBranch).
		const uint32_t setup = SetupOf(b);
		switch (b.exit)
		{
			case Exit::Branch: Emit(StepOp::Leave, 1, 0, setup); break;
			case Exit::CondBranch: Emit(StepOp::Leave, 2, a.count >= 2 ? static_cast<uint32_t>(a.words[a.count - 2]) >> 25 : 0, setup); break;
			case Exit::Jump: Emit(StepOp::Leave, 3, 0, setup); break;
			case Exit::EvilJump: Emit(StepOp::Leave, 4, 0, setup); break;
			// The program stops or ends here, which is the provider's; these
			// three link first all the same.
			case Exit::MBit:
			case Exit::CondBranchEBit:
			case Exit::EvilJumpEBit: Emit(StepOp::Leave, 0, 0, setup); break;
			default: Emit(StepOp::Leave, 0, 0, 0); break;
		}
		return true;
	}

private:
	void Emit(StepOp op, uint32_t o0 = 0, uint32_t o1 = 0, uint32_t o2 = 0, uint32_t o3 = 0, uint32_t o4 = 0)
	{
		Step s;
		s.op = op;
		s.o[0] = o0;
		s.o[1] = o1;
		s.o[2] = o2;
		s.o[3] = o3;
		s.o[4] = o4;
		out.push_back(s);
	}

	bool Fail(const char* reason, uint32_t code, uint32_t pair, std::string& what)
	{
		char text[160];
		std::snprintf(text, sizeof(text), "VU%d: %s (%08x) at %04x", V.index, reason, code, (b.a.startPC + pair * 8) & (V.memSize - 8));
		what = text;
		return false;
	}
	bool Fail(const char* reason, uint32_t code, uint32_t pair) { return Fail(reason, code, pair, what_); }

	// An encoding microVU knows nothing of (mVUunknown): the code it generates
	// for it is none, so the plan has no step for it. (Its first pass ends the
	// block after one, unless it is 8000033C, the lower NOP's word.) Counted,
	// for the report.
	bool Unknown()
	{
		unknown_++;
		return true;
	}

	// ---- VI registers in host registers (microRegAlloc's GPR side, microVU_IR.h) -----------
	struct HostGpr // microMapGPR
	{
		int vi = -1; // VIreg: -1 nothing (a temporary)
		int count = 0;
		bool needed = false;
		bool dirty = false;
		bool zext = false; // isZeroExtended
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
	void Preload()
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
				Emit(StepOp::ViBackup, static_cast<uint32_t>(i)); // writeVIBackup
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
			Emit(StepOp::ViZero, static_cast<uint32_t>(x));
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
							Emit(StepOp::ViLoad, static_cast<uint32_t>(x), static_cast<uint32_t>(write));
							Emit(StepOp::ViBackup, static_cast<uint32_t>(x));
							backup = false;
						}
						Emit(zextIfDirty ? StepOp::ViCopyZ : StepOp::ViCopy, static_cast<uint32_t>(x), static_cast<uint32_t>(i));
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
					Emit(StepOp::ViZext, static_cast<uint32_t>(i));
					gpr_[i].zext = true;
				}
				gpr_[i].needed = true;
				if (backup)
					Emit(StepOp::ViBackup, static_cast<uint32_t>(i));
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
			Emit(StepOp::ViLoad, static_cast<uint32_t>(x), static_cast<uint32_t>(write));
			Emit(StepOp::ViBackup, static_cast<uint32_t>(x));
			backup = false;
		}
		if (load > 0)
			Emit(StepOp::ViLoad, static_cast<uint32_t>(x), static_cast<uint32_t>(load));
		else if (load == 0)
			Emit(StepOp::ViZero, static_cast<uint32_t>(x));
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
					Emit(StepOp::ViLoad, static_cast<uint32_t>(x), static_cast<uint32_t>(write));
				Emit(StepOp::ViBackup, static_cast<uint32_t>(x));
			}
		}
		g.count = thisCounter;
		g.needed = true;
		return x;
	}
	void Done(int i) { gpr_[i].needed = false; } // clearNeeded
	// The VI register a host register holds, which a result written to it goes
	// to as well; 0 for none.
	uint32_t ViOf(int i) const { return gpr_[i].vi > 0 ? static_cast<uint32_t>(gpr_[i].vi) : 0; }
	// moveVIToGPR: the host register of a VI register that is only read, for
	// the step that reads it next; kNone for VI00.
	uint32_t MoveVi(uint32_t vi)
	{
		if (vi == 0)
			return kNone;
		const int s = AllocGpr(static_cast<int>(vi));
		Done(s);
		return static_cast<uint32_t>(s);
	}
	// The VI register written: a host register for it alone (allocGPR(-1, vi)),
	// given the value on its way (t0).
	void WriteVi(const OpInfo& op, uint32_t vi)
	{
		const int t = AllocGpr(-1, static_cast<int>(vi), op.lOp.backupVI);
		Emit(StepOp::ViSetT, static_cast<uint32_t>(t), ViOf(t));
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
	// these two registers, when it writes AVX (EmitSIMDImpl: a two-byte VEX
	// prefix needs the second source in xmm0 to xmm7, the first may be any).
	static bool Swapped(int first, int second) { return second >= 8 && first < 8; }

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

	// ---- what the settings and the analysis decided, for a step's operands -------------------
	uint32_t ClampBits() const
	{
		return (clamp.overflow ? mode::kOverflow : 0) | (clamp.extra ? mode::kExtra : 0) | (clamp.sign ? mode::kSign : 0);
	}
	// Where an instruction's flags go (mVUupdateFlags); what it does not look
	// at is left out, so that the same code gives the same steps.
	uint32_t FlagBits(const OpInfo& op) const
	{
		uint32_t fl = 0;
		if (op.sFlag.doFlag)
		{
			fl |= flag::kStatus | (op.sFlag.doNonSticky ? flag::kNonSticky : 0) |
				  (static_cast<uint32_t>(op.sFlag.lastWrite) << flag::kStatusLastShift) |
				  (static_cast<uint32_t>(op.sFlag.write) << flag::kStatusWriteShift) | (V.overflowHack ? flag::kOverflowHack : 0);
		}
		if (op.mFlag.doFlag)
			fl |= flag::kMac | (static_cast<uint32_t>(op.mFlag.write) << flag::kMacWriteShift);
		return fl;
	}
	uint32_t FmacMode(const OpInfo& op, d::Second k, uint32_t clampOps) const
	{
		uint32_t m = (static_cast<uint32_t>(k) << mode::kSecondShift) | (clampOps << mode::kClampShift) | ClampBits();
		if (k == d::Second::Q && op.readQ)
			m |= mode::kReadQ;
		return m;
	}

	// ---- the upper instructions (microVU_Upper.inl) -----------------------------------------

	// setupFtReg's allocations: Ft's host register, and tempFt's (-1: none).
	struct FtRegs
	{
		int ft;
		int tempFt;
	};
	FtRegs SetupFt(const OpInfo& op, d::Second k, uint32_t clampOps, uint32_t dest, uint32_t ft)
	{
		const d::Clamp c = clamp;
		const bool single = d::Single(dest);
		switch (k)
		{
			case d::Second::Vector:
			{
				const bool willClamp = c.extra || ((clampOps & d::cFt) && !c.extra && (c.overflow || c.sign));
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
			case d::Second::Bc:
			{
				const int whole = AllocXmm(static_cast<int>(ft));
				const int r = AllocXmm();
				DoneXmm(whole);
				return {r, r};
			}
			case d::Second::I:
			{
				const int r = AllocXmm(33, 0, static_cast<int>(dest));
				return {r, r};
			}
			case d::Second::Q:
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

	void FMACa(const OpInfo& op, d::Kind kind, d::Second k, bool toAcc, uint32_t clampOps, uint32_t code)
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
		const bool single = d::Single(dest);
		const bool minMax = kind == d::Kind::Max || kind == d::Kind::Min;
		uint32_t m = FmacMode(op, k, clampOps) | static_cast<uint32_t>(kind) | (toAcc ? mode::kToAcc : 0);
		if (kind == d::Kind::AddI && single && V.addSubHack)
			m |= mode::kAddSubHack;
		if (kind == d::Kind::Sub && k == d::Second::Vector && fs == ft) // doSafeSub
		{
			const int r = AllocXmm(-1, toAcc ? 32 : static_cast<int>(fd), static_cast<int>(dest));
			FlagTemps(op, dest, false, false);
			DoneXmm(r);
			Emit(StepOp::FMACa, code, m, FlagBits(op));
			return;
		}
		// The host registers, as mVU_FMACa allocates them.
		const FtRegs f = SetupFt(op, k, clampOps, dest, ft);
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
		if (minMax)
			MinMaxTemps(single);
		const bool swap = !single && (kind == d::Kind::Add || kind == d::Kind::Mul || kind == d::Kind::AddI) && Swapped(sReg, f.ft);
		if (toAcc)
		{
			FlagTemps(op, dest, true, f.tempFt >= 0);
			DoneXmm(accReg);
		}
		else if (!minMax)
		{
			FlagTemps(op, dest, f.tempFt >= 0, false);
		}
		DoneXmm(sReg);
		DoneXmm(f.ft);
		if (swap)
			m |= mode::kSwap;
		Emit(StepOp::FMACa, code, m, minMax ? 0 : FlagBits(op));
	}

	void FMACb(const OpInfo& op, bool subtract, d::Second k, uint32_t clampOps, uint32_t code) // MADDA, MSUBA
	{
		const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clampOps, dest, ft);
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
		Emit(StepOp::FMACb, code,
			FmacMode(op, k, clampOps) | (subtract ? mode::kSubtract : 0) | (swapMul ? mode::kSwap : 0) | (swapAdd ? mode::kSwapAdd : 0), FlagBits(op));
	}

	void FMACc(const OpInfo& op, d::Second k, uint32_t clampOps, uint32_t code) // MADD
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clampOps, dest, ft);
		const int accReg = AllocXmm(32);
		const int sReg = AllocXmm(static_cast<int>(fs), static_cast<int>(fd), static_cast<int>(dest));
		const bool swapMul = !single && Swapped(sReg, f.ft);
		const bool swapAdd = !single && Swapped(sReg, accReg);
		FlagTemps(op, dest, f.tempFt >= 0, false);
		DoneXmm(sReg);
		DoneXmm(f.ft);
		DoneXmm(accReg);
		Emit(StepOp::FMACc, code, FmacMode(op, k, clampOps) | (swapMul ? mode::kSwap : 0) | (swapAdd ? mode::kSwapAdd : 0), FlagBits(op));
	}

	void FMACd(const OpInfo& op, d::Second k, uint32_t clampOps, uint32_t code) // MSUB
	{
		const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
		const bool single = d::Single(dest);
		const FtRegs f = SetupFt(op, k, clampOps, dest, ft);
		const int sReg = AllocXmm(static_cast<int>(fs), 0, static_cast<int>(dest));
		const int dReg = AllocXmm(32, static_cast<int>(fd), static_cast<int>(dest));
		const bool swapMul = !single && Swapped(sReg, f.ft);
		FlagTemps(op, dest, true, f.tempFt >= 0);
		DoneXmm(dReg);
		DoneXmm(f.ft);
		DoneXmm(sReg);
		Emit(StepOp::FMACd, code, FmacMode(op, k, clampOps) | (swapMul ? mode::kSwap : 0), FlagBits(op));
	}

	bool Upper(const OpInfo& op, uint32_t code, uint32_t pair);
	bool Lower(const OpInfo& op, uint32_t code, uint32_t pair);
	bool LowerT3(const OpInfo& op, uint32_t code, uint32_t pair, int table);
	bool Execute(const OpInfo& op, uint64_t words, uint32_t pair);

	void DivSet(const OpInfo& op) // mVUdivSet
	{
		if (!op.doDivFlag)
			return;
		Emit(StepOp::DivSet, op.sFlag.write, op.sFlag.lastWrite, op.sFlag.doFlag ? 0 : 1);
	}

	// I, from the lower word of an I-bit pair (doIbit).
	void Ibit(uint32_t code, uint32_t pair)
	{
		if (ibitHack)
		{
			Emit(StepOp::IbitLive, pair);
			return;
		}
		uint32_t i = code;
		if (clamp.overflow && (i & 0x7fffffffu) >= 0x7f800000u)
			i = (0x80000000u & i) | 0x7f7fffffu;
		Emit(StepOp::IbitConst, i);
	}

	void XgkickDelay() // mVU_XGKICK_DELAY
	{
		FlushCallerSavedXmms();
		FlushCallerSavedGprs();
		Emit(StepOp::Xgkick);
	}

	// The address of a quadword: an address register and the instruction's
	// immediate, which with the I-bit game fix is read from micro memory as the
	// code runs.
	void EaOf(uint32_t reg, uint32_t code, uint32_t pair)
	{
		const uint32_t h = MoveVi(reg);
		if (ibitHack)
			Emit(StepOp::Ea, h, vu1 ? 1 : 0, 1, 0, pair);
		else
			Emit(StepOp::Ea, h, vu1 ? 1 : 0, 0, step::Imm11(code), 0);
	}

	const Vu& V;
	const ShadowBlock& b;
	std::vector<Step>& out;
	const bool vu1;
	const bool evil; // a block of one pair, run after a branch whose delay slot held another (isEvilBlock)
	const bool ibitHack;
	const d::Clamp clamp;
	std::string what_;
	uint32_t unknown_ = 0;

public:
	uint32_t unknown() const { return unknown_; }
};

bool Planner::Upper(const OpInfo& op, uint32_t code, uint32_t pair)
{
	using d::cACC;
	using d::cFs;
	using d::cFt;
	using K = d::Kind;
	using S = d::Second;
	const uint32_t dest = (code >> 21) & 0xF;
	const bool ps = dest == 0xF;
	const uint32_t op6 = code & 0x3f;
	const uint32_t fs = (code >> 11) & 31, ft = (code >> 16) & 31;
	if (op6 < 0x30)
	{
		const uint32_t col = op6 & 3;
		switch (op6 >> 2)
		{
			case 0x0: FMACa(op, K::Add, S::Bc, false, 0, code); return true; // ADDbc
			case 0x1: FMACa(op, K::Sub, S::Bc, false, ps ? (cFs | cFt) : 0, code); return true; // SUBbc
			case 0x2: FMACc(op, S::Bc, cFs, code); return true; // MADDbc (micro mode: MADDw too)
			case 0x3: FMACd(op, S::Bc, 0, code); return true; // MSUBbc
			case 0x4: FMACa(op, K::Max, S::Bc, false, 0, code); return true; // MAXbc
			case 0x5: FMACa(op, K::Min, S::Bc, false, 0, code); return true; // MINIbc
			case 0x6: FMACa(op, K::Mul, S::Bc, false, ps ? (cFs | cFt) : cFs, code); return true; // MULbc
			case 0x7:
				switch (col)
				{
					case 0: FMACa(op, K::Mul, S::Q, false, ps ? (cFs | cFt) : cFs, code); return true; // MULq
					case 1: FMACa(op, K::Max, S::I, false, 0, code); return true; // MAXi
					case 2: FMACa(op, K::Mul, S::I, false, ps ? (cFs | cFt) : cFs, code); return true; // MULi
					default: FMACa(op, K::Min, S::I, false, 0, code); return true; // MINIi
				}
			case 0x8:
				switch (col)
				{
					case 0: FMACa(op, K::Add, S::Q, false, 0, code); return true; // ADDq
					case 1: FMACc(op, S::Q, 0, code); return true; // MADDq
					case 2: FMACa(op, K::AddI, S::I, false, 0, code); return true; // ADDi
					default: FMACc(op, S::I, 0, code); return true; // MADDi
				}
			case 0x9:
				switch (col)
				{
					case 0: FMACa(op, K::Sub, S::Q, false, ps ? (cFs | cFt) : 0, code); return true; // SUBq
					case 1: FMACd(op, S::Q, 0, code); return true; // MSUBq
					case 2: FMACa(op, K::Sub, S::I, false, ps ? (cFs | cFt) : 0, code); return true; // SUBi
					default: FMACd(op, S::I, 0, code); return true; // MSUBi
				}
			case 0xA:
				switch (col)
				{
					case 0: FMACa(op, K::Add, S::Vector, false, 0, code); return true; // ADD
					case 1: FMACc(op, S::Vector, 0, code); return true; // MADD
					case 2: FMACa(op, K::Mul, S::Vector, false, ps ? (cFs | cFt) : cFs, code); return true; // MUL
					default: FMACa(op, K::Max, S::Vector, false, 0, code); return true; // MAX
				}
			default:
				switch (col)
				{
					case 0: FMACa(op, K::Sub, S::Vector, false, ps ? (cFs | cFt) : 0, code); return true; // SUB
					case 1: FMACd(op, S::Vector, 0, code); return true; // MSUB (micro mode: no clamp)
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
						Emit(StepOp::Opmsub, code, ClampBits() | (swap ? mode::kSwap : 0), FlagBits(op));
						return true;
					}
					default: FMACa(op, K::Min, S::Vector, false, 0, code); return true; // MINI
				}
		}
	}
	if (op6 < 0x3C)
		return Unknown();

	const uint32_t row = (code >> 6) & 0x1f;
	const uint32_t col = op6 & 3;
	switch (row)
	{
		case 0: FMACa(op, K::Add, S::Bc, true, 0, code); return true; // ADDAbc
		case 1: FMACa(op, K::Sub, S::Bc, true, 0, code); return true; // SUBAbc
		case 2: FMACb(op, false, S::Bc, cFs, code); return true; // MADDAbc
		case 3: FMACb(op, true, S::Bc, 0, code); return true; // MSUBAbc
		case 4: // ITOF0/4/12/15
		{
			if (ft == 0)
				return true;
			DoneXmm(AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf)));
			static constexpr uint32_t kScale[4] = {0, 0x3D800000u, 0x39800000u, 0x38000000u};
			Emit(StepOp::Itof, code, kScale[col]);
			return true;
		}
		case 5: // FTOI0/4/12/15
		{
			if (ft == 0)
				return true;
			const int sReg = AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf));
			const int t1 = AllocXmm();
			DoneXmm(sReg);
			DoneXmm(t1);
			static constexpr uint32_t kScale[4] = {0, 0x41800000u, 0x45800000u, 0x47000000u};
			Emit(StepOp::Ftoi, code, kScale[col]);
			return true;
		}
		case 6: // MULAbc
			FMACa(op, K::Mul, S::Bc, true, col == 3 ? (ps ? (cFs | cFt) : cFs) : cFs, code);
			return true;
		default: break;
	}
	switch (col)
	{
		case 0:
			switch (row)
			{
				case 7: FMACa(op, K::Mul, S::Q, true, 0, code); return true; // MULAq
				case 8: FMACa(op, K::Add, S::Q, true, 0, code); return true; // ADDAq
				case 9: FMACa(op, K::Sub, S::Q, true, 0, code); return true; // SUBAq
				case 10: FMACa(op, K::Add, S::Vector, true, 0, code); return true; // ADDA
				case 11: FMACa(op, K::Sub, S::Vector, true, 0, code); return true; // SUBA
			}
			break;
		case 1:
			switch (row)
			{
				case 7: // ABS
					if (ft)
					{
						DoneXmm(AllocXmm(static_cast<int>(fs), static_cast<int>(ft), static_cast<int>(dest), !(fs == ft && dest == 0xf)));
						Emit(StepOp::Abs, code);
					}
					return true;
				case 8: FMACb(op, false, S::Q, 0, code); return true; // MADDAq
				case 9: FMACb(op, true, S::Q, 0, code); return true; // MSUBAq
				case 10: FMACb(op, false, S::Vector, 0, code); return true; // MADDA
				case 11: FMACb(op, true, S::Vector, 0, code); return true; // MSUBA
			}
			break;
		case 2:
			switch (row)
			{
				case 7: FMACa(op, K::Mul, S::I, true, 0, code); return true; // MULAi
				case 8: FMACa(op, K::Add, S::I, true, 0, code); return true; // ADDAi
				case 9: FMACa(op, K::Sub, S::I, true, 0, code); return true; // SUBAi
				case 10: FMACa(op, K::Mul, S::Vector, true, 0, code); return true; // MULA
				case 11: // OPMULA
				{
					const int tReg = AllocXmm(static_cast<int>(ft), 0, static_cast<int>(dest));
					const int sReg = AllocXmm(static_cast<int>(fs), 32, static_cast<int>(dest));
					const bool swap = Swapped(sReg, tReg);
					DoneXmm(tReg);
					FlagTemps(op, dest, false, false);
					DoneXmm(sReg);
					Emit(StepOp::Opmula, code, ClampBits() | (swap ? mode::kSwap : 0), FlagBits(op));
					return true;
				}
			}
			break;
		default:
			switch (row)
			{
				case 7: // CLIP
				{
					const int sReg = AllocXmm(static_cast<int>(fs), 0, 0xf);
					const int tReg = AllocXmm(static_cast<int>(ft), 0, 0x1);
					const int t1 = AllocXmm();
					const int t2 = AllocXmm();
					DoneXmm(sReg);
					DoneXmm(tReg);
					DoneXmm(t1);
					DoneXmm(t2);
					Emit(StepOp::Clip, code, op.cFlag.lastWrite, op.cFlag.write);
					return true;
				}
				case 8: FMACb(op, false, S::I, 0, code); return true; // MADDAi
				case 9: FMACb(op, true, S::I, 0, code); return true; // MSUBAi
				case 11: return true; // NOP
			}
			break;
	}
	return Unknown();
}

bool Planner::Lower(const OpInfo& op, uint32_t code, uint32_t pair)
{
	const uint32_t it = (code >> 16) & 0xF, is = (code >> 11) & 0xF, id = (code >> 6) & 0xF;
	const uint32_t ft = (code >> 16) & 31, fs = (code >> 11) & 31;
	const uint32_t dest = (code >> 21) & 0xF;
	const uint32_t lane = (dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3));
	// The link of a BAL or JALR, on its way (t0).
	auto link = [&]() {
		if (!op.lOp.evilBranch)
			Emit(StepOp::T0Link, pair * 8 + 16);
		else
			Emit(StepOp::T0LinkEvil, evil ? 1 : 0);
	};
	// Where a B, BAL or conditional branch goes, from the block's start (branchAddr).
	const uint32_t target = pair * 8 + 8 + step::Imm11(code) * 8;

	switch (code >> 25)
	{
		// The address registers through moveVIToGPR (none for VI00); ILW and ISW
		// with VI00 take a constant address (mVUoptimizeConstantAddr), the same.
		case 0x00: // LQ
			EaOf(is, code, pair);
			DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
			if (!op.lOp.noWriteVF)
				Emit(StepOp::LoadQ, ft, dest);
			return true;
		case 0x01: // SQ
			EaOf(it, code, pair);
			DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
			Emit(StepOp::StoreQ, fs, dest);
			return true;
		case 0x04: // ILW
			EaOf(is, code, pair);
			Emit(StepOp::T0Rd16, lane);
			WriteVi(op, it);
			return true;
		case 0x05: // ISW: all 32 bits of the host register (zero-extended if written in the block)
		{
			EaOf(is, code, pair);
			const int t = AllocGpr(static_cast<int>(it), -1, false, true);
			Emit(StepOp::Wr32, static_cast<uint32_t>(t), dest);
			Done(t);
			return true;
		}
		case 0x08: // IADDIU
		case 0x09: // ISUBIU
		{
			const bool add = (code >> 25) == 0x08;
			if (add && is == 0)
			{
				if (ibitHack)
					Emit(StepOp::T0Live, pair, 0);
				else
					Emit(StepOp::T0Const, step::Imm15(code));
				WriteVi(op, it);
				return true;
			}
			const int s = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
			if (ibitHack)
				Emit(StepOp::ViAddLive, static_cast<uint32_t>(s), ViOf(s), pair, add ? 0 : 1);
			else
				Emit(StepOp::ViAddK, static_cast<uint32_t>(s), ViOf(s), add ? step::Imm15(code) : 0u - step::Imm15(code));
			Done(s);
			return true;
		}
		case 0x10: // FCEQ
			Emit(StepOp::T0Fceq, op.cFlag.read, code & 0xffffff);
			WriteVi(op, 1);
			return true;
		case 0x11: // FCSET
			Emit(StepOp::Fcset, op.cFlag.write, code & 0xffffff);
			return true;
		case 0x12: // FCAND
			Emit(StepOp::T0Fcand, op.cFlag.read, code & 0xffffff);
			WriteVi(op, 1);
			return true;
		case 0x13: // FCOR
			Emit(StepOp::T0Fcor, op.cFlag.read, code & 0xffffff);
			WriteVi(op, 1);
			return true;
		case 0x1C: // FCGET
			Emit(StepOp::T0Fcget, op.cFlag.read);
			WriteVi(op, it);
			return true;
		case 0x18: // FMEQ: all 32 bits of is's host register against the MAC flag
		case 0x1A: // FMAND
		case 0x1B: // FMOR
		{
			const int t = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
			Emit(StepOp::Fmop, static_cast<uint32_t>(t), ViOf(t), op.mFlag.read, (code >> 25) == 0x18 ? 0 : (code >> 25) == 0x1A ? 1 : 2);
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
			Emit(StepOp::T0Fseq, op.sFlag.read, imm);
			WriteVi(op, it);
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
			Emit(StepOp::Fsset, op.sFlag.write, op.sFlag.lastWrite, (op.sFlag.doFlag || op.doDivFlag) ? 0 : 1, imm);
			return true;
		}
		case 0x16: // FSAND
		case 0x17: // FSOR
		{
			const uint32_t imm12 = (((code >> 21) & 1) << 11) | (code & 0x7ff);
			Emit((code >> 25) == 0x16 ? StepOp::T0Fsand : StepOp::T0Fsor, op.sFlag.read, imm12);
			WriteVi(op, it);
			return true;
		}
		case 0x20: // B: the block's end does it, but where a branch is in another's delay slot
		case 0x21: // BAL
		{
			if ((code >> 25) == 0x21)
			{
				link();
				WriteVi(op, it);
			}
			const uint32_t vars = (op.lOp.badBranch ? 2u : 0u) | (op.lOp.evilBranch ? (evil ? 8u : 4u) : 0u);
			if (vars)
				Emit(StepOp::BranchRel, vars, target);
			return true;
		}
		case 0x24: // JR
		case 0x25: // JALR
		{
			// normJumpPass2
			Emit(StepOp::T0Jump, MoveVi(is));
			Emit(StepOp::BranchT, (op.lOp.evilBranch ? (evil ? 8u : 4u) : 1u) | (op.lOp.badBranch ? 2u : 0u));
			if ((code >> 25) == 0x25)
			{
				link();
				WriteVi(op, it);
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
			if (op.lOp.memReadIs)
				Emit(StepOp::T0Backup);
			else
				Emit(StepOp::T0FromH, MoveVi(is));
			if ((code >> 25) <= 0x29)
			{
				if (op.lOp.memReadIt)
				{
					Emit(StepOp::T0XorBackup);
				}
				else
				{
					const int t = AllocGpr(static_cast<int>(it));
					Emit(StepOp::T0XorH, static_cast<uint32_t>(t));
					Done(t);
				}
			}
			// condEvilBranch, where a branch is in another's delay slot: where
			// each of the two goes.
			const uint32_t kind = (!op.lOp.badBranch && !op.lOp.evilBranch) ? 0 : op.lOp.badBranch ? 1 : evil ? 2 : 3;
			if (kind == 0)
				Emit(StepOp::CondSet, 0, 0, 0, 0);
			else
				Emit(StepOp::CondSet, kind, code >> 25, target, pair * 8 + 16);
			return true;
		}
		case 0x40:
			switch (code & 0x3f)
			{
				case 0x30: // IADD
					if (is == 0 || it == 0)
					{
						const int s = AllocGpr(static_cast<int>(is ? is : it), -1);
						const int dst = AllocGpr(-1, static_cast<int>(id), op.lOp.backupVI);
						Emit(StepOp::ViMove, static_cast<uint32_t>(dst), ViOf(dst), static_cast<uint32_t>(s));
						Done(dst);
						Done(s);
						return true;
					}
					[[fallthrough]];
				case 0x34: // IAND
				case 0x35: // IOR
				{
					const int t = AllocGpr(static_cast<int>(it), -1);
					const int s = AllocGpr(static_cast<int>(is), static_cast<int>(id), op.lOp.backupVI);
					const uint32_t low = code & 0x3f;
					Emit(StepOp::ViAlu, static_cast<uint32_t>(s), static_cast<uint32_t>(t), ViOf(s),
						low == 0x30 ? 0 : (it == is) ? 4 : (low == 0x34 ? 2 : 3));
					Done(s);
					Done(t);
					return true;
				}
				case 0x31: // ISUB
					if (it != is)
					{
						const int t = AllocGpr(static_cast<int>(it), -1);
						const int s = AllocGpr(static_cast<int>(is), static_cast<int>(id), op.lOp.backupVI);
						Emit(StepOp::ViAlu, static_cast<uint32_t>(s), static_cast<uint32_t>(t), ViOf(s), 1);
						Done(s);
						Done(t);
					}
					else
					{
						Emit(StepOp::T0Const, 0);
						WriteVi(op, id);
					}
					return true;
				case 0x32: // IADDI: the immediate sign-extended to 32 bits
				{
					if (is == 0)
					{
						if (ibitHack)
							Emit(StepOp::T0Live, pair, 2);
						else
							Emit(StepOp::T0Const, step::Imm5(code));
						WriteVi(op, it);
						return true;
					}
					const int s = AllocGpr(static_cast<int>(is), static_cast<int>(it), op.lOp.backupVI);
					if (ibitHack)
						Emit(StepOp::ViAddLive, static_cast<uint32_t>(s), ViOf(s), pair, 2);
					else
						Emit(StepOp::ViAddK, static_cast<uint32_t>(s), ViOf(s), step::Imm5(code));
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
					return LowerT3(op, code, pair, table);
				}
			}
			break;
	}
	return Unknown();
}

bool Planner::LowerT3(const OpInfo& op, uint32_t code, uint32_t pair, int table)
{
	const uint32_t row = (code >> 6) & 0x1f;
	const uint32_t it = (code >> 16) & 0xF, is = (code >> 11) & 0xF;
	const uint32_t ft = (code >> 16) & 31, fs = (code >> 11) & 31;
	const uint32_t dest = (code >> 21) & 0xF;
	const uint32_t fsf = (code >> 21) & 3;
	const uint32_t lane = (dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3));
	const uint32_t vu1Bit = vu1 ? 1 : 0;
	const uint32_t qMode = ClampBits() | (op.writeQ ? mode::kWriteQ : 0);
	// The EFU's instructions: P to its own instance. VU0 has no EFU.
	auto efu = [&](EfuOp which) {
		if (vu1)
			Emit(StepOp::Efu, static_cast<uint32_t>(which), code,
				ClampBits() | (op.writeP ? mode::kWriteP : 0) | ((which == EfuOp::Esum && esumSwap_) ? mode::kSwap : 0));
		return true;
	};

	switch (table)
	{
		case 0:
			switch (row)
			{
				case 12: Emit(StepOp::Move, code); return true; // MOVE
				case 13: // LQI
				{
					if (is)
					{
						const int r = AllocGpr(static_cast<int>(is), static_cast<int>(is), op.lOp.backupVI);
						Emit(StepOp::EaH, static_cast<uint32_t>(r), vu1Bit, 1);
						Emit(StepOp::ViAddK, static_cast<uint32_t>(r), ViOf(r), 1);
						Done(r);
					}
					else
					{
						Emit(StepOp::EaZero, vu1Bit);
					}
					if (!op.lOp.noWriteVF)
					{
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
						Emit(StepOp::LoadQ, ft, dest);
					}
					return true;
				}
				case 14: Emit(StepOp::Div, code, qMode); return true; // DIV
				case 15: // MTIR: the whole float in the host register
				{
					const int s = AllocXmm(static_cast<int>(fs), 0, 1 << (3 - fsf));
					Emit(StepOp::T0VfLane, fs, fsf);
					WriteVi(op, it);
					DoneXmm(s);
					return true;
				}
				case 16: // RNEXT: the next of R's sequence
				{
					const int temp = AllocGpr(); // a temporary for it
					if (!op.lOp.noWriteVF)
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
					Emit(StepOp::Rnext, code, op.lOp.noWriteVF ? 1 : 0);
					Done(temp);
					return true;
				}
				case 25: // MFP
					if (vu1)
						Emit(StepOp::Mfp, code, op.readP ? 1 : 0);
					return true;
				case 26: // XTOP
					if (vu1)
					{
						Emit(StepOp::T0Vif, 0);
						WriteVi(op, it);
					}
					return true;
				case 27: // XGKICK
					if (!vu1)
						return true;
					if (op.doXGKICK)
						XgkickDelay();
					{
						const int r = AllocGpr(static_cast<int>(is), -1);
						Emit(StepOp::SetXgkick, static_cast<uint32_t>(r));
						Done(r);
					}
					return true;
				case 28: return efu(EfuOp::Esadd);
				case 29: return efu(EfuOp::EatanXY);
				case 30: return efu(EfuOp::Esqrt);
				case 31: return efu(EfuOp::Esin);
			}
			break;
		case 1:
			switch (row)
			{
				case 12: Emit(StepOp::Mr32, code); return true; // MR32
				case 13: // SQI
				{
					if (it)
					{
						const int r = AllocGpr(static_cast<int>(it), static_cast<int>(it), op.lOp.backupVI);
						Emit(StepOp::EaH, static_cast<uint32_t>(r), vu1Bit, 0);
						Emit(StepOp::ViAddK, static_cast<uint32_t>(r), ViOf(r), 1);
						Done(r);
					}
					else
					{
						Emit(StepOp::EaZero, vu1Bit);
					}
					DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
					Emit(StepOp::StoreQ, fs, dest);
					return true;
				}
				case 14: Emit(StepOp::Sqrt, code, qMode); return true; // SQRT
				case 15: // MFIR: sign-extends the host register in place, saying nothing
				{
					const int t = AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest));
					uint32_t h = kNone;
					if (is)
					{
						const int r = AllocGpr(static_cast<int>(is), -1);
						h = static_cast<uint32_t>(r);
						Done(r);
					}
					DoneXmm(t);
					Emit(StepOp::Mfir, h, code);
					return true;
				}
				case 16: // RGET
					if (!op.lOp.noWriteVF)
						Emit(StepOp::Rget, code);
					return true;
				case 26: // XITOP
					Emit(StepOp::T0Vif, vu1 ? 2 : 1);
					WriteVi(op, it);
					return true;
				case 28: return efu(EfuOp::Ersadd);
				case 29: return efu(EfuOp::EatanXZ);
				case 30: return efu(EfuOp::Ersqrt);
				case 31: return efu(EfuOp::Eatan);
			}
			break;
		case 2:
			switch (row)
			{
				case 13: // LQD: the address register decremented first
				{
					if (is || !vu1)
					{
						const int r = AllocGpr(static_cast<int>(is), static_cast<int>(is), op.lOp.backupVI); // VU0's VI00: a temporary
						Emit(StepOp::ViAddK, static_cast<uint32_t>(r), ViOf(r), 0xffffffffu);
						Emit(StepOp::EaH, static_cast<uint32_t>(r), vu1Bit, 1);
						Done(r);
					}
					else
					{
						Emit(StepOp::EaFixed); // as microVU addresses it
					}
					if (!op.lOp.noWriteVF)
					{
						DoneXmm(AllocXmm(-1, static_cast<int>(ft), static_cast<int>(dest)));
						Emit(StepOp::LoadQ, ft, dest);
					}
					return true;
				}
				case 14: Emit(StepOp::Rsqrt, code, qMode); return true; // RSQRT
				case 15: // ILWR
					Emit(StepOp::Ea, MoveVi(is), vu1Bit, 0, 0, 0);
					Emit(StepOp::T0Rd16, lane);
					WriteVi(op, it);
					return true;
				case 16: Emit(StepOp::Rinit, code); return true; // RINIT
				case 28: return efu(EfuOp::Eleng);
				case 29: return efu(EfuOp::Esum);
				case 30: return efu(EfuOp::Ercpr);
				case 31: return efu(EfuOp::Eexp);
			}
			break;
		default:
			switch (row)
			{
				case 13: // SQD: the address register decremented first
				{
					if (it || !vu1)
					{
						const int r = AllocGpr(static_cast<int>(it), static_cast<int>(it), op.lOp.backupVI); // VU0's VI00: a temporary
						Emit(StepOp::ViAddK, static_cast<uint32_t>(r), ViOf(r), 0xffffffffu);
						Emit(StepOp::EaH, static_cast<uint32_t>(r), vu1Bit, 0);
						Done(r);
					}
					else
					{
						Emit(StepOp::EaFixed); // as microVU addresses it
					}
					DoneXmm(AllocXmm(static_cast<int>(fs), dest == 0xf ? -1 : 0, static_cast<int>(dest)));
					Emit(StepOp::StoreQ, fs, dest);
					return true;
				}
				case 14: return true; // WAITQ: its stall is the analysis'
				case 15: // ISWR
				{
					Emit(StepOp::Ea, MoveVi(is), vu1Bit, 0, 0, 0);
					const int r = AllocGpr(static_cast<int>(it), -1, false, true); // all 32 bits, as ISW
					Emit(StepOp::Wr32, static_cast<uint32_t>(r), dest);
					Done(r);
					return true;
				}
				case 16: Emit(StepOp::Rxor, code); return true; // RXOR
				case 28: return efu(EfuOp::Erleng);
				case 30: return true; // WAITP
			}
			break;
	}
	return Unknown();
}

bool Planner::Execute(const OpInfo& op, uint64_t words, uint32_t pair) // mVUexecuteInstruction
{
	const uint32_t lower = static_cast<uint32_t>(words);
	const uint32_t upper = static_cast<uint32_t>(words >> 32);
	if (op.lOp.isNOP)
	{
		if (!Upper(op, upper, pair))
			return false;
		DivSet(op);
		if (op.uOp.iBit)
			Ibit(lower, pair);
		return true;
	}
	if (!op.swapOps)
	{
		if (!Upper(op, upper, pair))
			return false;
		DivSet(op);
		return Lower(op, lower, pair);
	}
	if (op.backupVF && !op.lOp.noWriteVF)
	{
		// doSwapOp: the lower instruction first, the upper reading the register
		// as it was, then the lower's result written over it whole.
		const uint32_t reg = op.lOp.VF_write.reg;
		const int t1 = AllocXmm(static_cast<int>(reg));
		const int t2 = AllocXmm();
		DoneXmm(t1);
		if (reg)
			Emit(StepOp::VfSave, reg);
		if (!Lower(op, lower, pair))
			return false;
		DoneXmm(AllocXmm(static_cast<int>(reg), static_cast<int>(reg), 0xf, false));
		if (reg)
			Emit(StepOp::VfSwap, reg);
		if (!Upper(op, upper, pair))
			return false;
		DivSet(op);
		DoneXmm(AllocXmm(-1, static_cast<int>(reg), 0xf));
		DoneXmm(t2);
		if (reg)
			Emit(StepOp::VfRestore, reg);
		return true;
	}
	if (!Lower(op, lower, pair))
		return false;
	if (!Upper(op, upper, pair))
		return false;
	DivSet(op);
	return true;
}

struct ShadowBlock;
BlockFn BlockFnOf(const ShadowBlock& b);

// What a block's code is given of it (Link), worked out once from its
// analysis: how it is carried out and the blocks after it; and which
// instances the flag registers take when the program stops or ends there.
void FinishBlock(ShadowBlock& b)
{
	const Block& a = b.a;
	b.cycles = a.cycles;
	b.base = a.startPC;
	b.pairs = a.count;
	b.stop = b.exit == Exit::Unsupported || !b.unsupported.empty() || !b.plan;
	b.fn = BlockFnOf(b);
	b.condOp = a.count >= 2 ? static_cast<uint8_t>(static_cast<uint32_t>(a.words[a.count - 2]) >> 25) : 0;
	const bool leaves = b.plan && !b.plan->steps.empty() && b.plan->steps.back().op == StepOp::Leave;
	b.leave = leaves ? static_cast<uint8_t>(b.plan->steps.back().o[0]) : 0;
	b.setup = leaves ? b.plan->steps.back().o[2] : 0;
	b.next[0] = b.notTaken;
	b.next[1] = b.taken;

	const mvu::FlagCycles& mFC = a.flagCycles;
	const int* const flags[3] = {mFC.xStatus, mFC.xMac, mFC.xClip};
	for (int k = 0; k < 3; k++)
	{
		b.lastInst[0][k] = static_cast<uint8_t>(LastFlagInst(b.pState, flags[k], k, false));
		b.lastInst[1][k] = static_cast<uint8_t>(LastFlagInst(b.pState, flags[k], k, true));
	}
}

// A block's plan, made when the block is compiled: its own record if no block
// before it had the same, and the compiled function a build has for it.
void PlanBlock(Vu& V, ShadowBlock& b)
{
	b.plan = nullptr;
	if (b.exit == Exit::Unsupported || !b.unsupported.empty())
	{
		FinishBlock(b);
		return; // a block the run stops at
	}
	std::vector<Step> steps;
	steps.reserve(b.a.count * 4);
	std::string what;
	Planner planner(V, b, steps);
	if (!planner.Run(what))
	{
		b.unsupported = what;
		FinishBlock(b);
		return;
	}
	V.stats.unknown += planner.unknown();
	const PlanKey key = planKey(V.index, steps.data(), steps.size());
	auto it = V.plans.find(key);
	if (it == V.plans.end())
	{
		auto record = std::make_unique<PlanRecord>();
		record->key = key;
		record->steps = std::move(steps);
		record->at = b.a.startPC;
		record->words = b.a.words;
		if (s_useCompiled)
		{
			const auto compiled = s_compiled[V.index].find(key);
			if (compiled != s_compiled[V.index].end())
				record->fn = compiled->second;
		}
		it = V.plans.emplace(key, std::move(record)).first;
	}
	b.plan = it->second.get();
	FinishBlock(b);
}

// Every block of the record takes its plan's compiled function again: after
// the compiled blocks the host has, or whether they are used, changed.
void TakeCompiled(Vu& V)
{
	for (auto& [key, record] : V.plans)
	{
		const auto found = s_compiled[V.index].find(key);
		record->fn = (s_useCompiled && found != s_compiled[V.index].end()) ? found->second : nullptr;
	}
	for (const std::unique_ptr<Program>& prog : V.owned)
	{
		for (const std::unique_ptr<BlockManager>& bm : prog->block)
		{
			if (bm)
				bm->forEach([](ShadowBlock& b) { b.fn = BlockFnOf(b); });
		}
	}
}

ShadowBlock* Compile(Vu& V, uint32_t startPC, const PipeState& pState, bool fromLp)
{
	ShadowBlock* b = CompileBlock(V, startPC, pState, fromLp);
	PlanBlock(V, *b);
	return b;
}

// ---- carrying a plan out ---------------------------------------------------------------------

// A plan's steps, one after the other (ps2n/vu_ops.h), up to its last, Leave.
// 0, or 1 + which of the block's T bits ended the program.
uint32_t Exec(Ctx& c, const Step* s, const Step* end)
{
	step::Mem a{c};
	for (; s != end; ++s)
	{
		const uint32_t* o = s->o;
		switch (s->op)
		{
#define PS2N_VU_ARGS0
#define PS2N_VU_ARGS1 , o[0]
#define PS2N_VU_ARGS2 , o[0], o[1]
#define PS2N_VU_ARGS3 , o[0], o[1], o[2]
#define PS2N_VU_ARGS4 , o[0], o[1], o[2], o[3]
#define PS2N_VU_ARGS5 , o[0], o[1], o[2], o[3], o[4]
#define PS2N_VU_STEP(name, operands) \
	case StepOp::name: \
		step::name(a PS2N_VU_ARGS##operands); \
		break;
#define PS2N_VU_STEP_CONTROL(name, operands)
#include "ps2n/vu_steps.inc"
#undef PS2N_VU_STEP_CONTROL
#undef PS2N_VU_STEP
#undef PS2N_VU_ARGS0
#undef PS2N_VU_ARGS1
#undef PS2N_VU_ARGS2
#undef PS2N_VU_ARGS3
#undef PS2N_VU_ARGS4
#undef PS2N_VU_ARGS5
			case StepOp::TBit:
				if (step::TBit(a, o[0], o[1]))
					return o[0] + 1;
				break;
			case StepOp::Leave: // the block's end: the caller's
			case StepOp::Count:
				return 0;
		}
	}
	return 0;
}

// A block whose plan no build has compiled: its steps interpreted, and then
// on to the next block as a compiled one goes.
const Link* PS2N_VU_CC InterpretBlock(Ctx& c, const Link* b)
{
	const ShadowBlock& block = *static_cast<const ShadowBlock*>(b);
	PlanRecord& plan = *block.plan;
	plan.runs++;
	c.interpreted++;
	c.base = b->base;
	std::memset(c.h, 0, sizeof(c.h)); // the host registers hold nothing (reset)
	const uint32_t ended = Exec(c, plan.steps.data(), plan.steps.data() + plan.steps.size());
	if (ended)
		return Stop(c, b, kStopTBit + ended - 1);
	const uint32_t how = block.leave, op = block.condOp, setup = block.setup;
	step::Mem a{c};
	PS2N_VU_LEAVE(how, op, setup);
}

// A block that holds something not carried out: the program stops at it.
const Link* PS2N_VU_CC StopBlock(Ctx& c, const Link* b)
{
	return Stop(c, b, kStopUnsupported);
}

// How a block is carried out: by the compiled function a build has for its
// plan, or by the interpreter.
BlockFn BlockFnOf(const ShadowBlock& b)
{
	if (b.stop)
		return &StopBlock;
	return b.plan->fn ? b.plan->fn : &InterpretBlock;
}

// ---- running programs: where they start and stop (microVU_Branch.inl) ------------------------

class Runner
{
public:
	explicit Runner(Vu& V)
		: V(V)
		, c(V.c)
		, R(*V.R)
		, vu1(V.index != 0)
	{
	}

	void Run(uint32_t startPC, uint32_t cycles);

private:
	// mVUendProgram; isEbit: 0 the program pauses, 1 or 2 it ends. `e`: what
	// the end was compiled with, when not the block's own end (mVUDTendProgram).
	void EndProgram(const ShadowBlock& b, int isEbit, uint32_t tpc, const EndInfo* e = nullptr)
	{
		if (!e)
			e = &b.end;
		const bool ebit = isEbit != 0;
		const int fStatus = b.lastInst[ebit][0];
		const int fMac = b.lastInst[ebit][1];
		const int fClip = b.lastInst[ebit][2];
		int qInst = 0, pInst = 0;
		if (ebit)
		{
			qInst = e->qInst;
			pInst = e->pInst;
			if (e->doDivFlag)
				c.statFlag[fStatus] = (c.statFlag[fStatus] & 0xfff3ffffu) | c.divFlag;
			if (e->doXGKICK)
				Xgkick(c.VIxgkick); // mVU_XGKICK_DELAY
			if (!V.cleared)
				V.lpState = PipeState{}; // mVU0clearlpStateJIT, mVU1clearlpStateJIT
		}
		// P and Q
		R.VI[REG_Q].UL = c.pq[qInst ? 1 : 0];
		R.pending_q = c.pq[qInst ? 0 : 1];
		if (vu1)
		{
			R.VI[REG_P].UL = c.pq[pInst ? 3 : 2];
			R.pending_p = c.pq[pInst ? 2 : 3];
		}
		// The flags
		R.VI[REG_STATUS_FLAG].UL = d::Normalize(c.statFlag[fStatus]);
		R.VI[REG_MAC_FLAG].UL = c.macFlag[fMac] & 0xffff;
		R.VI[REG_CLIP_FLAG].UL = c.clipFlag[fClip];
		if (!ebit)
		{
			std::memcpy(R.micro_macflags, c.macFlag, sizeof(c.macFlag));
			std::memcpy(R.micro_clipflags, c.clipFlag, sizeof(c.clipFlag));
			std::memcpy(R.micro_statusflags, c.statFlag, sizeof(c.statFlag));
		}
		else
		{
			const uint32_t clip = R.VI[REG_CLIP_FLAG].UL, mac = R.VI[REG_MAC_FLAG].UL, status = c.statFlag[fStatus];
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

	// The program stopped at the end of block `b`: what the block's code does
	// there in microVU.
	void EndAt(const ShadowBlock& b)
	{
		switch (b.exit)
		{
			case Exit::EBit:
			case Exit::BranchEBit:
				EndProgram(b, 1, b.exitPC);
				break;
			case Exit::MBit: // the block has linked (mVUsetupBranch) already
				V.lpState = b.pStateEnd;
				if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
					R.nextBlockCycles = 0;
				EndProgram(b, 0, b.exitPC);
				break;
			case Exit::CondBranchEBit:
			{
				// condBranch links first (mVUsetupBranch), which the block has
				// done, then ends the program.
				const bool taken = step::Taken(b.condOp, c.branch);
				EndProgram(b, 2, taken ? b.target : b.notTakenPC);
				break;
			}
			case Exit::JumpEBit:
				EndProgram(b, 2, c.branch);
				break;
			case Exit::EvilJumpEBit:
			{
				const uint32_t target = c.evilBranch;
				c.evilBranch = c.evilevilBranch;
				EndProgram(b, 2, target);
				// normJumpCompile stores arg1regd in TPC after mVUendProgram,
				// which has used that register (ecx, gprT2, on Windows) for
				// the clip flag last. (On Linux it is edi, which the call to
				// mVU1clearlpStateJIT leaves as it may: nothing to follow.)
				R.VI[REG_TPC].UL = R.VI[REG_CLIP_FLAG].UL;
				break;
			}
			default:
				break; // a block that links on does not stop the program at its end
		}
	}

	Vu& V;
	Ctx& c;
	VURegs& R;
	const bool vu1;
};

void Runner::Run(uint32_t startPC, uint32_t cycles)
{
	V.stats.runs++;
	// mVUexecute
	c.cycles = static_cast<int32_t>(cycles);
	V.totalCycles = cycles;
	const uint32_t vuLimit = vu1 ? 0x3ff8 : 0xff8;
	const Link* b = SearchProg(V, startPC & vuLimit, V.lpState, true);

	// mVUdispatcherAB: the control register, P and Q, the flags.
	const uint32_t vuFPCR = vu1 ? EmuConfig.Cpu.VU1FPCR.bitmask : EmuConfig.Cpu.VU0FPCR.bitmask;
	const bool fpcr = EmuConfig.Cpu.FPUFPCR.bitmask != vuFPCR;
	if (fpcr)
		_mm_setcsr(vuFPCR);
	c.pq[0] = R.VI[REG_Q].UL;
	c.pq[1] = R.pending_q;
	c.pq[2] = R.VI[REG_P].UL;
	c.pq[3] = vu1 ? R.pending_p : R.VI[REG_P].UL;
	std::memcpy(c.macFlag, R.micro_macflags, sizeof(c.macFlag));
	std::memcpy(c.clipFlag, R.micro_clipflags, sizeof(c.clipFlag));
	std::memcpy(c.statFlag, R.micro_statusflags, sizeof(c.statFlag));

	// The blocks, each going on to the next by itself, until the program
	// stops: before a block whose cycles it has not (mVUtestCycles), at a
	// block's end, at a T bit.
	c.syncHack = EmuConfig.Gamefixes.VUSyncHack ? 1 : 0;
	c.stop = 0;
	c.at = nullptr;
	if (Enter(c, b))
	{
		do
			b = b->fn(c, b);
		while (b);
	}

	const ShadowBlock& at = *static_cast<const ShadowBlock*>(c.at);
	if (c.stop == kStopCycles)
	{
		V.lpState = at.pState; // copyPLState
		if (c.syncHack || EmuConfig.Gamefixes.FullVU0SyncHack)
			R.nextBlockCycles = at.cycles;
		EndProgram(at, 0, at.base);
		V.stats.pauses++;
	}
	else if (c.stop == kStopEnd)
	{
		EndAt(at);
	}
	else if (c.stop >= kStopTBit)
	{
		// mVUDoTBit: the T bit enabled, the program ends there.
		const TBitEnd& t = at.tBits[c.stop - kStopTBit];
		c.pairs -= at.pairs - (t.pair + 1); // the pairs after it did not run
		VU0.VI[REG_VPU_STAT].UL |= vu1 ? 0x400u : 0x4u;
		R.flags |= VUFLAG_INTCINTERRUPT;
		EndProgram(at, 1, t.tpc, &t.end);
	}
	else if (V.unsupported.empty())
	{
		V.unsupported = at.unsupported.empty() ? std::string("a block without a plan") : at.unsupported;
	}

	if (fpcr)
		_mm_setcsr(EmuConfig.Cpu.FPUFPCR.bitmask);
	V.stats.blocks = c.blocks;
	V.stats.pairs = c.pairs;
	V.stats.interpreted = c.interpreted;

	// mVUcleanUp
	const int32_t used = static_cast<int32_t>(V.totalCycles) - std::max(0, c.cycles);
	R.cycle += static_cast<uint32_t>(used);
	const uint32_t passed = static_cast<uint32_t>(std::min(used, 3000)) * static_cast<uint32_t>(EmuConfig.Speedhacks.EECycleSkip);
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

void ReadSettings(Vu& V)
{
	mvu::Settings& s = V.settings;
	s.flagHack = EmuConfig.Speedhacks.vuFlagHack;
	s.ibitHack = EmuConfig.Gamefixes.IbitHack;
	s.xgkickHack = EmuConfig.Gamefixes.XgKickHack;
	s.vuSyncHack = EmuConfig.Gamefixes.VUSyncHack;
	s.fullVu0SyncHack = EmuConfig.Gamefixes.FullVU0SyncHack;
	s.eeCycleRate = EmuConfig.Speedhacks.EECycleRate;
	const auto& rec = EmuConfig.Cpu.Recompiler;
	V.clamp = V.index ? d::Clamp{rec.vu1Overflow, rec.vu1ExtraOverflow, rec.vu1SignOverflow} :
						d::Clamp{rec.vu0Overflow, rec.vu0ExtraOverflow, rec.vu0SignOverflow};
	V.overflowHack = EmuConfig.Gamefixes.VUOverflowHack;
	V.addSubHack = EmuConfig.Gamefixes.VuAddSubHack;
	V.c.avx = UseAvx() ? 1 : 0;
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
		ReadSettings(V);
		R.VI[REG_TPC].UL <<= 3;
		Runner(V).Run(R.VI[REG_TPC].UL, cycles);
		R.VI[REG_TPC].UL >>= 3;
		if (R.flags & VUFLAG_INTCINTERRUPT)
		{
			R.flags &= ~VUFLAG_INTCINTERRUPT;
			hwIntcIrq(m_Idx ? 7 : 6);
		}
	}
	// A write into micro memory, announced before it is made.
	void Clear(u32 addr, u32 size) override
	{
		Vu& V = s_vu[m_Idx];
		if (size && !V.chunkWrite.empty())
		{
			// Which chunks it reaches, for the searches that need not be made
			// (SearchProg); past the end of micro memory, from its start again.
			V.writeGen++;
			const u32 chunks = static_cast<u32>(V.chunkWrite.size());
			const u32 first = (addr & (V.memSize - 1)) / kWriteChunk;
			const u32 count = std::min(chunks, ((addr & (kWriteChunk - 1)) + size + kWriteChunk - 1) / kWriteChunk);
			for (u32 i = 0; i < count; i++)
				V.chunkWrite[(first + i) % chunks] = V.writeGen;
		}
		ClearVu(V);
	}
	void ResumeXGkick() override {}
};

Provider s_provider0(0);
Provider s_provider1(1);

} // namespace

// ---- what a block's code calls (ps2n/vu_recomp.h) -----------------------------------------

// mVUcompileJIT: an indirect jump's target, as the start of a program of its
// own, through the jump cache of the block it leaves.
const Link* JumpTo(Ctx& c, const Link* fromLink, uint32_t target)
{
	Vu& V = s_vu[c.index];
	ShadowBlock* from = const_cast<ShadowBlock*>(static_cast<const ShadowBlock*>(fromLink));
	V.stats.jumps++;
	V.R->start_pc = target;
	JumpCacheEntry& jc = from->jumpCache[target / 8];
	if (jc.prog && jc.prog == QuickAt(V, target / 8).prog) [[likely]]
		return jc.block;
	ShadowBlock* to = SearchProg(V, target, from->pStateEnd, false, jc.prog, jc.block);
	jc.prog = QuickAt(V, target / 8).prog;
	jc.block = to;
	return to;
}

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

void SetImages(const ImageInfo* const* images, size_t count)
{
	for (auto& map : s_compiled)
		map.clear();
	for (size_t i = 0; i < count; i++)
	{
		const ImageInfo& image = *images[i];
		for (uint32_t k = 0; k < image.block_count; k++)
		{
			const BlockInfo& block = image.blocks[k];
			s_compiled[block.vu ? 1 : 0][PlanKey{block.key_a, block.key_b}] = block.fn;
		}
	}
	// The plans and blocks met so far take the compiled functions there are now.
	for (int vu = 0; vu < 2; vu++)
	{
		s_vu[vu].index = vu;
		TakeCompiled(s_vu[vu]);
	}
}

void SetCompiledBlocksEnabled(bool enabled)
{
	s_useCompiled = enabled;
	for (int vu = 0; vu < 2; vu++)
	{
		s_vu[vu].index = vu;
		TakeCompiled(s_vu[vu]);
	}
}

size_t CompiledBlockCount(int vu)
{
	return s_compiled[vu ? 1 : 0].size();
}

PlanStats Plans(int vu)
{
	PlanStats st;
	for (const auto& [key, record] : s_vu[vu ? 1 : 0].plans)
	{
		st.plans++;
		st.steps += record->steps.size();
		if (record->fn)
			st.compiled++;
		else if (record->runs)
			st.interpreted++;
	}
	const InterpreterStats& run = s_vu[vu ? 1 : 0].stats;
	st.interpretedRuns = run.interpreted;
	st.compiledRuns = run.blocks - run.interpreted;
	return st;
}

bool WriteCapture(const char* path)
{
	std::FILE* f = std::fopen(path, "wb");
	if (!f)
		return false;
	std::fputs("# The blocks of micro code a run carried out by PS2Native's VU interpreter, as their plans\n"
			   "# (core/include/ps2n/vu_plan.h). Give this file to the recompiler (ps2n-recomp vu) to have\n"
			   "# them compiled.\n",
		f);
	for (int vu = 0; vu < 2; vu++)
	{
		// In the order of their keys, so that the same run writes the same file.
		std::vector<const PlanRecord*> records;
		for (const auto& [key, record] : s_vu[vu].plans)
		{
			if (!record->fn && record->runs)
				records.push_back(record.get());
		}
		std::sort(records.begin(), records.end(), [](const PlanRecord* x, const PlanRecord* y) { return x->key < y->key; });
		for (const PlanRecord* record : records)
		{
			const std::string text =
				planText(vu, record->steps.data(), record->steps.size(), record->at, record->words.data(), record->words.size());
			std::fwrite(text.data(), 1, text.size(), f);
		}
	}
	return std::fclose(f) == 0;
}

} // namespace ps2n::vu
