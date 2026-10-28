#include "iop/hle.h"

#include "log.h"

#include "ps2uc/irx.h"

#include <bit>
#include <map>

namespace ps2::iop::hle {

namespace {

constexpr uint32_t kImportMagic = 0x41E00000u, kExportMagic = 0x41C00000u;
constexpr uint32_t kJrRa = 0x03E00008u;
constexpr int32_t KE_OK = 0, KE_LIBRARY_FOUND = -212, KE_LIBRARY_NOTFOUND = -213, KE_ILLEGAL_LIBRARY = -214;

struct Library {
    std::string name;
    uint16_t version = 0;
    bool host = false;
    uint32_t guestTable = 0;               // export table address (guest libraries)
    std::vector<uint32_t> functions;        // export index -> address, 0 = none
    std::vector<std::string> names;         // host libraries: export names
};

std::vector<Library> g_libraries;
std::map<std::string, uint32_t> g_missingTraps;   // "lib::name" -> reporting trap
struct Pending {
    uint32_t table;
    std::string library;
    std::string module;
};
std::vector<Pending> g_pending;

std::string libName(uint32_t addr) {
    std::string s;
    for (uint32_t i = 0; i < 8; ++i) {
        const char c = static_cast<char>(mem::read8(addr + i));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

Library* findHost(const std::string& name) {
    for (auto& l : g_libraries)
        if (l.host && l.name == name) return &l;
    return nullptr;
}

Library* findGuest(const std::string& name, uint16_t version) {
    // Newest registration first; the major version has to match.
    for (auto it = g_libraries.rbegin(); it != g_libraries.rend(); ++it)
        if (!it->host && it->name == name && (it->version >> 8) == (version >> 8)) return &*it;
    return nullptr;
}

void missingLibraryCall() {
    const std::string& name = trapName(currentTrap());
    PS2_UNIMPL("iop", "IOP call into a library nobody provides: " + name,
               "IOP code called %s, but no loaded module or kernel library provides it; returning 0", name.c_str());
    ret(0);
}

uint32_t reportingTrap(const std::string& library, uint32_t index, bool libraryMissing) {
    const std::string name = exportName(library, index) + (libraryMissing ? " (library not loaded)" : "");
    auto it = g_missingTraps.find(name);
    if (it != g_missingTraps.end()) return it->second;
    const uint32_t t = addTrap(libraryMissing ? &missingLibraryCall : &unimplementedExport, name);
    g_missingTraps[name] = t;
    return t;
}

// Resolves one import. Returns 0 when the library is not available at all.
uint32_t resolve(const std::string& library, uint16_t version, uint32_t index, bool& missing) {
    missing = false;
    if (Library* g = findGuest(library, version)) {
        if (index < g->functions.size() && g->functions[index]) return g->functions[index];
        return kJrRa;   // what loadcore links past the end of a table
    }
    if (Library* h = findHost(library)) {
        if (index < h->functions.size() && h->functions[index]) return h->functions[index];
        return reportingTrap(library, index, false);
    }
    missing = true;
    return reportingTrap(library, index, true);
}

// Links one import table. Returns false if the library is missing.
bool linkTable(uint32_t table) {
    const std::string library = libName(table + 12);
    const auto version = mem::read16(table + 8);
    bool anyMissing = false;
    for (uint32_t stub = table + 20; mem::read32(stub) != 0; stub += 8) {
        const uint32_t insn1 = mem::read32(stub + 4);
        if ((insn1 >> 26) != 9) break;
        bool missing = false;
        const uint32_t target = resolve(library, version, insn1 & 0xFFFF, missing);
        anyMissing |= missing;
        mem::write32(stub, target == kJrRa ? kJrRa : (0x08000000u | ((target >> 2) & 0x03FFFFFFu)));
    }
    mem::write16(table + 10, static_cast<uint16_t>((mem::read16(table + 10) & ~6u) | 2u));
    return !anyMissing;
}

// Same validity check loadcore applies before linking (CheckCallerStub).
bool looksLikeImportTable(uint32_t table, uint32_t limit) {
    if (mem::read32(table) != kImportMagic) return false;
    uint32_t stub = table + 20;
    while (stub + 8 <= limit) {
        const uint32_t i0 = mem::read32(stub), i1 = mem::read32(stub + 4);
        if (i0 == 0 && i1 == 0) return stub > table + 20;
        if ((i1 >> 26) != 9 || (i0 != kJrRa && (i0 >> 26) != 2)) return false;
        stub += 8;
    }
    return false;
}

} // namespace

void unimplementedExport() {
    const std::string& name = trapName(currentTrap());
    PS2_UNIMPL("iop", "IOP call " + name, "IOP code called %s, which is not implemented; returning 0", name.c_str());
    ret(0);
}

std::string exportName(const std::string& library, uint32_t index) {
    if (Library* h = findHost(library); h && index < h->names.size() && !h->names[index].empty())
        return library + "::" + h->names[index];
    if (const char* n = ps2uc::irxKnownExportName(library, index)) return library + "::" + n;
    return library + "::#" + std::to_string(index);
}

void defineLibrary(const char* name, uint16_t version, std::initializer_list<Export> exports) {
    Library lib;
    lib.name = name;
    lib.version = version;
    lib.host = true;
    uint16_t maxIndex = 0;
    for (const Export& e : exports) maxIndex = std::max(maxIndex, e.index);
    lib.functions.assign(maxIndex + 1u, 0);
    lib.names.assign(maxIndex + 1u, std::string());
    for (const Export& e : exports) {
        lib.names[e.index] = e.name;
        const std::string full = std::string(name) + "::" + e.name;
        lib.functions[e.index] = addTrap(e.fn ? e.fn : &unimplementedExport, full);
    }
    if (Library* existing = findHost(name)) *existing = std::move(lib);
    else g_libraries.push_back(std::move(lib));
}

void setExport(const char* library, uint16_t index, uint32_t addr) {
    Library* h = findHost(library);
    if (!h) return;
    if (index >= h->functions.size()) {
        h->functions.resize(index + 1u, 0);
        h->names.resize(index + 1u);
    }
    h->functions[index] = addr;
}

uint32_t exportAddress(const char* library, uint16_t index) {
    Library* h = findHost(library);
    return h && index < h->functions.size() ? h->functions[index] : 0;
}

bool isHostLibrary(const std::string& name) { return findHost(name) != nullptr; }

int registerGuestLibrary(uint32_t table) {
    if (!table || mem::read32(table) != kExportMagic) return KE_ILLEGAL_LIBRARY;
    Library lib;
    lib.name = libName(table + 12);
    lib.version = mem::read16(table + 8);
    lib.guestTable = table;
    for (uint32_t p = table + 20; mem::read32(p) != 0 && lib.functions.size() < 1024; p += 4) lib.functions.push_back(mem::read32(p));
    if (Library* old = findGuest(lib.name, lib.version); old && (lib.version & 0xFF) <= (old->version & 0xFF))
        return KE_LIBRARY_FOUND;
    PS2_DEBUG("iop", "library %s v%d.%d registered (%zu exports)", lib.name.c_str(), lib.version >> 8, lib.version & 0xFF,
              lib.functions.size());
    const std::string name = lib.name;
    g_libraries.push_back(std::move(lib));
    // Modules that were loaded before this library can now reach it.
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->library == name && linkTable(it->table)) {
            PS2_DEBUG("iop", "relinked imports of %s from %s", it->module.c_str(), name.c_str());
            it = g_pending.erase(it);
        } else {
            ++it;
        }
    }
    return KE_OK;
}

int releaseGuestLibrary(uint32_t table) {
    for (auto it = g_libraries.begin(); it != g_libraries.end(); ++it) {
        if (!it->host && it->guestTable == table) {
            g_libraries.erase(it);
            return KE_OK;
        }
    }
    return KE_LIBRARY_NOTFOUND;
}

uint32_t guestLibraryTable(const std::string& name) {
    for (auto it = g_libraries.rbegin(); it != g_libraries.rend(); ++it)
        if (!it->host && it->name == name) return it->guestTable;
    return 0;
}

void linkImports(uint32_t textStart, uint32_t textSize, const std::string& moduleName) {
    const uint32_t end = textStart + textSize;
    for (uint32_t a = textStart; a + 28 <= end; a += 4) {
        if (!looksLikeImportTable(a, end)) continue;
        const std::string library = libName(a + 12);
        if (!linkTable(a)) {
            PS2_WARN("iop", "%s imports from library \"%s\", which nothing provides yet", moduleName.c_str(), library.c_str());
            g_pending.push_back({a, library, moduleName});
        }
        // Skip past this table's stubs.
        uint32_t stub = a + 20;
        while (mem::read32(stub) != 0 || mem::read32(stub + 4) != 0) stub += 8;
        a = stub + 4;
    }
}

// ---- CodeBuilder ------------------------------------------------------------------------------
void CodeBuilder::emit(uint32_t insn) {
    mem::write32(addr_, insn);
    addr_ += 4;
}

CodeBuilder::Label CodeBuilder::label() {
    labels_.push_back(0);
    return static_cast<Label>(labels_.size() - 1);
}

void CodeBuilder::bind(Label l) {
    labels_[static_cast<size_t>(l)] = addr_;
    for (const auto& [at, target] : fixups_) {
        if (target != l) continue;
        const uint32_t off = ((addr_ - (at + 4)) >> 2) & 0xFFFF;
        mem::write32(at, (mem::read32(at) & 0xFFFF0000u) | off);
    }
}

void CodeBuilder::branch(uint32_t op, uint32_t rs, uint32_t rt, Label l) {
    const uint32_t target = labels_[static_cast<size_t>(l)];
    if (target) {
        emit(itype(op, rs, rt, static_cast<uint16_t>(((target - (addr_ + 4)) >> 2) & 0xFFFF)));
    } else {
        fixups_.emplace_back(addr_, l);
        emit(itype(op, rs, rt, 0));
    }
}

// ---- printf -----------------------------------------------------------------------------------
uint32_t IopArgs::slot(int i) const {
    if (va_) return mem::read32(va_ + 4 * static_cast<uint32_t>(i));
    if (i < 4) return g_cpu.gpr[R_A0 + i];
    return mem::read32(g_cpu.gpr[R_SP] + 4 * static_cast<uint32_t>(i));
}

uint64_t IopArgs::nextInt(bool wide) {
    if (!wide) return slot(slot_++);
    if (slot_ & 1) ++slot_;   // 64-bit values take an even/odd pair (8-byte aligned)
    const uint64_t lo = slot(slot_), hi = slot(slot_ + 1);
    slot_ += 2;
    return lo | (hi << 32);
}

double IopArgs::nextDouble() { return std::bit_cast<double>(nextInt(true)); }

std::string formatIop(uint32_t fmtAddr, int firstArg) {
    IopArgs args(firstArg);
    return formatPrintf(mem::readString(fmtAddr, 4096), args);
}

void initRegistry() {
    g_libraries.clear();
    g_missingTraps.clear();
    g_pending.clear();
}

} // namespace ps2::iop::hle
