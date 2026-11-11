// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guest memory access for PS2Native's instruction semantics.
//
// Memory belongs to the PCSX2 core. Every access goes through the core's
// virtual-address table (vtlb), which follows the mappings the BIOS kernel
// sets up: a page is either host memory, read and written in place, or a
// hardware register range, served by one of the core's handlers.
//
// The table lookup is done here, inline, so that an access to RAM costs a
// table load, an add and a test. PCSX2's own accessors do the same lookup out
// of line (vtlb_memRead / vtlb_memWrite in pcsx2/vtlb.cpp); they are called
// for the handler case only.
//
// As in PCSX2's recompiler: a misaligned address raises no exception, and an
// access is not checked for crossing into the next page.
#pragma once

#include "vtlb.h"

#include <cstdint>
#include <cstring>

namespace ps2n::ee
{
	namespace detail
	{
		template <typename T>
		inline T memRead(uint32_t addr)
		{
			const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
			if (!entry.isHandler(addr)) [[likely]]
			{
				T value;
				std::memcpy(&value, reinterpret_cast<const void*>(entry.assumePtr(addr)), sizeof(T));
				return value;
			}
			return vtlb_memRead<T>(addr);
		}

		template <typename T>
		inline void memWrite(uint32_t addr, T value)
		{
			const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
			if (!entry.isHandler(addr)) [[likely]]
			{
				std::memcpy(reinterpret_cast<void*>(entry.assumePtr(addr)), &value, sizeof(T));
				return;
			}
			vtlb_memWrite<T>(addr, value);
		}
	} // namespace detail

	inline uint8_t read8(uint32_t addr) { return detail::memRead<mem8_t>(addr); }
	inline uint16_t read16(uint32_t addr) { return detail::memRead<mem16_t>(addr); }
	inline uint32_t read32(uint32_t addr) { return detail::memRead<mem32_t>(addr); }
	inline uint64_t read64(uint32_t addr) { return detail::memRead<mem64_t>(addr); }

	inline void write8(uint32_t addr, uint8_t value) { detail::memWrite<mem8_t>(addr, value); }
	inline void write16(uint32_t addr, uint16_t value) { detail::memWrite<mem16_t>(addr, value); }
	inline void write32(uint32_t addr, uint32_t value) { detail::memWrite<mem32_t>(addr, value); }
	inline void write64(uint32_t addr, uint64_t value) { detail::memWrite<mem64_t>(addr, value); }

	// Quadword accesses ignore the low four address bits, as LQ and SQ do.
	// out[0] and in[0] are bits 0-63.
	inline void read128(uint32_t addr, uint64_t out[2])
	{
		addr &= ~15u;
		const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
		if (!entry.isHandler(addr)) [[likely]]
		{
			std::memcpy(out, reinterpret_cast<const void*>(entry.assumePtr(addr)), 16);
			return;
		}
		r128_store_unaligned(out, vtlb_memRead128(addr));
	}

	inline void write128(uint32_t addr, const uint64_t in[2])
	{
		addr &= ~15u;
		const auto entry = vtlb_private::vtlbdata.vmap[addr >> vtlb_private::VTLB_PAGE_BITS];
		if (!entry.isHandler(addr)) [[likely]]
		{
			std::memcpy(reinterpret_cast<void*>(entry.assumePtr(addr)), in, 16);
			return;
		}
		alignas(16) const uint64_t aligned[2] = {in[0], in[1]};
		vtlb_memWrite128(addr, r128_load(aligned));
	}
} // namespace ps2n::ee
