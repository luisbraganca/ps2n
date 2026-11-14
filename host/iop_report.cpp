// SPDX-License-Identifier: GPL-3.0-or-later
//
// See iop_report.h.

#include "iop_report.h"

#include "sync_trace.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/DebugTools/BiosDebugData.h"
#include "pcsx2/IopMem.h"
#include "pcsx2/Memory.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/x86/BaseblockEx.h"

#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// Where the host code of the block that begins at each address is: PCSX2's
// IOP recompiler's own table (pcsx2/x86/iR3000A.cpp).
extern uptr psxRecLUT[];

namespace
{
	using ull = unsigned long long;

	std::string Grouped(u64 n)
	{
		std::string digits = std::to_string(n);
		for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
			digits.insert(static_cast<size_t>(at), ",");
		return digits;
	}

	// What is counted, over one frame or over the whole run.
	struct Counts
	{
		u64 slices = 0; // stretches of IOP time (ExecuteBlock)
		u64 ee_cycles = 0; // what the EE's clock gave them
		u64 iop_cycles = 0; // what the IOP's clock moved in them
		u64 ticks = 0; // time-stamp counter ticks spent in them
		u64 stores = 0; // writes of one word announced to the provider: the IOP's own stores
		u64 transfers = 0; // longer ones: from the EE, the disc, the sound hardware
		u64 transfer_words = 0;
		u64 hits = 0; // words written where a block began
		u64 formed = 0; // blocks seen that were not there at the frame before

		void Add(const Counts& o)
		{
			slices += o.slices;
			ee_cycles += o.ee_cycles;
			iop_cycles += o.iop_cycles;
			ticks += o.ticks;
			stores += o.stores;
			transfers += o.transfers;
			transfer_words += o.transfer_words;
			hits += o.hits;
			formed += o.formed;
		}
	};

	// A stretch of memory the recompiler has a table for.
	struct Region
	{
		const char* name;
		u32 base = 0; // physical address
		u32 entries = 0;
		BASEBLOCK* blocks = nullptr;
		std::vector<uptr> last; // each entry as it was when last looked at
		u32 live = 0; // blocks there now
		u64 formed = 0;
		u64 contents = 0;
		u64 words = 0;
		u64 slices = 0;
		u64 slice_cycles = 0;
	};

	// A module of the IOP's kernel's list, at the place it was loaded.
	struct Module
	{
		std::string name;
		u16 version = 0;
		u32 text = 0;
		u32 text_size = 0;
		u32 data_size = 0;
		u32 bss_size = 0;
		u32 first_frame = 0;
		u32 last_frame = 0;
		bool listed = false;
		u64 formed = 0; // blocks seen formed in its text
		u64 contents = 0; // of them, those not seen with that content at that place before
		u64 words = 0; // the instructions of those
		u64 slices = 0; // stretches of IOP time that ended there
		u64 slice_cycles = 0; // their IOP cycles
	};

	bool s_active = false;
	bool s_census = false; // the provider watched is PCSX2's IOP recompiler
	std::string s_path;
	std::FILE* s_frames_file = nullptr;
	u32 s_frame = 0;
	u64 s_tsc_start = 0;
	std::chrono::steady_clock::time_point s_clock_start;

	R3000Acpu* s_real = nullptr;
	R3000Acpu s_wrapper;
	u64 s_resets = 0;

	Counts s_frame_counts;
	Counts s_total;

	uptr s_stub = 0; // what the recompiler's table holds where no block begins
	Region s_regions[4] = {{"RAM"}, {"the ROM"}, {"the second ROM"}, {"the third ROM"}};
	std::vector<Module> s_modules;
	std::vector<std::string> s_events; // the list of modules, as it changed
	u64 s_outside_formed = 0, s_outside_contents = 0, s_outside_words = 0; // RAM that is in no listed module's text
	u64 s_outside_slices = 0, s_outside_slice_cycles = 0;

	// The contents seen at each place a block began, hashed.
	std::unordered_map<u32, std::vector<u64>> s_places;

	u32 Word(u32 address)
	{
		return iopMemRead32(address);
	}

	// How far the recompiler's look-ahead goes from `pc` by the code alone: up
	// to the first branch or jump and its delay slot (iopRecRecompile). Where it
	// really ended the block also depended on which blocks it held then.
	u32 BlockLength(u32 pc)
	{
		constexpr u32 kLimit = 4096;
		for (u32 n = 0; n < kLimit; n++)
		{
			const u32 code = Word(pc + n * 4);
			switch (code >> 26)
			{
				case 0:
					if ((code & 0x3F) == 8 || (code & 0x3F) == 9)
						return n + 2;
					break;
				case 1:
				{
					const u32 rt = (code >> 16) & 0x1F;
					if (rt == 0 || rt == 1 || rt == 16 || rt == 17)
						return n + 2;
					break;
				}
				case 2:
				case 3:
				case 4:
				case 5:
				case 6:
				case 7:
					return n + 2;
				default:
					break;
			}
		}
		return kLimit;
	}

	u64 HashBlock(u32 pc, u32 length)
	{
		u64 h = 0x243F6A8885A308D3ull ^ length;
		for (u32 n = 0; n < length; n++)
		{
			h = (h ^ Word(pc + n * 4)) * 0x9E3779B97F4A7C15ull;
			h ^= h >> 29;
		}
		return h;
	}

	Module* ModuleAt(u32 address)
	{
		for (Module& m : s_modules)
		{
			if (m.listed && address >= m.text && address - m.text < m.text_size)
				return &m;
		}
		return nullptr;
	}

	Region* RegionAt(u32 address)
	{
		for (Region& r : s_regions)
		{
			if (r.blocks && address >= r.base && (address - r.base) / 4 < r.entries)
				return &r;
		}
		return nullptr;
	}

	// A block the recompiler holds now and did not at the frame before.
	void Formed(Region& r, u32 address)
	{
		const u32 length = BlockLength(address);
		const u64 hash = HashBlock(address, length);
		std::vector<u64>& contents = s_places[address];
		const bool fresh = std::find(contents.begin(), contents.end(), hash) == contents.end();
		if (fresh)
			contents.push_back(hash);

		s_frame_counts.formed++;
		r.formed++;
		if (fresh)
		{
			r.contents++;
			r.words += length;
		}
		if (&r != &s_regions[0])
			return;
		if (Module* m = ModuleAt(address))
		{
			m->formed++;
			if (fresh)
			{
				m->contents++;
				m->words += length;
			}
		}
		else
		{
			s_outside_formed++;
			if (fresh)
			{
				s_outside_contents++;
				s_outside_words += length;
			}
		}
	}

	void Census(Region& r)
	{
		if (!r.blocks)
			return;
		for (u32 i = 0; i < r.entries; i++)
		{
			const uptr fn = r.blocks[i].GetFnptr();
			if (fn == r.last[i])
				continue;
			const bool was = r.last[i] != s_stub;
			r.last[i] = fn;
			if (fn == s_stub)
			{
				r.live--;
				continue;
			}
			if (!was)
				r.live++;
			Formed(r, r.base + i * 4);
		}
	}

	// The IOP's kernel's list of modules, as the core finds it for its debugger.
	void Modules(u32 frame)
	{
		const std::vector<IopMod> now = getIOPModules();
		for (Module& m : s_modules)
			m.listed = false;
		for (const IopMod& mod : now)
		{
			const u32 text = mod.text_addr & 0x1FFFFFFF;
			Module* found = nullptr;
			for (Module& m : s_modules)
			{
				if (m.text == text && m.text_size == mod.text_size && m.version == mod.version && m.name == mod.name)
				{
					found = &m;
					break;
				}
			}
			if (!found)
			{
				Module m;
				m.name = mod.name;
				m.version = mod.version;
				m.text = text;
				m.text_size = mod.text_size;
				m.data_size = mod.data_size;
				m.bss_size = mod.bss_size;
				m.first_frame = frame;
				s_modules.push_back(std::move(m));
				found = &s_modules.back();
				s_events.push_back(fmt::format("frame {:6}: listed    {:08X}-{:08X} {} {}.{}", frame, text, text + mod.text_size, mod.name,
					mod.version >> 8, mod.version & 0xFF));
			}
			found->listed = true;
			found->last_frame = frame;
		}
		for (const Module& m : s_modules)
		{
			// Listed until the frame before, and no longer.
			if (!m.listed && m.last_frame + 1 == frame)
			{
				s_events.push_back(
					fmt::format("frame {:6}: no longer {:08X}-{:08X} {} {}.{}", frame, m.text, m.text + m.text_size, m.name, m.version >> 8,
						m.version & 0xFF));
			}
		}
	}

	s32 ExecuteBlockWatched(s32 ee_cycles)
	{
		const u64 cycle = psxRegs.cycle;
		const u64 start = __rdtsc();
		const s32 left = s_real->ExecuteBlock(ee_cycles);
		Counts& c = s_frame_counts;
		c.ticks += __rdtsc() - start;
		c.slices++;
		if (ee_cycles > 0)
			c.ee_cycles += static_cast<u64>(ee_cycles);
		const u64 ran = psxRegs.cycle - cycle;
		c.iop_cycles += ran;

		// Where the IOP was when the stretch ended: a hint at where its time
		// goes, not a count.
		const u32 pc = psxRegs.pc & 0x1FFFFFFF;
		if (pc < Ps2MemSize::ExposedIopRam)
		{
			if (Module* m = ModuleAt(pc))
			{
				m->slices++;
				m->slice_cycles += ran;
			}
			else
			{
				s_outside_slices++;
				s_outside_slice_cycles += ran;
			}
		}
		else if (Region* r = RegionAt(pc))
		{
			r->slices++;
			r->slice_cycles += ran;
		}
		return left;
	}

	void ClearWatched(u32 address, u32 size)
	{
		Counts& c = s_frame_counts;
		if (size == 1)
		{
			c.stores++;
		}
		else
		{
			c.transfers++;
			c.transfer_words += size;
		}
		if (s_census)
		{
			const Region& ram = s_regions[0];
			for (u32 i = 0; i < size; i++)
			{
				const u32 at = address + i * 4;
				if (at / 4 < ram.entries && ram.blocks[at / 4].GetFnptr() != s_stub)
					c.hits++;
			}
		}
		s_real->Clear(address, size);
	}

	void ResetWatched()
	{
		s_resets++;
		s_real->Reset();
	}

	// How much of the recompiler's room for host code holds something.
	u64 RecompilerRoomUsed()
	{
		const u8* start = SysMemory::GetIOPRec();
		const u8* end = SysMemory::GetIOPRecEnd();
		while (end - start >= 8)
		{
			u64 word;
			std::memcpy(&word, end - 8, 8);
			if (word)
				break;
			end -= 8;
		}
		return static_cast<u64>(end - start);
	}
} // namespace

bool ps2n::host::iopreport::Start(const char* path)
{
	s_path = path;
	const size_t slash = s_path.find_last_of("/\\");
	const size_t dot = s_path.find_last_of('.');
	const std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? s_path.substr(0, dot) : s_path;

	s_frames_file = std::fopen((stem + "-frames.csv").c_str(), "wb");
	if (!s_frames_file)
		return false;
	std::setvbuf(s_frames_file, nullptr, _IOFBF, 1 << 20);
	std::fputs("frame,slices,ee_cycles,iop_cycles,ticks,stores,transfers,transfer_words,hits,formed,blocks_ram,blocks_rom,modules\n",
		s_frames_file);

	s_active = true;
	s_tsc_start = __rdtsc();
	s_clock_start = std::chrono::steady_clock::now();
	return true;
}

void ps2n::host::iopreport::Install()
{
	if (!s_active || !psxCpu || psxCpu == &s_wrapper)
		return;

	s_real = psxCpu;
	s_wrapper = *psxCpu;
	s_wrapper.ExecuteBlock = ExecuteBlockWatched;
	s_wrapper.Clear = ClearWatched;
	s_wrapper.Reset = ResetWatched;

	// The recompiler's table says something only when the recompiler is what
	// runs the IOP. No block begins at address 0, so what the table holds there
	// is what it holds wherever none does.
	s_census = (s_real == &psxRec);
	if (s_census)
	{
		s_stub = PC_GETBLOCK_(0u, psxRecLUT)->GetFnptr();
		const struct
		{
			u32 base, bytes;
		} kWhere[4] = {{0x00000000, Ps2MemSize::ExposedIopRam}, {0x1FC00000, Ps2MemSize::Rom}, {0x1E000000, Ps2MemSize::Rom1},
			{0x1E400000, 0x00080000}};
		for (int i = 0; i < 4; i++)
		{
			Region& r = s_regions[i];
			r.base = kWhere[i].base;
			r.entries = kWhere[i].bytes / 4;
			r.blocks = PC_GETBLOCK_(r.base, psxRecLUT);
			r.last.assign(r.entries, s_stub);
		}
	}
	psxCpu = &s_wrapper;
}

bool ps2n::host::iopreport::Installed()
{
	return !s_active || ps2n::host::SyncTraceUnder(psxCpu) == &s_wrapper;
}

const void* ps2n::host::iopreport::Under(const void* provider)
{
	return provider == &s_wrapper ? s_real : provider;
}

uint32_t ps2n::host::iopreport::RecompilerBlocks()
{
	// What the table holds where no block begins: the last word of RAM is
	// never run.
	const uptr none = PC_GETBLOCK_(Ps2MemSize::ExposedIopRam - 4, psxRecLUT)->GetFnptr();
	const struct
	{
		u32 base, bytes;
	} kWhere[4] = {{0x00000000, Ps2MemSize::ExposedIopRam}, {0x1FC00000, Ps2MemSize::Rom}, {0x1E000000, Ps2MemSize::Rom1},
		{0x1E400000, 0x00080000}};
	u32 blocks = 0;
	for (const auto& where : kWhere)
	{
		const BASEBLOCK* const table = PC_GETBLOCK_(where.base, psxRecLUT);
		for (u32 i = 0; i < where.bytes / 4; i++)
			blocks += table[i].GetFnptr() != none ? 1 : 0;
	}
	return blocks;
}

void ps2n::host::iopreport::Frame(uint32_t frame)
{
	if (!s_active)
		return;
	s_frame = frame;

	Modules(frame);
	if (s_census)
	{
		Census(s_regions[0]);
		if (frame % 64 == 0)
		{
			for (int i = 1; i < 4; i++)
				Census(s_regions[i]);
		}
	}

	const Counts& c = s_frame_counts;
	u32 listed = 0;
	for (const Module& m : s_modules)
		listed += m.listed ? 1 : 0;
	std::fprintf(s_frames_file, "%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u\n", frame, ull(c.slices), ull(c.ee_cycles),
		ull(c.iop_cycles), ull(c.ticks), ull(c.stores), ull(c.transfers), ull(c.transfer_words), ull(c.hits), ull(c.formed), s_regions[0].live,
		s_regions[1].live, listed);
	s_total.Add(s_frame_counts);
	s_frame_counts = {};
}

bool ps2n::host::iopreport::Stop(uint32_t frames)
{
	if (!s_active)
		return true;
	s_active = false;

	if (s_census)
	{
		for (Region& r : s_regions)
			Census(r);
	}
	s_total.Add(s_frame_counts);
	s_frame_counts = {};

	if (psxCpu == &s_wrapper)
		psxCpu = s_real;

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_clock_start).count();
	const double tick_rate = seconds > 0.0 ? static_cast<double>(__rdtsc() - s_tsc_start) / seconds : 1.0;

	if (s_frames_file)
		std::fclose(s_frames_file);
	s_frames_file = nullptr;

	std::FILE* fp = std::fopen(s_path.c_str(), "wb");
	if (!fp)
		return false;

	const Counts& c = s_total;
	std::fprintf(fp, "# What the I/O processor ran (ps2n-host -iop-report), over %u frames.\n", frames);
	std::fprintf(fp, "# Time is the host's in the IOP's provider, from the time-stamp counter (%.3f GHz over the run): the IOP's\n"
					 "# code and what the core does for it when it reads and writes its devices. Cycles are the IOP's own.\n"
					 "# Here are addresses, counts and the names of modules; nothing of their code.\n\n",
		tick_rate / 1e9);

	std::fprintf(fp, "IOP time\n");
	std::fprintf(fp, "  %s cycles of the IOP's clock, in %s stretches (%.1f cycles each); the EE's clock gave them %s cycles\n",
		Grouped(c.iop_cycles).c_str(), Grouped(c.slices).c_str(), c.slices ? static_cast<double>(c.iop_cycles) / c.slices : 0.0,
		Grouped(c.ee_cycles).c_str());
	std::fprintf(fp, "  the host spent %.3f s in them, %.2f%% of the %.1f s the run took from the first to the last look\n\n", c.ticks / tick_rate,
		seconds > 0.0 ? 100.0 * (c.ticks / tick_rate) / seconds : 0.0, seconds);

	std::fprintf(fp, "Writes into the IOP's memory, as they were announced to its provider\n");
	std::fprintf(fp, "  %s stores of the IOP's own; %s transfers of %s words in all\n", Grouped(c.stores).c_str(), Grouped(c.transfers).c_str(),
		Grouped(c.transfer_words).c_str());
	if (s_census)
		std::fprintf(fp, "  %s of the words written were where a block began: the recompiler threw those blocks away\n", Grouped(c.hits).c_str());
	else
		std::fprintf(fp, "  (a provider that makes the IOP's own stores into RAM itself, as PS2Native's does, announces none of them)\n");
	std::fprintf(fp, "  the provider was reset %s times\n\n", Grouped(s_resets).c_str());

	if (!s_census)
	{
		std::fprintf(fp, "The provider watched is not PCSX2's IOP recompiler: its blocks cannot be counted from here.\n\n");
	}
	else
	{
		u64 places_many = 0, contents = 0;
		for (const auto& [address, hashes] : s_places)
		{
			contents += hashes.size();
			places_many += hashes.size() > 1 ? 1 : 0;
		}
		u64 formed = 0;
		for (const Region& r : s_regions)
			formed += r.formed;
		std::fprintf(fp, "Blocks of PCSX2's IOP recompiler, as its table showed them at the end of each frame\n");
		std::fprintf(fp, "  %s blocks were seen that had not been there the frame before, at %s places; %s different contents in all,\n"
						 "  %s places with more than one (a content: the code from the place to the first branch and its delay slot)\n",
			Grouped(formed).c_str(), Grouped(s_places.size()).c_str(), Grouped(contents).c_str(), Grouped(places_many).c_str());
		std::fprintf(fp, "  %-16s %10s %10s %12s %10s\n", "", "seen", "contents", "instructions", "at the end");
		for (const Region& r : s_regions)
		{
			std::fprintf(fp, "  %-16s %10s %10s %12s %10s\n", r.name, Grouped(r.formed).c_str(), Grouped(r.contents).c_str(),
				Grouped(r.words).c_str(), Grouped(r.live).c_str());
		}
		const u64 used = RecompilerRoomUsed();
		const u64 room = static_cast<u64>(SysMemory::GetIOPRecEnd() - SysMemory::GetIOPRec());
		std::fprintf(fp, "  the recompiler's host code took %.2f MB of the %.0f MB it has room for\n\n", used / 1048576.0, room / 1048576.0);
	}

	std::fprintf(fp, "The modules the IOP's kernel listed, by address: text, name and version, frames listed; blocks seen formed in\n"
					 "the text, different contents among them, their instructions; stretches of IOP time that ended there, and\n"
					 "those stretches' IOP cycles (a hint at where time goes, not a count)\n");
	std::vector<const Module*> order;
	for (const Module& m : s_modules)
		order.push_back(&m);
	std::stable_sort(order.begin(), order.end(), [](const Module* a, const Module* b) {
		return a->text != b->text ? a->text < b->text : a->first_frame < b->first_frame;
	});
	for (const Module* m : order)
	{
		std::fprintf(fp, "  %08X-%08X %-28s %2u.%-3u %6u-%-6u %8s %8s %9s %12s %14s\n", m->text, m->text + m->text_size, m->name.c_str(),
			m->version >> 8, m->version & 0xFF, m->first_frame, m->last_frame, Grouped(m->formed).c_str(), Grouped(m->contents).c_str(),
			Grouped(m->words).c_str(), Grouped(m->slices).c_str(), Grouped(m->slice_cycles).c_str());
	}
	std::fprintf(fp, "  %-17s %-28s %6s %13s %8s %8s %9s %12s %14s\n", "RAM", "in no listed module's text", "", "", Grouped(s_outside_formed).c_str(),
		Grouped(s_outside_contents).c_str(), Grouped(s_outside_words).c_str(), Grouped(s_outside_slices).c_str(),
		Grouped(s_outside_slice_cycles).c_str());
	for (int i = 1; i < 4; i++)
	{
		const Region& r = s_regions[i];
		std::fprintf(fp, "  %-17s %-28s %6s %13s %8s %8s %9s %12s %14s\n", r.name, "", "", "", Grouped(r.formed).c_str(),
			Grouped(r.contents).c_str(), Grouped(r.words).c_str(), Grouped(r.slices).c_str(), Grouped(r.slice_cycles).c_str());
	}

	std::fprintf(fp, "\nThe list of modules as it changed\n");
	for (const std::string& event : s_events)
		std::fprintf(fp, "  %s\n", event.c_str());

	if (s_census)
	{
		std::vector<std::pair<u32, size_t>> many;
		for (const auto& [address, hashes] : s_places)
		{
			if (hashes.size() > 1)
				many.emplace_back(address, hashes.size());
		}
		std::stable_sort(many.begin(), many.end(), [](const auto& a, const auto& b) {
			return a.second != b.second ? a.second > b.second : a.first < b.first;
		});
		std::fprintf(fp, "\nThe places with the most different contents (%s places have more than one)\n", Grouped(many.size()).c_str());
		for (size_t i = 0; i < many.size() && i < 40; i++)
			std::fprintf(fp, "  %08X %4zu\n", many[i].first, many[i].second);
	}

	const bool ok = std::ferror(fp) == 0;
	std::fclose(fp);
	return ok;
}
