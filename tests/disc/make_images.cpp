// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small disc of our own, written in three of the formats PCSX2 plays, for the
// test of the runtime's reading of a disc through PCSX2's readers
// (tests/disc_test.cmake; host/disc.h): an .iso of 2048-byte sectors; a raw
// .bin of 2352-byte sectors (mode 2, form 1: sync, header, subheader, the data,
// no EDC or ECC, which PCSX2 does not check); and a .cso (version 1), its
// blocks deflated as stored blocks, which need no compressor, but for one kept
// plain, as a CSO marks a block that does not compress.
//
// The disc: SYSTEM.CNF booting SLES_000.00, an executable of the EE's; SUB with
// GAME.ELF, another, and MOD.IRX, an IOP module; BIG.BIN over two sectors.
// Beside the images: the boot executable's bytes, to compare what is
// extracted with (boot.expected).
//
//   ps2n-disc-images <folder>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr uint32_t kSector = 2048;
	constexpr uint32_t kSectors = 26;

	void Both32(uint8_t* p, uint32_t v)
	{
		for (int i = 0; i < 4; i++)
		{
			p[i] = static_cast<uint8_t>(v >> (8 * i));
			p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
		}
	}

	size_t Record(uint8_t* at, std::string_view name, uint32_t where, uint32_t size, bool directory)
	{
		const size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
		at[0] = static_cast<uint8_t>(length);
		Both32(at + 2, where);
		Both32(at + 10, size);
		at[25] = directory ? 2 : 0;
		at[32] = static_cast<uint8_t>(name.size());
		std::memcpy(at + 33, name.data(), name.size());
		return length;
	}

	void ElfHeader(uint8_t* p, uint16_t type, size_t size)
	{
		const uint8_t ident[] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
		std::memcpy(p, ident, sizeof(ident));
		p[16] = static_cast<uint8_t>(type);
		p[17] = static_cast<uint8_t>(type >> 8);
		p[18] = 8; // MIPS
		for (size_t i = 52; i < size; i++)
			p[i] = static_cast<uint8_t>(i * 13);
	}

	bool Write(const std::string& path, const std::vector<uint8_t>& bytes)
	{
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return static_cast<bool>(out);
	}

	void Put32(std::vector<uint8_t>* out, uint32_t v)
	{
		for (int i = 0; i < 4; i++)
			out->push_back(static_cast<uint8_t>(v >> (8 * i)));
	}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
		return 4;
	const std::string folder = argv[1];

	std::vector<uint8_t> image(kSectors * kSector, 0);
	const auto sector = [&](uint32_t n) { return image.data() + n * kSector; };
	constexpr uint32_t kBootSize = 300;
	const std::string cnf = "BOOT2 = cdrom0:\\SLES_000.00;1\r\nVER = 1.00\r\nVMODE = PAL\r\n";

	uint8_t* pvd = sector(16);
	pvd[0] = 1;
	std::memcpy(pvd + 1, "CD001", 5);
	pvd[6] = 1;
	Both32(pvd + 80, kSectors);
	pvd[128] = 0x00; // 2048, in both byte orders
	pvd[129] = 0x08;
	pvd[130] = 0x08;
	pvd[131] = 0x00;
	Record(pvd + 156, std::string_view("\0", 1), 18, kSector, true);
	uint8_t* terminator = sector(17);
	terminator[0] = 255;
	std::memcpy(terminator + 1, "CD001", 5);
	terminator[6] = 1;

	size_t at = 0;
	at += Record(sector(18) + at, std::string_view("\0", 1), 18, kSector, true);
	at += Record(sector(18) + at, std::string_view("\1", 1), 18, kSector, true);
	at += Record(sector(18) + at, "SYSTEM.CNF;1", 20, static_cast<uint32_t>(cnf.size()), false);
	at += Record(sector(18) + at, "SLES_000.00;1", 21, kBootSize, false);
	at += Record(sector(18) + at, "SUB", 19, kSector, true);
	at = 0;
	at += Record(sector(19) + at, std::string_view("\0", 1), 19, kSector, true);
	at += Record(sector(19) + at, std::string_view("\1", 1), 18, kSector, true);
	at += Record(sector(19) + at, "BIG.BIN;1", 22, kSector + 10, false);
	at += Record(sector(19) + at, "GAME.ELF;1", 24, 64, false);
	at += Record(sector(19) + at, "MOD.IRX;1", 25, 64, false);
	std::memcpy(sector(20), cnf.data(), cnf.size());
	ElfHeader(sector(21), 2, kBootSize);
	for (uint32_t i = 0; i < kSector + 10; i++)
		sector(22)[i] = static_cast<uint8_t>(i * 7);
	ElfHeader(sector(24), 2, 64);
	ElfHeader(sector(25), 0xFF80, 64);

	// The .iso, and the boot executable's bytes.
	if (!Write(folder + "/disc.iso", image) || !Write(folder + "/boot.expected", std::vector<uint8_t>(sector(21), sector(21) + kBootSize)))
		return 1;

	// The raw .bin.
	std::vector<uint8_t> raw;
	for (uint32_t s = 0; s < kSectors; s++)
	{
		uint8_t frame[2352] = {};
		std::memset(frame + 1, 0xFF, 10);
		const uint32_t lba = s + 150;
		const uint32_t m = lba / (60 * 75), sec = lba / 75 % 60, f = lba % 75;
		frame[12] = static_cast<uint8_t>((m / 10) << 4 | m % 10);
		frame[13] = static_cast<uint8_t>((sec / 10) << 4 | sec % 10);
		frame[14] = static_cast<uint8_t>((f / 10) << 4 | f % 10);
		frame[15] = 2; // mode 2
		frame[18] = 8; // form 1, data
		frame[22] = 8;
		std::memcpy(frame + 24, sector(s), kSector);
		raw.insert(raw.end(), frame, frame + sizeof(frame));
	}
	if (!Write(folder + "/disc.bin", raw))
		return 1;

	// The .cso: a header, an index of where each block begins (its top bit set
	// for a block kept plain), then the blocks, each one deflate block stored.
	std::vector<uint8_t> cso = {'C', 'I', 'S', 'O'};
	Put32(&cso, 0x18);
	Put32(&cso, static_cast<uint32_t>(image.size()));
	Put32(&cso, 0);
	Put32(&cso, kSector);
	cso.push_back(1); // version
	cso.push_back(0); // the index's alignment
	cso.push_back(0);
	cso.push_back(0);
	const size_t index = cso.size();
	cso.resize(index + (kSectors + 1) * 4);
	for (uint32_t s = 0; s <= kSectors; s++)
	{
		uint32_t where = static_cast<uint32_t>(cso.size());
		if (s == 21)
			where |= 0x80000000u; // the boot executable's sector, plain
		std::memcpy(cso.data() + index + s * 4, &where, 4);
		if (s == kSectors)
			break;
		if (s != 21)
		{
			const uint8_t stored[] = {0x01, 0x00, 0x08, 0xFF, 0xF7}; // the last block, stored, 2048 bytes
			cso.insert(cso.end(), stored, stored + sizeof(stored));
		}
		cso.insert(cso.end(), sector(s), sector(s) + kSector);
	}
	if (!Write(folder + "/disc.cso", cso))
		return 1;
	return 0;
}
