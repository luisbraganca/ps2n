// SPDX-License-Identifier: GPL-3.0-or-later
//
// See sync_trace.h.

#include "sync_trace.h"

#include "ps2n/r5900.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/VU.h"

#include "fmt/format.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	using ull = unsigned long long;

	std::FILE* s_file = nullptr;
	std::FILE* s_reference = nullptr;
	u64 s_detail_from = ~0ull;
	u64 s_count = 0;
	bool s_active = false;

	std::string s_time_mismatch;
	std::string s_state_mismatch;

	// The IOP provider the core chose, and what the core calls instead.
	R3000Acpu* s_real = nullptr;
	R3000Acpu s_wrapper;

	// The last record that was the same in both runs: where the stretch in
	// which they part begins.
	std::string s_last_same;

	struct Fold
	{
		u64 h = 14695981039346656037ull;

		void add(u64 v)
		{
			h = (h ^ v) * 1099511628211ull;
			h ^= h >> 29;
		}
		void add(const void* data, size_t size)
		{
			const u8* p = static_cast<const u8*>(data);
			for (; size >= 8; size -= 8, p += 8)
			{
				u64 v;
				std::memcpy(&v, p, 8);
				add(v);
			}
			if (size)
			{
				u64 v = 0;
				std::memcpy(&v, p, size);
				add(v);
			}
		}
		u32 value() const { return static_cast<u32>(h ^ (h >> 32)); }
	};

	// What a read of the count register would give at this moment. The core
	// brings the register itself up to date only now and then, at moments that
	// depend on the EE provider.
	u32 CountNow()
	{
		return cpuRegs.CP0.n.Count + static_cast<u32>(cpuRegs.cycle - cpuRegs.lastCOP0Cycle);
	}

	void WriteDetail()
	{
		// Lines that begin with a space are for reading, not comparing.
		for (u32 r = 0; r < 32; r++)
		{
			std::fprintf(s_file, "%s%s=%016llX:%016llX%s", (r % 4) ? " " : "  ", ps2n::r5900::gprName(r), static_cast<ull>(cpuRegs.GPR.r[r].UD[1]),
				static_cast<ull>(cpuRegs.GPR.r[r].UD[0]), (r % 4 == 3) ? "\n" : "");
		}
		std::fprintf(s_file, "  hi=%016llX:%016llX lo=%016llX:%016llX sa=%X\n", static_cast<ull>(cpuRegs.HI.UD[1]),
			static_cast<ull>(cpuRegs.HI.UD[0]), static_cast<ull>(cpuRegs.LO.UD[1]), static_cast<ull>(cpuRegs.LO.UD[0]), cpuRegs.sa);
		std::fputs("  cp0", s_file);
		for (u32 r = 0; r < 32; r++)
			std::fprintf(s_file, " %08X", r == 9 ? CountNow() : cpuRegs.CP0.r[r]);
		std::fputs("\n  fpr", s_file);
		for (u32 r = 0; r < 32; r++)
			std::fprintf(s_file, " %08X", fpuRegs.fpr[r].UL);
		std::fprintf(s_file, "\n  acc=%08X fcr0=%08X fcr31=%08X\n  vi ", fpuRegs.ACC.UL, fpuRegs.fprc[0], fpuRegs.fprc[31]);
		for (u32 r = 0; r < 32; r++)
			std::fprintf(s_file, " %08X", VU0.VI[r].UL);
		std::fputs("\n", s_file);
		for (u32 r = 0; r < 32; r++)
		{
			std::fprintf(s_file, "%svf%02u=%08X.%08X.%08X.%08X%s", (r % 4) ? " " : "  ", r, VU0.VF[r].UL[0], VU0.VF[r].UL[1], VU0.VF[r].UL[2],
				VU0.VF[r].UL[3], (r % 4 == 3) ? "\n" : "");
		}
		std::fprintf(s_file, "  vacc=%08X.%08X.%08X.%08X q=%08X p=%08X\n", VU0.ACC.UL[0], VU0.ACC.UL[1], VU0.ACC.UL[2], VU0.ACC.UL[3], VU0.q.UL,
			VU0.p.UL);
	}

	// One sync point: the core is about to let the IOP run for `ee_cycles`
	// cycles of the EE's clock.
	void Record(s32 ee_cycles)
	{
		Fold gpr;
		gpr.add(&cpuRegs.GPR, sizeof(cpuRegs.GPR));
		gpr.add(&cpuRegs.HI, sizeof(cpuRegs.HI));
		gpr.add(&cpuRegs.LO, sizeof(cpuRegs.LO));
		gpr.add(cpuRegs.sa);

		Fold fpu;
		fpu.add(&fpuRegs.fpr, sizeof(fpuRegs.fpr));
		fpu.add(fpuRegs.ACC.UL);
		fpu.add(fpuRegs.fprc[31]);

		Fold cp0;
		for (u32 r = 0; r < 32; r++)
			cp0.add(r == 9 ? CountNow() : cpuRegs.CP0.r[r]);

		Fold vu;
		vu.add(&VU0.VF, sizeof(VU0.VF));
		vu.add(&VU0.VI, sizeof(VU0.VI));
		vu.add(&VU0.ACC, sizeof(VU0.ACC));
		vu.add(VU0.q.UL);
		vu.add(VU0.p.UL);

		// number, PC, EE cycle, cycles the IOP is given, IOP cycle; then the
		// registers, folded: general, FPU, COP0, VU0.
		char time[96];
		std::snprintf(time, sizeof(time), "%llu %08X %llu %d %llu", static_cast<ull>(s_count), cpuRegs.pc, static_cast<ull>(cpuRegs.cycle),
			ee_cycles, static_cast<ull>(psxRegs.cycle));
		char state[48];
		std::snprintf(state, sizeof(state), "%08X %08X %08X %08X", gpr.value(), fpu.value(), cp0.value(), vu.value());

		if (s_file)
		{
			std::fprintf(s_file, "%s %s\n", time, state);
			if (s_count >= s_detail_from)
				WriteDetail();
		}

		if (s_reference && (s_time_mismatch.empty() || s_state_mismatch.empty()))
		{
			char reference[4096];
			bool have = false;
			while (std::fgets(reference, sizeof(reference), s_reference))
			{
				if (reference[0] != ' ')
				{
					have = true;
					break;
				}
			}

			const std::string mine = fmt::format("{} {}", time, state);
			if (!have)
			{
				if (s_time_mismatch.empty())
					s_time_mismatch = fmt::format("the reference run had no sync point {}; this run: \"{}\"", s_count, mine);
			}
			else
			{
				reference[std::strcspn(reference, "\r\n")] = 0;
				const size_t time_length = std::strlen(time);
				const bool same_time = std::strncmp(reference, time, time_length) == 0 && reference[time_length] == ' ';
				if (!same_time && s_time_mismatch.empty())
				{
					s_time_mismatch = fmt::format("at sync point {}: reference \"{}\", this run \"{}\"; the last one that was the same: \"{}\"",
						s_count, reference, mine, s_last_same);
				}
				else if (same_time && s_state_mismatch.empty() && s_time_mismatch.empty() && mine != reference)
				{
					s_state_mismatch = fmt::format("at sync point {}: reference \"{}\", this run \"{}\"; the last one that was the same: \"{}\"",
						s_count, reference, mine, s_last_same);
				}
				if (mine == reference)
					s_last_same = mine;
			}
		}

		s_count++;
	}

	s32 ExecuteBlockTraced(s32 ee_cycles)
	{
		Record(ee_cycles);
		return s_real->ExecuteBlock(ee_cycles);
	}
} // namespace

bool ps2n::host::StartSyncTrace(const char* write_path, const char* against_path, uint64_t detail_from)
{
	s_detail_from = detail_from;
	if (write_path)
	{
		s_file = std::fopen(write_path, "wb");
		if (!s_file)
			return false;
		std::setvbuf(s_file, nullptr, _IOFBF, 1 << 20);
		std::fputs("# sync point, PC, EE cycle, EE cycles the IOP is given, IOP cycle; registers folded: general, FPU, COP0, VU0\n", s_file);
	}
	if (against_path)
	{
		s_reference = std::fopen(against_path, "rb");
		if (!s_reference)
			return false;
		std::setvbuf(s_reference, nullptr, _IOFBF, 1 << 20);

		// The heading.
		char line[256];
		const long start = std::ftell(s_reference);
		if (std::fgets(line, sizeof(line), s_reference) && line[0] != '#')
			std::fseek(s_reference, start, SEEK_SET);
	}
	s_active = (s_file || s_reference);
	return true;
}

void ps2n::host::InstallSyncTrace()
{
	if (!s_active || !psxCpu || psxCpu == &s_wrapper)
		return;

	s_real = psxCpu;
	s_wrapper = *psxCpu;
	s_wrapper.ExecuteBlock = ExecuteBlockTraced;
	psxCpu = &s_wrapper;
}

bool ps2n::host::SyncTraceInstalled()
{
	return !s_active || psxCpu == &s_wrapper;
}

void ps2n::host::StopSyncTrace()
{
	if (psxCpu == &s_wrapper)
		psxCpu = s_real;

	if (s_file)
	{
		std::fclose(s_file);
		s_file = nullptr;
	}
	if (s_reference)
	{
		char reference[4096];
		while (s_time_mismatch.empty() && std::fgets(reference, sizeof(reference), s_reference))
		{
			if (reference[0] != ' ')
			{
				reference[std::strcspn(reference, "\r\n")] = 0;
				s_time_mismatch =
					fmt::format("this run stopped after sync point {}; the reference run went on: \"{}\"", s_count - 1, reference);
			}
		}
		std::fclose(s_reference);
		s_reference = nullptr;
	}
	s_active = false;
}

const char* ps2n::host::GetSyncTimeMismatch()
{
	return s_time_mismatch.empty() ? nullptr : s_time_mismatch.c_str();
}

const char* ps2n::host::GetSyncStateMismatch()
{
	return s_state_mismatch.empty() ? nullptr : s_state_mismatch.c_str();
}

uint64_t ps2n::host::GetSyncPointCount()
{
	return s_count;
}
