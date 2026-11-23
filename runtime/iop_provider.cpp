// SPDX-License-Identifier: GPL-3.0-or-later
//
// See iop_provider.h. The names in the comments are PCSX2's (v2.9.189,
// pcsx2/x86/iR3000A.cpp, iR3000Atables.cpp, BaseblockEx.{h,cpp}; Copyright
// 2002-2026 PCSX2 Dev Team, GPL-3.0+), so that each piece can be read beside
// the code it follows.

#include "iop_provider.h"

#include "ps2n/iop_ops.h"
#include "ps2n/iop_recomp.h"
#include "ps2n/r3000a.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/Config.h"
#include "pcsx2/DebugTools/SymbolGuardian.h"
#include "pcsx2/IopBios.h"
#include "pcsx2/IopHw.h"
#include "pcsx2/IopMem.h"
#include "pcsx2/R3000A.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2n::iop
{
	uint32_t g_generation = 1;
	uint32_t g_clearLimit = 0;
	uint32_t g_ramMask = 0;
	Block** g_lut = nullptr;
	int32_t g_page[0x10000];
	Counters g_counters;

	namespace
	{
		namespace r3 = ps2n::r3000a;
		using r3::Op;

		// A block the provider has met, by its instructions: what is counted
		// for the report, and what a capture holds.
		struct ShapeRecord
		{
			r3::Key key;
			uint32_t pc = 0; // where it was first formed
			std::vector<uint32_t> words;
			BlockFn fn = nullptr; // the function a build compiled for it, if any
			uint64_t runs = 0; // of the blocks that had it and are gone
			uint64_t formed = 0;
		};

		struct KeyHash
		{
			size_t operator()(const r3::Key& k) const { return static_cast<size_t>(k.a ^ (k.b * 0x9E3779B97F4A7C15ull)); }
		};

		bool s_reserved = false;
		uint32_t s_ram_entries = 0;
		uint32_t s_entries = 0; // RAM's, the ROM's, the second ROM's, the third's: one table, as recLutReserve
		uint32_t s_ram_bytes = 0;

		// recBlocks: the blocks in the order of the addresses they are filed
		// under (BaseBlockArray).
		std::vector<Block*> s_blocks;
		// Blocks thrown away while one of them may still be running: freed
		// when the IOP is next given time.
		std::vector<Block*> s_retired;

		std::vector<std::unique_ptr<ShapeRecord>> s_shapes;
		std::unordered_map<r3::Key, uint32_t, KeyHash> s_shape_index;
		std::unordered_map<r3::Key, BlockFn, KeyHash> s_compiled;
		bool s_compiled_enabled = true;

		Stats s_stats;
		std::string s_unsupported;

		bool PS2N_IOP_CC Interpret(Block* b);

		void Refuse(const char* what, uint32_t pc)
		{
			if (!s_unsupported.empty())
				return;
			char text[256];
			std::snprintf(text, sizeof(text), "%s (IOP PC %08X, cycle %llu)", what, pc, static_cast<unsigned long long>(psxRegs.cycle));
			s_unsupported = text;
		}

		// psxhwLUT: the address a block is filed under. The recompiler's table
		// has RAM and the ROMs at three places each (0, 80000000, A0000000).
		uint32_t Hw(uint32_t pc)
		{
			const uint32_t page = pc >> 16;
			if (g_page[page] < 0)
				return pc & 0xFFFF;
			if (page >= 0xA000)
				return pc - 0xA0000000u;
			if (page >= 0x8000)
				return pc - 0x80000000u;
			return pc;
		}

		void FreeBlock(Block* b)
		{
			delete[] b->words;
			delete[] b->imports;
			delete b;
		}

		// A block leaves the record. Its counts stay with its shape.
		void Retire(Block* b)
		{
			s_shapes[b->shape]->runs += b->runs;
			b->runs = 0;
			b->flags |= kRetired;
			s_retired.push_back(b);
		}

		// ---- BaseBlocks ---------------------------------------------------------------------

		int LastIndex(uint32_t startpc)
		{
			if (s_blocks.empty())
				return -1;
			int imin = 0, imax = static_cast<int>(s_blocks.size()) - 1;
			while (imin != imax)
			{
				const int imid = (imin + imax + 1) >> 1;
				if (s_blocks[static_cast<size_t>(imid)]->hw > startpc)
					imax = imid - 1;
				else
					imin = imid;
			}
			return imin;
		}

		int Index(uint32_t startpc)
		{
			const int idx = LastIndex(startpc);
			if (idx == -1)
				return -1;
			const Block* b = s_blocks[static_cast<size_t>(idx)];
			if (startpc < b->hw || (b->count && startpc >= b->hw + b->count * 4))
				return -1;
			return idx;
		}

		Block* At(int idx)
		{
			if (idx < 0 || idx >= static_cast<int>(s_blocks.size()))
				return nullptr;
			return s_blocks[static_cast<size_t>(idx)];
		}

		void Insert(Block* b)
		{
			size_t imin = 0, imax = s_blocks.size();
			while (imin < imax)
			{
				const size_t imid = (imin + imax) >> 1;
				if (s_blocks[imid]->hw > b->hw)
					imax = imid;
				else
					imin = imid + 1;
			}
			s_blocks.insert(s_blocks.begin() + static_cast<std::ptrdiff_t>(imin), b);
		}

		// The table's entry for an address (PSX_GETBLOCK), where it has one.
		Block** Entry(uint32_t pc)
		{
			const int32_t base = g_page[pc >> 16];
			return base < 0 ? nullptr : &g_lut[static_cast<uint32_t>(base) + ((pc & 0xFFFF) >> 2)];
		}

		// psxRecClearMem: the block that begins at `pc`, and every block that
		// overlaps what is thrown away with it, go. Returns how far the next
		// address to look at is.
		uint32_t ClearMem(uint32_t pc)
		{
			Block** const entry = Entry(pc);
			if (!entry || !*entry)
				return 4;

			pc = Hw(pc);
			uint32_t lower = pc, upper = pc + 4;
			int idx = Index(pc);

			while (Block* b = At(idx - 1))
			{
				if (b->hw + b->count * 4 <= lower)
					break;
				lower = std::min(lower, b->hw);
				idx--;
			}

			const int first = idx;
			while (Block* b = At(idx))
			{
				if (b->hw >= upper)
					break;
				lower = std::min(lower, b->hw);
				upper = std::max(upper, b->hw + b->count * 4);
				idx++;
			}

			if (first != idx && first >= 0)
			{
				for (int i = first; i < idx; i++)
				{
					Retire(s_blocks[static_cast<size_t>(i)]);
					s_stats.thrown++;
				}
				s_blocks.erase(s_blocks.begin() + first, s_blocks.begin() + idx);
				g_generation++;
			}

			// iopClearRecLUT(PSX_GETBLOCK(lowerextent), upperextent - lowerextent)
			if (Block** const from = Entry(lower))
			{
				const uint32_t count = (upper - lower) / 4;
				const size_t at = static_cast<size_t>(from - g_lut);
				for (uint32_t i = 0; i < count && at + i < s_entries; i++)
				{
					if (from[i])
					{
						// Every block that began in here was thrown away above;
						// one that was not is a block the record no longer has
						// under this address.
						if (!(from[i]->flags & kRetired))
							Refuse("a block of the IOP's was lost track of (code run through a mirror of its memory)", lower + i * 4);
						from[i] = nullptr;
					}
				}
			}
			return upper - pc;
		}

		// ---- iopRecRecompile ----------------------------------------------------------------

		Block* Form(uint32_t startpc)
		{
			if (!s_unsupported.empty())
				return nullptr;
			if (startpc & 3)
			{
				Refuse("the IOP went on at an address that is not a multiple of four", startpc);
				return nullptr;
			}
			Block** const entry = Entry(startpc);
			if (!entry)
			{
				Refuse("the IOP went on at an address where it has no memory", startpc);
				return nullptr;
			}
			if (psxHu32(HW_ICFG) & 8)
			{
				Refuse("the IOP's PlayStation mode", startpc);
				return nullptr;
			}

			// What the recompiler does when it compiles a block at one of three
			// addresses: its debugger's list of modules is emptied where the
			// kernel is started over; a module the user asked for is put in the
			// kernel's list; and the boot code is told how much RAM there is.
			if (startpc == 0x890)
				R3000SymbolGuardian.ClearIrxModules();
			if (startpc == 0x1630 && EmuConfig.CurrentIRX.length() > 3)
			{
				if (iopMemRead32(0x20018) == 0x1F)
					iopMemWrite32(0x20094, 0xBFFC0000);
			}
			if (startpc == 0xBFC4A000)
				psxRegs.GPR.n.a0 = Ps2MemSize::ExposedIopRam >> 20;

			// The look-ahead: to the first branch or jump and its delay slot; or
			// to an address at which there is a block already; or, when a
			// conditional branch goes back into the block, to where it goes.
			uint32_t end = 0;
			for (uint32_t i = startpc;; i += 4)
			{
				if (i != startpc)
				{
					Block** const there = Entry(i);
					if (!there || *there)
					{
						end = i;
						break;
					}
				}
				if ((i - startpc) / 4 >= r3::kMaxBlockWords)
				{
					Refuse("code that runs on for 16,384 instructions without a branch", startpc);
					return nullptr;
				}
				const uint32_t code = iopMemRead32(i);
				const Op op = r3::decode(code);
				if (!r3::isBranch(op))
					continue;
				end = i + 8;
				if (r3::isConditional(op))
				{
					const uint32_t to = r3::branchTarget(i, code);
					if (to > startpc && to < i)
						end = to;
				}
				break;
			}

			// The words, and after them each one's instruction, decoded once here
			// for the interpreter (OpsOf), in the same allocation: Block is
			// shared with compiled code and keeps its layout.
			const uint32_t count = (end - startpc) / 4;
			uint32_t* const words = new uint32_t[count + (count + 3) / 4];
			Op* const ops = reinterpret_cast<Op*>(words + count);
			uint32_t imports = 0;
			for (uint32_t i = 0; i < count; i++)
			{
				words[i] = iopMemRead32(startpc + i * 4);
				const Op op = r3::decode(words[i]);
				ops[i] = op;
				imports += op == Op::IMPORT ? 1 : 0;
				s_stats.unknown += op == Op::Unknown ? 1 : 0;
			}

			const r3::Shape shape = r3::shape(words, count);
			const char* refused = !shape.valid ? "a block that PCSX2's IOP recompiler does not form" : shape.refused;

			// psxRecompileIrxImport: for each import a stub names, the core's
			// function for it, where it has one. That is looked up now, from the
			// name of the library in the table the stub belongs to, as memory
			// has it now.
			irxHLE* hles = nullptr;
			if (imports)
			{
				hles = new irxHLE[imports];
				uint32_t slot = 0;
				for (uint32_t i = 0; i < count; i++)
				{
					if (r3::decode(words[i]) != Op::IMPORT)
						continue;
					irxHLE hle = nullptr;
					if (const u32 table = R3000A::irxImportTableAddr(startpc + i * 4))
						hle = R3000A::irxImportHLE(iopMemReadString(table + 12, 8), static_cast<u16>(words[i] & 0xFFFF));
					// The recompiler writes the registers back before the call,
					// but not one whose value the rest of the block never
					// reads. In a stub nothing comes before the call.
					if (hle && !(count == 2 && i == 1) && !refused)
						refused = "a module's import with a function of the core's, in a block longer than the import's stub";
					hles[slot++] = hle;
				}
			}

			if (refused)
			{
				Refuse(refused, startpc);
				delete[] words;
				delete[] hles;
				return nullptr;
			}

			if (!(end & 0x10000000))
				g_clearLimit = std::max(end & ~0xA0000000u, g_clearLimit);

			Block* b = new Block;
			b->pc = startpc;
			b->hw = Hw(startpc);
			b->count = count;
			b->cycles = shape.cycles;
			b->words = words;
			b->imports = hles;
			if (shape.branch)
			{
				b->flags |= kBranch;
				if (EmuConfig.Speedhacks.WaitLoop && r3::waitLoop(startpc, words, count))
					b->flags |= kWaitLoop;
			}

			// The shape, and the function a build compiled for it.
			const r3::Key key = r3::key(words, count);
			auto found = s_shape_index.find(key);
			if (found == s_shape_index.end())
			{
				auto record = std::make_unique<ShapeRecord>();
				record->key = key;
				record->pc = startpc;
				record->words.assign(words, words + count);
				if (auto fn = s_compiled.find(key); fn != s_compiled.end())
					record->fn = fn->second;
				found = s_shape_index.emplace(key, static_cast<uint32_t>(s_shapes.size())).first;
				s_shapes.push_back(std::move(record));
			}
			b->shape = found->second;
			ShapeRecord& record = *s_shapes[b->shape];
			record.formed++;
			if (record.fn && s_compiled_enabled)
			{
				b->fn = record.fn;
				b->flags |= kCompiled;
			}
			else
			{
				b->fn = Interpret;
			}

			// recBlocks.Get(HWADDR(startpc)), and New() unless that is a block
			// filed under this very address.
			const int at = Index(b->hw);
			if (at >= 0 && s_blocks[static_cast<size_t>(at)]->hw == b->hw)
			{
				Retire(s_blocks[static_cast<size_t>(at)]);
				s_blocks[static_cast<size_t>(at)] = b;
				g_generation++;
			}
			else
			{
				Insert(b);
			}
			*entry = b;
			s_stats.formed++;
			return b;
		}

		// ---- the interpreter ----------------------------------------------------------------

		// The instructions of a block's words, decoded when it was formed: after
		// the words, in the same allocation (Form).
		PS2N_IOP_OP const Op* OpsOf(const Block* b)
		{
			return reinterpret_cast<const Op*>(b->words + b->count);
		}

		// One instruction that is not a branch or jump, `op` its word `code`
		// decoded. True: the block is left here (an exception, or an import the
		// core's function did).
		PS2N_IOP_OP bool Step(Block* b, Op op, uint32_t code, uint32_t pc, uint32_t& so_far, uint32_t& import)
		{
			uint32_t* const r = psxRegs.GPR.r;
			const uint32_t s = r3::rs(code), t = r3::rt(code), d = r3::rd(code);
			so_far += r3::cycles(op);
			switch (op)
			{
				case Op::ADDIU:
					if (t)
						r[t] = val::ADDIU(r[s], r3::imm(code));
					break;
				case Op::SLTI:
					if (t)
						r[t] = val::SLTI(r[s], r3::imm(code));
					break;
				case Op::SLTIU:
					if (t)
						r[t] = val::SLTIU(r[s], r3::imm(code));
					break;
				case Op::ANDI:
					if (t)
						r[t] = val::ANDI(r[s], r3::immu(code));
					break;
				case Op::ORI:
					if (t)
						r[t] = val::ORI(r[s], r3::immu(code));
					break;
				case Op::XORI:
					if (t)
						r[t] = val::XORI(r[s], r3::immu(code));
					break;
				case Op::LUI:
					if (t)
						r[t] = val::LUI(r3::immu(code));
					break;
				case Op::ADDU:
					if (d)
						r[d] = val::ADDU(r[s], r[t]);
					break;
				case Op::SUBU:
					if (d)
						r[d] = val::SUBU(r[s], r[t]);
					break;
				case Op::AND:
					if (d)
						r[d] = val::AND(r[s], r[t]);
					break;
				case Op::OR:
					if (d)
						r[d] = val::OR(r[s], r[t]);
					break;
				case Op::XOR:
					if (d)
						r[d] = val::XOR(r[s], r[t]);
					break;
				case Op::NOR:
					if (d)
						r[d] = val::NOR(r[s], r[t]);
					break;
				case Op::SLT:
					if (d)
						r[d] = val::SLT(r[s], r[t]);
					break;
				case Op::SLTU:
					if (d)
						r[d] = val::SLTU(r[s], r[t]);
					break;
				case Op::SLL:
					if (d)
						r[d] = val::SLL(r[t], r3::sa(code));
					break;
				case Op::SRL:
					if (d)
						r[d] = val::SRL(r[t], r3::sa(code));
					break;
				case Op::SRA:
					if (d)
						r[d] = val::SRA(r[t], r3::sa(code));
					break;
				case Op::SLLV:
					if (d)
						r[d] = val::SLL(r[t], r[s]);
					break;
				case Op::SRLV:
					if (d)
						r[d] = val::SRL(r[t], r[s]);
					break;
				case Op::SRAV:
					if (d)
						r[d] = val::SRA(r[t], r[s]);
					break;
				case Op::MULT:
				case Op::MULTU:
				case Op::DIV:
				case Op::DIVU:
				{
					const uint64_t v = op == Op::MULT  ? val::MULT(r[s], r[t]) :
									   op == Op::MULTU ? val::MULTU(r[s], r[t]) :
									   op == Op::DIV   ? val::DIV(r[s], r[t]) :
														 val::DIVU(r[s], r[t]);
					psxRegs.GPR.n.lo = static_cast<uint32_t>(v);
					psxRegs.GPR.n.hi = static_cast<uint32_t>(v >> 32);
					break;
				}
				case Op::MFHI:
					if (d)
						r[d] = psxRegs.GPR.n.hi;
					break;
				case Op::MTHI:
					psxRegs.GPR.n.hi = r[s];
					break;
				case Op::MFLO:
					if (d)
						r[d] = psxRegs.GPR.n.lo;
					break;
				case Op::MTLO:
					psxRegs.GPR.n.lo = r[s];
					break;
				case Op::LB:
				case Op::LBU:
				{
					const uint32_t address = r[s] + static_cast<uint32_t>(r3::imm(code));
					if (!t)
						LoadNowhere(address, 8);
					else if (op == Op::LB)
						r[t] = LoadS8(address);
					else
						r[t] = Load8(address);
					break;
				}
				case Op::LH:
				case Op::LHU:
				{
					const uint32_t address = r[s] + static_cast<uint32_t>(r3::imm(code));
					if (!t)
						LoadNowhere(address, 16);
					else if (op == Op::LH)
						r[t] = LoadS16(address);
					else
						r[t] = Load16(address);
					break;
				}
				case Op::LW:
				{
					const uint32_t address = r[s] + static_cast<uint32_t>(r3::imm(code));
					if (!t)
						LoadNowhere(address, 32);
					else
						r[t] = Load32(address);
					break;
				}
				case Op::LWL:
				case Op::LWR:
				{
					const uint32_t address = r[s] + static_cast<uint32_t>(r3::imm(code));
					const uint32_t mem = Read32(address & ~3u);
					if (t)
						r[t] = op == Op::LWL ? val::LWL(r[t], mem, address) : val::LWR(r[t], mem, address);
					break;
				}
				case Op::SB:
					Store8(r[s] + static_cast<uint32_t>(r3::imm(code)), r[t]);
					break;
				case Op::SH:
					Store16(r[s] + static_cast<uint32_t>(r3::imm(code)), r[t]);
					break;
				case Op::SW:
					Store32(r[s] + static_cast<uint32_t>(r3::imm(code)), r[t]);
					break;
				case Op::SWL:
				case Op::SWR:
				{
					const uint32_t address = r[s] + static_cast<uint32_t>(r3::imm(code));
					const uint32_t mem = Read32(address & ~3u);
					Store32(address & ~3u, op == Op::SWL ? val::SWL(r[t], mem, address) : val::SWR(r[t], mem, address));
					break;
				}
				case Op::MFC0:
					if (t)
						r[t] = psxRegs.CP0.r[d];
					break;
				case Op::MTC0:
					psxRegs.CP0.r[d] = r[t];
					break;
				case Op::RFE:
					RFE();
					break;
				case Op::SYSCALL:
					if (Exception(code, pc, 0x20, so_far))
						return true;
					break;
				case Op::BREAK:
					if (Exception(code, pc, 0x24, so_far))
						return true;
					break;
				case Op::GTE:
					Gte(code);
					break;
				case Op::IMPORT:
					if (Import(b, import++, code, pc))
						return true;
					break;
				default: // Unknown: nothing. A branch is not given to Step.
					break;
			}
			return false;
		}

		// The same for a delay slot's instruction, not put in the place of each
		// of its calls.
		[[gnu::noinline]] bool Slot(Block* b, Op op, uint32_t code, uint32_t pc, uint32_t& so_far, uint32_t& import)
		{
			return Step(b, op, code, pc, so_far, import);
		}


		bool PS2N_IOP_CC Interpret(Block* b)
		{
			b->runs++;
			uint32_t* const r = psxRegs.GPR.r;
			const uint32_t* const w = b->words;
			const Op* const ops = OpsOf(b);
			const bool branch = (b->flags & kBranch) != 0;
			const uint32_t body = branch ? b->count - 2 : b->count;
			uint32_t pc = b->pc;
			uint32_t so_far = 0;
			uint32_t import = 0;

			for (uint32_t i = 0; i < body; i++, pc += 4)
			{
				if (Step(b, ops[i], w[i], pc, so_far, import)) [[unlikely]]
					return true;
			}

			if (!branch)
			{
				// Cut short: it runs on into the block at its end.
				RunOn(b->cycles);
				psxRegs.pc = pc;
				PS2N_IOP_GO(1u, pc, false);
			}

			const uint32_t code = w[body];
			const uint32_t slot = w[body + 1];
			const Op op = ops[body];
			const uint32_t s = r3::rs(code), t = r3::rt(code), d = r3::rd(code);
			uint32_t target = 0;
			uint32_t way = 1;
			bool left = false;
			switch (op)
			{
				case Op::J:
					left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
					target = r3::jumpTarget(pc, code);
					break;
				case Op::JAL:
					r[31] = pc + 8;
					left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
					target = r3::jumpTarget(pc, code);
					break;
				case Op::JR:
					if (r3::swapsDelaySlot(s, 0, 0, slot))
					{
						left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
						target = r[s];
					}
					else
					{
						target = r[s];
						left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
					}
					break;
				case Op::JALR:
					// The order PCSX2's recompiler keeps (r3000a::swapsDelaySlot).
					if (d != s && r3::swapsDelaySlot(s, 0, d, slot))
					{
						left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
						if (d)
							r[d] = pc + 8;
						target = r[s];
					}
					else
					{
						target = r[s];
						if (d)
							r[d] = pc + 8;
						left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
					}
					break;
				default:
				{
					// The conditional branches; the two that link, link first.
					if (op == Op::BLTZAL || op == Op::BGEZAL)
						r[31] = pc + 8;
					const bool taken = val::Taken(op, r[s], r[t]);
					left = Slot(b, ops[body + 1], slot, pc + 4, so_far, import);
					target = taken ? r3::branchTarget(pc, code) : pc + 8;
					way = taken ? 1 : 0;
					break;
				}
			}
			if (left) [[unlikely]]
				return true;

			psxRegs.pc = target;
			if (op == Op::JR || op == Op::JALR)
			{
				if (target & 3) [[unlikely]]
					return Unaligned(target);
				const int went = BranchTest(b->cycles);
				if (went == kOver)
					return false;
				PS2N_IOP_GO_ANYWHERE(target, went == kOnTested);
			}

			const int went = ((b->flags & kWaitLoop) && way == 1 && target == b->pc) ? WaitLoopTest() : BranchTest(b->cycles);
			if (went == kOver)
				return false;
			PS2N_IOP_GO(way, target, went == kOnTested);
		}

		// ---- the provider -------------------------------------------------------------------

		void DropEverything()
		{
			for (Block* b : s_blocks)
				Retire(b);
			s_blocks.clear();
			if (g_lut)
				std::memset(g_lut, 0, sizeof(Block*) * s_entries);
			g_generation++;
			g_clearLimit = 0;
		}

		void FreeRetired()
		{
			for (Block* b : s_retired)
				FreeBlock(b);
			s_retired.clear();
		}

		// recReserveRAM and the table of pages recResetIOP fills in.
		void Map()
		{
			const uint32_t ram = Ps2MemSize::ExposedIopRam;
			if (!g_lut || s_ram_bytes != ram)
			{
				std::free(g_lut);
				s_ram_bytes = ram;
				s_ram_entries = ram / 4;
				s_entries = s_ram_entries + (Ps2MemSize::Rom + Ps2MemSize::Rom1 + Ps2MemSize::Rom2) / 4;
				g_lut = static_cast<Block**>(std::calloc(s_entries, sizeof(Block*)));
			}
			g_ramMask = ram - 1;

			for (int32_t& page : g_page)
				page = -1;
			const uint32_t rom = s_ram_entries;
			const uint32_t rom1 = rom + Ps2MemSize::Rom / 4;
			const uint32_t rom2 = rom1 + Ps2MemSize::Rom1 / 4;
			const uint32_t mask = (ram / 0x10000) - 1;
			for (const uint32_t base : {0x0000u, 0x8000u, 0xA000u})
			{
				for (uint32_t i = 0; i < 0x80; i++)
					g_page[base + i] = static_cast<int32_t>((i & mask) << 14);
				for (uint32_t i = 0x1FC0; i < 0x2000; i++)
					g_page[base + i] = static_cast<int32_t>(rom + ((i - 0x1FC0) << 14));
				for (uint32_t i = 0x1E00; i < 0x1E40; i++)
					g_page[base + i] = static_cast<int32_t>(rom1 + ((i - 0x1E00) << 14));
				for (uint32_t i = 0x1E40; i < 0x1E48; i++)
					g_page[base + i] = static_cast<int32_t>(rom2 + ((i - 0x1E40) << 14));
			}
		}

		void Reserve()
		{
			if (s_reserved)
				return;
			s_reserved = true;
			Map();
		}

		// recResetIOP.
		void Reset()
		{
			Reserve();
			DropEverything();
			Map();
			std::memset(g_lut, 0, sizeof(Block*) * s_entries);
			s_stats.resets++;
		}

		// recExecuteBlock.
		s32 ExecuteBlock(s32 ee_cycles)
		{
			FreeRetired();
			s_stats.stretches++;
			psxRegs.iopBreak = 0;
			psxRegs.iopCycleEE = ee_cycles;

			for (;;)
			{
				Block* b = Find(psxRegs.pc);
				if (!b || (psxRegs.pc & 3)) [[unlikely]]
				{
					b = Form(psxRegs.pc);
					if (!b)
						break;
				}
				if (!b->fn(b))
					break;
			}
			return psxRegs.iopBreak + psxRegs.iopCycleEE;
		}

		// recClearIOP.
		void Clear(u32 address, u32 size)
		{
			u32 pc = address;
			while (pc < address + size * 4)
			{
				if (pc >= g_clearLimit)
				{
					pc += 4;
					continue;
				}
				// PCSX2's recompiler would read its table between two entries.
				if (pc & 3) [[unlikely]]
				{
					Refuse("a write into the IOP's code announced at an address that is not a multiple of four", pc);
					return;
				}
				pc += ClearMem(pc);
			}
		}

		void Shutdown()
		{
			DropEverything();
			FreeRetired();
			std::free(g_lut);
			g_lut = nullptr;
			s_reserved = false;
		}

		R3000Acpu s_provider = {Reserve, Reset, ExecuteBlock, Clear, Shutdown};
	} // namespace

	void ClearBlocksAt(uint32_t address)
	{
		ClearMem(address);
	}

	bool Unaligned(uint32_t target)
	{
		Refuse("a jump to an address that is not a multiple of four", target);
		return false;
	}

	void Gte(uint32_t code)
	{
		// Only what the recompiler has an instruction for (r3000a::decode):
		// its tables have the same functions at the same places as the
		// interpreter's, which the core lets others see (rpsxCP2, rpsxCP2BSC
		// and rpsxBSC; psxCP2, psxCP2BSC and psxBSC).
		if (r3::decode(code) != Op::GTE)
			return;
		psxRegs.code = code;
		if ((code >> 26) != 18)
			psxBSC[code >> 26]();
		else if (r3::funct(code) == 0)
			psxCP2BSC[r3::rs(code)]();
		else
			psxCP2[r3::funct(code)]();
	}

	R3000Acpu* Provider()
	{
		Reserve();
		return &s_provider;
	}

	Stats GetStats()
	{
		Stats st = s_stats;
		st.live = s_blocks.size();
		st.exceptions = g_counters.exceptions;
		st.imports = g_counters.imports;
		st.importsDone = g_counters.importsDone;
		st.waits = g_counters.waits;

		// The runs of the blocks the record holds are still with the blocks.
		std::vector<uint64_t> runs(s_shapes.size(), 0);
		for (const Block* b : s_blocks)
			runs[b->shape] += b->runs;
		st.shapes = s_shapes.size();
		for (size_t i = 0; i < s_shapes.size(); i++)
		{
			const ShapeRecord& record = *s_shapes[i];
			const uint64_t n = record.runs + runs[i];
			const bool compiled = record.fn && s_compiled_enabled;
			st.shapeWords += record.words.size();
			st.shapesCompiled += compiled ? 1 : 0;
			st.shapesInterpreted += (!compiled && n) ? 1 : 0;
			st.runs += n;
			st.runsInterpreted += compiled ? 0 : n;
			st.instructions += n * record.words.size();
		}
		return st;
	}

	const char* Unsupported()
	{
		return s_unsupported.empty() ? nullptr : s_unsupported.c_str();
	}

	void ClearUnsupported()
	{
		s_unsupported.clear();
	}

	void SetImages(const ImageInfo* const* images, size_t count)
	{
		s_compiled.clear();
		for (size_t i = 0; i < count; i++)
		{
			const ImageInfo* image = images[i];
			for (uint32_t k = 0; k < image->block_count; k++)
			{
				const BlockInfo& info = image->blocks[k];
				s_compiled.emplace(r3::Key{info.key_a, info.key_b}, info.fn);
			}
		}
		for (auto& record : s_shapes)
		{
			auto fn = s_compiled.find(record->key);
			record->fn = fn == s_compiled.end() ? nullptr : fn->second;
		}
	}

	void SetCompiledBlocksEnabled(bool enabled)
	{
		s_compiled_enabled = enabled;
	}

	size_t CompiledBlockCount()
	{
		return s_compiled.size();
	}

	bool WriteCapture(const char* path, bool all)
	{
		std::FILE* f = std::fopen(path, "wb");
		if (!f)
			return false;
		std::fputs("# The blocks of IOP code a run carried out by PS2Native's IOP interpreter, each as PCSX2's IOP\n"
				   "# recompiler would have formed it: the address it was first met at, and its instructions. Give\n"
				   "# this file to the recompiler (ps2n-recomp iop) to have them compiled.\n",
			f);

		std::vector<uint64_t> runs(s_shapes.size(), 0);
		for (const Block* b : s_blocks)
			runs[b->shape] += b->runs;

		// In the order of their addresses, then their keys, so that the same
		// run writes the same file.
		std::vector<const ShapeRecord*> records;
		for (size_t i = 0; i < s_shapes.size(); i++)
		{
			const ShapeRecord& record = *s_shapes[i];
			const bool compiled = record.fn && s_compiled_enabled;
			if (all || (!compiled && record.runs + runs[i]))
				records.push_back(&record);
		}
		std::sort(records.begin(), records.end(), [](const ShapeRecord* x, const ShapeRecord* y) {
			if (x->pc != y->pc)
				return x->pc < y->pc;
			if (x->key.a != y->key.a)
				return x->key.a < y->key.a;
			return x->key.b < y->key.b;
		});
		for (const ShapeRecord* record : records)
		{
			std::fprintf(f, "\n[[block]]\npc = 0x%08X\nwords = [", record->pc);
			for (size_t i = 0; i < record->words.size(); i++)
				std::fprintf(f, "%s0x%08X", i == 0 ? "" : (i % 8 == 0 ? ",\n\t" : ", "), record->words[i]);
			std::fputs("]\n", f);
		}
		return std::fclose(f) == 0;
	}

	size_t BlockStarts(uint32_t from, uint32_t to, uint32_t* out, size_t capacity)
	{
		size_t n = 0;
		if (!g_lut)
			return 0;
		for (uint32_t address = from & ~3u; address < to; address += 4)
		{
			if (Find(address))
			{
				if (n < capacity)
					out[n] = address;
				n++;
			}
		}
		return n;
	}
} // namespace ps2n::iop
