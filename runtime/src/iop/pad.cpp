#include "iop/pad.h"

#include "hw/hw.h"
#include "internal.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "log.h"
#include "ps2/memory.h"
#include "video/video.h"

#include "ps2uc/util.h"

#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace ps2::iop::pad {

namespace {

// libpad RPC commands; the command is the first word of the 128-byte buffer.
constexpr int32_t CMD_OPEN = 0x01, CMD_SET_MMODE = 0x06, CMD_SET_ACTDIR = 0x07, CMD_SET_ACTALIGN = 0x08, CMD_GET_BTNMASK = 0x09,
                  CMD_SET_BTNINFO = 0x0A, CMD_SET_VREF = 0x0B, CMD_GET_PORTMAX = 0x0C, CMD_GET_SLOTMAX = 0x0D, CMD_CLOSE = 0x0E,
                  CMD_END = 0x0F, CMD_INIT = 0x10, CMD_GET_MODVER = 0x12;
// The original PADMAN's numbering of the same commands, and its two queries.
constexpr int32_t OLD_BASE = static_cast<int32_t>(0x80000100u);
constexpr int32_t OLD_OPEN = OLD_BASE, OLD_INFO_ACT = OLD_BASE + 2, OLD_INFO_COMB = OLD_BASE + 3, OLD_INFO_MODE = OLD_BASE + 4,
                  OLD_SET_MMODE = OLD_BASE + 5, OLD_SET_ACTDIR = OLD_BASE + 6, OLD_SET_ACTALIGN = OLD_BASE + 7,
                  OLD_GET_BTNMASK = OLD_BASE + 8, OLD_SET_BTNINFO = OLD_BASE + 9, OLD_SET_VREF = OLD_BASE + 10,
                  OLD_GET_PORTMAX = OLD_BASE + 11, OLD_GET_SLOTMAX = OLD_BASE + 12, OLD_CLOSE = OLD_BASE + 13, OLD_END = OLD_BASE + 14;

constexpr uint8_t STATE_DISCONN = 0, STATE_FINDCTP1 = 2, STATE_EXECCMD = 5, STATE_STABLE = 6;
constexpr uint8_t RSTAT_COMPLETE = 0, RSTAT_BUSY = 2;
constexpr uint8_t TASK_UPDATE_PAD = 1, TASK_QUERY_PAD = 2, TASK_SET_MAIN_MODE = 4;
constexpr uint8_t ID_DIGITAL = 0x41, ID_ANALOG = 0x73, ID_PRESSURE = 0x79;
constexpr int kBusyFrames = 2;   // how long a mode change keeps reqState busy

// A DualShock 2 as its configuration queries describe it.
constexpr uint8_t kModel = 3;
constexpr uint16_t kModeTable[2] = {4, 7};                          // digital, DualShock
constexpr uint8_t kActData[2][4] = {{1, 2, 0, 10}, {1, 1, 1, 20}};  // small and large motor

struct Port {
    bool open = false;
    bool newFormat = true;     // 128-byte pad_data (libpad2 / XPADMAN) or 64-byte (PADMAN)
    uint32_t area = 0;         // EE address of the two pad_data buffers
    uint32_t frame = 0;
    uint8_t state = STATE_FINDCTP1;
    uint8_t reqState = RSTAT_BUSY;
    uint8_t task = TASK_QUERY_PAD;
    int busy = kBusyFrames;    // frames until the pending task completes
    uint8_t modeId = ID_DIGITAL, pendingId = ID_DIGITAL;
    uint8_t modeOffs = 0, lock = 0;
    uint8_t actAlign[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t actDirect[6] = {};
};

std::array<Port, 2> g_ports;
uint32_t g_statBuf = 0;   // EE open_slot[2] for padGetConnection
uint32_t g_statFrame = 0;
std::map<uint64_t, Input> g_script;   // frame -> input from that frame on
bool g_haveScript = false;

int32_t rd(const uint8_t* d, size_t off) {
    int32_t v;
    std::memcpy(&v, d + off, 4);
    return v;
}
void wr(std::vector<uint8_t>& b, size_t off, int32_t v) { std::memcpy(b.data() + off, &v, 4); }

Input currentInput(int port) {
    if (g_haveScript) {
        if (port != 0) return Input{false};
        const uint64_t frame = hw::vsyncCount();
        auto it = g_script.upper_bound(frame);
        if (it == g_script.begin()) return Input{};
        return std::prev(it)->second;
    }
    const video::HostPad h = video::hostPad(port);
    Input in;
    in.connected = h.connected;
    in.buttons = h.buttons;
    in.lx = h.lx;
    in.ly = h.ly;
    in.rx = h.rx;
    in.ry = h.ry;
    in.l2 = h.l2;
    in.r2 = h.r2;
    return in;
}

// The 32 bytes libpad's padRead returns: ok, mode id, the controller's response.
void buttonData(const Port& p, const Input& in, uint8_t out[32]) {
    std::memset(out, 0, 32);
    out[1] = p.modeId;
    const uint16_t raw = static_cast<uint16_t>(~in.buttons);   // active low on the wire
    out[2] = static_cast<uint8_t>(raw);
    out[3] = static_cast<uint8_t>(raw >> 8);
    out[4] = in.rx;
    out[5] = in.ry;
    out[6] = in.lx;
    out[7] = in.ly;
    auto pressure = [&](uint16_t b, uint8_t analog = 0) -> uint8_t { return analog ? analog : (in.buttons & b) ? 0xFF : 0; };
    const uint8_t pr[12] = {pressure(RIGHT), pressure(LEFT), pressure(UP), pressure(DOWN), pressure(TRIANGLE), pressure(CIRCLE),
                            pressure(CROSS), pressure(SQUARE), pressure(L1), pressure(R1), pressure(L2, in.l2), pressure(R2, in.r2)};
    std::memcpy(out + 8, pr, 12);
    out[31] = 0x5A;
}

void writePort(Port& p, const Input& in) {
    const bool present = in.connected;
    const uint8_t state = present ? p.state : STATE_DISCONN;
    uint8_t data[32];
    buttonData(p, in, data);
    const bool ready = present && p.state == STATE_STABLE;
    if (p.newFormat) {
        std::vector<uint8_t> b(128, 0);
        std::memcpy(b.data(), data, 32);
        std::memcpy(b.data() + 32, p.actDirect, 6);
        std::memcpy(b.data() + 40, p.actAlign, 6);
        std::memcpy(b.data() + 48, kActData, sizeof kActData);
        std::memcpy(b.data() + 80, kModeTable, sizeof kModeTable);
        wr(b, 88, static_cast<int32_t>(p.frame));
        wr(b, 96, ready ? 32 : 0);   // length
        b[100] = present ? 2 : 0;    // modeConfig: MODE_CONFIG_READY
        b[101] = p.modeId;
        b[102] = kModel;
        b[103] = ready ? 1 : 0;      // buttonDataReady
        b[104] = 2;                  // nrOfModes
        b[105] = p.modeOffs;
        b[106] = 2;                  // nrOfActuators
        b[109] = p.modeOffs;         // mode
        b[110] = p.lock;
        b[111] = 6;                  // actDirSize
        b[112] = state;
        b[113] = p.reqState;
        b[114] = p.task;
        b[115] = p.task;
        writeBytes(p.area + (p.frame & 1) * 128, b.data(), 128);
    } else {
        std::vector<uint8_t> b(64, 0);
        wr(b, 0, static_cast<int32_t>(p.frame));
        b[4] = state;
        b[5] = p.reqState;
        b[6] = ready ? 1 : 0;
        std::memcpy(b.data() + 8, data, 32);
        wr(b, 40, ready ? 32 : 0);
        b[45] = 2;   // CTP: configurable
        b[46] = kModel;
        writeBytes(p.area + (p.frame & 1) * 64, b.data(), 64);
    }
    ++p.frame;
}

// Starts a mode change or other configuration request; it completes a few frames later.
void startTask(Port& p, uint8_t task, uint8_t newId) {
    p.task = task;
    p.pendingId = newId;
    p.reqState = RSTAT_BUSY;
    p.busy = kBusyFrames;
}

class PadServer final : public RpcServer {
public:
    PadServer(const char* name, bool newFormat, uint16_t version) : RpcServer(name), newFormat_(newFormat), version_(version) {}
    uint32_t bufferSize() const override { return 0x100; }

    void call(uint32_t, const uint8_t* d, uint32_t, uint32_t recvSize, Reply reply) override {
        std::vector<uint8_t> out(std::max<uint32_t>(recvSize, 128), 0);
        std::memcpy(out.data(), d, 128);
        const int32_t cmd = rd(d, 0);
        const auto port = static_cast<uint32_t>(rd(d, 4)), slot = static_cast<uint32_t>(rd(d, 8));
        Port* p = port < g_ports.size() && slot == 0 ? &g_ports[port] : nullptr;
        auto result = [&](size_t off, int32_t v) { wr(out, off, v); };
        switch (cmd) {
        case CMD_INIT:
            g_statBuf = static_cast<uint32_t>(rd(d, 16));
            result(12, 1);
            break;
        case CMD_OPEN:
        case OLD_OPEN:
            if (!p) {
                result(12, 0);
                break;
            }
            *p = Port{};
            p->open = true;
            p->newFormat = newFormat_;
            p->area = static_cast<uint32_t>(rd(d, 16));
            result(12, 1);
            result(20, static_cast<int32_t>(kernel::kKernelData + 0x1000 + port * 0x100));   // the IOP-side buffer
            PS2_DEBUG("pad", "port %u opened, pad area 0x%08x", port, p->area);
            break;
        case CMD_CLOSE:
        case OLD_CLOSE:
            if (p) p->open = false;
            result(12, 1);
            break;
        case CMD_END:
        case OLD_END:
            for (auto& q : g_ports) q.open = false;
            result(12, 1);
            break;
        case CMD_GET_PORTMAX:
        case OLD_GET_PORTMAX: result(12, 2); break;
        case CMD_GET_SLOTMAX:
        case OLD_GET_SLOTMAX: result(12, 1); break;
        case CMD_GET_MODVER: result(12, version_); break;   // libpad compares the major with its own
        case CMD_SET_MMODE:
        case OLD_SET_MMODE: {
            const int32_t mode = rd(d, 12), lock = rd(d, 16);
            if (!p || mode < 0 || mode > 1) {
                result(20, 0);
                break;
            }
            p->lock = static_cast<uint8_t>(lock == 3 ? 3 : 0);
            p->modeOffs = static_cast<uint8_t>(mode);
            startTask(*p, TASK_SET_MAIN_MODE, mode ? ID_ANALOG : ID_DIGITAL);
            result(20, 1);
            break;
        }
        case CMD_GET_BTNMASK:
        case OLD_GET_BTNMASK: result(12, 0x3FFFF); break;   // pressure-sensitive buttons
        case CMD_SET_BTNINFO:
        case OLD_SET_BTNINFO:
            if (p) startTask(*p, TASK_SET_MAIN_MODE, rd(d, 12) ? ID_PRESSURE : ID_ANALOG);
            result(16, p ? 1 : 0);
            break;
        case CMD_SET_ACTALIGN:
        case OLD_SET_ACTALIGN:
            if (p) {
                std::memcpy(p->actAlign, d + 12, 6);
                startTask(*p, TASK_SET_MAIN_MODE, p->modeId);
            }
            result(20, p ? 1 : 0);
            break;
        case CMD_SET_ACTDIR:
        case OLD_SET_ACTDIR:
            if (p) std::memcpy(p->actDirect, d + 12, 6);
            result(20, p ? 1 : 0);
            break;
        case CMD_SET_VREF:
        case OLD_SET_VREF: result(12, 1); break;
        case OLD_INFO_MODE: result(20, p ? infoMode(*p, rd(d, 12), rd(d, 16)) : 0); break;
        case OLD_INFO_ACT: {
            const int32_t act = rd(d, 12), what = rd(d, 16);
            int32_t v = 0;
            if (act == -1) v = 2;
            else if (act >= 0 && act < 2 && what >= 0 && what < 4) v = kActData[act][what];
            result(20, v);
            break;
        }
        case OLD_INFO_COMB: result(20, 0); break;
        default: {
            char key[64];
            std::snprintf(key, sizeof key, "PADMAN command 0x%08x", static_cast<uint32_t>(cmd));
            PS2_UNIMPL("pad", key, "%s not implemented", key);
            result(12, 0);
            break;
        }
        }
        reply(std::move(out));
    }

private:
    static int32_t infoMode(const Port& p, int32_t what, int32_t index) {
        switch (what) {
        case 1: return p.modeId >> 4;    // PAD_MODECURID
        case 2: return kModeTable[p.modeOffs];   // PAD_MODECUREXID
        case 3: return p.modeOffs;       // PAD_MODECUROFFS
        case 4:                          // PAD_MODETABLE
            if (index == -1) return 2;
            return index >= 0 && index < 2 ? kModeTable[index] : 0;
        default: return 0;
        }
    }

    bool newFormat_;
    uint16_t version_;
};

} // namespace

bool parseScriptLine(const std::string& raw, uint64_t& frame, Input& in) {
    std::string line = raw.substr(0, raw.find('#'));
    std::istringstream ss(line);
    std::string buttons;
    if (!(ss >> frame)) return false;
    in = Input{};
    static const std::map<std::string, uint16_t> names = {
        {"select", SELECT}, {"l3", L3}, {"r3", R3}, {"start", START}, {"up", UP}, {"right", RIGHT}, {"down", DOWN},
        {"left", LEFT}, {"l2", L2}, {"r2", R2}, {"l1", L1}, {"r1", R1}, {"triangle", TRIANGLE}, {"circle", CIRCLE},
        {"cross", CROSS}, {"square", SQUARE},
    };
    std::string tok;
    while (ss >> tok) {
        const auto eq = tok.find('=');
        if (eq != std::string::npos) {
            const std::string key = ps2uc::toLower(tok.substr(0, eq));
            const auto v = static_cast<uint8_t>(std::strtoul(tok.c_str() + eq + 1, nullptr, 0));
            if (key == "lx") in.lx = v;
            else if (key == "ly") in.ly = v;
            else if (key == "rx") in.rx = v;
            else if (key == "ry") in.ry = v;
            else return false;
            continue;
        }
        if (tok == "-" || tok == "none") continue;
        for (const auto& part : ps2uc::split(ps2uc::toLower(tok), '+')) {
            auto it = names.find(part);
            if (it == names.end()) return false;
            in.buttons |= it->second;
        }
    }
    return true;
}

void init() {
    reset();
    g_script.clear();
    g_haveScript = false;
    if (g_opt.padScript.empty()) return;
    std::ifstream f(g_opt.padScript);
    if (!f) {
        PS2_ERROR("pad", "cannot read pad script %s", g_opt.padScript.string().c_str());
        return;
    }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.find_first_not_of(" \t\r") == std::string::npos || line[line.find_first_not_of(" \t\r")] == '#') continue;
        uint64_t frame = 0;
        Input in;
        if (!parseScriptLine(line, frame, in)) {
            PS2_ERROR("pad", "%s:%d: cannot parse \"%s\"", g_opt.padScript.string().c_str(), n, line.c_str());
            continue;
        }
        g_script[frame] = in;
    }
    g_haveScript = true;
}

void reset() {
    g_ports = {};
    g_statBuf = 0;
    g_statFrame = 0;
}

void enable(uint16_t version) {
    if (hasServer(0x80000100u)) return;
    if (!version) version = 0x0306;   // a BIOS module (rom0:XPADMAN) has no file to read it from
    // libpad2 / XPADMAN, and the original PADMAN; each binds a pair of sids.
    registerServer(0x80000100u, std::make_unique<PadServer>("PADMAN", true, version));
    registerServer(0x80000101u, std::make_unique<PadServer>("PADMAN", true, version));
    registerServer(0x8000010Fu, std::make_unique<PadServer>("PADMAN (old)", false, version));
    registerServer(0x8000011Fu, std::make_unique<PadServer>("PADMAN (old)", false, version));
}

void onVblank() {
    uint32_t openMask[2] = {0, 0};
    for (size_t i = 0; i < g_ports.size(); ++i) {
        Port& p = g_ports[i];
        if (!p.open || !p.area) continue;
        openMask[i] = 1;
        if (p.busy > 0 && --p.busy == 0) {   // the pending task finishes
            p.state = STATE_STABLE;
            p.modeId = p.pendingId;
            p.reqState = RSTAT_COMPLETE;
            p.task = TASK_UPDATE_PAD;
        } else if (p.busy > 0 && p.state != STATE_STABLE) {
            p.state = STATE_EXECCMD;
        }
        writePort(p, currentInput(static_cast<int>(i)));
    }
    if (g_statBuf) {   // open_slot[2] {frame, openSlots[2]}, double-buffered like the pad data
        uint8_t b[128] = {};
        std::memcpy(b, &g_statFrame, 4);
        std::memcpy(b + 4, openMask, 8);
        writeBytes(g_statBuf + (g_statFrame & 1) * 128, b, 128);
        ++g_statFrame;
    }
}

} // namespace ps2::iop::pad
