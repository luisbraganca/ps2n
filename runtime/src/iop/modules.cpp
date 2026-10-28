#include "iop/modules.h"

#include "iop/cpu.h"
#include "iop/hle.h"
#include "iop/hostfs.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "log.h"

#include "ps2uc/elf.h"
#include "ps2uc/irx.h"
#include "ps2uc/util.h"

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

namespace ps2::iop::modules {

using ps2uc::ElfFile;
using ps2uc::IrxInfo;
using ps2uc::toLower;
using namespace hle;

namespace {

constexpr int32_t KE_OK = 0, KE_ILLEGAL_CONTEXT = -100, KE_ILLEGAL_OBJECT = -201, KE_UNKNOWN_MODULE = -202, KE_NOFILE = -203,
                  KE_ALREADY_STARTED = -206, KE_NOT_REMOVABLE = -211, KE_NO_MEMORY = -400;
constexpr uint32_t kModuleInfoSize = 0x30;
constexpr uint32_t R_MIPS_16 = 1, R_MIPS_32 = 2, R_MIPS_26 = 4, R_MIPS_HI16 = 5, R_MIPS_LO16 = 6, R_MIPSSCE_MHI16 = 250;

struct Module {
    int id = 0;
    std::string name;
    std::string path;
    uint16_t version = 0;
    bool hle = false;
    uint32_t block = 0;   // sysmem block: ModuleInfo_t, then the image
    uint32_t info = 0;
    uint32_t textStart = 0, textSize = 0, dataSize = 0, bssSize = 0, entry = 0, gp = 0;
    bool started = false;
};

struct PendingStart {
    int thread;
    int module;
    uint32_t argBlock;
    Done done;
};

std::vector<Module> g_modules;
std::vector<PendingStart> g_starts;
int g_nextId = 1;
uint32_t g_startReturnTrap = 0;

// Modules whose job the HLE kernel does: loading one is a no-op.
const std::set<std::string> kKernelModules = {
    "sysmem", "system_memory_manager", "loadcore", "module_manager", "excepman", "exception_manager", "intrman",
    "interrupt_manager", "ssbusc", "ssbus_service", "dmacman", "timrman", "timer_manager", "sysclib", "system_c_lib",
    "heaplib", "heap_lib", "threadman", "multi_thread_manager", "vblank", "vblank_service", "ioman", "io/file_manager",
    "iomanx", "iox/file_manager", "modload", "moldule_file_loader", "module_file_loader", "stdio", "sifman",
    "iop_sif_manager", "sifcmd", "iop_sif_rpc_interface", "sifinit", "loadfile", "loadmodulebyee", "fileio",
    "fileio_service", "secrman", "secrman_special", "secrman_for_cex", "eesync", "syncee", "rmreset", "reboot",
};

std::string baseName(const std::string& path) {
    std::string p = path;
    if (auto c = p.find(':'); c != std::string::npos) p = p.substr(c + 1);
    if (auto s = p.find_last_of("/\\"); s != std::string::npos) p = p.substr(s + 1);
    if (auto semi = p.find(';'); semi != std::string::npos) p = p.substr(0, semi);
    if (auto dot = p.rfind('.'); dot != std::string::npos) p = p.substr(0, dot);
    return toLower(p);
}

Module* findModule(int id) {
    for (auto& m : g_modules)
        if (m.id == id) return &m;
    return nullptr;
}

int addHleModule(const std::string& name, const std::string& path) {
    Module m;
    m.id = g_nextId++;
    m.name = name;
    m.path = path;
    m.hle = true;
    m.started = true;
    g_modules.push_back(m);
    return m.id;
}

// What loadcore's ApplyElfRelSection does, with the image at `base`.
void relocate(const ElfFile& elf, uint32_t base) {
    const auto& b = elf.bytes();
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24)); };
    for (const auto& s : elf.sections) {
        if (s.type != ps2uc::elf::SHT_REL) continue;
        const uint32_t count = s.size / 8;
        if (s.offset + static_cast<size_t>(count) * 8 > b.size()) continue;
        auto relOffset = [&](uint32_t i) { return u32(s.offset + 8 * static_cast<size_t>(i)); };
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t off = relOffset(i), type = u32(s.offset + 8 * static_cast<size_t>(i) + 4) & 0xFF;
            const uint32_t at = base + off;
            uint32_t w = mem::read32(at);
            switch (type) {
            case R_MIPS_16: {
                const uint32_t v = base + static_cast<uint32_t>(static_cast<int16_t>(w & 0xFFFF));
                w = (w & 0xFFFF0000u) | (v & 0xFFFF);
                break;
            }
            case R_MIPS_32: w += base; break;
            case R_MIPS_26: {
                const uint32_t v = base + ((off & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2));
                w = (w & 0xFC000000u) | ((v >> 2) & 0x03FFFFFFu);
                break;
            }
            case R_MIPS_HI16: {
                const auto lo = i + 1 < count ? static_cast<int16_t>(mem::read32(base + relOffset(i + 1)) & 0xFFFF) : int16_t(0);
                const uint32_t v = base + static_cast<uint32_t>(lo) + (w << 16);
                w = (w & 0xFFFF0000u) | ((((v >> 15) + 1) >> 1) & 0xFFFF);
                break;
            }
            case R_MIPS_LO16: w = (w & 0xFFFF0000u) | ((base + w) & 0xFFFF); break;
            case R_MIPSSCE_MHI16: {
                // A chain of HI16s; each immediate holds the word distance to the next one.
                const uint32_t v = i + 1 < count ? ((((base + relOffset(i + 1)) >> 15) + 1) >> 1) & 0xFFFF : 0;
                uint32_t p = at;
                for (int guard = 0; guard < 4096; ++guard) {
                    const uint32_t insn = mem::read32(p);
                    const auto step = static_cast<int16_t>(insn & 0xFFFF);
                    mem::write32(p, (insn & 0xFFFF0000u) | v);
                    if (step == 0) break;
                    p += static_cast<uint32_t>(step) * 4;
                }
                ++i;
                continue;
            }
            default: continue;
            }
            mem::write32(at, w);
        }
    }
}

// Loads (relocates, links) an IRX image. Returns the module id or a KE_ error.
int loadImage(std::vector<uint8_t> bytes, const std::string& path, int allocMode = 0, uint32_t allocAddr = 0) {
    std::unique_ptr<ElfFile> elf;
    IrxInfo irx;
    try {
        elf = std::make_unique<ElfFile>(ElfFile::parse(std::move(bytes), path));
        irx = ps2uc::parseIrx(*elf);
    } catch (const std::exception& e) {
        PS2_WARN("iop", "%s is not an IRX module (%s)", path.c_str(), e.what());
        return KE_ILLEGAL_OBJECT;
    }
    const ps2uc::ElfSegment* seg = nullptr;
    for (const auto& s : elf->segments)
        if (s.type == ps2uc::elf::PT_LOAD) seg = &s;
    if (!seg || seg->offset + static_cast<size_t>(seg->filesz) > elf->bytes().size()) return KE_ILLEGAL_OBJECT;

    const uint32_t memsz = std::max(seg->memsz, irx.textSize + irx.dataSize + irx.bssSize);
    const uint32_t block = kernel::allocMemory(memsz + kModuleInfoSize, allocMode, allocAddr);
    if (!block) return KE_NO_MEMORY;
    const uint32_t text = block + kModuleInfoSize;
    mem::writeBlock(text, elf->bytes().data() + seg->offset, seg->filesz);
    mem::fill(text + seg->filesz, 0, memsz - seg->filesz);
    relocate(*elf, text);

    Module m;
    m.id = g_nextId++;
    m.name = irx.name.empty() ? baseName(path) : irx.name;
    m.path = path;
    m.version = irx.version;
    m.block = block;
    m.info = block;
    m.textStart = text;
    m.textSize = irx.textSize;
    m.dataSize = irx.dataSize;
    m.bssSize = irx.bssSize;
    m.entry = text + irx.entry;
    m.gp = text + irx.gp;
    hle::linkImports(text, irx.textSize ? irx.textSize : memsz, m.name);

    const uint32_t namePtr = irx.moduleInfo != 0xFFFFFFFFu ? mem::read32(text + irx.moduleInfo) : 0;
    mem::fill(block, 0, kModuleInfoSize);
    mem::write32(block + 4, namePtr);
    mem::write16(block + 8, irx.version);
    mem::write16(block + 12, static_cast<uint16_t>(m.id));
    mem::write32(block + 16, m.entry);
    mem::write32(block + 20, m.gp);
    mem::write32(block + 24, m.textStart);
    mem::write32(block + 28, m.textSize);
    mem::write32(block + 32, m.dataSize);
    mem::write32(block + 36, m.bssSize);
    PS2_INFO("iop", "loaded %s (\"%s\" v%d.%02d) at 0x%08x, %u bytes", path.c_str(), m.name.c_str(), irx.version >> 8,
             irx.version & 0xFF, text, memsz);
    g_modules.push_back(m);
    return m.id;
}

void startReturned() {
    const int tid = kernel::currentThread();
    auto it = std::find_if(g_starts.begin(), g_starts.end(), [&](const PendingStart& p) { return p.thread == tid; });
    if (it == g_starts.end()) {
        kernel::exitCurrent(true);
        return;
    }
    const PendingStart ps = *it;
    g_starts.erase(it);
    const auto result = static_cast<int32_t>(g_cpu.gpr[R_V0]);
    kernel::freeMemory(ps.argBlock);
    kernel::exitCurrent(true);
    if (Module* m = findModule(ps.module)) {
        PS2_DEBUG("iop", "module %s started, result %d", m->name.c_str(), result);
        if ((result & 3) == 1) {   // NO_RESIDENT_END: the module is done and leaves memory
            kernel::freeMemory(m->block);
            g_modules.erase(g_modules.begin() + (m - g_modules.data()));
        }
    }
    ps.done(ps.module, result);
}

void startModule(int id, const std::string& path, const std::string& args, Done done) {
    Module* m = findModule(id);
    if (!m) return done(KE_UNKNOWN_MODULE, 0);
    if (m->started) return done(KE_ALREADY_STARTED, 0);
    m->started = true;
    // argv: the path, then the NUL-separated arguments.
    std::vector<std::string> argv{path};
    for (size_t i = 0; i < args.size();) {
        const size_t end = args.find('\0', i);
        argv.push_back(args.substr(i, end == std::string::npos ? std::string::npos : end - i));
        if (end == std::string::npos) break;
        i = end + 1;
    }
    uint32_t strings = 0;
    for (const auto& a : argv) strings += static_cast<uint32_t>(a.size()) + 1;
    const uint32_t ptrs = 4 * static_cast<uint32_t>(argv.size() + 1);
    const uint32_t block = kernel::allocMemory(ptrs + strings);
    if (!block) return done(KE_NO_MEMORY, 0);
    uint32_t s = block + ptrs;
    for (size_t i = 0; i < argv.size(); ++i) {
        mem::write32(block + 4 * static_cast<uint32_t>(i), s);
        mem::writeBlock(s, argv[i].c_str(), static_cast<uint32_t>(argv[i].size()) + 1);
        s += static_cast<uint32_t>(argv[i].size()) + 1;
    }
    mem::write32(block + 4 * static_cast<uint32_t>(argv.size()), 0);
    const int tid = kernel::spawnThread(m->entry, 8, 0x1000, m->gp, g_startReturnTrap, static_cast<uint32_t>(argv.size()), block, 0,
                                        m->info);
    if (tid < 0) {
        kernel::freeMemory(block);
        return done(tid, 0);
    }
    g_starts.push_back({tid, id, block, std::move(done)});
}

// Finds the image for `path`: the IRX file if there is one, plus the name the
// module goes by.
bool locate(const std::string& path, std::vector<uint8_t>& image, std::string& name) {
    name = baseName(path);
    if (!hostfs::readWholeFile(path, image)) return false;
    try {
        const IrxInfo irx = ps2uc::parseIrx(ElfFile::parse(image, path));
        if (!irx.name.empty()) name = toLower(irx.name);
    } catch (const std::exception&) {
    }
    return true;
}

// Modules the HLE kernel provides: enables their host services. Returns the
// module id, or 0 if the module is not one of them.
int provide(const std::string& name, const std::string& path) {
    const std::string file = baseName(path);
    if (!providedByHle(name) && !providedByHle(file)) return 0;
    if (!kKernelModules.count(name) && !kKernelModules.count(file)) {
        if (!enableModuleServices(name)) enableModuleServices(file);
    }
    for (const auto& m : g_modules)
        if (m.hle && (m.name == name || baseName(m.path) == file)) return m.id;
    PS2_INFO("iop", "module %s (\"%s\") is provided by the HLE kernel", path.c_str(), name.c_str());
    return addHleModule(name, path);
}

// ---- loadcore exports -----------------------------------------------------------------------------
void lcRegisterLibraryEntries() { ret(registerGuestLibrary(arg(0))); }
void lcReleaseLibraryEntries() { ret(releaseGuestLibrary(arg(0))); }
void lcLinkLibraryEntries() {
    linkImports(arg(0), arg(1), "(LinkLibraryEntries)");
    ret(KE_OK);
}
void lcQueryLibraryEntryTable() {
    std::string name;
    for (uint32_t i = 0; i < 8; ++i) {
        const char c = static_cast<char>(mem::read8(arg(0) + 12 + i));
        if (!c) break;
        name.push_back(c);
    }
    const uint32_t t = guestLibraryTable(name);
    retU(t ? t + 20 : 0);
}
void lcSearchModuleCBByAddr() {
    const uint32_t a = arg(0) & 0x1FFFFFFFu;
    for (const auto& m : g_modules)
        if (!m.hle && a >= m.textStart && a < m.textStart + m.textSize + m.dataSize + m.bssSize) return retU(m.info);
    retU(0);
}
void lcInternalData() { retU(kernel::kKernelData + 0xA00); }
void zero() { ret(0); }

// ---- modload exports ----------------------------------------------------------------------------------
std::string argBytes(uint32_t addr, int len) {
    std::string s;
    for (int i = 0; i < len && addr; ++i) s.push_back(static_cast<char>(mem::read8(addr + static_cast<uint32_t>(i))));
    return s;
}

// Runs `start` and returns its id through $v0, blocking the calling thread
// until a module start function has run if it has to.
void blockingLoad(const std::function<void(Done)>& start, uint32_t resultPtr) {
    if (kernel::inInterrupt()) return ret(KE_ILLEGAL_CONTEXT);
    struct State {
        bool async = false, finished = false;
        int id = 0;
    };
    auto st = std::make_shared<State>();
    const int caller = kernel::currentThread();
    start([st, caller, resultPtr](int id, int result) {
        if (resultPtr && id >= 0) mem::write32(resultPtr, static_cast<uint32_t>(result));
        st->finished = true;
        st->id = id;
        if (st->async) kernel::wakeThread(caller, id);
    });
    if (st->finished) return ret(st->id);
    st->async = true;
    kernel::blockCurrent(kernel::TSW_SERVICE, 0);
}

void mlLoadStartModule() {
    const std::string path = mem::readString(arg(0));
    const std::string args = argBytes(arg(2), sarg(1));
    blockingLoad([&](Done d) { load(path, args, std::move(d)); }, arg(3));
}

void mlLoadModule() {
    const std::string path = mem::readString(arg(0));
    std::vector<uint8_t> image;
    std::string name;
    const bool found = locate(path, image, name);
    if (int id = provide(name, path)) return ret(id);
    if (!found) return ret(KE_NOFILE);
    ret(loadImage(std::move(image), path));
}

void mlLoadModuleAddress() {
    const std::string path = mem::readString(arg(0));
    std::vector<uint8_t> image;
    std::string name;
    const bool found = locate(path, image, name);
    if (int id = provide(name, path)) return ret(id);
    if (!found) return ret(KE_NOFILE);
    ret(loadImage(std::move(image), path, arg(1) ? 2 : 0, arg(1)));
}

std::vector<uint8_t> imageAt(uint32_t addr) {
    // The image size is not passed; take everything up to the end of RAM and let the ELF reader find its extent.
    const uint32_t a = addr & (mem::kRamSize - 1);
    std::vector<uint8_t> image(mem::kRamSize - a);
    mem::readBlock(a, image.data(), static_cast<uint32_t>(image.size()));
    return image;
}

void mlLoadModuleBuffer() { ret(loadImage(imageAt(arg(0)), "(buffer)")); }
void mlLoadModuleBufferAddress() { ret(loadImage(imageAt(arg(0)), "(buffer)", arg(1) ? 2 : 0, arg(1))); }

void mlStartModule() {
    const int id = sarg(0);
    const std::string path = mem::readString(arg(1));
    const std::string args = argBytes(arg(3), sarg(2));
    blockingLoad([&](Done d) { startModule(id, path, args, std::move(d)); }, arg(4));
}

void mlSearchModuleByName() { ret(searchByName(mem::readString(arg(0), 64))); }

void mlSearchModuleByAddress() {
    const uint32_t a = arg(0) & 0x1FFFFFFFu;
    for (const auto& m : g_modules)
        if (!m.hle && a >= m.textStart && a < m.textStart + m.textSize + m.dataSize + m.bssSize) return ret(m.id);
    ret(KE_UNKNOWN_MODULE);
}

void mlGetModuleIdList() {
    const uint32_t buf = arg(0), cap = arg(1);
    uint32_t n = 0;
    for (const auto& m : g_modules) {
        if (n < cap) mem::write32(buf + 4 * n, static_cast<uint32_t>(m.id));
        ++n;
    }
    if (arg(2)) mem::write32(arg(2), n);
    ret(KE_OK);
}

void mlReferModuleStatus() {
    const Module* m = findModule(sarg(0));
    if (!m) return ret(KE_UNKNOWN_MODULE);
    const uint32_t st = arg(1);
    mem::fill(st, 0, 96);
    const std::string n = m->name.substr(0, 55);
    mem::writeBlock(st, n.c_str(), static_cast<uint32_t>(n.size()));
    mem::write16(st + 56, m->version);
    mem::write16(st + 58, m->started ? 3 : 1);
    mem::write32(st + 60, static_cast<uint32_t>(m->id));
    mem::write32(st + 64, m->entry);
    mem::write32(st + 68, m->gp);
    mem::write32(st + 72, m->textStart);
    mem::write32(st + 76, m->textSize);
    mem::write32(st + 80, m->dataSize);
    mem::write32(st + 84, m->bssSize);
    ret(KE_OK);
}

void mlStopModule() { ret(KE_NOT_REMOVABLE); }
void mlAllocLoadMemory() { retU(kernel::allocMemory(arg(1), sarg(0), arg(2))); }
void mlFreeLoadMemory() { ret(kernel::freeMemory(arg(0))); }

} // namespace

int searchByName(const std::string& name) {
    for (const auto& m : g_modules)
        if (m.name == name || toLower(m.name) == toLower(name)) return m.id;
    return KE_UNKNOWN_MODULE;
}

bool providedByHle(const std::string& rawName) {
    const std::string name = toLower(rawName);
    return kKernelModules.count(name) != 0 || isServiceModule(name);
}

void load(const std::string& path, const std::string& args, Done done) {
    std::vector<uint8_t> image;
    std::string name;
    const bool found = locate(path, image, name);
    if (int id = provide(name, path)) return done(id, 0);
    if (!found) {
        if (ps2uc::startsWith(toLower(path), "rom")) {
            // A BIOS module we have no replacement for. Pretend it loaded and say so.
            PS2_UNIMPL("iop", "IOP module without HLE: " + name,
                       "IOP module %s is in the BIOS, which is not available, and the HLE kernel does not provide it; "
                       "reporting a successful load", path.c_str());
            return done(addHleModule(name, path), 0);
        }
        PS2_WARN("iop", "IOP module %s not found", path.c_str());
        return done(KE_NOFILE, 0);
    }
    const int id = loadImage(std::move(image), path);
    if (id < 0) return done(id, 0);
    startModule(id, path, args, std::move(done));
}

void loadFromMemory(uint32_t addr, const std::string& args, Done done) {
    std::vector<uint8_t> image = imageAt(addr);
    std::string name;
    try {
        const IrxInfo irx = ps2uc::parseIrx(ElfFile::parse(image, "(buffer)"));
        name = toLower(irx.name);
    } catch (const std::exception&) {
    }
    if (!name.empty())
        if (int id = provide(name, name)) return done(id, 0);
    const int id = loadImage(std::move(image), "(buffer)");
    if (id < 0) return done(id, 0);
    startModule(id, findModule(id)->name, args, std::move(done));
}

void init() {
    g_modules.clear();
    g_starts.clear();
    g_nextId = 1;
    g_startReturnTrap = addTrap(&startReturned, "(module start returned)");

    defineLibrary("loadcore", 0x0101, {
        {3, "GetLoadcoreInternalData", &lcInternalData},
        {4, "FlushIcache", &zero},
        {5, "FlushDcache", &zero},
        {6, "RegisterLibraryEntries", &lcRegisterLibraryEntries},
        {7, "ReleaseLibraryEntries", &lcReleaseLibraryEntries},
        {8, "LinkLibraryEntries", &lcLinkLibraryEntries},
        {9, "UnLinkLibraryEntries", &zero},
        {10, "RegisterNonAutoLinkEntries", &lcRegisterLibraryEntries},
        {11, "QueryLibraryEntryTable", &lcQueryLibraryEntryTable},
        {12, "QueryBootMode", &zero},
        {13, "RegisterBootMode", &zero},
        {14, "LockLibraryClient", &zero},
        {15, "UnLockLibraryClient", &zero},
        {16, "RegisterModule", &zero},
        {17, "ReleaseModule", &zero},
        {20, "AddRebootNotifyHandler", &zero},
        {21, "SetCacheCtrl", &zero},
        {22, "ProbeExecutableObject", nullptr},
        {23, "LoadExecutableObject", nullptr},
        {24, "SearchModuleCBByAddr", &lcSearchModuleCBByAddr},
        {25, "ApplyElfRelSection", nullptr},
        {26, "CopyModInfo", nullptr},
        {27, "SetRebootTimeLibraryHandlingMode", &zero},
    });
    defineLibrary("modload", 0x0101, {
        {3, "GetModloadInternalData", &lcInternalData},
        {4, "ReBootStart", nullptr},
        {5, "LoadModuleAddress", &mlLoadModuleAddress},
        {6, "LoadModule", &mlLoadModule},
        {7, "LoadStartModule", &mlLoadStartModule},
        {8, "StartModule", &mlStartModule},
        {9, "LoadModuleBufferAddress", &mlLoadModuleBufferAddress},
        {10, "LoadModuleBuffer", &mlLoadModuleBuffer},
        {11, "LoadStartKelfModule", nullptr},
        {12, "SetSecrmanCallbacks", &zero},
        {13, "SetCheckKelfPathCallback", &zero},
        {14, "GetLoadfileCallbacks", &zero},
        {15, "IsIllegalBootDevice", &zero},
        {16, "GetModuleIdList", &mlGetModuleIdList},
        {17, "ReferModuleStatus", &mlReferModuleStatus},
        {18, "GetModuleIdListByName", nullptr},
        {19, "LoadModuleWithOption", nullptr},
        {20, "StopModule", &mlStopModule},
        {21, "UnloadModule", &mlStopModule},
        {22, "SearchModuleByName", &mlSearchModuleByName},
        {23, "SearchModuleByAddress", &mlSearchModuleByAddress},
        {26, "SelfStopModule", &mlStopModule},
        {27, "SelfUnloadModule", &mlStopModule},
        {28, "AllocLoadMemory", &mlAllocLoadMemory},
        {29, "FreeLoadMemory", &mlFreeLoadMemory},
        {30, "SetModuleFlags", &zero},
    });
}

} // namespace ps2::iop::modules
