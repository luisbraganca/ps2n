// SPDX-License-Identifier: GPL-3.0-or-later
//
// See vu_report.h.

#include "vu_report.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/VU.h"
#include "pcsx2/VUmicro.h"

#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace
{
	using ull = unsigned long long;

	// Contents are told apart by a hash of their 64-bit words: quick, and with
	// the few hundred thousand contents a run can write, two different ones come
	// out the same about never.
	u64 Hash(const u8* data, u32 size)
	{
		u64 h = 0x243F6A8885A308D3ull ^ size;
		u32 i = 0;
		for (; i + 8 <= size; i += 8)
		{
			u64 word;
			std::memcpy(&word, data + i, 8);
			h = (h ^ word) * 0x9E3779B97F4A7C15ull;
			h ^= h >> 29;
		}
		for (; i < size; i++)
		{
			h = (h ^ data[i]) * 0x9E3779B97F4A7C15ull;
			h ^= h >> 29;
		}
		return h;
	}

	u32 WordAt(const u8* data, u32 index)
	{
		u32 word;
		std::memcpy(&word, data + index * 4, 4);
		return word;
	}

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
		u64 writes = 0; // writes into micro memory announced to the provider (Clear)
		u64 bytes = 0; // their length
		u64 changed = 0; // writes after which the place held something else
		u64 new_contents = 0; // writes of a content not written at that place before
		u64 starts = 0; // programs started (SetStartPC)
		u64 new_states = 0; // starts at an address with a micro memory not started from there before
		u64 runs = 0; // calls of Execute
		u64 ticks = 0; // time-stamp counter ticks spent in them
		u64 cycles = 0; // VU cycles they ran

		void Add(const Counts& o)
		{
			writes += o.writes;
			bytes += o.bytes;
			changed += o.changed;
			new_contents += o.new_contents;
			starts += o.starts;
			new_states += o.new_states;
			runs += o.runs;
			ticks += o.ticks;
			cycles += o.cycles;
		}
	};

	// A place in micro memory as it is written: an address and a length.
	struct Place
	{
		u64 writes = 0;
		u64 changed = 0;
		std::unordered_set<u64> contents;
		std::vector<u32> first; // the first content written there, word by word
		std::vector<u32> differs; // for each word: in how many later contents it is not the first's
	};

	// An address programs are started at.
	struct StartAddress
	{
		u64 starts = 0;
		u64 ticks = 0;
		u64 cycles = 0;
		std::unordered_set<u64> states; // the whole micro memory, hashed, at its starts
	};

	// A write announced and not yet made: the core announces a write before it
	// copies the data, so its content is read at the next call of the provider.
	struct Pending
	{
		u32 address;
		u32 size;
		u64 before; // what the place held when the write was announced, hashed
	};

	// What every call of Execute took, minus what the other VU's provider
	// running inside it took: the sum of the inner calls' time so far.
	u64 s_nested_ticks = 0;

	bool s_active = false;
	std::string s_path;
	std::FILE* s_frames_file = nullptr;
	std::FILE* s_uploads_file = nullptr;
	u64 s_uploads_bytes = 0;
	bool s_uploads_full = false;
	constexpr u64 kUploadsLimit = 512ull << 20;
	u32 s_frame = 0;
	u64 s_tsc_start = 0;
	std::chrono::steady_clock::time_point s_clock_start;

	// Each distinct content, as it is first written: the VU, the place, the
	// frame, the hash, then the bytes.
	void WriteContent(int vu, u32 address, u32 size, u64 hash, const u8* data)
	{
		if (!s_uploads_file || s_uploads_full)
			return;
		const u32 head[4] = {static_cast<u32>(vu), address, size, s_frame};
		if (s_uploads_bytes + sizeof(head) + sizeof(hash) + size > kUploadsLimit)
		{
			s_uploads_full = true;
			return;
		}
		std::fwrite(head, sizeof(head), 1, s_uploads_file);
		std::fwrite(&hash, sizeof(hash), 1, s_uploads_file);
		std::fwrite(data, 1, size, s_uploads_file);
		s_uploads_bytes += sizeof(head) + sizeof(hash) + size;
	}

	class Watch final : public BaseVUmicroCPU
	{
	public:
		BaseVUmicroCPU* real = nullptr;
		int vu = 0;
		VURegs* regs = nullptr;
		u32 memory_size = 0;

		Counts frame;
		Counts total;
		u64 resets = 0;
		std::map<std::pair<u32, u32>, Place> places;
		std::map<u32, StartAddress> start_addresses; // std::map: `current` stays valid
		std::unordered_set<u64> states;
		std::vector<Pending> pending;
		u64 memory_hash = 0;
		bool memory_changed = true;
		StartAddress* current = nullptr;

		const char* GetShortName() const override { return real->GetShortName(); }
		const char* GetLongName() const override { return real->GetLongName(); }
		size_t GetCommittedCache() const override { return real->GetCommittedCache(); }
		void Shutdown() override { real->Shutdown(); }

		void Reset() override
		{
			Settle();
			resets++;
			memory_changed = true;
			real->Reset();
		}

		void SetStartPC(u32 start) override
		{
			Settle();
			NoteStart(start);
			real->SetStartPC(start);
		}

		void Execute(u32 cycles) override
		{
			Settle();
			const u64 cycle_before = regs->cycle;
			const u64 nested_before = s_nested_ticks;
			const u64 t0 = __rdtsc();
			real->Execute(cycles);
			const u64 elapsed = __rdtsc() - t0;
			const u64 nested = s_nested_ticks - nested_before;
			const u64 own = elapsed > nested ? elapsed - nested : 0;
			s_nested_ticks += own;
			const u64 ran = regs->cycle - cycle_before;
			frame.runs++;
			frame.ticks += own;
			frame.cycles += ran;
			if (current)
			{
				current->ticks += own;
				current->cycles += ran;
			}
		}

		void Step() override
		{
			Settle();
			real->Step();
		}

		void Clear(u32 address, u32 size) override
		{
			Settle();
			NoteWrite(address, size);
			real->Clear(address, size);
		}

		void ResumeXGkick() override { real->ResumeXGkick(); }

		void NoteWrite(u32 address, u32 size)
		{
			frame.writes++;
			frame.bytes += size;
			address &= memory_size - 1;
			size = std::min(size, memory_size - address);
			if (size)
				pending.push_back({address, size, Hash(regs->Micro + address, size)});
		}

		void NoteStart(u32 start)
		{
			frame.starts++;
			if (memory_changed)
			{
				memory_hash = Hash(regs->Micro, memory_size);
				memory_changed = false;
			}
			StartAddress& at = start_addresses[start];
			at.starts++;
			if (at.states.insert(memory_hash).second)
				frame.new_states++;
			states.insert(memory_hash);
			current = &at;
		}

		// The writes announced so far have been made: count what they wrote.
		void Settle()
		{
			if (pending.empty())
				return;
			for (const Pending& p : pending)
			{
				const u8* data = regs->Micro + p.address;
				const u64 after = Hash(data, p.size);
				Place& place = places[{p.address, p.size}];
				place.writes++;
				if (after != p.before)
				{
					place.changed++;
					frame.changed++;
					memory_changed = true;
				}
				if (!place.contents.insert(after).second)
					continue;

				frame.new_contents++;
				const u32 words = p.size / 4;
				if (place.first.empty())
				{
					place.first.resize(words);
					place.differs.assign(words, 0);
					for (u32 i = 0; i < words; i++)
						place.first[i] = WordAt(data, i);
				}
				else
				{
					for (u32 i = 0; i < words; i++)
						place.differs[i] += WordAt(data, i) != place.first[i];
				}
				WriteContent(vu, p.address, p.size, after, data);
			}
			pending.clear();
		}
	};

	Watch s_watch[2];

	void WriteVU(std::FILE* fp, const Watch& w, double tick_rate)
	{
		const Counts& c = w.total;
		const double seconds = c.ticks / tick_rate;
		std::fprintf(fp, "VU%d, micro memory of %u bytes, provider %s\n", w.vu, w.memory_size, w.real ? w.real->GetLongName() : "none");
		std::fprintf(fp, "  Writes into micro memory:          %s, %s bytes; after %s of them the place held something else\n",
			Grouped(c.writes).c_str(), Grouped(c.bytes).c_str(), Grouped(c.changed).c_str());
		std::fprintf(fp, "  Distinct contents written:         %s, at %s places (address and length)\n", Grouped(c.new_contents).c_str(),
			Grouped(w.places.size()).c_str());
		std::fprintf(fp, "  Programs started:                  %s, at %s addresses\n", Grouped(c.starts).c_str(),
			Grouped(w.start_addresses.size()).c_str());
		std::fprintf(fp, "  Distinct micro memory at a start:  %s; distinct (start address, micro memory): %s\n",
			Grouped(w.states.size()).c_str(), Grouped(c.new_states).c_str());
		std::fprintf(fp, "  Time running:                      %.2f s in %s calls; %s VU cycles\n", seconds, Grouped(c.runs).c_str(),
			Grouped(c.cycles).c_str());
		std::fprintf(fp, "  The provider reset:                %s times\n\n", Grouped(w.resets).c_str());

		// The places, most writes first.
		std::vector<std::pair<std::pair<u32, u32>, const Place*>> places;
		for (const auto& [key, place] : w.places)
			places.emplace_back(key, &place);
		std::stable_sort(places.begin(), places.end(), [](const auto& a, const auto& b) { return a.second->writes > b.second->writes; });
		std::fprintf(fp, "  The places written, most writes first: address, length, writes, of which changed what was there,\n"
						 "  distinct contents, and of the place's words those that ever differed from the first content's:\n");
		constexpr size_t kRows = 80;
		for (size_t i = 0; i < places.size() && i < kRows; i++)
		{
			const auto& [key, place] = places[i];
			const size_t varying = static_cast<size_t>(std::count_if(place->differs.begin(), place->differs.end(), [](u32 n) { return n != 0; }));
			std::fprintf(fp, "    %04x %6u %12s %12s %9s %7s of %s\n", key.first, key.second, Grouped(place->writes).c_str(),
				Grouped(place->changed).c_str(), Grouped(place->contents.size()).c_str(), Grouped(varying).c_str(),
				Grouped(place->differs.size()).c_str());
		}
		if (places.size() > kRows)
			std::fprintf(fp, "    and %s places more\n", Grouped(places.size() - kRows).c_str());

		// The start addresses, most time first.
		std::vector<std::pair<u32, const StartAddress*>> starts;
		for (const auto& [address, at] : w.start_addresses)
			starts.emplace_back(address, &at);
		std::stable_sort(starts.begin(), starts.end(), [](const auto& a, const auto& b) { return a.second->ticks > b.second->ticks; });
		std::fprintf(fp, "\n  The addresses programs were started at, most time first: address, starts, distinct micro memory\n"
						 "  at them, time running until the next start, its share of the VU's, VU cycles:\n");
		for (size_t i = 0; i < starts.size() && i < kRows; i++)
		{
			const auto& [address, at] = starts[i];
			std::fprintf(fp, "    %04x %12s %9s %9.3f s %6.2f%% %16s\n", address, Grouped(at->starts).c_str(), Grouped(at->states.size()).c_str(),
				at->ticks / tick_rate, c.ticks ? 100.0 * at->ticks / c.ticks : 0.0, Grouped(at->cycles).c_str());
		}
		if (starts.size() > kRows)
			std::fprintf(fp, "    and %s addresses more\n", Grouped(starts.size() - kRows).c_str());
		std::fputc('\n', fp);
	}
} // namespace

bool ps2n::host::vureport::Start(const char* path)
{
	s_path = path;
	const size_t slash = s_path.find_last_of("/\\");
	const size_t dot = s_path.find_last_of('.');
	const std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? s_path.substr(0, dot) : s_path;

	s_frames_file = std::fopen((stem + "-frames.csv").c_str(), "wb");
	s_uploads_file = std::fopen((stem + "-uploads.bin").c_str(), "wb");
	if (!s_frames_file || !s_uploads_file)
	{
		if (s_frames_file)
			std::fclose(s_frames_file);
		if (s_uploads_file)
			std::fclose(s_uploads_file);
		s_frames_file = s_uploads_file = nullptr;
		return false;
	}
	std::setvbuf(s_frames_file, nullptr, _IOFBF, 1 << 20);
	std::setvbuf(s_uploads_file, nullptr, _IOFBF, 1 << 20);

	std::fputs("frame", s_frames_file);
	for (int vu = 0; vu < 2; vu++)
	{
		for (const char* name : {"writes", "bytes", "changed", "new_contents", "starts", "new_states", "runs", "ticks", "cycles"})
			std::fprintf(s_frames_file, ",vu%d_%s", vu, name);
	}
	std::fputc('\n', s_frames_file);

	static const char kMagic[16] = "ps2n-vu-uploads";
	std::fwrite(kMagic, sizeof(kMagic), 1, s_uploads_file);

	s_active = true;
	s_tsc_start = __rdtsc();
	s_clock_start = std::chrono::steady_clock::now();
	return true;
}

void ps2n::host::vureport::Install()
{
	if (!s_active)
		return;
	for (int vu = 0; vu < 2; vu++)
	{
		BaseVUmicroCPU*& pointer = vu ? CpuVU1 : CpuVU0;
		Watch& w = s_watch[vu];
		if (!pointer || pointer == &w)
			continue;
		w.real = pointer;
		w.vu = vu;
		w.regs = vu ? &VU1 : &VU0;
		w.memory_size = vu ? VU1_PROGSIZE : VU0_PROGSIZE;
		w.m_Idx = pointer->m_Idx;
		w.IsInterpreter = pointer->IsInterpreter;
		pointer = &w;
	}
}

bool ps2n::host::vureport::Installed()
{
	return !s_active || (CpuVU0 == &s_watch[0] && CpuVU1 == &s_watch[1]);
}

void ps2n::host::vureport::Frame(uint32_t frame)
{
	if (!s_active)
		return;
	s_frame = frame;
	if (s_frames_file)
	{
		std::fprintf(s_frames_file, "%u", frame);
		for (const Watch& w : s_watch)
		{
			const Counts& c = w.frame;
			std::fprintf(s_frames_file, ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu", ull(c.writes), ull(c.bytes), ull(c.changed),
				ull(c.new_contents), ull(c.starts), ull(c.new_states), ull(c.runs), ull(c.ticks), ull(c.cycles));
		}
		std::fputc('\n', s_frames_file);
	}
	for (Watch& w : s_watch)
	{
		w.total.Add(w.frame);
		w.frame = {};
	}
	if (frame % 100 == 0)
		Console.WriteLn("ps2n-host: vu-report: frame %u", frame);
}

bool ps2n::host::vureport::Stop(uint32_t frames)
{
	if (!s_active)
		return true;
	s_active = false;

	for (int vu = 0; vu < 2; vu++)
	{
		BaseVUmicroCPU*& pointer = vu ? CpuVU1 : CpuVU0;
		Watch& w = s_watch[vu];
		if (pointer == &w)
			pointer = w.real;
		if (w.real)
			w.Settle();
		w.total.Add(w.frame);
		w.frame = {};
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_clock_start).count();
	const double tick_rate = seconds > 0.0 ? static_cast<double>(__rdtsc() - s_tsc_start) / seconds : 1.0;

	if (s_frames_file)
		std::fclose(s_frames_file);
	if (s_uploads_file)
		std::fclose(s_uploads_file);
	s_frames_file = s_uploads_file = nullptr;

	std::FILE* fp = std::fopen(s_path.c_str(), "wb");
	if (!fp)
		return false;
	std::fprintf(fp, "# What the vector units ran (ps2n-host -vu-report), over %u frames.\n", frames);
	std::fprintf(fp, "# Time is the host's in each VU's provider, from the time-stamp counter (%.3f GHz over the run); one\n"
					 "# provider running inside the other's is counted for the inner one only. Cycles are the VU's own.\n",
		tick_rate / 1e9);
	std::fprintf(fp, "# What was written and run is the game's: here are only addresses and counts. The contents are in the\n"
					 "# file of uploads beside this one%s.\n\n",
		s_uploads_full ? " (cut short: it reached its limit)" : "");
	for (int vu = 1; vu >= 0; vu--)
		WriteVU(fp, s_watch[vu], tick_rate);
	const bool ok = std::ferror(fp) == 0;
	std::fclose(fp);
	return ok;
}
