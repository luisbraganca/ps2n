// Vector unit micro mode: the interpreter for VU0 and VU1 micro programs.
//
// What each instruction pair does to the pipeline, and how the pipeline keeps
// time, is in ps2n/vu_micro.h (shared with the recompiler); what each
// operation does to values is in ps2/vu_micro.h (shared with recompiled micro
// programs). This file decodes pairs, steps through them and runs programs.
//
// Programs run to completion when they are started (MSCAL, VCALLMS), as if
// the VU were infinitely fast compared to the EE. In real-time runs VU1's run
// on a thread of their own (vu_thread.cpp), and the EE still sees each
// finished the moment it starts.
//
// A pair is decoded once: its timing and the operations its halves run, all
// of which its words fix. The decoding serves while the words stay the same:
// micro memory changes between programs (MPG, EE stores), never during one.
#include "vu.h"

#include "hw/gs.h"
#include "hw/vif.h"
#include "internal.h"
#include "log.h"
#include "ps2/vu_micro.h"
#include "ps2n/vu_program.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace ps2 {

namespace vu {

namespace {

struct Vu1Regs {
    VuReg vf[32] = {{{0, 0, 0, 0x3F800000u}}};   // vf0 is the constant (0, 0, 0, 1)
    VuReg acc{};
    uint32_t vi[32]{};
};
Vu1Regs g_vu1;

// A decoded instruction pair: its timing, and the operations its halves run
// (none for a NOP, or a lower operation the pair drops). A pair that
// isolates a register runs both halves together, from lower().
struct Decoded {
    uint64_t words = 0;
    Op upper = nullptr;
    Op lower = nullptr;
    Op upperAlone = nullptr;   // the upper operation when it runs by itself
    bool isolating = false;
    ps2n::vu::Pair p;
};

Decoded g_code0[4096 / 8], g_code1[16384 / 8];

void decode(int unit, Decoded& d, uint64_t words) {
    const auto lower = static_cast<uint32_t>(words), upper = static_cast<uint32_t>(words >> 32);
    d.words = words;
    d.p = ps2n::vu::decodePair(lower, upper, unit);
    d.upper = upperFn(upper);
    d.lower = d.p.runLower ? lowerFn(lower, unit) : nullptr;
    d.isolating = d.p.isolate && d.lower;
    d.upperAlone = d.isolating ? nullptr : d.upper;
}

Unit makeUnit(int index, VuReg* vf, VuReg* acc, uint32_t* vi, uint8_t* data, uint32_t dataMask, uint8_t* micro, uint32_t microMask,
              Decoded* code) {
    for (uint32_t pc = 0; pc < microMask; pc += 8) decode(index, code[pc / 8], 0);   // micro memory starts cleared
    Unit u;
    u.index = index;
    u.vf = vf;
    u.acc = acc;
    u.vi = vi;
    u.data = data;
    u.dataMask = dataMask;
    u.micro = micro;
    u.microMask = microMask;
    return u;
}

Unit g_unit[2] = {
    makeUnit(0, cpu.vf, &cpu.vacc, cpu.vi, vu0Mem, 0xFFF, vu0Micro, 0xFFF, g_code0),
    makeUnit(1, g_vu1.vf, &g_vu1.acc, g_vu1.vi, vu1Mem, 0x3FFF, vu1Micro, 0x3FFF, g_code1),
};
Decoded* const g_code[2] = {g_code0, g_code1};

// The values side of the timing: what lands, and the pair's operations.
struct Hooks {
    Unit& u;
    const Decoded& d;
    uint32_t lowerWord, upperWord;

    PS2_ALWAYS_INLINE void commit(uint32_t slot) { commitFlags(u, u.ring[slot], u.t.fmac[slot].clip, u.t.fmac[slot].fsset); }
    PS2_ALWAYS_INLINE void landQ() { vu::landQ(u); }
    PS2_ALWAYS_INLINE void landP() { vu::landP(u); }
    PS2_NOINLINE void retire() { ps2n::vu::retireDue(u.t, *this); }   // after a stall: out of the hot path
    PS2_ALWAYS_INLINE void readQ() { u.qRead = u.vi[Q]; }
    PS2_ALWAYS_INLINE void upper() {
        if (d.upperAlone) d.upperAlone(u, upperWord);
    }
    PS2_ALWAYS_INLINE void lower() {
        if (d.isolating) runIsolated(u, d.p.isolate, d.upper, upperWord, d.lower, lowerWord);
        else if (d.lower) d.lower(u, lowerWord);
    }
    PS2_ALWAYS_INLINE void loadI() { u.vi[I] = lowerWord; }
    PS2_ALWAYS_INLINE void issued(uint32_t slot) { u.ring[slot] = {u.macNext, u.statusNext, u.clipNext}; }
};

// Executes one instruction pair. Returns false when the program has ended.
bool step(Unit& u, Decoded* code) {
    const uint32_t pc = u.vi[TPC] & u.microMask & ~7u;
    uint64_t words;
    std::memcpy(&words, u.micro + pc, 8);
    Decoded& d = code[pc / 8];
    if (d.words != words) decode(u.index, d, words);
    const auto lower = static_cast<uint32_t>(words), upper = static_cast<uint32_t>(words >> 32);
    u.vi[TPC] = (pc + 8) & u.microMask;
    if (d.p.ebit) u.ebit = 2;          // E: stop after the next instruction

    Hooks h{u, d, lower, upper};
    ps2n::vu::stepPair(u.t, d.p, h);

    if (u.branch > 0 && u.branch-- == 1) {
        u.vi[TPC] = u.branchPc;
        if (u.delayBranch) {
            u.branch = 1;
            u.branchPc = u.delayPc;
            u.delayBranch = false;
        }
    }
    if (u.ebit > 0 && u.ebit-- == 1) {
        ps2n::vu::flushAll(u.t, h);
        return false;
    }
    return true;
}

// ---- recompiled programs -----------------------------------------------------------------------
// Micro memory's pairs as ps2n/vu_program.h reads them.
struct MicroWords {
    const Unit& u;
    uint64_t operator()(uint32_t pc) const {
        uint64_t w;
        std::memcpy(&w, u.micro + (pc & u.microMask & ~7u), 8);
        return w;
    }
};

// The game's table, by unit and the key of a program's first pairs
// (ps2n::vu::entryKey): a program runs compiled wherever micro memory holds
// it. Programs whose keys match each check whether micro memory holds their code.
struct Compiled {
    ProgramFn fn;
    uint32_t entry;   // where it was compiled from
};
std::unordered_map<uint64_t, std::vector<Compiled>> g_compiled[2];
bool g_useCompiled = true;

// The micro code the interpreter ran, for the next recompile: each program
// once (ps2n::vu::programKey), wherever it sat, with the micro memory it was
// first seen in, where it started there, and where compiled code handed it
// over, as addresses in that micro memory.
struct Recorded {
    int unit = 0;
    std::vector<uint8_t> micro;
    uint32_t entry = 0;
    std::set<uint32_t> targets;   // where compiled code handed it over (a JR or JALR it did not expect)
};
bool g_recording = false;
std::map<uint64_t, Recorded> g_recorded;   // VU0's on the guest thread, VU1's on the VU1 thread
std::mutex g_recordedMutex;                // for the map; a record is only ever touched by its unit's thread
struct {
    uint64_t generation = ~uint64_t(0);
    uint32_t lastEntry = ~0u, lastTarget = ~0u;
    uint64_t key = 0;   // the program at lastEntry
} g_recordedNow[2];

uint64_t hashMicro(const uint8_t* micro, size_t bytes) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ bytes;
    for (size_t i = 0; i < bytes; i += 8) {
        uint64_t w;
        std::memcpy(&w, micro + i, 8);
        h = (h ^ w) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}

// A program the interpreter ran from `entry` (all of it, or after compiled
// code handed it over at `target`: ~0 for none).
void record(const Unit& u, uint32_t entry, uint32_t target) {
    if (!g_recording) return;
    auto& now = g_recordedNow[u.index];
    const uint64_t generation = g_microGeneration[u.index];
    if (now.generation == generation && now.lastEntry == entry && now.lastTarget == target) return;
    if (now.generation != generation || now.lastEntry != entry) now.key = ps2n::vu::programKey(MicroWords{u}, entry, u.microMask, u.index);
    now.generation = generation;
    now.lastEntry = entry;
    now.lastTarget = target;
    std::lock_guard<std::mutex> lock(g_recordedMutex);
    Recorded& r = g_recorded[now.key];
    if (r.micro.empty()) {
        r.unit = u.index;
        r.micro.assign(u.micro, u.micro + u.microMask + 1);
        r.entry = entry;
    }
    if (target != ~0u) r.targets.insert((target - entry + r.entry) & u.microMask & ~7u);
}

// Runs a program from TPC: recompiled code for it if there is any that micro
// memory holds, else (or from where that hands over) the interpreter.
void run(Unit& u, uint32_t startByte) {
    Decoded* const code = g_code[u.index];
    if (startByte != 0xFFFFFFFFu) u.vi[TPC] = startByte & u.microMask;
    // The flag registers are what the program starts from.
    u.macNext = u.vi[MAC];
    u.statusNext = u.vi[STATUS];
    u.clipNext = u.vi[CLIP];
    u.t.count = 0;
    u.t.fdiv = {};
    u.t.efu = {};
    u.t.ialu = {};
    u.fdiv = {};
    u.efuP = 0;
    u.branch = 0;
    u.delayBranch = false;
    u.ebit = 0;
    u.t.viBackupCycles = 0;
    u.steps = 0;
    const uint32_t start = u.vi[TPC] & ~7u;
    uint32_t compiled = kNotRun;
    if (g_useCompiled && !g_compiled[u.index].empty()) {
        const auto& table = g_compiled[u.index];
        if (const auto it = table.find(ps2n::vu::entryKey(MicroWords{u}, start, u.microMask, u.index)); it != table.end())
            for (const Compiled& c : it->second)
                if ((compiled = c.fn(u, c.entry, (start - c.entry) & u.microMask)) != kNotRun) break;
    }
    if (compiled == kEnded) {
        PS2_TRACE("vu", "VU%d program at 0x%04x ran %llu instructions, recompiled", u.index, start, static_cast<unsigned long long>(u.steps));
        return;
    }
    if (compiled != kNotRun)
        PS2_DEBUG("vu", "VU%d program from 0x%04x goes on in the interpreter at 0x%04x", u.index, start, compiled);
    record(u, start, compiled == kNotRun ? ~0u : compiled);
    bool more = true;
    while (more) {
        if (u.steps == g_stepLimit) {
            PS2_ERROR("vu", "VU%d micro program started at 0x%04x has not ended after %llu instructions (pc 0x%04x); stopping it",
                      u.index, start, static_cast<unsigned long long>(g_stepLimit), u.vi[TPC]);
            Hooks h{u, code[0], 0, 0};
            ps2n::vu::flushAll(u.t, h);
            break;
        }
        more = step(u, code);
        ++u.steps;
    }
    PS2_TRACE("vu", "VU%d program at 0x%04x ran %llu instructions", u.index, start, static_cast<unsigned long long>(u.steps));
}

} // namespace

uint64_t g_microGeneration[2] = {0, 0};
uint64_t g_stepLimit = 50000000;

bool microMatches(const Unit& u, uint32_t pc, const uint64_t* words, uint32_t pairs) {
    for (uint32_t i = 0; i < pairs; ++i) {
        uint64_t w;
        std::memcpy(&w, u.micro + ((pc + 8 * i) & u.microMask), 8);
        if (w != words[i]) return false;
    }
    return true;
}

void registerPrograms(const CompiledProgram* programs, size_t count) {
    sync();
    for (auto& table : g_compiled) table.clear();
    for (size_t i = 0; i < count; ++i)
        if (programs[i].fn) g_compiled[programs[i].unit & 1][programs[i].key].push_back({programs[i].fn, programs[i].entry});
}

void useCompiledPrograms(bool on) {
    sync();
    g_useCompiled = on;
}

void recordPrograms(bool on) {
    sync();
    g_recording = on;
}

void writeRecordedPrograms(const std::filesystem::path& file) {
    sync();
    std::lock_guard<std::mutex> lock(g_recordedMutex);
    if (g_recorded.empty() || file.empty()) return;
    std::ofstream out(file);
    if (!out) return;
    out << "# VU micro code ps2n interpreted during the last run: each program once,\n"
        << "# with what micro memory held when it was first seen and where it started\n"
        << "# there. List this file in [vu] code_files of the game's config (or merge\n"
        << "# its images into a file listed there) and recompile, and those programs\n"
        << "# run compiled, wherever the game puts them.\n";
    // Programs seen in the same micro memory go in one image.
    struct Image {
        const Recorded* first;
        std::set<uint32_t> entries, targets;
    };
    std::map<uint64_t, Image> images;
    for (const auto& [key, r] : g_recorded) {
        Image& im = images.try_emplace(hashMicro(r.micro.data(), r.micro.size()) ^ static_cast<uint64_t>(r.unit), Image{&r, {}, {}})
                        .first->second;
        im.entries.insert(r.entry);
        im.targets.insert(r.targets.begin(), r.targets.end());
    }
    char buf[96];
    auto list = [&](const std::set<uint32_t>& addresses) {
        bool first = true;
        for (uint32_t a : addresses) {
            std::snprintf(buf, sizeof buf, "%s0x%04x", first ? "" : ", ", a);
            out << buf;
            first = false;
        }
    };
    for (const auto& [hash, im] : images) {
        out << "\n[[image]]\nunit = " << im.first->unit << "\nentries = [";
        list(im.entries);
        out << "]\n";
        if (!im.targets.empty()) {
            out << "# where recompiled code handed programs over (jumps through a register it did not expect)\ntargets = [";
            list(im.targets);
            out << "]\n";
        }
        out << "# pairs (lower word | upper word << 32) from each address; lines of zeros left out\ncode = \"\"\"\n";
        const std::vector<uint8_t>& micro = im.first->micro;
        for (size_t at = 0; at < micro.size(); at += 32) {
            uint64_t w[4];
            std::memcpy(w, micro.data() + at, 32);
            if (!(w[0] | w[1] | w[2] | w[3])) continue;
            std::snprintf(buf, sizeof buf, "%04zx %016llx %016llx %016llx %016llx\n", at, static_cast<unsigned long long>(w[0]),
                          static_cast<unsigned long long>(w[1]), static_cast<unsigned long long>(w[2]), static_cast<unsigned long long>(w[3]));
            out << buf;
        }
        out << "\"\"\"\n";
    }
    PS2_INFO("vu", "%zu interpreted micro program%s in %zu image(s) written to %s", g_recorded.size(), g_recorded.size() == 1 ? "" : "s",
             images.size(), file.string().c_str());
}

Unit& unit(int index) { return g_unit[index & 1]; }

void unknown(Unit& u, const char* which, uint32_t c) {
    char key[64];
    std::snprintf(key, sizeof key, "VU%d %s instruction 0x%08x", u.index, which, c);
    PS2_UNIMPL("vu", key, "unknown VU%d %s instruction 0x%08x at 0x%04x", u.index, which, c, u.vi[TPC] - 8);
}

uint32_t vifTop(const Unit& u) { return u.index ? u.top : vif::top(1); }
uint32_t vifItop(const Unit& u) { return u.index ? u.itop : vif::itop(0); }

void xgkick(Unit& u, uint32_t c) {
    if (u.index != 1) return unknown(u, "XGKICK on VU0", c);
    // Collect the GIF packet: tags and data until the tag with EOP.
    std::vector<uint64_t>& q = u.kick;
    q.clear();
    uint32_t a = viRead(u, fFs(c)) & 0x3FF;
    for (int guard = 0; guard < 1024; ++guard) {
        uint64_t tag[2];
        std::memcpy(tag, u.data + a * 16, 16);
        q.push_back(tag[0]);
        q.push_back(tag[1]);
        a = (a + 1) & 0x3FF;
        const uint32_t nloop = static_cast<uint32_t>(tag[0] & 0x7FFF);
        const bool eop = (tag[0] >> 15) & 1;
        const uint32_t flg = static_cast<uint32_t>((tag[0] >> 58) & 3);
        uint32_t nreg = static_cast<uint32_t>((tag[0] >> 60) & 0xF);
        if (!nreg) nreg = 16;
        uint32_t data = 0;
        if (flg == 0) data = nloop * nreg;               // PACKED
        else if (flg == 1) data = (nloop * nreg + 1) / 2; // REGLIST
        else data = nloop;                               // IMAGE
        for (uint32_t i = 0; i < data; ++i) {
            uint64_t d[2];
            std::memcpy(d, u.data + a * 16, 16);
            q.push_back(d[0]);
            q.push_back(d[1]);
            a = (a + 1) & 0x3FF;
        }
        if (eop) break;
    }
    // On the VU1 thread the packet goes to the GS thread's PATH1 queue, which
    // it decodes where the guest queued the program.
    if (u.threaded) gs::path1Kick(q.data(), q.size() / 2);
    else gs::gifTransfer(q.data(), q.size() / 2, 1);
}

uint32_t vu1Reg(uint32_t index) {
    sync();
    return g_vu1.vi[index & 31];
}

void runVu1(uint32_t startByte, uint32_t top, uint32_t itop, bool threaded) {
    Unit& u = g_unit[1];
    u.top = top;
    u.itop = itop;
    u.threaded = threaded;
    run(u, startByte);
}

} // namespace vu

// ---- hooks used by recompiled code and the VIF ---------------------------------------------------
void vu0CallMicro(uint32_t addr) { vu::run(vu::g_unit[0], addr); }

// VU0's window on VU1's registers: VF0-31, then VI0-31, a quadword each.
namespace {
void windowOnControl(uint32_t vi, const char* access) {   // VI16-31: as held, which the console may not show
    char key[64];
    std::snprintf(key, sizeof key, "VU0 window %s of VU1 VI%u", access, vi);
    PS2_UNIMPL("vu", key, "VU0 %s VU1's control register VI%u through its window, as the register is held", access, vi);
}
} // namespace

void vu0WindowRead(uint32_t qwAddr, uint32_t (&w)[4]) {
    vu::sync();
    const uint32_t r = qwAddr & 0x3F;
    if (r < 32) {
        std::memcpy(w, vu::g_vu1.vf[r].c, 16);
        return;
    }
    const uint32_t vi = r - 32;
    if (vi >= 16) windowOnControl(vi, "reads");
    w[0] = vu::g_vu1.vi[vi];
    w[1] = w[2] = w[3] = 0;
}

void vu0WindowWrite(uint32_t qwAddr, uint32_t dest, const uint32_t (&w)[4]) {
    vu::sync();
    const uint32_t r = qwAddr & 0x3F;
    if (r < 32) {
        if (r == 0) return;   // VF0 is constant
        for (int i = 0; i < 4; ++i)
            if (vu::on(dest, i)) vu::g_vu1.vf[r].c[i] = w[i];
        return;
    }
    const uint32_t vi = r - 32;
    if (vi >= 16) windowOnControl(vi, "writes");
    if (vi == 0 || !vu::on(dest, 0)) return;   // VI0 is constant; the register is the x word
    vu::g_vu1.vi[vi] = vi < 16 ? w[0] & 0xFFFF : w[0];
}

void vu1Start(uint32_t addr) { vu::startVu1(addr, vif::top(1), vif::itop(1)); }

void vu0Reset() {
    for (uint32_t i = 1; i < 32; ++i) std::memset(cpu.vf[i].c, 0, 16);
    for (uint32_t i = 0; i < 16; ++i) cpu.vi[i] = 0;
    std::memset(cpu.vacc.c, 0, 16);
    cpu.vi[VU_STATUS] = cpu.vi[VU_MAC] = cpu.vi[VU_CLIP] = 0;
}

void vu1Reset() {
    vu::sync();
    vu::g_vu1 = {};
}

} // namespace ps2
