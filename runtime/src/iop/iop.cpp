#include "iop/iop.h"

#include "clock.h"
#include "hw/hw.h"
#include "internal.h"
#include "iop/cdvd.h"
#include "iop/cpu.h"
#include "iop/disc.h"
#include "iop/hle.h"
#include "iop/hostfs.h"
#include "iop/kernel.h"
#include "iop/libs.h"
#include "iop/memcard.h"
#include "iop/modules.h"
#include "iop/pad.h"
#include "iop/sif.h"
#include "iop/sound.h"
#include "iop/spu2.h"
#include "log.h"
#include "ps2/memory.h"

#include "ps2n/util.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>

namespace ps2::iop {

namespace {

// ---- SIF constants (sifcmd-common.h / sifdma.h) --------------------------------------
constexpr uint32_t SIF_CMD_CHANGE_SADDR = 0x80000000u, SIF_CMD_SET_SREG = 0x80000001u, SIF_CMD_INIT_CMD = 0x80000002u,
                   SIF_CMD_RESET_CMD = 0x80000003u, SIF_CMD_RPC_END = 0x80000008u, SIF_CMD_RPC_BIND = 0x80000009u,
                   SIF_CMD_RPC_CALL = 0x8000000Au, SIF_CMD_RPC_RDATA = 0x8000000Cu;
constexpr uint32_t SIF_REG_MAINADDR = 1, SIF_REG_SUBADDR = 2, SIF_REG_MSFLAG = 3, SIF_REG_SMFLAG = 4;
constexpr uint32_t SIF_STAT_SIFINIT = 0x10000, SIF_STAT_CMDINIT = 0x20000, SIF_STAT_BOOTEND = 0x40000;

struct HostServer {
    std::unique_ptr<RpcServer> impl;
    uint32_t sd = 0;     // IOP address standing in for SifRpcServerData_t
    uint32_t buf = 0;    // IOP receive buffer
};
std::map<uint32_t, HostServer> g_servers;   // by sid

struct CommandHandler {
    uint32_t fn = 0, arg = 0, gp = 0;
};
std::map<uint32_t, CommandHandler> g_cmdHandlers;   // module code's SIF command handlers, by cid
std::vector<uint32_t> g_rpcQueues;                 // SifRpcDataQueue_t of module servers, in registration order
std::map<uint32_t, uint64_t> g_unserved;           // sid -> time of the first bind nobody answered
std::array<uint32_t, 32> g_iopSregs{};

uint32_t g_mainAddr = 0, g_msflag = 0, g_smflag = 0;
std::map<uint32_t, uint32_t> g_sysregs;   // SIF_SYSREG_* (0x8000000x) storage
bool g_rebootPending = false;
constexpr uint32_t kBiosModuleVersion = 1000;
std::map<std::string, uint32_t> g_moduleVersions;   // from the replacement image of the last IOP reset
uint32_t g_eeCmdBuffer = 0;              // EE address of its receive buffer
uint32_t g_dmaId = 1;
uint64_t g_iopTime = 0;                  // EE cycles the IOP has caught up to

std::deque<std::vector<uint8_t>> g_toEe;   // IOP->EE command packets waiting for the EE's buffer

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); }

// Delivers the next queued packet if the EE's receive buffer is free.
void pumpToEe() {
    if (g_toEe.empty() || !g_eeCmdBuffer) return;
    if (read8(g_eeCmdBuffer) != 0) return;   // psize != 0: the EE has not consumed the previous packet
    const std::vector<uint8_t> pkt = std::move(g_toEe.front());
    g_toEe.pop_front();
    writeBytes(g_eeCmdBuffer, pkt.data(), static_cast<uint32_t>(pkt.size()));
    hw::dmacRaise(hw::DMA_SIF0);
}

void sendToEe(std::vector<uint8_t> pkt) {
    g_toEe.push_back(std::move(pkt));
    pumpToEe();
}

std::vector<uint8_t> makePacket(uint32_t cid, uint32_t size) {
    std::vector<uint8_t> p(size, 0);
    p[0] = static_cast<uint8_t>(size);   // psize
    wr32(p, 8, cid);
    return p;
}

// SifRpcRendPkt_t to the EE.
void sendRpcEnd(uint32_t recId, uint32_t pktAddr, uint32_t rpcId, uint32_t cd, uint32_t forCid, uint32_t sd, uint32_t buf) {
    auto p = makePacket(SIF_CMD_RPC_END, 64);
    wr32(p, 16, recId);
    wr32(p, 20, pktAddr);
    wr32(p, 24, rpcId);
    wr32(p, 28, cd);
    wr32(p, 32, forCid);
    wr32(p, 36, sd);
    wr32(p, 40, buf);
    sendToEe(std::move(p));
}

// Completion of a call made without an end function: no RPC_END; the IOP
// overwrites the EE's request packet so its ids no longer match, which is
// what sceSifCheckStatRpc looks for.
void releaseEePacket(uint32_t pktAddr) {
    if (!pktAddr) return;
    write32(pktAddr + 16, 0);   // rec_id
    write32(pktAddr + 24, 0);   // rpc_id
}

HostServer* hostServerBySd(uint32_t sd) {
    for (auto& [sid, s] : g_servers)
        if (s.sd == sd) return &s;
    return nullptr;
}

uint32_t guestServerBySid(uint32_t sid) {
    for (uint32_t q : g_rpcQueues)
        for (uint32_t sd = mem::read32(q + 8); sd; sd = mem::read32(sd + 56))
            if (mem::read32(sd) == sid) return sd;
    return 0;
}

bool isGuestServer(uint32_t sd) {
    for (uint32_t q : g_rpcQueues)
        for (uint32_t s = mem::read32(q + 8); s; s = mem::read32(s + 56))
            if (s == sd) return true;
    return false;
}

void handleBind(const uint8_t* pkt) {
    const uint32_t recId = rd32(pkt + 16), pktAddr = rd32(pkt + 20), rpcId = rd32(pkt + 24);
    const uint32_t cd = rd32(pkt + 28), sid = rd32(pkt + 32);
    uint32_t sd = 0, buf = 0;
    if (auto it = g_servers.find(sid); it != g_servers.end()) {
        sd = it->second.sd;
        buf = it->second.buf;
    } else if (const uint32_t g = guestServerBySid(sid)) {
        sd = g;
        buf = mem::read32(g + 8);
    } else {
        // Nobody serves this sid yet. The EE's bind loop retries, which gives a
        // module that is starting time to register; after a while a null
        // service answers, so a missing module does not hang the game.
        const auto [since, first] = g_unserved.try_emplace(sid, nowCycles());
        if (nowCycles() - since->second > kEeClockHz / 2) {
            PS2_UNIMPL("iop", "RPC bind to unknown sid",
                       "RPC bind to sid 0x%08x: no module serves it; answering with a null service", sid);
            registerServer(sid, makeNullServer("unknown", sid));
            sd = g_servers[sid].sd;
            buf = g_servers[sid].buf;
        } else {
            PS2_DEBUG("iop", "RPC bind to sid 0x%08x: no server yet", sid);
        }
    }
    PS2_DEBUG("iop", "bind sid 0x%08x -> sd 0x%08x", sid, sd);
    sendRpcEnd(recId, pktAddr, rpcId, cd, SIF_CMD_RPC_BIND, sd, buf);
}

void handleCall(const uint8_t* pkt) {
    const uint32_t recId = rd32(pkt + 16), pktAddr = rd32(pkt + 20), rpcId = rd32(pkt + 24), cd = rd32(pkt + 28);
    const uint32_t fno = rd32(pkt + 32), ssize = rd32(pkt + 36);
    const uint32_t recvbuf = rd32(pkt + 40), rsize = rd32(pkt + 44), rmode = rd32(pkt + 48), sd = rd32(pkt + 52);
    if (HostServer* s = hostServerBySd(sd)) {
        const uint32_t sendSize = std::min(ssize, s->impl->bufferSize());
        PS2_TRACE("iop", "call %s fno %u send %u recv %u", s->impl->name().c_str(), fno, ssize, rsize);
        const uint32_t buf = s->buf;
        s->impl->call(fno, mem::ram() + buf, sendSize, rsize,
                      [=](std::vector<uint8_t> out) {
                          if (rsize && recvbuf) {
                              out.resize(rsize, 0);
                              writeBytes(recvbuf, out.data(), rsize);   // the SIF0 return-data transfer
                          }
                          if (rmode) sendRpcEnd(recId, pktAddr, rpcId, cd, SIF_CMD_RPC_CALL, sd, buf);
                          else releaseEePacket(pktAddr);
                      });
        return;
    }
    if (!isGuestServer(sd)) {
        PS2_ERROR("iop", "RPC call to an unknown server (sd 0x%08x, fno %u)", sd, fno);
        if (rmode) sendRpcEnd(recId, pktAddr, rpcId, cd, SIF_CMD_RPC_CALL, sd, 0);
        else releaseEePacket(pktAddr);
        return;
    }
    // A server in module code: queue the request (as sifcmd's _request_call does) and wake its thread.
    const uint32_t qd = mem::read32(sd + 64);
    mem::write32(sd + 60, 0);
    if (mem::read32(qd + 12)) mem::write32(mem::read32(qd + 16) + 60, sd);
    else mem::write32(qd + 12, sd);
    mem::write32(qd + 16, sd);
    mem::write32(sd + 32, pktAddr);
    mem::write32(sd + 28, cd);
    mem::write32(sd + 36, fno);
    mem::write32(sd + 12, ssize);
    mem::write32(sd + 40, recvbuf);
    mem::write32(sd + 44, rsize);
    mem::write32(sd + 48, rmode);
    mem::write32(sd + 52, recId);
    PS2_TRACE("iop", "call module server sd 0x%08x fno %u send %u recv %u", sd, fno, ssize, rsize);
    const auto thread = static_cast<int32_t>(mem::read32(qd));
    if (thread >= 0 && !mem::read32(qd + 4)) kernel::wakeupThread(thread);
}

void handleOtherData(const uint8_t* pkt) {
    const uint32_t recId = rd32(pkt + 16), pktAddr = rd32(pkt + 20), rpcId = rd32(pkt + 24);
    const uint32_t recvData = rd32(pkt + 28), src = rd32(pkt + 32), dest = rd32(pkt + 36), size = rd32(pkt + 40);
    if (size && src < kIopRamSize) writeBytes(dest, mem::ram() + src, std::min(size, kIopRamSize - src));
    sendRpcEnd(recId, pktAddr, rpcId, recvData, SIF_CMD_RPC_RDATA, 0, 0);
}

void resetIop();

// The SDK release of each module in the replacement images of a reset
// command ("rom0:UDNL cdrom0:\MODULES\IOPRP214.IMG;1"). Each module in an
// image carries a 16-byte id, its name padded to 8 characters and its
// release: "PsIIfileio  2140". An image without ids gives its release in its
// name instead (IOPRP214 is 2140, IOPRP23 is 2300).
std::map<std::string, uint32_t> imageModuleVersions(const std::string& resetArg) {
    std::map<std::string, uint32_t> versions;
    const std::vector<std::string> words = ps2n::split(resetArg.c_str(), ' ');
    for (size_t w = 1; w < words.size(); ++w) {   // words[0] is the loader, rom0:UDNL
        const std::string& path = words[w];
        if (path.empty() || ps2n::startsWith(ps2n::toLower(path), "rom0:")) continue;   // the BIOS's own (rom0:EELOADCNF)
        std::vector<uint8_t> image;
        if (!hostfs::readWholeFile(path, image)) PS2_WARN("iop", "cannot read the IOP replacement image %s", path.c_str());
        bool ids = false;
        for (size_t i = 0; i + 16 <= image.size(); ++i) {
            const char* id = reinterpret_cast<const char*>(image.data() + i);
            if (std::memcmp(id, "PsII", 4) != 0) continue;
            if (!std::all_of(id + 4, id + 12, [](char c) { return c >= 0x20 && c < 0x7F; })) continue;
            if (!std::all_of(id + 12, id + 16, [](char c) { return c >= '0' && c <= '9'; }) || id[12] == '0') continue;
            versions.emplace(ps2n::toLower(ps2n::trim(std::string_view(id + 4, 8))), static_cast<uint32_t>(std::stoul(std::string(id + 12, 4))));
            ids = true;
        }
        unsigned n = 0;
        const std::string name = ps2n::toUpper(path.substr(path.find_last_of(":\\/") + 1));
        if (!ids && std::sscanf(name.c_str(), "IOPRP%u", &n) == 1 && n >= 10 && n < 1000) {
            versions.emplace("fileio", n < 100 ? n * 100 : n * 10);
            versions.emplace("loadfile", n < 100 ? n * 100 : n * 10);
        }
    }
    return versions;
}

void handleCommand(uint32_t pktAddr) {
    const uint8_t* pkt = mem::ram() + pktAddr;
    const uint32_t cid = rd32(pkt + 8), opt = rd32(pkt + 12);
    switch (cid) {
    case SIF_CMD_CHANGE_SADDR:
        g_eeCmdBuffer = rd32(pkt + 16);
        PS2_DEBUG("iop", "EE command buffer is now 0x%08x", g_eeCmdBuffer);
        pumpToEe();
        return;
    case SIF_CMD_SET_SREG: setIopSreg(rd32(pkt + 16), rd32(pkt + 20)); return;
    case SIF_CMD_INIT_CMD:
        if (opt == 0) {
            g_eeCmdBuffer = rd32(pkt + 16);
            PS2_DEBUG("iop", "SIF command init, EE buffer 0x%08x", g_eeCmdBuffer);
        } else {   // RPC init: tell the EE we are ready (sreg SIF_SREG_RPCINIT = 1)
            auto p = makePacket(SIF_CMD_SET_SREG, 24);
            wr32(p, 16, 0);
            wr32(p, 20, 1);
            sendToEe(std::move(p));
        }
        pumpToEe();
        return;
    case SIF_CMD_RESET_CMD: {
        const uint32_t argLen = rd32(pkt + 16);
        std::string arg(reinterpret_cast<const char*>(pkt + 24), std::min<uint32_t>(argLen, 80));
        PS2_INFO("iop", "IOP reset requested (\"%s\")", arg.c_str());
        resetIop();
        g_moduleVersions = imageModuleVersions(arg);
        if (!g_moduleVersions.empty())
            PS2_INFO("iop", "replacement image: FILEIO release %u, LOADFILE release %u", moduleVersion("fileio"), moduleVersion("loadfile"));
        g_toEe.clear();
        g_eeCmdBuffer = 0;
        g_rebootPending = true;   // completes when the EE next polls SMFLAG
        return;
    }
    case SIF_CMD_RPC_BIND: handleBind(pkt); return;
    case SIF_CMD_RPC_CALL: handleCall(pkt); return;
    case SIF_CMD_RPC_RDATA: handleOtherData(pkt); return;
    default: break;
    }
    if (auto it = g_cmdHandlers.find(cid); it != g_cmdHandlers.end() && it->second.fn) {
        kernel::callHandler(it->second.fn, pktAddr, it->second.gp, it->second.arg);
        return;
    }
    PS2_UNIMPL("iop", "SIF command", "unhandled SIF command 0x%08x", cid);
}

void registerBuiltinServers() {
    // Services resident in the BIOS IOP image.
    registerServer(0x80000001u, makeFileIoServer());
    registerServer(0x80000003u, makeIopHeapServer());
    registerServer(0x80000006u, makeLoadFileServer());
    cdvd::registerServers();
}

void initIopState() {
    spu2::onIopReboot();   // the SPU2 is not reset: its voices play on, but the IOP's DMA stops
    initCpu();
    hle::initRegistry();
    kernel::init();
    libs::init();
    sound::init();
    cdvd::init();
    sif::init();
    modules::init();
    hostfs::reset();
    memcard::reset();
    pad::reset();
    g_servers.clear();
    g_cmdHandlers.clear();
    g_rpcQueues.clear();
    g_unserved.clear();
    g_iopSregs.fill(0);
    g_moduleVersions.clear();   // the BIOS's modules, until a reset names a replacement image
    // The SIF command buffer the EE writes into belongs to the kernel area.
    registerBuiltinServers();
}

void resetIop() {
    initIopState();
    g_iopTime = nowCycles();
}

} // namespace

void init() {
    spu2::init();
    pad::init();
    disc::insert(g_opt.cdrom);
    reboot();
}

uint32_t moduleVersion(const std::string& module) {
    const auto it = g_moduleVersions.find(module);
    return it == g_moduleVersions.end() ? kBiosModuleVersion : it->second;
}

void reboot() {
    initIopState();
    g_mainAddr = g_msflag = 0;
    g_smflag = SIF_STAT_SIFINIT | SIF_STAT_CMDINIT | SIF_STAT_BOOTEND;
    g_sysregs.clear();
    g_rebootPending = false;
    g_eeCmdBuffer = 0;
    g_toEe.clear();
    g_dmaId = 1;
    g_iopTime = nowCycles();
}

uint8_t* ramPtr(uint32_t offset) { return mem::ram() + (offset & (kIopRamSize - 1)); }

// ---- time ------------------------------------------------------------------------------------
void sync() {
    const uint64_t now = nowCycles();
    if (now <= g_iopTime) return;
    const uint64_t owed = (now - g_iopTime) / kEeCyclesPerIopCycle;
    g_iopTime = now;
    if (!kernel::hasReadyThread()) return;
    // Bounded, so a busy IOP cannot hold the EE up for long after a stall.
    kernel::runSlice(static_cast<int64_t>(std::min<uint64_t>(owed, 400000)));
}

bool busy() { return kernel::hasReadyThread(); }

void runPending() {
    if (kernel::hasReadyThread()) kernel::runSlice(20000);
}

uint64_t runWhileEeIdle() {
    const int64_t used = kernel::runSlice(20000);
    const uint64_t ee = static_cast<uint64_t>(std::max<int64_t>(used, 1)) * kEeCyclesPerIopCycle;
    clockAdvance(ee);
    g_iopTime = nowCycles();
    return ee;
}

void onVblank(bool start) {
    kernel::onVblank(start);
    if (start) pad::onVblank();
}
std::string describeThreads() { return kernel::describeThreads(); }

// ---- EE side -----------------------------------------------------------------------------------
int32_t sifSetDma(uint32_t desc, int32_t count) {
    std::vector<uint32_t> commandPackets;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t d = desc + static_cast<uint32_t>(i) * 16;
        const uint32_t src = read32(d), dest = read32(d + 4) & (kIopRamSize - 1), size = read32(d + 8);
        if (dest + size > kIopRamSize) {
            PS2_ERROR("iop", "SIF DMA past the end of IOP memory (dest 0x%08x size 0x%x)", dest, size);
            continue;
        }
        readBytes(src, mem::ram() + dest, size);
        if (dest == kernel::kSifCommandBuffer) commandPackets.push_back(dest);
    }
    for (uint32_t p : commandPackets) handleCommand(p);
    return static_cast<int32_t>(g_dmaId++ & 0x7FFFFFFF);
}

int32_t sifDmaStat(uint32_t) { return -1; }   // every transfer completes immediately

int32_t sifSetReg(uint32_t reg, uint32_t value) {
    if (reg & 0x80000000u) {
        g_sysregs[reg] = value;
        return 0;
    }
    switch (reg) {
    case SIF_REG_MAINADDR: g_mainAddr = value; break;
    case SIF_REG_MSFLAG: g_msflag |= value; break;
    case SIF_REG_SMFLAG: g_smflag &= ~value; break;   // the EE acknowledges (clears) IOP status bits
    default: break;
    }
    return 0;
}

uint32_t sifGetReg(uint32_t reg) {
    if (reg & 0x80000000u) {
        auto it = g_sysregs.find(reg);
        return it == g_sysregs.end() ? 0 : it->second;
    }
    switch (reg) {
    case SIF_REG_MAINADDR: return g_mainAddr;
    case SIF_REG_SUBADDR: return kernel::kSifCommandBuffer;
    case SIF_REG_MSFLAG: return g_msflag;
    case SIF_REG_SMFLAG:
        if (g_rebootPending) {   // the IOP finished rebooting
            g_rebootPending = false;
            g_smflag |= SIF_STAT_SIFINIT | SIF_STAT_CMDINIT | SIF_STAT_BOOTEND;
        }
        return g_smflag;
    default: return 0;
    }
}

void sifSetDChain() { pumpToEe(); }

uint32_t sbusRead(uint32_t addr) {
    switch (addr) {
    case 0x1000F200u: return g_mainAddr;    // MSCOM
    case 0x1000F210u: return kernel::kSifCommandBuffer;   // SMCOM
    case 0x1000F220u: return g_msflag;
    case 0x1000F230u: return sifGetReg(SIF_REG_SMFLAG);
    default: return 0;
    }
}

void sbusWrite(uint32_t addr, uint32_t v) {
    switch (addr) {
    case 0x1000F200u: g_mainAddr = v; break;
    case 0x1000F220u: g_msflag |= v; break;
    case 0x1000F230u: g_smflag &= ~v; break;
    default: break;
    }
}

uint32_t sifRegReadIop(uint32_t phys) {
    switch (phys & 0xFF) {
    case 0x00: return g_mainAddr;
    case 0x10: return kernel::kSifCommandBuffer;
    case 0x20: return g_msflag;
    case 0x30: return g_smflag;
    default: return 0;
    }
}

void sifRegWriteIop(uint32_t phys, uint32_t v) {
    switch (phys & 0xFF) {
    case 0x20: g_msflag &= ~v; break;   // the IOP acknowledges EE status bits
    case 0x30: g_smflag |= v; break;
    default: break;
    }
}

// ---- IOP side ----------------------------------------------------------------------------------
void dmaToEe(uint32_t iopSrc, uint32_t eeDest, uint32_t size) {
    if (!size) return;
    std::vector<uint8_t> tmp(size);
    mem::readBlock(iopSrc, tmp.data(), size);
    writeBytes(eeDest, tmp.data(), size);
}

void sendCommandToEe(uint32_t cid, uint32_t packet, uint32_t packetSize, uint32_t extraSrc, uint32_t extraDest, uint32_t extraSize) {
    packetSize = std::clamp<uint32_t>(packetSize, 16, 112);
    std::vector<uint8_t> p(packetSize);
    mem::readBlock(packet, p.data(), packetSize);
    p[0] = static_cast<uint8_t>(packetSize);
    p[1] = static_cast<uint8_t>(extraSize);
    p[2] = static_cast<uint8_t>(extraSize >> 8);
    p[3] = static_cast<uint8_t>(extraSize >> 16);
    wr32(p, 4, extraDest);
    wr32(p, 8, cid);
    dmaToEe(extraSrc, extraDest, extraSize);
    sendToEe(std::move(p));
}

void finishGuestRpc(uint32_t sd, uint32_t result) {
    const uint32_t cd = mem::read32(sd + 28), pktAddr = mem::read32(sd + 32), recvbuf = mem::read32(sd + 40);
    const uint32_t rsize = mem::read32(sd + 44), rmode = mem::read32(sd + 48);
    if (result && rsize && recvbuf) dmaToEe(result, recvbuf, rsize);
    if (rmode) sendRpcEnd(0, 0, 0, cd, SIF_CMD_RPC_CALL, 0, 0);
    else releaseEePacket(pktAddr);
}

void addRpcQueue(uint32_t queue) {
    if (std::find(g_rpcQueues.begin(), g_rpcQueues.end(), queue) == g_rpcQueues.end()) g_rpcQueues.push_back(queue);
}

void removeRpcQueue(uint32_t queue) { g_rpcQueues.erase(std::remove(g_rpcQueues.begin(), g_rpcQueues.end(), queue), g_rpcQueues.end()); }

void setCommandHandler(uint32_t cid, uint32_t handler, uint32_t arg, uint32_t gp) { g_cmdHandlers[cid] = {handler, arg, gp}; }

uint32_t iopSreg(uint32_t index) { return index < g_iopSregs.size() ? g_iopSregs[index] : 0; }
void setIopSreg(uint32_t index, uint32_t value) {
    if (index < g_iopSregs.size()) g_iopSregs[index] = value;
}

// ---- modules ------------------------------------------------------------------------------------
void loadModule(const std::string& path, const std::string& args, std::function<void(int, int)> done) {
    modules::load(path, args, std::move(done));
}

int loadModuleSync(const std::string& path, const std::string& args) {
    bool finished = false;
    int id = 0;
    modules::load(path, args, [&](int i, int) {
        finished = true;
        id = i;
    });
    for (int guard = 0; !finished && guard < 100000; ++guard) {
        if (kernel::hasReadyThread()) {
            runWhileEeIdle();
        } else {
            const uint64_t next = nextEventTime();
            if (next == UINT64_MAX) break;
            clockWaitUntil(next);
        }
        runDueEvents();
    }
    if (!finished) PS2_ERROR("iop", "module %s did not finish starting", path.c_str());
    return id;
}

// ---- host services --------------------------------------------------------------------------------
void registerServer(uint32_t sid, std::unique_ptr<RpcServer> server) {
    HostServer s;
    s.sd = kernel::allocMemory(0x100);
    s.buf = kernel::allocMemory(server->bufferSize());
    s.impl = std::move(server);
    if (s.sd) mem::write32(s.sd, sid);
    g_servers[sid] = std::move(s);
}

bool hasServer(uint32_t sid) { return g_servers.count(sid) != 0; }

std::string hostPathFor(const std::string& ps2Path) { return hostfs::hostPath(ps2Path); }

} // namespace ps2::iop
