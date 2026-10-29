#include "hw/ipu.h"

#include "hw/dmac.h"
#include "hw/hw.h"
#include "hw/mpeg.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ps2::ipu {

namespace {

enum Command : uint32_t { BCLR, IDEC, BDEC, VDEC, FDEC, SETIQ, SETVQ, CSC, PACK, SETTH };
constexpr const char* kCommandName[16] = {"BCLR", "IDEC", "BDEC", "VDEC", "FDEC", "SETIQ", "SETVQ", "CSC",
                                          "PACK", "SETTH", "0xA", "0xB", "0xC", "0xD", "0xE", "0xF"};

constexpr uint32_t kInCapacity = 10;   // qwords: the input FIFO (8) and the bitstream buffer (FP, 2)
constexpr uint32_t kOutCapacity = 8;   // qwords in the output FIFO

// IPU_CTRL
constexpr uint32_t kEcd = 1u << 14, kScd = 1u << 15, kRst = 1u << 30, kBusy = 1u << 31;
constexpr uint32_t kCtrlWritable = 0x07F30000u;   // IDP, AS, IVF, QST, MP1, PCT

struct State {
    uint32_t ctrl = 0;   // CBP, ECD, SCD and the writable fields
    uint32_t data = 0;   // IPU_CMD.DATA: the VDEC/FDEC result
    uint32_t th0 = 0, th1 = 0;
    uint8_t intraMatrix[64] = {}, nonIntraMatrix[64] = {};   // raster order
    uint16_t clut[16] = {};
    int16_t dcPred[3] = {};

    // What the input FIFO and the bitstream buffer hold, as stream bytes. The
    // bit pointer is `pos` bits into it.
    std::vector<uint8_t> in;
    uint64_t pos = 0;

    std::vector<uint64_t> out;   // output qwords, two words each; the first 8 are the FIFO
    size_t outHead = 0;          // words already taken

    uint32_t cmd = 0;
    bool active = false;         // BUSY
    bool decoded = false;        // every step done; its output may not fit the FIFO yet
    bool firstStep = true;
    uint32_t mbLeft = 0;         // CSC, PACK
    uint32_t qsc = 0;            // IDEC's quantiser_scale_code
    bool starved = false;        // the last step ran out of input
    size_t starvedSize = 0;      // in.size() when it did
    uint64_t starvedPos = 0;     // how far it had read: where the hardware would stand
};

State g;
std::vector<uint64_t> g_pending;   // a step's output, appended when it finishes

struct NeedInput {};   // a step ran out of input

int idp() { return static_cast<int>((g.ctrl >> 16) & 3); }
bool alternateScan() { return (g.ctrl >> 20) & 1; }
bool intraVlcFormat() { return (g.ctrl >> 21) & 1; }
bool nonLinearScale() { return (g.ctrl >> 22) & 1; }
bool mpeg1() { return (g.ctrl >> 23) & 1; }
uint32_t pictureType() { return (g.ctrl >> 24) & 7; }

uint32_t outQwords() { return static_cast<uint32_t>((g.out.size() - g.outHead) / 2); }
uint64_t bitsFrom(uint64_t pos) { return g.in.size() * 8 > pos ? g.in.size() * 8 - pos : 0; }

// The 32 bits at `pos`, zeros past the end of the data.
uint32_t peek32(uint64_t pos) {
    const size_t byte = static_cast<size_t>(pos >> 3);
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) v = v << 8 | (byte + i < g.in.size() ? g.in[byte + i] : 0);
    return static_cast<uint32_t>((v << (pos & 7)) >> 32);
}

void appendInput(const uint64_t q[2]) {
    const size_t at = g.in.size();
    g.in.resize(at + 16);
    std::memcpy(g.in.data() + at, q, 16);
}

// One qword from DMA channel 4; false if it has none to give.
bool pullQword() {
    uint64_t q[2];
    if (dmac::toIpu(q, 1) == 0) return false;
    appendInput(q);
    return true;
}

void appendOutput(const void* data, size_t bytes) {
    const size_t at = g_pending.size();
    g_pending.resize(at + bytes / 8);
    std::memcpy(g_pending.data() + at, data, bytes);
}

// Reads the bitstream for one step, pulling from DMA as it goes.
class Reader {
public:
    explicit Reader(uint64_t pos) : pos_(pos) {}
    uint64_t pos() const { return pos_; }
    uint64_t available(uint64_t want) {
        while (bitsFrom(pos_) < want && pullQword()) {}
        return bitsFrom(pos_);
    }
    uint32_t peek(int n) {
        if (available(static_cast<uint64_t>(n)) < static_cast<uint64_t>(n)) throw NeedInput{};
        return n == 0 ? 0 : peek32(pos_) >> (32 - n);
    }
    uint32_t get(int n) {
        const uint32_t v = peek(n);
        pos_ += static_cast<uint64_t>(n);
        return v;
    }
    void skip(uint64_t n) {
        if (available(n) < n) throw NeedInput{};
        pos_ += n;
    }
    void bytes(uint8_t* dst, size_t n) {
        if (available(n * 8) < n * 8) throw NeedInput{};
        if ((pos_ & 7) == 0) {
            std::memcpy(dst, g.in.data() + (pos_ >> 3), n);
            pos_ += n * 8;
        } else {
            for (size_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>(get(8));
        }
    }
    void align() { pos_ = (pos_ + 7) & ~uint64_t{7}; }
    uint32_t top() const { return peek32(pos_); }
    // A variable-length code; false if the bits match none.
    bool code(mpeg::Vlc table, int& value) {
        const int max = mpeg::maxLength(table);
        const uint64_t avail = available(static_cast<uint64_t>(max));
        const mpeg::Code c = mpeg::decode(table, top());
        if (c.length == 0 || c.length > avail) {
            if (avail < static_cast<uint64_t>(max)) throw NeedInput{};
            return false;
        }
        pos_ += c.length;
        value = c.value;
        return true;
    }

private:
    uint64_t pos_;
};

// A bad code ends the command with ECD.
bool fail() {
    g.ctrl |= kEcd;
    return true;
}

// One block's coefficients, inverse-scanned and inverse-quantised (7.2 to 7.4
// of the standard), in raster order. False on a code that matches nothing.
bool readBlock(Reader& r, int16_t f[64], int cc, bool intra, int qsc, int16_t pred[3]) {
    std::fill_n(f, 64, int16_t{0});
    const uint8_t* scan = alternateScan() ? mpeg::kAlternate : mpeg::kZigzag;
    const uint8_t* w = intra ? g.intraMatrix : g.nonIntraMatrix;
    const int scale = mpeg::quantiserScale(nonLinearScale(), qsc);
    const bool m1 = mpeg1();
    int i = 0;
    if (intra) {   // DC: a difference from the previous block of the same component
        int size = 0;
        if (!r.code(cc == 0 ? mpeg::Vlc::DcSizeLuminance : mpeg::Vlc::DcSizeChrominance, size)) return false;
        int diff = 0;
        if (size) {
            diff = static_cast<int>(r.get(size));
            if (diff < 1 << (size - 1)) diff += 1 - (1 << size);
        }
        pred[cc] = static_cast<int16_t>(pred[cc] + diff);
        f[0] = static_cast<int16_t>(pred[cc] * (8 >> idp()));
        i = 1;
    }
    const bool tableOne = intra && intraVlcFormat();
    for (bool first = !intra;; first = false) {
        int run, level;
        if (first && r.peek(1)) {   // a non-intra block starts with "1s" for run 0, level 1
            r.skip(1);
            run = 0;
            level = r.get(1) ? -1 : 1;
        } else {
            const uint64_t avail = r.available(16);
            const mpeg::Coefficient c = mpeg::decodeCoefficient(tableOne, r.top());
            if (c.kind == mpeg::Coefficient::Invalid || c.length > avail) {
                if (avail < 16) throw NeedInput{};
                return false;
            }
            r.skip(c.length);
            if (c.kind == mpeg::Coefficient::EndOfBlock) break;
            if (c.kind == mpeg::Coefficient::Escape) {
                run = static_cast<int>(r.get(6));
                if (m1) {   // 8-bit level, or 16 bits after 0x00 and 0x80
                    level = static_cast<int>(r.get(8));
                    if (level == 0) level = static_cast<int>(r.get(8));
                    else if (level == 128) level = static_cast<int>(r.get(8)) - 256;
                    else if (level > 128) level -= 256;
                } else {
                    level = static_cast<int>(r.get(12));
                    if (level & 0x800) level -= 0x1000;
                }
            } else {
                run = c.run;
                level = r.get(1) ? -c.level : c.level;
            }
        }
        i += run;
        if (i > 63) return false;
        const int at = scan[i++];
        int v = intra ? 2 * level * w[at] * scale / 32 : (2 * level + (level > 0 ? 1 : -1)) * w[at] * scale / 32;
        if (m1 && v != 0 && (v & 1) == 0) v += v > 0 ? -1 : 1;   // MPEG-1 rounds every coefficient to odd
        f[at] = static_cast<int16_t>(std::clamp(v, -2048, 2047));
    }
    if (!m1) {   // mismatch control: an even sum makes the last coefficient odd
        int sum = 0;
        for (int k = 0; k < 64; ++k) sum += f[k];
        if ((sum & 1) == 0) f[63] = static_cast<int16_t>(f[63] ^ 1);
    }
    return true;
}

// The six blocks of a macroblock, decoded and transformed. `cbp` picks the
// coded ones (bit 5 = first luminance block); the rest stay zero.
bool readMacroblock(Reader& r, int16_t blocks[6][64], int cbp, bool intra, int qsc, int16_t pred[3]) {
    for (int b = 0; b < 6; ++b) {
        std::fill_n(blocks[b], 64, int16_t{0});
        if (!(cbp & (32 >> b))) continue;
        int16_t f[64];
        if (!readBlock(r, f, b < 4 ? 0 : b - 3, intra, qsc, pred)) return false;
        mpeg::idct(f, blocks[b]);
    }
    return true;
}

// RAW16 layout: 16x16 luminance (the four blocks in frame or field order),
// then 8x8 Cb and Cr.
void arrange(const int16_t blocks[6][64], bool fieldDct, int16_t mb[384]) {
    for (int y = 0; y < 16; ++y) {
        const int left = fieldDct ? (y & 1) * 2 : (y / 8) * 2;
        const int row = fieldDct ? y / 2 : y % 8;
        for (int x = 0; x < 16; ++x) mb[y * 16 + x] = blocks[left + x / 8][row * 8 + x % 8];
    }
    std::copy_n(blocks[4], 64, mb + 256);
    std::copy_n(blocks[5], 64, mb + 320);
}

// CSC output for one macroblock: RGB32, or RGB16 with OFM.
void emitRgb(const uint8_t raw8[384], bool rgb16, bool dither, bool sign) {
    uint32_t px[256];
    mpeg::yuvToRgb32(raw8, px, g.th0, g.th1);
    if (sign)
        for (uint32_t& p : px) p ^= 0x808080u;
    if (!rgb16) return appendOutput(px, sizeof px);
    uint16_t px16[256];
    mpeg::rgb32ToRgb16(px, px16, dither);
    appendOutput(px16, sizeof px16);
}

void flagStartCode(Reader& r) {
    if (r.available(8) >= 8 && r.peek(8) == 0) g.ctrl |= kScd;
}

// ---- commands: each step returns true when the command has nothing left to do ----

bool stepVdec(Reader& r) {
    mpeg::Vlc table = mpeg::Vlc::MacroblockAddressIncrement;
    switch ((g.cmd >> 26) & 3) {
    case 1:
        switch (pictureType()) {
        case 2: table = mpeg::Vlc::MacroblockTypeP; break;
        case 3: table = mpeg::Vlc::MacroblockTypeB; break;
        case 4: table = mpeg::Vlc::MacroblockTypeD; break;
        default: table = mpeg::Vlc::MacroblockTypeI; break;
        }
        break;
    case 2: table = mpeg::Vlc::MotionCode; break;
    case 3: table = mpeg::Vlc::DmVector; break;
    default: break;
    }
    const uint64_t start = r.pos();
    int value = 0;
    if (!r.code(table, value)) {
        g.data = 0;
        return fail();
    }
    // The decoded value, and in the upper half how many bits the code took.
    g.data = static_cast<uint32_t>(r.pos() - start) << 16 | static_cast<uint16_t>(value);
    return true;
}

bool stepBdec(Reader& r) {
    const bool intra = (g.cmd >> 27) & 1;
    int cbp = 0x3F;
    if (!intra && !r.code(mpeg::Vlc::CodedBlockPattern, cbp)) return fail();
    int16_t pred[3];
    if ((g.cmd >> 26) & 1) std::fill_n(pred, 3, static_cast<int16_t>(128 << idp()));   // DCR
    else std::copy_n(g.dcPred, 3, pred);
    int16_t blocks[6][64];
    if (!readMacroblock(r, blocks, cbp, intra, static_cast<int>((g.cmd >> 16) & 31), pred)) return fail();
    std::copy_n(pred, 3, g.dcPred);
    g.ctrl = (g.ctrl & ~0x3F00u) | static_cast<uint32_t>(cbp) << 8;
    int16_t mb[384];
    arrange(blocks, (g.cmd >> 25) & 1, mb);
    appendOutput(mb, sizeof mb);
    flagStartCode(r);
    return true;
}

// One macroblock of an intra slice, through to CSC; the command ends at the
// next start code.
bool stepIdec(Reader& r) {
    if (!g.firstStep) {   // macroblock_address_increment: always 1 in an I picture
        int increment = 0;
        do {
            if (!r.code(mpeg::Vlc::MacroblockAddressIncrement, increment)) return fail();
        } while (increment == 0x22);   // stuffing
        if (increment != 1) return fail();
    }
    int type = 0;
    if (!r.code(mpeg::Vlc::MacroblockTypeI, type)) return fail();
    const bool fieldDct = (g.cmd >> 24) & 1 ? r.get(1) != 0 : false;   // DTD: dct_type is coded
    int qsc = static_cast<int>(g.qsc);
    if (type & 0x10) qsc = static_cast<int>(r.get(5));
    int16_t pred[3];
    if (g.firstStep) std::fill_n(pred, 3, static_cast<int16_t>(128 << idp()));
    else std::copy_n(g.dcPred, 3, pred);
    int16_t blocks[6][64];
    if (!readMacroblock(r, blocks, 0x3F, true, qsc, pred)) return fail();
    // Zero bits after a macroblock end the slice; skip them up to the start code.
    bool end = false, bad = false;
    if (r.peek(8) == 0) {
        r.align();
        while (r.peek(24) == 0) r.skip(8);
        end = true;
        bad = r.peek(24) != 1;
    }
    g.qsc = static_cast<uint32_t>(qsc);
    std::copy_n(pred, 3, g.dcPred);
    int16_t mb[384];
    arrange(blocks, fieldDct, mb);
    uint8_t raw8[384];
    for (int i = 0; i < 384; ++i) raw8[i] = static_cast<uint8_t>(std::clamp<int>(mb[i], 0, 255));
    emitRgb(raw8, (g.cmd >> 27) & 1, (g.cmd >> 26) & 1, (g.cmd >> 25) & 1);
    if (bad) return fail();
    if (end) g.ctrl |= kScd;
    return end;
}

bool stepSetiq(Reader& r) {
    uint8_t m[64];
    r.bytes(m, 64);
    uint8_t* matrix = (g.cmd >> 27) & 1 ? g.nonIntraMatrix : g.intraMatrix;
    for (int i = 0; i < 64; ++i) matrix[mpeg::kZigzag[i]] = m[i];   // sent in zigzag order
    return true;
}

bool stepSetvq(Reader& r) {
    uint8_t b[32];
    r.bytes(b, 32);
    for (int i = 0; i < 16; ++i) g.clut[i] = static_cast<uint16_t>(b[2 * i] | b[2 * i + 1] << 8);
    return true;
}

bool stepCsc(Reader& r) {
    uint8_t raw8[384];
    r.bytes(raw8, 384);
    emitRgb(raw8, (g.cmd >> 27) & 1, (g.cmd >> 26) & 1, false);
    return --g.mbLeft == 0;
}

bool stepPack(Reader& r) {
    uint32_t px[256];
    r.bytes(reinterpret_cast<uint8_t*>(px), sizeof px);
    uint16_t px16[256];
    mpeg::rgb32ToRgb16(px, px16, (g.cmd >> 26) & 1);
    if ((g.cmd >> 27) & 1) {
        appendOutput(px16, sizeof px16);
    } else {   // INDX4 through the VQ CLUT, first pixel in the low nibble
        uint8_t indices[128];
        for (int i = 0; i < 128; ++i)
            indices[i] = static_cast<uint8_t>(mpeg::nearestColour(px16[2 * i], g.clut) |
                                              mpeg::nearestColour(px16[2 * i + 1], g.clut) << 4);
        appendOutput(indices, sizeof indices);
    }
    return --g.mbLeft == 0;
}

bool hasForwardBits(uint32_t cmd) {
    const uint32_t c = cmd >> 28;
    return c == IDEC || c == BDEC || c == VDEC || c == FDEC || c == SETIQ;
}

void compact() {
    const size_t drop = static_cast<size_t>(g.pos >> 7) << 4;
    if (drop >= 4096) {
        g.in.erase(g.in.begin(), g.in.begin() + static_cast<std::ptrdiff_t>(drop));
        g.pos -= drop * 8;
    }
    if (g.outHead >= 4096) {
        g.out.erase(g.out.begin(), g.out.begin() + static_cast<std::ptrdiff_t>(g.outHead));
        g.outHead = 0;
    }
}

// Runs one step of the current command. False if it ran out of input, in
// which case nothing changed.
bool step() {
    Reader r(g.pos);
    g_pending.clear();
    bool done = true;
    try {
        if (g.firstStep && hasForwardBits(g.cmd)) r.skip(g.cmd & 0x3F);
        switch (g.cmd >> 28) {
        case IDEC: done = stepIdec(r); break;
        case BDEC: done = stepBdec(r); break;
        case VDEC: done = stepVdec(r); break;
        case FDEC: g.data = r.peek(32); break;
        case SETIQ: done = stepSetiq(r); break;
        case SETVQ: done = stepSetvq(r); break;
        case CSC: done = stepCsc(r); break;
        case PACK: done = stepPack(r); break;
        default: break;
        }
    } catch (const NeedInput&) {
        if (!g.starved) PS2_TRACE("ipu", "%s waits for input (bit %llu)", kCommandName[g.cmd >> 28], static_cast<unsigned long long>(r.pos()));
        g.starved = true;
        g.starvedSize = g.in.size();
        g.starvedPos = r.pos();
        return false;
    }
    g.starved = false;
    g.pos = r.pos();
    g.out.insert(g.out.end(), g_pending.begin(), g_pending.end());
    g.firstStep = false;
    g.decoded = done;
    compact();
    return true;
}

// Keeps the input FIFO full from DMA channel 4, as the DMAC does.
bool fill() {
    if (g.starved) return false;
    const size_t head = static_cast<size_t>(g.pos >> 7) << 4;
    size_t have = g.in.size() > head ? (g.in.size() - head) / 16 : 0;
    bool any = false;
    for (; have < kInCapacity && pullQword(); ++have) any = true;
    return any;
}

// Hands output to DMA channel 3.
bool drain() {
    const uint32_t n = outQwords();
    if (n == 0) return false;
    const uint32_t moved = dmac::fromIpu(g.out.data() + g.outHead, n);
    g.outHead += size_t{moved} * 2;
    if (g.outHead == g.out.size()) {
        g.out.clear();
        g.outHead = 0;
    }
    return moved > 0;
}

void finish() {
    g.active = false;
    g.decoded = false;
    hw::raiseIntc(hw::INTC_IPU);
}

void startCommand(uint32_t v) {
    if (g.active)
        PS2_UNIMPL("ipu", "IPU command while busy", "IPU command 0x%08x written while %s still runs; the old one is dropped", v,
                   kCommandName[g.cmd >> 28]);
    PS2_TRACE("ipu", "%s 0x%08x (ctrl 0x%08x)", kCommandName[v >> 28], v, g.ctrl);
    g.cmd = v;
    g.ctrl &= ~(kEcd | kScd);
    g.active = true;
    g.decoded = false;
    g.firstStep = true;
    g.starved = false;
    switch (v >> 28) {
    case BCLR:   // empties the input FIFO; the bit pointer starts at BP
        g.in.clear();
        g.pos = v & 0x7F;
        g.decoded = true;
        break;
    case SETTH:
        g.th0 = v & 0x1FF;
        g.th1 = (v >> 16) & 0x1FF;
        g.decoded = true;
        break;
    case IDEC: g.qsc = (v >> 16) & 31; break;
    case CSC:
    case PACK:
        g.mbLeft = v & 0x7FF;
        g.decoded = g.mbLeft == 0;
        break;
    case BDEC: case VDEC: case FDEC: case SETIQ: case SETVQ: break;
    default:
        PS2_UNIMPL("ipu", "IPU command 0xA-0xF", "unknown IPU command 0x%08x", v);
        g.decoded = true;
        break;
    }
    run();
}

void reset() {
    g.in.clear();
    g.pos = 0;
    g.out.clear();
    g.outHead = 0;
    g.active = g.decoded = g.starved = false;
    g.ctrl &= kCtrlWritable;   // clears CBP, ECD and SCD
    g.cmd = 0;
    g.data = 0;
    g.th0 = g.th1 = 0;
}

// Where the registers say the bit pointer is: after a step that ran out of
// data, as far as it got.
uint64_t visiblePos() { return g.starved ? g.starvedPos : g.pos; }

struct Fifo {
    uint32_t bp, ifc, fp;
};
Fifo fifoState() {
    const uint64_t pos = visiblePos();
    const size_t head = static_cast<size_t>(pos >> 7) << 4;
    const uint32_t qwords = g.in.size() > head ? static_cast<uint32_t>((g.in.size() - head) / 16) : 0;
    const uint32_t fp = std::min(qwords, 2u);
    return {static_cast<uint32_t>(pos & 127), std::min(qwords - fp, 8u), fp};
}

void unknownRegister(const char* what, uint32_t addr) {
    char key[64];
    std::snprintf(key, sizeof key, "%s of IPU register 0x%08x", what, addr);
    PS2_UNIMPL("ipu", key, "%s", key);
}

} // namespace

void init() {
    g = State{};
    std::copy_n(mpeg::kDefaultIntraMatrix, 64, g.intraMatrix);
    std::fill_n(g.nonIntraMatrix, 64, uint8_t{16});
    g_pending.clear();
}

uint32_t regRead(uint32_t addr) {
    switch (addr) {
    case 0x10002000u: {   // IPU_CMD: the VDEC/FDEC result; after other commands the bitstream, like TOP
        const uint32_t c = g.cmd >> 28;
        return c == VDEC || c == FDEC ? g.data : peek32(visiblePos());
    }
    case 0x10002004u: return g.active ? kBusy : 0;
    case 0x10002010u: {
        const Fifo f = fifoState();
        return (g.ctrl & ~0xFFu) | f.ifc | std::min(outQwords(), kOutCapacity) << 4 | (g.active ? kBusy : 0);
    }
    case 0x10002020u: {
        const Fifo f = fifoState();
        return f.bp | f.ifc << 8 | f.fp << 16;
    }
    case 0x10002030u: return peek32(visiblePos());
    case 0x10002034u: return g.active || bitsFrom(visiblePos()) < 32 ? kBusy : 0;   // BUSY: fewer than 32 bits ready
    case 0x10002014u: case 0x10002024u: return 0;
    default:
        unknownRegister("read", addr);
        return 0;
    }
}

void regWrite(uint32_t addr, uint32_t value) {
    switch (addr) {
    case 0x10002000u: return startCommand(value);
    case 0x10002010u:
        if (value & kRst) reset();
        g.ctrl = (g.ctrl & ~kCtrlWritable) | (value & kCtrlWritable);
        return run();
    case 0x10002004u: case 0x10002014u: return;
    default: return unknownRegister("write", addr);
    }
}

void fifoRead(uint64_t out[2]) {
    if (outQwords() == 0) {   // the hardware would stall the CPU until data came
        PS2_UNIMPL("ipu", "read of the empty IPU output FIFO", "read of the empty IPU output FIFO");
        out[0] = out[1] = 0;
        return;
    }
    out[0] = g.out[g.outHead];
    out[1] = g.out[g.outHead + 1];
    g.outHead += 2;
    run();
}

void fifoWrite(const uint64_t q[2]) {
    appendInput(q);
    run();
}

void run() {
    static bool running = false;
    if (running) return;
    running = true;
    for (;;) {
        bool progress = drain();
        const bool mayStep = !g.starved || g.in.size() > g.starvedSize || dmac::toIpuReady();
        if (g.active && !g.decoded && outQwords() <= kOutCapacity && mayStep) progress |= step();
        if (g.active && g.decoded && outQwords() <= kOutCapacity) {
            finish();
            progress = true;
        }
        progress |= fill();
        if (!progress) break;
    }
    running = false;
}

} // namespace ps2::ipu
