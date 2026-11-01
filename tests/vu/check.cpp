// Recompiled VU micro programs against the interpreter. generate.cpp writes
// random VU1 and VU0 programs, `ps2n vu` compiles them at build time, and
// each entry runs from the same random states twice: interpreted, and
// recompiled. Every register, data memory, VU1's registers (VU0 reaches them
// through its window) and the cycle count must match, and enough of each
// unit's runs must finish in compiled code for the test to mean something.
// usage: ps2n_vu_tests <images.bin>
#include "fuzz.h"

#include "ps2/ops_vu0.h"
#include "ps2/vu_recomp.h"
#include "vu.h"

#include <algorithm>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vufuzz {
extern const ps2::vu::CompiledProgram kVuPrograms[];
extern const size_t kVuProgramCount;
}

namespace {

using namespace ps2;

uint8_t* dataOf(int unit) { return unit ? vu1Mem : vu0Mem; }
size_t dataSize(int unit) { return unit ? sizeof vu1Mem : sizeof vu0Mem; }

struct Snapshot {
    int unit = 1;
    uint32_t vf[32][4], acc[4], vi[32];
    uint32_t vu1Vf[32][4], vu1Vi[32];   // VU1's registers, which VU0's window reaches
    std::vector<uint8_t> mem;
    uint64_t cycles = 0;
};
Snapshot capture(int unit, uint64_t cycle0) {
    const vu::Unit& u = vu::unit(unit);
    const vu::Unit& vu1 = vu::unit(1);
    Snapshot s;
    s.unit = unit;
    std::memcpy(s.vf, u.vf, sizeof s.vf);
    std::memcpy(s.acc, u.acc, sizeof s.acc);
    std::memcpy(s.vi, u.vi, sizeof s.vi);
    std::memcpy(s.vu1Vf, vu1.vf, sizeof s.vu1Vf);
    std::memcpy(s.vu1Vi, vu1.vi, sizeof s.vu1Vi);
    s.mem.assign(dataOf(unit), dataOf(unit) + dataSize(unit));
    s.cycles = u.t.cycle - cycle0;
    return s;
}
void restore(const Snapshot& s) {
    vu::Unit& u = vu::unit(s.unit);
    vu::Unit& vu1 = vu::unit(1);
    std::memcpy(vu1.vf, s.vu1Vf, sizeof s.vu1Vf);   // before the unit's own, which for VU1 are the same
    std::memcpy(vu1.vi, s.vu1Vi, sizeof s.vu1Vi);
    std::memcpy(u.vf, s.vf, sizeof s.vf);
    std::memcpy(u.acc, s.acc, sizeof s.acc);
    std::memcpy(u.vi, s.vi, sizeof s.vi);
    std::memcpy(dataOf(s.unit), s.mem.data(), s.mem.size());
}
std::string differences(const Snapshot& a, const Snapshot& b) {
    std::string d;
    if (std::memcmp(a.vf, b.vf, sizeof a.vf)) d += " vf";
    if (std::memcmp(a.acc, b.acc, sizeof a.acc)) d += " acc";
    for (int r = 0; r < 32; ++r)
        if (a.vi[r] != b.vi[r]) d += " vi" + std::to_string(r);
    if (a.unit == 0) {
        if (std::memcmp(a.vu1Vf, b.vu1Vf, sizeof a.vu1Vf)) d += " VU1's vf";
        for (int r = 0; r < 32; ++r)
            if (a.vu1Vi[r] != b.vu1Vi[r]) d += " VU1's vi" + std::to_string(r);
    }
    if (a.mem != b.mem) d += " memory";
    if (a.cycles != b.cycles) d += " cycles (" + std::to_string(a.cycles) + " interpreted, " + std::to_string(b.cycles) + " compiled)";
    return d;
}

// Every compiled entry goes through here, to count how the compiled code ended.
std::map<std::pair<int, uint32_t>, vu::ProgramFn> g_compiled;
long g_ended[2] = {}, g_handedOver[2] = {};
uint32_t counted(vu::Unit& u, uint32_t entry) {
    const uint32_t r = g_compiled.at({u.index, entry})(u, entry);
    if (r == vu::kEnded) ++g_ended[u.index];
    else if (r != vu::kNotRun) ++g_handedOver[u.index];
    return r;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <images.bin>\n", argv[0]);
        return 2;
    }
    FILE* bin = std::fopen(argv[1], "rb");
    if (!bin) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::fesetround(FE_TOWARDZERO);
    cpu.vf[0].c[3] = 0x3F800000u;   // VU0's vf0, as start-up sets it
    vu::g_stepLimit = 5000;   // random programs loop: stop them early (each stop logs an error)
    std::vector<vu::CompiledProgram> table;
    for (size_t i = 0; i < vufuzz::kVuProgramCount; ++i) {
        const vu::CompiledProgram& p = vufuzz::kVuPrograms[i];
        g_compiled[{p.unit, p.entry}] = p.fn;
        table.push_back({p.unit, p.entry, &counted});
    }
    vu::registerPrograms(table.data(), table.size());
    vufuzz::Random random(0x5EED0002);
    random.memory(vu1Mem, sizeof vu1Mem);
    vufuzz::Random vu0Memory(0x5EED0003);   // apart from `random`, which the VU1 runs draw from first
    vu0Memory.memory(vu0Mem, sizeof vu0Mem);
    struct Image {
        int unit = 1;
        std::vector<uint32_t> entries;
        std::vector<uint8_t> micro;
    };
    std::vector<Image> images;
    for (uint32_t unit, n; std::fread(&unit, 4, 1, bin) == 1 && unit <= 1 && std::fread(&n, 4, 1, bin) == 1 && n <= 64;) {
        Image im;
        im.unit = static_cast<int>(unit);
        im.entries.resize(n);
        im.micro.resize(unit ? sizeof vu1Micro : sizeof vu0Micro);
        if (std::fread(im.entries.data(), 4, n, bin) != n || std::fread(im.micro.data(), 1, im.micro.size(), bin) != im.micro.size()) break;
        images.push_back(std::move(im));
    }
    auto load = [](const Image& im) {
        std::memcpy(im.unit ? vu1Micro : vu0Micro, im.micro.data(), im.micro.size());
        vu::microChanged(im.unit);
    };
    auto start = [](int unit, uint32_t entry) {
        if (unit) vu1Start(entry);
        else vu0CallMicro(entry);
    };
    long runs[2] = {}, mismatches = 0;
    // One program from the same random state both ways.
    auto runBoth = [&](size_t image, uint32_t entry, int run) {
        const int unit = images[image].unit;
        vu::Unit& u = vu::unit(unit);
        random.registers(reinterpret_cast<uint32_t(*)[4]>(u.vf), reinterpret_cast<uint32_t*>(u.acc), u.vi);
        if (unit == 0) {
            vu::Unit& vu1 = vu::unit(1);
            random.registers(reinterpret_cast<uint32_t(*)[4]>(vu1.vf), reinterpret_cast<uint32_t*>(vu1.acc), vu1.vi);
        }
        if (random.chance(30)) random.memory(dataOf(unit), dataSize(unit));
        const Snapshot before = capture(unit, 0);
        vu::useCompiledPrograms(false);
        uint64_t cycle0 = u.t.cycle;
        start(unit, entry);
        const Snapshot interpreted = capture(unit, cycle0);
        restore(before);
        vu::useCompiledPrograms(true);
        cycle0 = u.t.cycle;
        start(unit, entry);
        const Snapshot compiled = capture(unit, cycle0);
        ++runs[unit];
        const std::string d = differences(interpreted, compiled);
        if (!d.empty() && ++mismatches <= 10)
            std::printf("image %zu (VU%d), entry 0x%04x, run %d: differs in%s\n", image, unit, entry, run, d.c_str());
        restore(interpreted);
    };
    constexpr int kRunsPerEntry = 20;
    long directedEnded = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        load(images[i]);
        const long before = g_ended[1];
        for (const uint32_t entry : images[i].entries)
            for (int run = 0; run < kRunsPerEntry; ++run) runBoth(i, entry, run);
        if (i == 0) directedEnded = g_ended[1] - before;
    }
    for (int unit = 1; unit >= 0; --unit)
        std::printf("VU%d: %ld runs; compiled code ended %ld of them and handed %ld over to the interpreter\n", unit, runs[unit],
                    g_ended[unit], g_handedOver[unit]);
    std::printf("%ld runs differ\n", mismatches);
    for (int unit = 0; unit < 2; ++unit)
        if (runs[unit] == 0 || g_ended[unit] < runs[unit] / 4) {
            std::printf("too few VU%d runs ended in compiled code to test it\n", unit);
            return 1;
        }
    // The first image is written by hand: all of its programs end recompiled.
    if (!images.empty() && directedEnded != static_cast<long>(images[0].entries.size()) * kRunsPerEntry) {
        std::printf("the hand-written programs ended recompiled %ld times of %ld\n", directedEnded,
                    static_cast<long>(images[0].entries.size()) * kRunsPerEntry);
        return 1;
    }
    // Recompiled code runs only where micro memory holds it: with each image
    // loaded, the other images' entries for the same unit run on the interpreter.
    const long endedBefore = g_ended[0] + g_ended[1], handedBefore = g_handedOver[0] + g_handedOver[1];
    for (size_t i = 0; i < images.size(); ++i) {
        load(images[i]);
        for (size_t j = 0; j < images.size(); ++j)
            if (j != i && images[j].unit == images[i].unit)
                for (const uint32_t entry : images[j].entries)
                    if (std::find(images[i].entries.begin(), images[i].entries.end(), entry) == images[i].entries.end())
                        runBoth(i, entry, 0);
    }
    const long ended = g_ended[0] + g_ended[1] - endedBefore, handed = g_handedOver[0] + g_handedOver[1] - handedBefore;
    if (ended || handed) {
        std::printf("recompiled code ran %ld times over micro memory that holds other code\n", ended + handed);
        return 1;
    }
    return mismatches ? 1 : 0;
}
