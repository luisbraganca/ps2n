// cdvdfsv and cdvdman over the disc layer.
//
// EE side: the RPC servers libcdvd binds. 0x80000592 init, 0x8000059A and
// 0x8000059C disc ready, 0x80000593 S-commands (clock, disc type, status,
// tray, ...), 0x80000595 N-commands (reads, seeks, streaming) and 0x80000597
// file search. A read lands in EE memory before the reply, so the EE's
// completion handler finds it in place; the unaligned head and tail that the
// real server sends separately are written with the rest, and the packet for
// them says there is nothing to copy.
//
// IOP side: the cdvdman library game modules import. Commands finish a little
// after the call, as on the drive, then raise the sceCdCallback callback;
// sceCdSync waits for them. Streaming reads (sceCdSt*) come straight from the
// disc, with the ring buffer always reported full.
#include "iop/cdvd.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/disc.h"
#include "iop/hle.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "log.h"
#include "ps2/memory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <vector>

namespace ps2::iop::cdvd {

using namespace hle;

namespace {

constexpr int32_t kDiskNone = 0x00, kDiskPs2Cd = 0x12, kDiskPs2Dvd = 0x14;
constexpr int32_t kStatStop = 0x00, kStatPause = 0x0A;
constexpr int32_t kErrNone = 0x00, kErrAbort = 0x01, kErrRead = 0x30;
constexpr int32_t kComplete = 2, kNotReady = 6;
constexpr uint32_t kVersion = 0x0226;   // cdvdfsv / cdvdman 2.38
// sceCdCallback reasons.
constexpr uint32_t kFuncRead = 1, kFuncCdda = 2, kFuncSeek = 4, kFuncStandby = 5, kFuncStop = 6, kFuncPause = 7,
                   kFuncBreak = 8;
constexpr char kModel[] = "SCPH-39001";

struct Handler {
    uint32_t fn = 0, gp = 0, arg = 0;
};

struct Drive {
    int32_t error = kErrNone;
    int32_t status = kStatPause;
    bool trayChecked = false;   // the first tray check reports a new disc
    // The IOP command in flight.
    bool busy = false;
    uint32_t func = 0;
    uint64_t event = 0;
    std::vector<int> waiters;   // threads in sceCdSync
    uint32_t readLsn = 0, readSectors = 0, readBuf = 0, readSize = 2048;
    bool readCdda = false;
    uint32_t readPos = 0;
    Handler callback, powerOff;
    // Streaming.
    bool streaming = false;
    uint32_t streamLsn = 0, streamBufMax = 0;
};
Drive g;

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

int32_t diskType() {
    switch (disc::type()) {
    case disc::Type::Dvd: return kDiskPs2Dvd;
    case disc::Type::Cd: return kDiskPs2Cd;
    default: return kDiskNone;
    }
}
int32_t driveStatus() { return disc::type() == disc::Type::None ? kStatStop : g.status; }

// Bytes per sector for a read mode (sceCdRMode as a word: data pattern in bits 16-23).
uint32_t sectorSize(uint32_t mode) {
    if (disc::type() != disc::Type::Cd) return 2048;
    switch ((mode >> 16) & 0xFF) {
    case 1: return 2328;
    case 2: return 2340;
    default: return 2048;
    }
}

uint8_t bcd(int v) { return static_cast<uint8_t>((((v / 10) % 10) << 4) | (v % 10)); }
int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }

// Reads up to `count` sectors of `size` bytes from `lsn` into `out`; returns
// how many were read (fewer past the end of the disc). CD sectors larger than
// 2048 bytes are rebuilt around the user data an image holds: 2328 bytes are
// the data and zeros where the error correction codes would be; 2340 bytes
// add the header (the sector's address, mode 2) and the subheader before it.
uint32_t readSectors(uint32_t lsn, uint32_t count, uint32_t size, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(count) * size, 0);
    uint8_t data[2048];
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* s = out.data() + static_cast<size_t>(i) * size;
        if (!disc::read(lsn + i, 1, size == 2048 ? s : data)) {
            out.resize(static_cast<size_t>(i) * size);
            return i;
        }
        if (size == 2328) {
            std::memcpy(s, data, sizeof data);
        } else if (size == 2340) {
            const uint32_t abs = lsn + i + 150;
            s[0] = bcd(static_cast<int>(abs / 75 / 60));
            s[1] = bcd(static_cast<int>(abs / 75 % 60));
            s[2] = bcd(static_cast<int>(abs % 75));
            s[3] = 2;
            s[6] = s[10] = 0x08;   // subheader submode: data
            std::memcpy(s + 12, data, sizeof data);
        }
    }
    return count;
}

// sceCdCLOCK: status, second, minute, hour, padding, day, month, year, in BCD
// and Japan time like the console's clock.
std::array<uint8_t, 8> clockBcd() {
    std::time_t t = static_cast<std::time_t>(rtcTime() + 9 * 3600);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return {0, bcd(tm.tm_sec), bcd(tm.tm_min), bcd(tm.tm_hour), 0, bcd(tm.tm_mday), bcd(tm.tm_mon + 1), bcd(tm.tm_year % 100)};
}

// sceCdlFILE (32 bytes: lsn, size, name[16], date[8]) and the ISO 9660 flags
// after it, as cdvdman fills them.
bool searchFile(const std::string& path, int layer, std::array<uint8_t, 36>& rec) {
    disc::Entry e;
    if (layer != 0 || !disc::find(path, e)) return false;
    rec.fill(0);
    std::memcpy(rec.data(), &e.lsn, 4);
    std::memcpy(rec.data() + 4, &e.size, 4);
    std::memcpy(rec.data() + 8, e.name.data(), std::min<size_t>(15, e.name.size()));
    rec[25] = e.date[5];   // second
    rec[26] = e.date[4];   // minute
    rec[27] = e.date[3];   // hour
    rec[28] = e.date[2];   // day
    rec[29] = e.date[1];   // month
    const auto year = static_cast<uint16_t>(1900 + e.date[0]);
    std::memcpy(rec.data() + 30, &year, 2);
    rec[32] = e.dir ? 0x02 : 0x00;
    return true;
}

// ---- EE: N-commands ------------------------------------------------------------------------
// Sector data into EE memory, then the head/tail packet (nothing left to copy)
// and the read position. Returns the bytes delivered.
uint32_t readToEe(uint32_t lbn, uint32_t sectors, uint32_t dest, uint32_t size, bool cdda) {
    std::vector<uint8_t> data;
    uint32_t got = sectors;
    if (cdda) data.assign(static_cast<size_t>(sectors) * size, 0);   // no audio tracks: silence
    else got = readSectors(lbn, sectors, size, data);
    if (!data.empty()) writeBytes(dest, data.data(), static_cast<uint32_t>(data.size()));
    g.error = got == sectors ? kErrNone : kErrRead;
    g.status = kStatPause;
    return static_cast<uint32_t>(data.size());
}

// fno 1 / 0x13 (sceCdRead) and 2 (sceCdReadCDDA): lbn, sectors, EE buffer,
// mode, EE address of the head/tail packet, EE address of the read position.
int32_t eeRead(const uint8_t* d, bool cdda) {
    const uint32_t mode = u32(d, 12);
    const uint32_t size = cdda ? ((mode >> 16) & 0xFF) == 1 ? 2368u : 2352u : sectorSize(mode);
    const uint32_t n = readToEe(u32(d, 0), u32(d, 4), u32(d, 8), size, cdda);
    if (u32(d, 16)) fillBytes(u32(d, 16), 0, 144);
    if (u32(d, 20)) write32(u32(d, 20), n);
    return static_cast<int32_t>(n);
}

// fno 0x0D (sceCdReadIOPMem): lbn, sectors, IOP buffer, mode, -, EE address of the read position.
int32_t eeReadIopMem(const uint8_t* d) {
    std::vector<uint8_t> data;
    const uint32_t sectors = u32(d, 4);
    const uint32_t got = readSectors(u32(d, 0), sectors, sectorSize(u32(d, 12)), data);
    mem::writeBlock(u32(d, 8), data.data(), static_cast<uint32_t>(data.size()));
    g.error = got == sectors ? kErrNone : kErrRead;
    if (u32(d, 20)) write32(u32(d, 20), static_cast<uint32_t>(data.size()));
    return static_cast<int32_t>(data.size());
}

// fno 0x0F (sceCdReadChain): up to 64 {lbn, sectors, buffer} entries ending
// with an all-ones field; bit 0 of the buffer marks an IOP address.
int32_t eeReadChain(const uint8_t* d) {
    const uint32_t size = sectorSize(u32(d, 780));
    uint32_t pos = 0;
    for (int i = 0; i < 64; ++i) {
        const uint32_t lbn = u32(d, 12u * i), sectors = u32(d, 12u * i + 4), buf = u32(d, 12u * i + 8);
        if (lbn == 0xFFFFFFFFu || sectors == 0xFFFFFFFFu || buf == 0xFFFFFFFFu) break;
        std::vector<uint8_t> data;
        const uint32_t got = readSectors(lbn, sectors, size, data);
        if (buf & 1) mem::writeBlock(buf & ~1u, data.data(), static_cast<uint32_t>(data.size()));
        else writeBytes(buf, data.data(), static_cast<uint32_t>(data.size()));
        pos += static_cast<uint32_t>(data.size());
        if (got != sectors) {
            g.error = kErrRead;
            break;
        }
    }
    if (u32(d, 784)) write32(u32(d, 784), pos);
    return static_cast<int32_t>(pos);
}

// Streaming commands (sceCdSt*), from the EE (fno 9) and the IOP. Returns the
// result word: for reads, sectors copied in the low half and an error code in
// the high half.
int32_t streamCommand(uint32_t cmd, uint32_t arg1, uint32_t arg2, uint32_t buffer, bool eeBuffer) {
    switch (cmd) {
    case 5:   // init: ring size in sectors, banks, IOP buffer
        g.streamBufMax = arg1;
        return 1;
    case 1:   // start at lbn
    case 4:   // seek
    case 9:   // seek from cdrom0: lseek
        g.streamLsn = arg1;
        g.streaming = true;
        return 1;
    case 2: {   // read `arg2` sectors
        if (!g.streaming) return 0x111 << 16;
        std::vector<uint8_t> data;
        const uint32_t got = readSectors(g.streamLsn, arg2, 2048, data);
        if (eeBuffer) writeBytes(buffer, data.data(), static_cast<uint32_t>(data.size()));
        else mem::writeBlock(buffer, data.data(), static_cast<uint32_t>(data.size()));
        g.streamLsn += got;
        return static_cast<int32_t>((got & 0xFFFF) | (got == arg2 ? 0u : static_cast<uint32_t>(kErrRead) << 16));
    }
    case 3:   // stop
        g.streaming = false;
        return 1;
    case 6:   // sectors buffered
        return g.streaming ? static_cast<int32_t>(g.streamBufMax) : 0;
    case 7:   // pause
    case 8:   // resume
        return 1;
    default:
        return 0;
    }
}

class InitServer final : public RpcServer {
public:
    InitServer() : RpcServer("CDVDFSV init") {}
    void call(uint32_t, const uint8_t*, uint32_t, uint32_t, Reply reply) override {
        g.error = kErrNone;
        reply(words({1, static_cast<int32_t>(kVersion), static_cast<int32_t>(kVersion), 0}));
    }
};

// The drive is always ready; without a disc, the disc type says so.
class ReadyServer final : public RpcServer {
public:
    ReadyServer() : RpcServer("CDVDFSV disk ready") {}
    void call(uint32_t, const uint8_t*, uint32_t, uint32_t, Reply reply) override { reply(words({kComplete})); }
};

class SCmdServer final : public RpcServer {
public:
    SCmdServer() : RpcServer("CDVDFSV S-commands") {}
    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t, Reply reply) override {
        switch (fno) {
        case 0x01: {   // sceCdReadClock
            std::vector<uint8_t> out = words({1, 0, 0});
            const auto clock = clockBcd();
            std::memcpy(out.data() + 4, clock.data(), clock.size());
            return reply(out);
        }
        case 0x03: return reply(words({diskType()}));   // sceCdGetDiskType
        case 0x04: return reply(words({g.error}));       // sceCdGetError
        case 0x05: {                                     // sceCdTrayReq
            int32_t changed = 0;
            if (i32(d, 0) == 2) {
                changed = g.trayChecked ? 0 : 1;
                g.trayChecked = true;
            }
            return reply(words({1, changed}));
        }
        case 0x06: return reply(words({1, 0, 0, 0}));    // sceCdRI: i.LINK id
        case 0x0B: return reply(words({0, 0, 0, 0}));    // sceCdApplySCmd: 16 result bytes
        case 0x0C: return reply(words({driveStatus()}));   // sceCdStatus
        case 0x15: return reply(words({1, 0}));          // sceCdCtrlADout
        case 0x16:                                       // sceCdBreak
            if (g.streaming || g.busy) g.error = kErrAbort;
            return reply(words({1}));
        case 0x1A: {                                     // sceCdRM: model name
            std::vector<uint8_t> out = words({1, 0, 0, 0, 0, 0});
            std::memcpy(out.data() + 8, kModel, sizeof kModel - 1);
            return reply(out);
        }
        case 0x21:                                       // sceCdPowerOff
            PS2_INFO("cdvd", "the game asked to power off the console");
            return reply(words({1, 0}));
        case 0x22: return reply(words({1}));             // sceCdMmode
        case 0x23: return reply(words({0}));             // sceCdChangeThreadPriority
        case 0x24: return reply(words({1, 0, 0, 0}));    // sceCdReadGUID
        case 0x25: return reply(words({1}));             // sceCdSetTimeout
        case 0x26: return reply(words({1, 0}));          // sceCdReadModelID
        case 0x27: return reply(words({1, 0, 0}));       // sceCdReadDvdDualInfo: one layer
        default: {
            char key[64];
            std::snprintf(key, sizeof key, "CDVDFSV S-command 0x%02x", fno);
            PS2_UNIMPL("cdvd", key, "%s not implemented; answering 0", key);
            return reply(words({0, 0, 0, 0}));
        }
        }
    }
};

class NCmdServer final : public RpcServer {
public:
    NCmdServer() : RpcServer("CDVDFSV N-commands") {}
    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t, Reply reply) override {
        int32_t r = 1;
        switch (fno) {
        case 0x01: case 0x13: r = eeRead(d, false); break;
        case 0x02: r = eeRead(d, true); break;
        case 0x04: {   // sceCdGetToc: 2064 bytes to EE memory, then {result, is DVD}
            fillBytes(u32(d, 0), 0, 0x810);
            return reply(words({1, disc::type() == disc::Type::Dvd ? 1 : 0}));
        }
        case 0x05: case 0x06: case 0x08: g.status = kStatPause; break;   // seek, standby, pause
        case 0x07: g.status = kStatStop; break;                          // stop
        case 0x09: r = streamCommand(u32(d, 12), u32(d, 0), u32(d, 4), u32(d, 8), true); break;
        case 0x0C: break;                                                // sceCdApplyNCmd
        case 0x0D: r = eeReadIopMem(d); break;
        case 0x0E: r = diskType() == kDiskNone ? kNotReady : kComplete; break;
        case 0x0F: r = eeReadChain(d); break;
        default: {
            char key[64];
            std::snprintf(key, sizeof key, "CDVDFSV N-command 0x%02x", fno);
            PS2_UNIMPL("cdvd", key, "%s not implemented; answering 0", key);
            r = 0;
            break;
        }
        }
        reply(words({r}));
    }
};

// The request layout depends on its size: 0x124 (old libraries), 0x128 (with
// the file's ISO flags), 0x12C (with a layer for sceCdLayerSearchFile). The
// sceCdlFILE goes back to EE memory even when nothing is found, unchanged.
class SearchServer final : public RpcServer {
public:
    SearchServer() : RpcServer("CDVDFSV search") {}
    void call(uint32_t, const uint8_t* d, uint32_t size, uint32_t, Reply reply) override {
        const bool flags = size == 0x128 || size == 0x12C;
        const size_t pathAt = flags ? 0x24 : 0x20, destAt = flags ? 0x124 : 0x120;
        const size_t recordSize = flags ? 36 : 32;
        const std::string path = cstr(d, pathAt, 256);
        std::array<uint8_t, 36> rec{};
        std::memcpy(rec.data(), d, recordSize);
        const bool found = searchFile(path, size == 0x12C ? i32(d, 0x128) : 0, rec);
        PS2_DEBUG("cdvd", "sceCdSearchFile(\"%s\") = %d", path.c_str(), found ? 1 : 0);
        if (u32(d, destAt)) writeBytes(u32(d, destAt), rec.data(), static_cast<uint32_t>(recordSize));
        reply(words({found ? 1 : 0, 0, 0, 0}));
    }
};

// ---- IOP: cdvdman ----------------------------------------------------------------------------
void commandDone(uint64_t) {
    g.event = 0;
    if (g.func == kFuncRead || g.func == kFuncCdda) {
        std::vector<uint8_t> data;
        uint32_t got = g.readSectors;
        if (g.readCdda) data.assign(static_cast<size_t>(g.readSectors) * g.readSize, 0);
        else got = readSectors(g.readLsn, g.readSectors, g.readSize, data);
        mem::writeBlock(g.readBuf, data.data(), static_cast<uint32_t>(data.size()));
        g.readPos = static_cast<uint32_t>(data.size());
        g.error = got == g.readSectors ? kErrNone : kErrRead;
    }
    g.status = g.func == kFuncStop ? kStatStop : kStatPause;
    g.busy = false;
    for (int t : g.waiters) kernel::wakeThread(t, 0);
    g.waiters.clear();
    if (g.callback.fn) kernel::callHandler(g.callback.fn, g.func, g.callback.gp);
}

// Starts an IOP command; it ends `cycles` IOP cycles later.
bool startCommand(uint32_t func, uint64_t cycles) {
    if (g.busy) return false;
    g.busy = true;
    g.func = func;
    g.readPos = 0;
    g.event = scheduleEvent(nowCycles() + (1000 + cycles) * kEeCyclesPerIopCycle, &commandDone, 0);
    return true;
}

void startRead(bool cdda) {
    if (g.busy || disc::type() == disc::Type::None) return ret(0);
    const uint32_t mode = arg(3) ? mem::read32(arg(3)) : 0;
    g.readLsn = arg(0);
    g.readSectors = arg(1);
    g.readBuf = arg(2);
    g.readCdda = cdda;
    g.readSize = cdda ? ((mode >> 16) & 0xFF) == 1 ? 2368u : 2352u : sectorSize(mode);
    ret(startCommand(cdda ? kFuncCdda : kFuncRead, static_cast<uint64_t>(g.readSectors) * g.readSize / 16) ? 1 : 0);
}

void cdInit() {
    g.error = kErrNone;
    ret(1);
}
void cdRead() { startRead(false); }
void cdReadCdda() { startRead(true); }
void cdSeek() { ret(startCommand(kFuncSeek, 0) ? 1 : 0); }
void cdStandby() { ret(startCommand(kFuncStandby, 0) ? 1 : 0); }
void cdStop() { ret(startCommand(kFuncStop, 0) ? 1 : 0); }
void cdPause() { ret(startCommand(kFuncPause, 0) ? 1 : 0); }
void cdGetError() { ret(g.error); }
void cdGetDiskType() { ret(diskType()); }
void cdDiskReady() { ret(kComplete); }
void cdStatus() { ret(driveStatus()); }
void cdCheckCmd() { ret(g.busy ? 0 : 1); }
void cdGetReadPos() { retU(g.readPos); }
void cdOne() { ret(1); }
void cdZero() { ret(0); }

// sceCdSync: 0 when idle; the polling modes (1, 17) return 1 while busy, the
// others wait for the command to end.
void cdSync() {
    if (!g.busy) return ret(0);
    const int mode = sarg(0);
    if (mode == 1 || mode == 17 || kernel::inInterrupt() || kernel::currentThread() < 0) return ret(1);
    g.waiters.push_back(kernel::currentThread());
    kernel::blockCurrent(kernel::TSW_SERVICE, 0);
}

void cdBreak() {
    if (g.busy) {
        cancelEvent(g.event);
        g.event = 0;
        g.busy = false;
        g.error = kErrAbort;
        for (int t : g.waiters) kernel::wakeThread(t, 0);
        g.waiters.clear();
        if (g.callback.fn) kernel::callHandler(g.callback.fn, kFuncBreak, g.callback.gp);
    }
    ret(1);
}

void cdCallback() {
    if (g.busy) return ret(0);
    retU(g.callback.fn);
    g.callback = Handler{arg(0), g_cpu.gpr[R_GP], 0};
}

void cdPowerOffCallback() {
    retU(g.powerOff.fn);
    g.powerOff = Handler{arg(0), g_cpu.gpr[R_GP], arg(1)};
}

// sceCdSearchFile(sceCdlFILE*, path) and sceCdLayerSearchFile(..., layer).
void cdSearchFile() {
    std::array<uint8_t, 36> rec{};
    const std::string path = mem::readString(arg(1), 1024);
    const bool found = searchFile(path, 0, rec);
    if (found) mem::writeBlock(arg(0), rec.data(), 32);
    ret(found ? 1 : 0);
}
void cdLayerSearchFile() {
    std::array<uint8_t, 36> rec{};
    const bool found = searchFile(mem::readString(arg(1), 1024), sarg(2), rec);
    if (found) mem::writeBlock(arg(0), rec.data(), 32);
    ret(found ? 1 : 0);
}

void cdGetToc() {
    mem::fill(arg(0), 0, disc::type() == disc::Type::Dvd ? 2064 : 1024);
    ret(1);
}

void cdTrayReq() {
    if (arg(1)) {
        uint32_t changed = 0;
        if (sarg(0) == 2) {
            changed = g.trayChecked ? 0 : 1;
            g.trayChecked = true;
        }
        mem::write32(arg(1), changed);
    }
    ret(1);
}

void cdReadClock() {
    const auto clock = clockBcd();
    mem::writeBlock(arg(0), clock.data(), static_cast<uint32_t>(clock.size()));
    ret(1);
}

// sceCdlLOCCD: minute, second, sector (BCD), track; positions count from 00:02:00.
void cdPosToInt() {
    const uint32_t p = arg(0);
    const int sector = (fromBcd(mem::read8(p)) * 60 + fromBcd(mem::read8(p + 1))) * 75 + fromBcd(mem::read8(p + 2));
    ret(sector - 150);
}
void cdIntToPos() {
    const uint32_t i = arg(0) + 150, p = arg(1);
    mem::write8(p, bcd(static_cast<int>(i / 75 / 60)));
    mem::write8(p + 1, bcd(static_cast<int>(i / 75 % 60)));
    mem::write8(p + 2, bcd(static_cast<int>(i % 75)));
    mem::write8(p + 3, 0);
    retU(p);
}

void cdStatusOut(uint32_t statusPtr) {
    if (statusPtr) mem::write32(statusPtr, 0);
}
void cdRi() {   // (buffer[8], status*)
    mem::fill(arg(0), 0, 8);
    cdStatusOut(arg(1));
    ret(1);
}
void cdRm() {   // (name[16], status*)
    mem::fill(arg(0), 0, 16);
    mem::writeBlock(arg(0), kModel, sizeof kModel - 1);
    cdStatusOut(arg(1));
    ret(1);
}
void cdApplySCmd() {   // (cmd, in, in size, out[16])
    mem::fill(arg(3), 0, 16);
    ret(1);
}
void cdCtrlADout() {   // (mode, status*)
    cdStatusOut(arg(1));
    ret(1);
}
void cdPowerOff() {   // (status*)
    PS2_INFO("cdvd", "an IOP module asked to power off the console");
    cdStatusOut(arg(0));
    ret(1);
}
void cdReadDvdDualInfo() {   // (on dual*, layer 1 start*): one layer
    if (arg(0)) mem::write32(arg(0), 0);
    if (arg(1)) mem::write32(arg(1), 0);
    ret(1);
}

void stInit() { ret(streamCommand(5, arg(0), arg(1), arg(2), false)); }
void stStart() { ret(streamCommand(1, arg(0), 0, 0, false)); }
void stSeek() { ret(streamCommand(4, arg(0), 0, 0, false)); }
void stSeekF() { ret(streamCommand(9, arg(0), 0, 0, false)); }
void stStop() { ret(streamCommand(3, 0, 0, 0, false)); }
void stStat() { ret(streamCommand(6, 0, 0, 0, false)); }
void stPause() { ret(streamCommand(7, 0, 0, 0, false)); }
void stResume() { ret(streamCommand(8, 0, 0, 0, false)); }
// sceCdStRead(sectors, buffer, mode, error*): returns sectors read.
void stRead() {
    const auto r = static_cast<uint32_t>(streamCommand(2, 0, arg(0), arg(1), false));
    if (arg(3)) mem::write32(arg(3), r >> 16);
    ret(static_cast<int32_t>(r & 0xFFFF));
}

} // namespace

void init() {
    if (g.event) cancelEvent(g.event);
    g = Drive{};
    defineLibrary("cdvdman", 0x0101, {
        {2, "cdvdman_termcall", &cdZero},
        {4, "sceCdInit", &cdInit},
        {5, "sceCdStandby", &cdStandby},
        {6, "sceCdRead", &cdRead},
        {7, "sceCdSeek", &cdSeek},
        {8, "sceCdGetError", &cdGetError},
        {9, "sceCdGetToc", &cdGetToc},
        {10, "sceCdSearchFile", &cdSearchFile},
        {11, "sceCdSync", &cdSync},
        {12, "sceCdGetDiskType", &cdGetDiskType},
        {13, "sceCdDiskReady", &cdDiskReady},
        {14, "sceCdTrayReq", &cdTrayReq},
        {15, "sceCdStop", &cdStop},
        {16, "sceCdPosToInt", &cdPosToInt},
        {17, "sceCdIntToPos", &cdIntToPos},
        {21, "sceCdCheckCmd", &cdCheckCmd},
        {22, "sceCdRI", &cdRi},
        {24, "sceCdReadClock", &cdReadClock},
        {28, "sceCdStatus", &cdStatus},
        {29, "sceCdApplySCmd", &cdApplySCmd},
        {37, "sceCdCallback", &cdCallback},
        {38, "sceCdPause", &cdPause},
        {39, "sceCdBreak", &cdBreak},
        {40, "sceCdReadCDDA", &cdReadCdda},
        {44, "sceCdGetReadPos", &cdGetReadPos},
        {45, "sceCdCtrlADout", &cdCtrlADout},
        {46, "sceCdNop", &cdOne},
        {47, "sceGetFsvRbuf", nullptr},
        {50, "sceCdSC", nullptr},
        {51, "sceCdRC", &cdReadClock},
        {54, "sceCdApplyNCmd", &cdOne},
        {56, "sceCdStInit", &stInit},
        {57, "sceCdStRead", &stRead},
        {58, "sceCdStSeek", &stSeek},
        {59, "sceCdStStart", &stStart},
        {60, "sceCdStStat", &stStat},
        {61, "sceCdStStop", &stStop},
        {62, "sceCdRead0", nullptr},
        {63, "sceCdRV", nullptr},
        {64, "sceCdRM", &cdRm},
        {66, "sceCdReadChain", &cdZero},
        {67, "sceCdStPause", &stPause},
        {68, "sceCdStResume", &stResume},
        {74, "sceCdPowerOff", &cdPowerOff},
        {75, "sceCdMmode", &cdOne},
        {77, "sceCdStSeekF", &stSeekF},
        {78, "sceCdPOffCallback", &cdPowerOffCallback},
        {81, "sceCdSetTimeout", &cdOne},
        {83, "sceCdReadDvdDualInfo", &cdReadDvdDualInfo},
        {84, "sceCdLayerSearchFile", &cdLayerSearchFile},
        {114, "sceCdRE", &cdRead},
    });
}

void registerServers() {
    registerServer(0x80000592u, std::make_unique<InitServer>());
    registerServer(0x8000059Au, std::make_unique<ReadyServer>());
    registerServer(0x8000059Cu, std::make_unique<ReadyServer>());
    registerServer(0x80000593u, std::make_unique<SCmdServer>());
    registerServer(0x80000595u, std::make_unique<NCmdServer>());
    registerServer(0x80000597u, std::make_unique<SearchServer>());
}

} // namespace ps2::iop::cdvd
