// Recompiled VU micro programs against the interpreter. generate.cpp writes
// random VU1 programs, `ps2n vu` compiles them at build time, and each entry
// runs from the same random states twice: interpreted, and recompiled. Every
// register, data memory and the cycle count must match, and enough of the
// runs must finish in compiled code for the test to mean something.
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

struct Snapshot {
    uint32_t vf[32][4], acc[4], vi[32];
    std::vector<uint8_t> mem;
    uint64_t cycles = 0;
};
Snapshot capture(uint64_t cycle0) {
    vu::Unit& u = vu::unit(1);
    Snapshot s;
    std::memcpy(s.vf, u.vf, sizeof s.vf);
    std::memcpy(s.acc, u.acc, sizeof s.acc);
    std::memcpy(s.vi, u.vi, sizeof s.vi);
    s.mem.assign(vu1Mem, vu1Mem + sizeof vu1Mem);
    s.cycles = u.t.cycle - cycle0;
    return s;
}
void restore(const Snapshot& s) {
    vu::Unit& u = vu::unit(1);
    std::memcpy(u.vf, s.vf, sizeof s.vf);
    std::memcpy(u.acc, s.acc, sizeof s.acc);
    std::memcpy(u.vi, s.vi, sizeof s.vi);
    std::memcpy(vu1Mem, s.mem.data(), s.mem.size());
}
std::string differences(const Snapshot& a, const Snapshot& b) {
    std::string d;
    if (std::memcmp(a.vf, b.vf, sizeof a.vf)) d += " vf";
    if (std::memcmp(a.acc, b.acc, sizeof a.acc)) d += " acc";
    for (int r = 0; r < 32; ++r)
        if (a.vi[r] != b.vi[r]) d += " vi" + std::to_string(r);
    if (a.mem != b.mem) d += " memory";
    if (a.cycles != b.cycles) d += " cycles (" + std::to_string(a.cycles) + " interpreted, " + std::to_string(b.cycles) + " compiled)";
    return d;
}

// Every compiled entry goes through here, to count how the compiled code ended.
std::map<std::pair<int, uint32_t>, vu::ProgramFn> g_compiled;
long g_ended = 0, g_handedOver = 0;
uint32_t counted(vu::Unit& u, uint32_t entry) {
    const uint32_t r = g_compiled.at({u.index, entry})(u, entry);
    if (r == vu::kEnded) ++g_ended;
    else if (r != vu::kNotRun) ++g_handedOver;
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
    struct Image {
        std::vector<uint32_t> entries;
        std::vector<uint8_t> micro;
    };
    std::vector<Image> images;
    for (uint32_t n; std::fread(&n, 4, 1, bin) == 1 && n <= 64;) {
        Image im;
        im.entries.resize(n);
        im.micro.resize(sizeof vu1Micro);
        if (std::fread(im.entries.data(), 4, n, bin) != n || std::fread(im.micro.data(), 1, im.micro.size(), bin) != im.micro.size()) break;
        images.push_back(std::move(im));
    }
    auto load = [](const Image& im) {
        std::memcpy(vu1Micro, im.micro.data(), sizeof vu1Micro);
        vu::microChanged(1);
    };
    long runs = 0, mismatches = 0;
    // One program from the same random state both ways.
    auto runBoth = [&](size_t image, uint32_t entry, int run) {
        vu::Unit& u = vu::unit(1);
        random.registers(reinterpret_cast<uint32_t(*)[4]>(u.vf), reinterpret_cast<uint32_t*>(u.acc), u.vi);
        if (random.chance(30)) random.memory(vu1Mem, sizeof vu1Mem);
        const Snapshot start = capture(0);
        vu::useCompiledPrograms(false);
        uint64_t cycle0 = u.t.cycle;
        vu1Start(entry);
        const Snapshot interpreted = capture(cycle0);
        restore(start);
        vu::useCompiledPrograms(true);
        cycle0 = u.t.cycle;
        vu1Start(entry);
        const Snapshot compiled = capture(cycle0);
        ++runs;
        const std::string d = differences(interpreted, compiled);
        if (!d.empty() && ++mismatches <= 10) std::printf("image %zu, entry 0x%04x, run %d: differs in%s\n", image, entry, run, d.c_str());
        restore(interpreted);
    };
    constexpr int kRunsPerEntry = 20;
    long directedEnded = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        load(images[i]);
        const long before = g_ended;
        for (const uint32_t entry : images[i].entries)
            for (int run = 0; run < kRunsPerEntry; ++run) runBoth(i, entry, run);
        if (i == 0) directedEnded = g_ended - before;
    }
    std::printf("%ld runs, %ld differ; compiled code ended %ld of them and handed %ld over to the interpreter\n", runs, mismatches,
                g_ended, g_handedOver);
    if (runs == 0 || g_ended < runs / 4) {
        std::printf("too few runs ended in compiled code to test it\n");
        return 1;
    }
    // The first image is written by hand: all of its programs end recompiled.
    if (!images.empty() && directedEnded != static_cast<long>(images[0].entries.size()) * kRunsPerEntry) {
        std::printf("the hand-written programs ended recompiled %ld times of %ld\n", directedEnded,
                    static_cast<long>(images[0].entries.size()) * kRunsPerEntry);
        return 1;
    }
    // Recompiled code runs only where micro memory holds it: with each image
    // loaded, the other images' entries run on the interpreter.
    const long endedBefore = g_ended, handedBefore = g_handedOver;
    for (size_t i = 0; i < images.size(); ++i) {
        load(images[i]);
        for (size_t j = 0; j < images.size(); ++j)
            if (j != i)
                for (const uint32_t entry : images[j].entries)
                    if (std::find(images[i].entries.begin(), images[i].entries.end(), entry) == images[i].entries.end())
                        runBoth(i, entry, 0);
    }
    if (g_ended != endedBefore || g_handedOver != handedBefore) {
        std::printf("recompiled code ran %ld times over micro memory that holds other code\n",
                    (g_ended - endedBefore) + (g_handedOver - handedBefore));
        return 1;
    }
    return mismatches ? 1 : 0;
}
