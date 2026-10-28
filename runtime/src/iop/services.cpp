// Host RPC services: FILEIO (host-backed files), the IOP heap, LOADFILE
// (module loading), MTAPMAN (the multitap manager), null services for modules
// whose protocols are not implemented yet, and the list of modules whose
// services the host provides.
#include "iop/iop.h"

#include "iop/cpu.h"
#include "iop/hostfs.h"
#include "iop/kernel.h"
#include "iop/memcard.h"
#include "iop/modules.h"
#include "iop/pad.h"
#include "log.h"
#include "ps2/memory.h"

#include "ps2n/util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace ps2::iop {

using ps2n::toLower;

namespace {

int32_t i32(const uint8_t* p, size_t off) {
    int32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
uint32_t u32(const uint8_t* p, size_t off) { return static_cast<uint32_t>(i32(p, off)); }
std::string cstr(const uint8_t* p, size_t off, size_t max) {
    std::string s;
    for (size_t i = 0; i < max && p[off + i]; ++i) s.push_back(static_cast<char>(p[off + i]));
    return s;
}
std::vector<uint8_t> words(std::initializer_list<int32_t> v) {
    std::vector<uint8_t> out(v.size() * 4);
    size_t i = 0;
    for (int32_t x : v) std::memcpy(out.data() + 4 * i++, &x, 4);
    return out;
}

// The answer to the version query (function 255) that SDK 2.x's EE libraries
// send FILEIO and LOADFILE and check: the module's release in ASCII, "2140",
// or "2140...." when they ask for 8 bytes.
std::vector<uint8_t> versionReply(uint32_t release, uint32_t recvSize) {
    char text[16];
    std::snprintf(text, sizeof text, "%04u....", release);
    return std::vector<uint8_t>(text, text + (recvSize >= 8 ? 8 : 4));
}

// io_stat_t in EE memory (40 bytes, old ioman layout as FILEIO uses it).
void writeEeStat(uint32_t addr, const hostfs::Stat& st) {
    fillBytes(addr, 0, 40);
    write32(addr + 0, st.dir ? 0x0027u : 0x0017u);   // FIO_SO_IFDIR/IFREG | rwx
    write32(addr + 8, static_cast<uint32_t>(st.size));
    write32(addr + 36, static_cast<uint32_t>(st.size >> 32));
}

// ---- FILEIO (sid 0x80000001) ----------------------------------------------------------
class FileIoServer final : public RpcServer {
public:
    FileIoServer() : RpcServer("FILEIO") {}

    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t recvSize, Reply reply) override {
        const uint32_t release = moduleVersion("fileio");
        if (fno == 255) return reply(versionReply(release, recvSize));
        // The requests follow the module's release. 1000 is the BIOS's (and
        // ps2sdk's) layout. From 2100 (SDK 2.1) open carries a mode and a name
        // of up to 1024 bytes, while close, read, write and lseek are unchanged
        // (checked against SDK 2.1.4's EE library); its other calls are not
        // known yet. Releases from 2200 on reply asynchronously.
        const bool sdk21 = release >= 2100;
        if (release >= 2200 || (sdk21 && fno > 4)) {
            PS2_UNIMPL("fileio", "FILEIO " + std::to_string(release) + " function " + std::to_string(fno),
                       "FILEIO function %u of release %u is not implemented", fno, release);
            return reply(words({-1}));
        }
        switch (fno) {
        case 0: return reply(words({hostfs::open(sdk21 ? cstr(d, 8, 1024) : cstr(d, 4, 256), i32(d, 0))}));
        case 1: return reply(words({hostfs::close(i32(d, 0))}));
        case 2: return reply(words({read(i32(d, 0), u32(d, 4), i32(d, 8), u32(d, 12))}));
        case 3: return reply(words({write(i32(d, 0), u32(d, 4), i32(d, 8))}));
        case 4: return reply(words({static_cast<int32_t>(hostfs::lseek(i32(d, 0), i32(d, 4), i32(d, 8)))}));
        case 6: return reply(words({hostfs::remove(cstr(d, 0, 256))}));
        case 7: return reply(words({hostfs::mkdir(cstr(d, 0, 256))}));
        case 8: return reply(words({hostfs::rmdir(cstr(d, 0, 256))}));
        case 9: return reply(words({hostfs::dopen(cstr(d, 0, 256))}));
        case 10: return reply(words({hostfs::close(i32(d, 0))}));
        case 11: return reply(words({dread(i32(d, 0), u32(d, 4))}));
        case 12: return reply(words({getstat(cstr(d, 4, 256), u32(d, 0))}));
        case 13: return reply(words({0}));   // chstat
        default:
            PS2_UNIMPL("fileio", "FILEIO function " + std::to_string(fno), "FILEIO function %u not implemented", fno);
            return reply(words({-1}));
        }
    }

private:
    static int32_t read(int32_t fd, uint32_t ptr, int32_t size, uint32_t readData) {
        if (size < 0) return hostfs::E_INVAL;
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        const int n = hostfs::read(fd, buf.data(), size);
        if (n > 0) writeBytes(ptr, buf.data(), static_cast<uint32_t>(n));
        if (readData) fillBytes(readData, 0, 16);   // nothing left for the EE to patch up
        return n;
    }

    static int32_t write(int32_t fd, uint32_t ptr, int32_t size) {
        if (size < 0) return hostfs::E_INVAL;
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        readBytes(ptr, buf.data(), static_cast<uint32_t>(size));
        return hostfs::write(fd, buf.data(), size);
    }

    static int32_t dread(int32_t fd, uint32_t buf) {
        std::string name;
        hostfs::Stat st;
        const int r = hostfs::dread(fd, name, st);
        if (r <= 0) return r;
        writeEeStat(buf, st);
        name = name.substr(0, 255);
        fillBytes(buf + 40, 0, 256);
        writeBytes(buf + 40, name.c_str(), static_cast<uint32_t>(name.size()));
        return 1;
    }

    static int32_t getstat(const std::string& path, uint32_t buf) {
        hostfs::Stat st;
        const int r = hostfs::getstat(path, st);
        if (r == 0) writeEeStat(buf, st);
        return r;
    }
};

// ---- IOP heap (sid 0x80000003) --------------------------------------------------------
// SifAllocIopHeap / SifFreeIopHeap / SifLoadIopHeap: IOP memory for the EE.
class IopHeapServer final : public RpcServer {
public:
    IopHeapServer() : RpcServer("IOPHEAP") {}

    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t, Reply reply) override {
        switch (fno) {
        case 1: return reply(words({static_cast<int32_t>(kernel::allocMemory(u32(d, 0)))}));
        case 2: return reply(words({kernel::freeMemory(u32(d, 0))}));
        case 3: {   // {address, path}: a whole file into IOP memory
            std::vector<uint8_t> data;
            const std::string path = cstr(d, 4, 252);
            if (!hostfs::readWholeFile(path, data)) return reply(words({hostfs::E_NOENT}));
            mem::writeBlock(u32(d, 0), data.data(), static_cast<uint32_t>(data.size()));
            return reply(words({0}));
        }
        default:
            PS2_UNIMPL("iopheap", "IOP heap function " + std::to_string(fno), "IOP heap function %u not implemented", fno);
            return reply(words({0}));
        }
    }
};

// ---- LOADFILE (sid 0x80000006) --------------------------------------------------------
class LoadFileServer final : public RpcServer {
public:
    LoadFileServer() : RpcServer("LOADFILE") {}

    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t, Reply reply) override {
        switch (fno) {
        case 0:   // LF_F_MOD_LOAD: {arg_len, modres, path[252], args[252]}
        case 4: { // LF_F_MG_MOD_LOAD
            const int32_t argLen = i32(d, 0);
            const std::string path = cstr(d, 8, 252);
            const std::string args = argLen > 0 ? std::string(reinterpret_cast<const char*>(d + 260), std::min(argLen, 252)) : "";
            // The reply waits for the module's start function, which may need IOP time.
            loadModule(path, args, [reply](int id, int result) { reply(words({id, result})); });
            return;
        }
        case 1:   // LF_F_ELF_LOAD
        case 5:
            PS2_UNIMPL("loadfile", "SifLoadElf", "loading an EE ELF through the IOP (\"%s\") is not supported", cstr(d, 8, 252).c_str());
            return reply(words({-1, 0}));
        case 2: {  // LF_F_SET_ADDR: poke IOP memory
            uint8_t* p = ramPtr(u32(d, 0));
            const int32_t type = i32(d, 4);
            if (type == 0) *p = d[8];
            else if (type == 1) std::memcpy(p, d + 8, 2);
            else std::memcpy(p, d + 8, 4);
            return reply(words({0}));
        }
        case 3: {  // LF_F_GET_ADDR: peek IOP memory
            uint32_t v = 0;
            std::memcpy(&v, ramPtr(u32(d, 0)), 4);
            return reply(words({static_cast<int32_t>(v)}));
        }
        case 6: {  // LF_F_MOD_BUF_LOAD: {ptr, arg_len, unused[252], args[252]}
            const int32_t argLen = i32(d, 4);
            const std::string args = argLen > 0 ? std::string(reinterpret_cast<const char*>(d + 260), std::min(argLen, 252)) : "";
            modules::loadFromMemory(u32(d, 0), args, [reply](int id, int result) { reply(words({id, result})); });
            return;
        }
        case 7:    // LF_F_MOD_STOP
        case 8:    // LF_F_MOD_UNLOAD
            return reply(words({-211, 0}));   // KE_NOT_REMOVABLE
        case 9:    // LF_F_SEARCH_MOD_BY_NAME: {id, dummy, name[252]}
            return reply(words({modules::searchByName(cstr(d, 8, 252))}));
        case 0xFF: // LF_F_GET_VERSION: the module's release in ASCII ("2140")
            return reply(versionReply(moduleVersion("loadfile"), 4));
        default:
            PS2_UNIMPL("loadfile", "LOADFILE function " + std::to_string(fno), "LOADFILE function %u not implemented", fno);
            return reply(words({-1, 0}));
        }
    }
};

// ---- MTAPMAN (sids 0x80000901-0x80000905) ---------------------------------------------
// The multitap manager, with no multitap connected. Each sid is one function
// and the function number is ignored. As in Sony's module (multitap_manager
// 3.01), the request words come back with the result written over one of them.
class MtapServer final : public RpcServer {
public:
    MtapServer(uint32_t sid, uint16_t version) : RpcServer("MTAPMAN"), sid_(sid), version_(version) {}

    void call(uint32_t, const uint8_t* d, uint32_t size, uint32_t, Reply reply) override {
        std::vector<uint8_t> out(d, d + size);
        out.resize(std::max<size_t>(out.size(), 12), 0);
        auto set = [&](size_t word, int32_t v) { std::memcpy(out.data() + 4 * word, &v, 4); };
        auto priority = [&](size_t word) {   // what IOP threads may take
            const int32_t p = i32(out.data(), 4 * word);
            return p >= 9 && p <= 123;
        };
        switch (sid_) {
        case 0x80000901u: set(1, u32(out.data(), 0) < 4 ? 1 : 0); break;   // PortOpen(port): ports 0-3 open, multitap or not
        case 0x80000902u: set(1, 1); break;                                 // PortClose(port)
        case 0x80000903u: set(1, 0); break;                                 // GetConnection(port): no multitap
        case 0x80000904u: set(2, priority(0) && priority(1) ? 1 : 0); break;   // ChangeThreadPriority(main, sif)
        case 0x80000905u: set(0, version_); break;                          // GetModVersion: libmtap checks the major
        default: break;
        }
        reply(std::move(out));
    }

private:
    uint32_t sid_;
    uint16_t version_;
};

// ---- services for modules without a protocol implementation -------------------------------
class NullServer final : public RpcServer {
public:
    NullServer(const std::string& name, uint32_t sid) : RpcServer(name), sid_(sid) {}
    void call(uint32_t fno, const uint8_t*, uint32_t size, uint32_t recvSize, Reply reply) override {
        char key[96];
        std::snprintf(key, sizeof key, "RPC %s (sid 0x%08x) fno %u", name().c_str(), sid_, fno);
        PS2_UNIMPL("iop", key, "%s: send %u bytes, expects %u back; returning zeros", key, size, recvSize);
        reply(std::vector<uint8_t>(recvSize, 0));
    }

private:
    uint32_t sid_;
};

void addNull(const char* name, std::initializer_list<uint32_t> sids) {
    for (uint32_t sid : sids)
        if (!hasServer(sid)) registerServer(sid, makeNullServer(name, sid));
}

// Modules whose job the host does, by any of their names (file or .iopmod).
const std::set<std::string> kSio2 = {"sio2man", "xsio2man", "rsio2man", "freesio2", "sio2man-nano"};
const std::set<std::string> kPad = {"padman", "xpadman", "freepad", "rpadman"};
const std::set<std::string> kMtap = {"mtapman", "xmtapman", "multitap_manager", "freemtap"};
const std::set<std::string> kMcman = {"mcman", "xmcman", "mcman_cex"};
const std::set<std::string> kMcserv = {"mcserv", "xmcserv"};
const std::set<std::string> kCdvd = {"cdvdman", "cdvd_driver", "cdvdfsv", "cdvd_ee_driver", "cdvdstm", "cdvd_st_driver"};
const std::set<std::string> kSound = {"libsd", "freesd", "sdrdrv", "sdr_driver", "clearspu"};
const std::set<std::string> kNoHardware = {"dev9", "ps2dev9", "atad", "ps2atad", "smap", "usbd", "usb_driver", "poweroff",
                                           "poweroff_handler", "ps2hdd", "ps2fs", "iLinkman", "ilinkman", "netman"};

} // namespace

std::unique_ptr<RpcServer> makeFileIoServer() { return std::make_unique<FileIoServer>(); }
std::unique_ptr<RpcServer> makeLoadFileServer() { return std::make_unique<LoadFileServer>(); }
std::unique_ptr<RpcServer> makeIopHeapServer() { return std::make_unique<IopHeapServer>(); }
std::unique_ptr<RpcServer> makeNullServer(const std::string& name, uint32_t sid) { return std::make_unique<NullServer>(name, sid); }

bool isServiceModule(const std::string& moduleName) {
    const std::string n = toLower(moduleName);
    for (const auto* set : {&kSio2, &kPad, &kMtap, &kMcman, &kMcserv, &kCdvd, &kSound, &kNoHardware})
        if (set->count(n)) return true;
    return false;
}

bool enableModuleServices(const std::string& moduleName, uint16_t version) {
    const std::string n = toLower(moduleName);
    if (kSio2.count(n) || kMcman.count(n)) return true;   // drivers with no EE-visible service of their own
    if (kPad.count(n)) {
        pad::enable(version);
        return true;
    }
    if (kMtap.count(n)) {
        // 3.01 when the module's file gave no version (a BIOS module).
        for (uint32_t sid = 0x80000901u; sid <= 0x80000905u; ++sid)
            if (!hasServer(sid)) registerServer(sid, std::make_unique<MtapServer>(sid, version ? version : uint16_t(0x0301)));
        return true;
    }
    if (kMcserv.count(n)) {
        if (!hasServer(0x80000400u)) registerServer(0x80000400u, memcard::makeServer());
        return true;
    }
    if (kSound.count(n)) {
        addNull("SDRDRV", {0x80000701u});   // the EE's libsdr; libsd itself is sound.cpp
        return true;
    }
    if (kCdvd.count(n)) return true;   // resident: the servers exist from start-up (cdvd.cpp)
    if (kNoHardware.count(n)) {
        PS2_UNIMPL("iop", "IOP module for missing hardware: " + n,
                   "IOP module \"%s\" drives hardware that is not emulated (network, hard disk, USB, ...); it loads but does nothing",
                   n.c_str());
        return true;
    }
    return false;
}

} // namespace ps2::iop
