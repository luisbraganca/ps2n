// The SPU2 (spu2.h).
//
// Its voices work as the PS1 SPU's do, which psx-spx documents ("Sound
// Processing Unit"): ADPCM blocks and their loop flags, the pitch counter and
// its 4-point interpolation table, envelope and sweep steps, the noise
// generator, and the reverb with its resampling filter, here at 48 kHz. The
// SPU2's own parts follow ps2sdk's register map (spu2_mmio_hwport.h) and what
// emulators have found:
// - each core's routing into a dry mix and a reverb (wet) mix (VMIX, MMIX);
// - core 0's output feeding core 1 as its external input;
// - the PCM input, read from sound memory at 0x2000 and kept filled by AutoDMA;
// - the areas at 0x400-0x1FFF where the cores write voice 1, voice 3 and
//   their mixes every sample, which an interrupt address can watch.
#include "iop/spu2.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace ps2::iop::spu2 {

namespace {

constexpr uint32_t kRamWords = kRamBytes / 2;
constexpr uint32_t kWordMask = kRamWords - 1;   // addresses inside the SPU2 count 16-bit words
constexpr int kVoices = 24;
constexpr uint32_t kTickSamples = 64;           // the mixing event's period (1.3 ms)
constexpr int kIrqLine = 9;

// What the cores write every sample (word addresses, 0x200 words each, at
// the output position): voices 1 and 3 after their envelope, the voices'
// dry and wet sums, and core 0's output. Then each core's PCM input.
constexpr uint32_t kVoice1Out[2] = {0x0400, 0x0C00}, kVoice3Out[2] = {0x0600, 0x0E00};
constexpr uint32_t kCore0Out = 0x0800;          // left; right at 0x0A00
constexpr uint32_t kMixOut[2] = {0x1000, 0x1800};   // dry left, dry right, wet left, wet right
constexpr uint32_t kInput[2] = {0x2000, 0x2400};    // left; right at +0x200

// The interpolation table (psx-spx, "4-Point Gaussian Interpolation").
constexpr int16_t kGauss[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

// The reverb's resampling filter (psx-spx, "Reverb Buffer Resampling"): 39
// taps at the mixing rate around the reverb's half rate.
constexpr int16_t kResample[39] = {
    -1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246, 16384,
    10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1,
};

// ADPCM prediction filters: SPU-ADPCM has five.
constexpr int32_t kPredict[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

constexpr int32_t sat16(int32_t v) { return std::clamp(v, -0x8000, 0x7FFF); }
constexpr int32_t mul(int32_t a, int32_t b) { return (a * b) >> 15; }

// ---- Envelopes -------------------------------------------------------------------------------
// ADSR phases and volume sweeps step alike (psx-spx, "Envelope Operation"):
// a 7-bit rate is a shift (0-31) and a step (+7..+4 up, -8..-5 down); below
// shift 11 the steps grow, above it they come every 2^(shift-11) samples.
struct Envelope {
    int32_t level = 0;
    uint32_t counter = 0;
};

void stepEnvelope(Envelope& e, bool exp, bool dec, bool negative, int shift, int step, bool rateHasStep) {
    if (exp && dec) negative = false;   // the phase bit does nothing in exponential decrease
    int32_t inc = 7 - step;
    if (dec != negative) inc = ~inc;
    inc *= 1 << std::max(0, 11 - shift);
    uint32_t counterInc = 0x8000u >> std::max(0, shift - 11);
    if (exp && !dec && e.level > 0x6000) {
        if (shift < 10) inc >>= 2;
        else if (shift >= 11) counterInc >>= 2;
        else {
            inc >>= 1;
            counterInc >>= 1;
        }
    } else if (exp && dec) {
        inc = (inc * e.level) >> 15;
    }
    // A rate of all ones never steps.
    if (!(rateHasStep && shift == 31 && step == 3)) counterInc = std::max<uint32_t>(counterInc, 1);
    e.counter += counterInc;
    if (!(e.counter & 0x8000)) return;
    e.counter = 0;
    e.level += inc;
    if (!dec) e.level = std::clamp(e.level, -0x8000, 0x7FFF);
    else if (negative) e.level = std::clamp(e.level, -0x8000, 0);
    else e.level = std::clamp(e.level, 0, 0x7FFF);
}

// A volume register: a fixed level (bit 15 clear: the level halved), or a
// sweep from the current level.
struct Volume {
    uint16_t reg = 0;
    Envelope env;
};

void setVolume(Volume& v, uint16_t reg) {
    v.reg = reg;
    if (!(reg & 0x8000)) v.env.level = static_cast<int16_t>(reg << 1);
}

void stepVolume(Volume& v) {
    const uint16_t r = v.reg;
    if (!(r & 0x8000)) return;
    stepEnvelope(v.env, (r >> 14) & 1, (r >> 13) & 1, (r >> 12) & 1, (r >> 2) & 31, r & 3, true);
}

enum class Phase : uint8_t { Off, Attack, Decay, Sustain, Release };

// ---- State -----------------------------------------------------------------------------------
struct Voice {
    Volume vol[2];
    uint16_t pitch = 0, adsr1 = 0, adsr2 = 0;
    Phase phase = Phase::Off;
    Envelope env;
    uint32_t ssa = 0, lsax = 0;   // word addresses
    bool lsaxWritten = false;     // LSAX written since key on: loop-start flags leave it alone
    uint32_t block = 0;           // the block playing (its header word)
    uint8_t flags = 0;            // its loop flags
    int pos = 0;                  // its next sample, 0-27
    int16_t samples[28] = {};     // the block, decoded
    int32_t prev1 = 0, prev2 = 0;   // the decoder's last two samples
    int16_t hist[4] = {};         // the last four samples played, oldest first
    uint32_t counter = 0;         // pitch counter: samples from bit 12, interpolation in bits 4-11
    int32_t out = 0;              // this sample's output after the envelope (pitch modulation)
};

struct Reverb {
    int16_t in[2][64] = {};       // wet input, for the down-sampling filter
    int16_t out[2][64] = {};      // reverb output at half rate, zero between
    uint32_t pos = 0;
    uint64_t tick = 0;            // samples since ESA was set: left on even ones, right on odd
};

struct AutoDma {
    bool active = false;
    uint32_t src = 0, left = 0;
};

struct Core {
    Voice voices[kVoices];
    Volume mvol[2];
    uint32_t pmon = 0, non = 0, vmixl = 0, vmixel = 0, vmixr = 0, vmixer = 0;   // a bit per voice
    uint32_t endx = 0;
    uint32_t irqa = 0, tsa = 0;   // word addresses
    uint16_t noise = 0;
    int32_t noiseTimer = 0;
    Reverb rev;
    AutoDma adma;
};

std::vector<uint16_t> g_ram;                 // sound memory, as 16-bit words
std::array<uint16_t, 0x400> g_regs{};        // what was written, at register address / 2
Core g_core[2];
uint16_t g_irqInfo = 0;                      // 0x7C2: bit 2 + core, an interrupt address was touched
uint32_t g_outPos = 0;                       // position in the output and input areas, 0-0x1FF
uint64_t g_start = 0;                        // emulated time of sample 0
uint64_t g_samples = 0;                      // samples mixed
uint64_t g_tickEvent = 0;
bool g_mixing = false;
bool g_yield = false;                        // an interrupt went up: the IOP sees it before more is mixed
AutoDmaDone g_admaDone = nullptr;
Sink g_sink = nullptr;
std::vector<int16_t> g_outBuf;

uint16_t& reg(uint32_t off) { return g_regs[(off & 0x7FE) >> 1]; }
uint32_t coreReg(int c, uint32_t r) { return g_regs[((c * 0x400u + r) & 0x7FE) >> 1]; }
uint16_t extraReg(int c, int index) { return g_regs[(0x760u + c * 0x28u + index * 2u) >> 1]; }
// Two registers making one value: an address (the high one first, 4 bits) or a voice mask (the low 16 voices first).
uint32_t addrPair(uint32_t hi, uint32_t lo) { return ((hi & 0xF) << 16) | lo; }
uint32_t maskPair(uint32_t low16, uint32_t high8) { return low16 | ((high8 & 0xFF) << 16); }

uint16_t attr(int c) { return static_cast<uint16_t>(coreReg(c, 0x19A)); }
bool irqEnabled(int c) { return attr(c) & 0x40; }
bool irqArmed() { return irqEnabled(0) || irqEnabled(1); }

void raiseIrq(int c) {
    if (g_irqInfo & (4 << c)) return;   // already pending
    g_irqInfo |= static_cast<uint16_t>(4 << c);
    kernel::raiseInterrupt(kIrqLine);
    g_yield = true;
}

// Both cores watch every address the SPU2 reads or writes, whichever core does it.
void touch(uint32_t addr) {
    if (!irqArmed()) return;
    for (int c = 0; c < 2; ++c)
        if (irqEnabled(c) && g_core[c].irqa == (addr & kWordMask)) raiseIrq(c);
}

void touchRange(uint32_t addr, uint32_t words) {
    if (!irqArmed() || !words) return;
    for (int c = 0; c < 2; ++c)
        if (irqEnabled(c) && ((g_core[c].irqa - addr) & kWordMask) < words) raiseIrq(c);
}

void writeOut(uint32_t addr, int32_t value) {
    touch(addr);
    g_ram[addr & kWordMask] = static_cast<uint16_t>(value);
}

// ---- Voices ----------------------------------------------------------------------------------
void decodeBlock(Voice& v) {
    uint16_t words[8];
    for (uint32_t i = 0; i < 8; ++i) words[i] = g_ram[(v.block + i) & kWordMask];
    decodeAdpcm(words, v.prev1, v.prev2, v.samples);
}

void fetchBlock(Voice& v, uint32_t addr) {
    v.block = addr & kWordMask & ~7u;
    touch(v.block);
    v.flags = static_cast<uint8_t>(g_ram[v.block] >> 8);
    if ((v.flags & 4) && !v.lsaxWritten) v.lsax = v.block;   // loop start
    decodeBlock(v);
    v.pos = 0;
}

void keyOn(Core& c, int i) {
    Voice& v = c.voices[i];
    v.phase = Phase::Attack;
    v.env = {};
    v.lsaxWritten = false;
    v.counter = 0;
    v.prev1 = v.prev2 = 0;
    std::fill(std::begin(v.hist), std::end(v.hist), int16_t(0));
    v.out = 0;
    c.endx &= ~(1u << i);
    fetchBlock(v, v.ssa);
}

void keyOff(Voice& v) {
    if (v.phase == Phase::Off) return;
    v.phase = Phase::Release;
    v.env.counter = 0;
}

// The next sample into the interpolator. A block's last sample moves on to
// the next block, or for one with the loop-end flag to the loop start, where
// a block without the repeat flag also silences the voice.
void advance(Core& c, int i) {
    Voice& v = c.voices[i];
    if ((v.pos & 3) == 0) touch(v.block + 1 + v.pos / 4);
    v.hist[0] = v.hist[1];
    v.hist[1] = v.hist[2];
    v.hist[2] = v.hist[3];
    v.hist[3] = v.samples[v.pos];
    if (++v.pos < 28) return;
    uint32_t next = v.block + 8;
    if (v.flags & 1) {
        c.endx |= 1u << i;
        next = v.lsax;
        if (!(v.flags & 2)) {
            v.phase = Phase::Off;
            v.env.level = 0;
        }
    }
    fetchBlock(v, next);
}

void stepAdsr(Voice& v) {
    const uint16_t a1 = v.adsr1, a2 = v.adsr2;
    switch (v.phase) {
    case Phase::Attack:
        stepEnvelope(v.env, a1 >> 15, false, false, (a1 >> 10) & 31, (a1 >> 8) & 3, true);
        if (v.env.level >= 0x7FFF) {
            v.phase = Phase::Decay;
            v.env.counter = 0;
        }
        break;
    case Phase::Decay:
        stepEnvelope(v.env, true, true, false, (a1 >> 4) & 15, 0, false);
        if (v.env.level <= ((a1 & 15) + 1) << 11) {
            v.phase = Phase::Sustain;
            v.env.counter = 0;
        }
        break;
    case Phase::Sustain:
        stepEnvelope(v.env, a2 >> 15, (a2 >> 14) & 1, false, (a2 >> 8) & 31, (a2 >> 6) & 3, true);
        break;
    case Phase::Release:
        stepEnvelope(v.env, (a2 >> 5) & 1, true, false, a2 & 31, 0, false);
        if (v.env.level <= 0) {
            v.phase = Phase::Off;
            v.env.level = 0;
        }
        break;
    case Phase::Off: break;
    }
}

int32_t interpolate(const Voice& v) {
    const uint32_t i = (v.counter >> 4) & 0xFF;
    int32_t out = (kGauss[0xFF - i] * v.hist[0]) >> 15;
    out += (kGauss[0x1FF - i] * v.hist[1]) >> 15;
    out += (kGauss[0x100 + i] * v.hist[2]) >> 15;
    out += (kGauss[i] * v.hist[3]) >> 15;
    return out;
}

// psx-spx, "SPU Noise Generator": one level per core, clocked by ATTR.
void stepNoise(Core& c, int index) {
    const uint16_t a = attr(index);
    const int shift = (a >> 10) & 15, step = 4 + ((a >> 8) & 3);
    c.noiseTimer -= step;
    const uint16_t n = c.noise;
    const uint16_t parity = static_cast<uint16_t>(((n >> 15) ^ (n >> 12) ^ (n >> 11) ^ (n >> 10) ^ 1) & 1);
    if (c.noiseTimer < 0) {
        c.noise = static_cast<uint16_t>((n << 1) | parity);
        c.noiseTimer += 0x20000 >> shift;
        if (c.noiseTimer < 0) c.noiseTimer += 0x20000 >> shift;
    }
}

struct Stereo {
    int32_t l = 0, r = 0;
};

// One core's voices for one sample: their dry and wet sums.
void mixVoices(int index, Stereo& dry, Stereo& wet) {
    Core& c = g_core[index];
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = c.voices[i];
        stepVolume(v.vol[0]);
        stepVolume(v.vol[1]);
        int32_t sample = 0;
        if (v.phase != Phase::Off) {
            const int32_t raw = (c.non >> i) & 1 ? static_cast<int16_t>(c.noise) : interpolate(v);
            stepAdsr(v);
            sample = mul(raw, v.env.level);
        }
        v.out = sample;
        if (i == 1) writeOut(kVoice1Out[index] + g_outPos, sample);
        else if (i == 3) writeOut(kVoice3Out[index] + g_outPos, sample);
        if (sample) {
            const int32_t l = mul(sample, v.vol[0].env.level), r = mul(sample, v.vol[1].env.level);
            const uint32_t bit = 1u << i;
            if (c.vmixl & bit) dry.l += l;
            if (c.vmixr & bit) dry.r += r;
            if (c.vmixel & bit) wet.l += l;
            if (c.vmixer & bit) wet.r += r;
        }
        // The pitch counter runs whether the voice sounds or not.
        uint32_t step = v.pitch;
        if (i > 0 && ((c.pmon >> i) & 1)) {
            const int32_t factor = c.voices[i - 1].out + 0x8000;
            step = static_cast<uint32_t>((static_cast<int32_t>(static_cast<int16_t>(v.pitch)) * factor) >> 15) & 0xFFFF;
        }
        v.counter += std::min<uint32_t>(step, 0x4000);
        while (v.counter >= 0x1000) {
            v.counter -= 0x1000;
            advance(c, i);
        }
    }
}

// ---- Reverb ----------------------------------------------------------------------------------
// The reverb work area runs from ESA to EEA (whose register holds bits 16-19
// of a word address ending in 0xFFFF); every other register is an offset in
// it from the current position, which moves on one word per two samples.
// Left and right take turns, one per sample (psx-spx, "SPU Reverb Formula").
Stereo reverb(int index, const Stereo& wet) {
    Core& c = g_core[index];
    Reverb& rv = c.rev;
    rv.in[0][rv.pos] = static_cast<int16_t>(sat16(wet.l));
    rv.in[1][rv.pos] = static_cast<int16_t>(sat16(wet.r));
    const int right = static_cast<int>(rv.tick & 1);
    const uint64_t base = rv.tick >> 1;
    ++rv.tick;

    const uint32_t start = addrPair(coreReg(index, 0x2E0), coreReg(index, 0x2E2));
    const uint32_t end = ((coreReg(index, 0x33C) & 0xF) << 16) | 0xFFFF;
    int32_t produced = 0;
    if (start < end) {
        const uint32_t size = end - start + 1;
        auto offset = [&](int n) { return addrPair(coreReg(index, 0x2E4 + n * 4), coreReg(index, 0x2E6 + n * 4)); };
        auto at = [&](uint32_t off) { return start + static_cast<uint32_t>((base + off) % size); };
        auto mem = [&](uint32_t a) { return static_cast<int32_t>(static_cast<int16_t>(g_ram[a])); };
        // The offset registers, in order from 0x2E4.
        enum { APF1_SIZE, APF2_SIZE, SAME_L_DST, SAME_R_DST, COMB1_L, COMB1_R, COMB2_L, COMB2_R, SAME_L_SRC, SAME_R_SRC,
               DIFF_L_DST, DIFF_R_DST, COMB3_L, COMB3_R, COMB4_L, COMB4_R, DIFF_L_SRC, DIFF_R_SRC, APF1_L_DST, APF1_R_DST,
               APF2_L_DST, APF2_R_DST };
        const int s = right;   // the register pairs are left, right
        const uint32_t sameDst = at(offset(SAME_L_DST + s)), samePrev = at(offset(SAME_L_DST + s) + size - 1);
        const uint32_t sameSrc = at(offset(SAME_L_SRC + s));
        const uint32_t diffDst = at(offset(DIFF_L_DST + s)), diffPrev = at(offset(DIFF_L_DST + s) + size - 1);
        const uint32_t diffSrc = at(offset(DIFF_R_SRC - s));   // from the other side
        const uint32_t comb[4] = {at(offset(COMB1_L + s)), at(offset(COMB2_L + s)), at(offset(COMB3_L + s)), at(offset(COMB4_L + s))};
        const uint32_t apf1Dst = at(offset(APF1_L_DST + s)), apf1Src = at(offset(APF1_L_DST + s) + size - offset(APF1_SIZE) % size);
        const uint32_t apf2Dst = at(offset(APF2_L_DST + s)), apf2Src = at(offset(APF2_L_DST + s) + size - offset(APF2_SIZE) % size);
        const bool enabled = attr(index) & 0x80;
        if (enabled && irqArmed()) {
            for (uint32_t a : {sameDst, samePrev, sameSrc, diffDst, diffPrev, diffSrc, comb[0], comb[1], comb[2], comb[3], apf1Dst,
                               apf1Src, apf2Dst, apf2Src})
                touch(a);
        }
        // Down to the reverb's rate.
        int32_t x = 0;
        for (int k = 0; k < 39; ++k) x += kResample[k] * rv.in[right][(rv.pos + 64 - 38 + k) & 63];
        x = sat16(x >> 15);

        auto vol = [&](int i) { return static_cast<int32_t>(static_cast<int16_t>(extraReg(index, 10 + i))); };
        enum { IIR, COMB1V, COMB2V, COMB3V, COMB4V, WALL, APF1V, APF2V, IN_L, IN_R };
        const int32_t in = sat16(mul(x, vol(IN_L + right)));
        const int32_t same = sat16(mul(vol(IIR), sat16(in + mul(vol(WALL), mem(sameSrc)) - mem(samePrev))) + mem(samePrev));
        const int32_t diff = sat16(mul(vol(IIR), sat16(in + mul(vol(WALL), mem(diffSrc)) - mem(diffPrev))) + mem(diffPrev));
        const int32_t combSum = sat16(mul(vol(COMB1V), mem(comb[0])) + mul(vol(COMB2V), mem(comb[1])) +
                                      mul(vol(COMB3V), mem(comb[2])) + mul(vol(COMB4V), mem(comb[3])));
        const int32_t apf1 = sat16(combSum - mul(vol(APF1V), mem(apf1Src)));
        const int32_t out1 = sat16(mem(apf1Src) + mul(vol(APF1V), apf1));
        const int32_t apf2 = sat16(out1 - mul(vol(APF2V), mem(apf2Src)));
        produced = sat16(mem(apf2Src) + mul(vol(APF2V), apf2));
        // ATTR bit 7 off: the reverb still reads and sounds, but writes nothing.
        if (enabled) {
            g_ram[sameDst] = static_cast<uint16_t>(same);
            g_ram[diffDst] = static_cast<uint16_t>(diff);
            g_ram[apf1Dst] = static_cast<uint16_t>(apf1);
            g_ram[apf2Dst] = static_cast<uint16_t>(apf2);
        }
    }
    rv.out[right][rv.pos] = static_cast<int16_t>(produced);
    rv.out[right ^ 1][rv.pos] = 0;
    // Back up to the mixing rate (zero-stuffed, so the taps count double).
    Stereo result;
    for (int k = 0; k < 39; ++k) {
        const uint32_t p = (rv.pos + 64 - 38 + k) & 63;
        result.l += kResample[k] * 2 * rv.out[0][p];
        result.r += kResample[k] * 2 * rv.out[1][p];
    }
    rv.pos = (rv.pos + 1) & 63;
    return {sat16(result.l >> 15), sat16(result.r >> 15)};
}

// ---- Mixing ----------------------------------------------------------------------------------
// MMIX: bits 11-8 voices (dry left, dry right, wet left, wet right), 7-4
// the PCM input, 3-0 the external input (core 1 only: core 0's output).
Stereo mixCore(int index, const Stereo& voicesDry, const Stereo& voicesWet, const Stereo& input, const Stereo& ext) {
    Core& c = g_core[index];
    const uint32_t mmix = coreReg(index, 0x198) & (index == 0 ? 0xFF0 : 0xFFF);
    const Stereo vd{sat16(voicesDry.l), sat16(voicesDry.r)}, vw{sat16(voicesWet.l), sat16(voicesWet.r)};
    writeOut(kMixOut[index] + 0x000 + g_outPos, vd.l);
    writeOut(kMixOut[index] + 0x200 + g_outPos, vd.r);
    writeOut(kMixOut[index] + 0x400 + g_outPos, vw.l);
    writeOut(kMixOut[index] + 0x600 + g_outPos, vw.r);
    Stereo dry, wet;
    if (mmix & 0x800) dry.l += vd.l;
    if (mmix & 0x400) dry.r += vd.r;
    if (mmix & 0x200) wet.l += vw.l;
    if (mmix & 0x100) wet.r += vw.r;
    if (mmix & 0x080) dry.l += input.l;
    if (mmix & 0x040) dry.r += input.r;
    if (mmix & 0x020) wet.l += input.l;
    if (mmix & 0x010) wet.r += input.r;
    if (mmix & 0x008) dry.l += ext.l;
    if (mmix & 0x004) dry.r += ext.r;
    if (mmix & 0x002) wet.l += ext.l;
    if (mmix & 0x001) wet.r += ext.r;
    const Stereo rv = reverb(index, wet);
    const int32_t evolL = static_cast<int16_t>(extraReg(index, 2)), evolR = static_cast<int16_t>(extraReg(index, 3));
    stepVolume(c.mvol[0]);
    stepVolume(c.mvol[1]);
    return {mul(sat16(dry.l + mul(rv.l, evolL)), c.mvol[0].env.level), mul(sat16(dry.r + mul(rv.r, evolR)), c.mvol[1].env.level)};
}

// AutoDMA: as the input position enters one half of the 512-sample input
// area, the other half (just played) takes the transfer's next 1 KB block.
void fillInput(int index, uint32_t half) {
    AutoDma& a = g_core[index].adma;
    if (!a.active) return;
    const uint32_t take = std::min<uint32_t>(a.left, 1024);
    uint8_t block[1024] = {};
    mem::readBlock(a.src, block, take);
    const uint32_t base = kInput[index] + half * 0x100;
    std::memcpy(&g_ram[base], block, 512);
    std::memcpy(&g_ram[base + 0x200], block + 512, 512);
    if (take < 1024)
        PS2_FALLBACK("sound", "AutoDMA tail", "SPU2 core %d: an AutoDMA transfer ends with %u bytes, not a whole 1 KB block", index,
                     take);
    a.src += take;
    a.left -= take;
    if (a.left == 0) {
        a.active = false;
        g_yield = true;
        if (g_admaDone) g_admaDone(index);
    }
}

Stereo readInput(int index) {
    const uint32_t l = kInput[index] + g_outPos, r = l + 0x200;
    touch(l);
    touch(r);
    const int32_t bl = static_cast<int16_t>(extraReg(index, 6)), br = static_cast<int16_t>(extraReg(index, 7));
    return {mul(static_cast<int16_t>(g_ram[l]), bl), mul(static_cast<int16_t>(g_ram[r]), br)};
}

void mixSample() {
    if ((g_outPos & 0xFF) == 0)
        for (int c = 0; c < 2; ++c) fillInput(c, ((g_outPos >> 8) & 1) ^ 1);
    const Stereo in0 = readInput(0), in1 = readInput(1);
    Stereo dry[2], wet[2];
    for (int c = 0; c < 2; ++c) {
        stepNoise(g_core[c], c);
        mixVoices(c, dry[c], wet[c]);
    }
    const Stereo out0 = mixCore(0, dry[0], wet[0], in0, {});
    writeOut(kCore0Out + g_outPos, sat16(out0.l));
    writeOut(kCore0Out + 0x200 + g_outPos, sat16(out0.r));
    const int32_t avolL = static_cast<int16_t>(extraReg(1, 4)), avolR = static_cast<int16_t>(extraReg(1, 5));
    const Stereo ext{mul(sat16(out0.l), avolL), mul(sat16(out0.r), avolR)};
    const Stereo out = mixCore(1, dry[1], wet[1], in1, ext);
    g_outBuf.push_back(static_cast<int16_t>(sat16(out.l)));
    g_outBuf.push_back(static_cast<int16_t>(sat16(out.r)));
    g_outPos = (g_outPos + 1) & 0x1FF;
}

// Mixes up to `samples` samples, stopping after one that raised an
// interrupt. True if it stopped there.
bool mix(uint64_t samples) {
    g_mixing = true;
    g_yield = false;
    uint64_t done = 0;
    while (done < samples && !g_yield) {
        mixSample();
        ++done;
    }
    g_samples += done;
    g_mixing = false;
    if (g_sink && !g_outBuf.empty()) g_sink(g_outBuf.data(), g_outBuf.size() / 2);
    g_outBuf.clear();
    return g_yield;
}

bool catchUp() {
    if (g_mixing) return false;
    const uint64_t now = nowCycles();
    if (now <= g_start) return false;
    const uint64_t due = (now - g_start) / kEeCyclesPerSample;
    return due > g_samples && mix(due - g_samples);
}

void scheduleTick();

// The SPU2 can fall behind (the EE slept or drew a long frame) and then
// takes several blocks of streamed input at once. Each block's interrupt is
// handled by the IOP before the next is mixed, as it would be in time.
void tick(uint64_t) {
    g_tickEvent = 0;
    for (int i = 0; i < 256 && catchUp(); ++i) iop::runPending();
    scheduleTick();
}

void scheduleTick() {
    if (g_tickEvent) cancelEvent(g_tickEvent);
    g_tickEvent = scheduleEvent(g_start + (g_samples + kTickSamples) * kEeCyclesPerSample, &tick, 0);
}

// ---- Registers -------------------------------------------------------------------------------
void writeVoiceParam(Voice& v, int r, uint16_t value) {
    switch (r) {
    case 0: setVolume(v.vol[0], value); break;
    case 1: setVolume(v.vol[1], value); break;
    case 2: v.pitch = value; break;
    case 3: v.adsr1 = value; break;
    case 4: v.adsr2 = value; break;
    case 5: v.env.level = static_cast<int16_t>(value); break;   // ENVX: the envelope jumps there
    default: break;                                              // VOLXL, VOLXR: read only
    }
}

void writeVoiceAddr(Voice& v, int r, uint16_t value) {
    auto setHi = [&](uint32_t& a) { a = ((value & 0xFu) << 16) | (a & 0xFFF8); };
    auto setLo = [&](uint32_t& a) { a = (a & 0xF0000) | (value & 0xFFF8u); };
    switch (r) {
    case 0: setHi(v.ssa); break;
    case 1: setLo(v.ssa); break;
    case 2: setHi(v.lsax); v.lsaxWritten = true; break;
    case 3: setLo(v.lsax); v.lsaxWritten = true; break;
    default: {   // NAX: playback jumps to that block
        uint32_t a = v.block;
        if (r == 4) setHi(a);
        else setLo(a);
        fetchBlock(v, a);
        break;
    }
    }
}

uint16_t readVoiceAddr(const Voice& v, int r) {
    const uint32_t nax = (v.block + 1 + static_cast<uint32_t>(v.pos) / 4) & kWordMask;
    const uint32_t a = r < 2 ? v.ssa : r < 4 ? v.lsax : nax;
    return static_cast<uint16_t>(r % 2 == 0 ? a >> 16 : a & 0xFFFF);
}

void writeCore(int index, uint32_t r, uint16_t value) {
    Core& c = g_core[index];
    if (r < 0x180) return writeVoiceParam(c.voices[r >> 4], (r >> 1) & 7, value);
    if (r >= 0x1C0 && r < 0x2E0) return writeVoiceAddr(c.voices[(r - 0x1C0) / 12], static_cast<int>((r - 0x1C0) % 12 / 2), value);
    switch (r) {
    case 0x180: case 0x182: c.pmon = maskPair(coreReg(index, 0x180), coreReg(index, 0x182)) & ~1u; break;
    case 0x184: case 0x186: c.non = maskPair(coreReg(index, 0x184), coreReg(index, 0x186)); break;
    case 0x188: case 0x18A: c.vmixl = maskPair(coreReg(index, 0x188), coreReg(index, 0x18A)); break;
    case 0x18C: case 0x18E: c.vmixel = maskPair(coreReg(index, 0x18C), coreReg(index, 0x18E)); break;
    case 0x190: case 0x192: c.vmixr = maskPair(coreReg(index, 0x190), coreReg(index, 0x192)); break;
    case 0x194: case 0x196: c.vmixer = maskPair(coreReg(index, 0x194), coreReg(index, 0x196)); break;
    case 0x19A:   // ATTR: turning the interrupt off acknowledges it
        if (!(value & 0x40)) g_irqInfo &= static_cast<uint16_t>(~(4 << index));
        break;
    case 0x19C: case 0x19E: c.irqa = addrPair(coreReg(index, 0x19C), coreReg(index, 0x19E)); break;
    case 0x1A0: case 0x1A2: {   // KON
        const uint32_t bits = r == 0x1A0 ? value : (value & 0xFFu) << 16;
        for (int i = 0; i < kVoices; ++i)
            if (bits & (1u << i)) keyOn(c, i);
        break;
    }
    case 0x1A4: case 0x1A6: {   // KOFF
        const uint32_t bits = r == 0x1A4 ? value : (value & 0xFFu) << 16;
        for (int i = 0; i < kVoices; ++i)
            if (bits & (1u << i)) keyOff(c.voices[i]);
        break;
    }
    case 0x1A8: case 0x1AA: c.tsa = addrPair(coreReg(index, 0x1A8), coreReg(index, 0x1AA)); break;
    case 0x1AC:   // the data port: a manual transfer word at TSA
        touch(c.tsa);
        g_ram[c.tsa] = value;
        c.tsa = (c.tsa + 1) & kWordMask;
        break;
    case 0x1B0:   // ADMAS
        if (!(value & 3) && c.adma.active) stopAutoDma(index);
        break;
    case 0x2E0: case 0x2E2: c.rev.tick = 0; break;   // ESA: the reverb starts over there
    case 0x340: c.endx &= 0xFF0000; break;           // ENDX: a write clears it
    case 0x342: c.endx &= 0xFFFF; break;
    default: break;
    }
}

uint16_t readCore(int index, uint32_t r) {
    const Core& c = g_core[index];
    if (r < 0x180) {
        const Voice& v = c.voices[r >> 4];
        switch ((r >> 1) & 7) {
        case 5: return static_cast<uint16_t>(v.env.level);
        case 6: return static_cast<uint16_t>(v.vol[0].env.level);
        case 7: return static_cast<uint16_t>(v.vol[1].env.level);
        default: return static_cast<uint16_t>(coreReg(index, r));
        }
    }
    if (r >= 0x1C0 && r < 0x2E0) return readVoiceAddr(c.voices[(r - 0x1C0) / 12], static_cast<int>((r - 0x1C0) % 12 / 2));
    switch (r) {
    case 0x340: return static_cast<uint16_t>(c.endx);
    case 0x342: return static_cast<uint16_t>(c.endx >> 16);
    case 0x344:   // STATX: transfers are always done; bits 0-5 follow ATTR
        return static_cast<uint16_t>(0x80 | (attr(index) & 0x3F));
    default: return static_cast<uint16_t>(coreReg(index, r));
    }
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------
// The header word: shift in bits 0-3 (13-15 act as 9), filter in bits 4-6
// (5-7 predict nothing), loop flags in bits 8-15. Then 28 nibbles, low first.
void decodeAdpcm(const uint16_t block[8], int32_t& prev1, int32_t& prev2, int16_t samples[28]) {
    int shift = block[0] & 0xF;
    if (shift > 12) shift = 9;
    const int filter = (block[0] >> 4) & 7;
    const int32_t f0 = filter <= 4 ? kPredict[filter][0] : 0, f1 = filter <= 4 ? kPredict[filter][1] : 0;
    for (int i = 0; i < 28; ++i) {
        const uint32_t nibble = (block[1 + i / 4] >> ((i & 3) * 4)) & 0xF;
        const int32_t s = sat16((static_cast<int16_t>(static_cast<uint16_t>(nibble << 12)) >> shift) + ((f0 * prev1 + f1 * prev2 + 32) >> 6));
        samples[i] = static_cast<int16_t>(s);
        prev2 = prev1;
        prev1 = s;
    }
}

void init() {
    g_ram.assign(kRamWords, 0);
    g_regs.fill(0);
    for (Core& c : g_core) c = Core{};
    for (Core& c : g_core) c.noise = 1;
    g_irqInfo = 0;
    g_outPos = 0;
    g_start = nowCycles();
    g_samples = 0;
    g_mixing = false;
    g_outBuf.clear();
    scheduleTick();
}

void onIopReboot() {
    sync();
    for (int c = 0; c < 2; ++c) stopAutoDma(c);
    g_irqInfo = 0;
}

void sync() { catchUp(); }

void runSamples(uint32_t samples) {
    while (samples && !g_mixing) {
        const uint64_t before = g_samples;
        mix(samples);
        samples -= static_cast<uint32_t>(g_samples - before);
    }
}

uint16_t read16(uint32_t phys) {
    sync();
    const uint32_t off = phys & 0x7FE;
    if (off < 0x760) return readCore(off >= 0x400 ? 1 : 0, off & 0x3FF);
    if (off < 0x7B0) {
        const int c = static_cast<int>((off - 0x760) / 0x28), i = static_cast<int>((off - 0x760) % 0x28 / 2);
        if (i == 8 || i == 9) return static_cast<uint16_t>(g_core[c].mvol[i - 8].env.level);   // MVOLXL, MVOLXR
        return extraReg(c, i);
    }
    if (off == 0x7C2) return g_irqInfo;
    return reg(off);
}

void write16(uint32_t phys, uint16_t value) {
    sync();
    const uint32_t off = phys & 0x7FE;
    reg(off) = value;
    if (off < 0x760) return writeCore(off >= 0x400 ? 1 : 0, off & 0x3FF, value);
    if (off < 0x7B0) {
        const int c = static_cast<int>((off - 0x760) / 0x28), i = static_cast<int>((off - 0x760) % 0x28 / 2);
        if (i < 2) setVolume(g_core[c].mvol[i], value);
    }
}

void writeMemory(uint32_t addr, const uint8_t* src, uint32_t size) {
    sync();
    const uint32_t word = (addr >> 1) & kWordMask;
    touchRange(word, (size + 1) / 2);
    for (uint32_t i = 0; i < size; ++i) {
        const uint32_t b = (addr + i) & (kRamBytes - 1);
        uint16_t& w = g_ram[b >> 1];
        w = (b & 1) ? static_cast<uint16_t>((w & 0x00FF) | (src[i] << 8)) : static_cast<uint16_t>((w & 0xFF00) | src[i]);
    }
}

void readMemory(uint32_t addr, uint8_t* dst, uint32_t size) {
    sync();
    touchRange((addr >> 1) & kWordMask, (size + 1) / 2);
    for (uint32_t i = 0; i < size; ++i) {
        const uint32_t b = (addr + i) & (kRamBytes - 1);
        dst[i] = static_cast<uint8_t>(g_ram[b >> 1] >> ((b & 1) * 8));
    }
}

uint8_t* ram() { return reinterpret_cast<uint8_t*>(g_ram.data()); }

// The transfer's first block arrives when the input position next enters a
// half of the input area, so a new stream starts 5 to 11 ms later.
void startAutoDma(int core, uint32_t iopAddr, uint32_t size) {
    sync();
    g_core[core & 1].adma = {size > 0, iopAddr, size};
}

void stopAutoDma(int core) {
    sync();
    Core& c = g_core[core & 1];
    c.adma = {};
    std::fill_n(&g_ram[kInput[core & 1]], 0x400, uint16_t(0));
}

bool autoDmaActive(int core) { return g_core[core & 1].adma.active; }
uint32_t autoDmaAddress(int core) { return g_core[core & 1].adma.src; }
void setAutoDmaDone(AutoDmaDone fn) { g_admaDone = fn; }
void setSink(Sink sink) { g_sink = sink; }

} // namespace ps2::iop::spu2
