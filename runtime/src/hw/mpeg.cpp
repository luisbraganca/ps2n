#include "hw/mpeg.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

namespace ps2::mpeg {

namespace {

// Codes are written as in ISO/IEC 13818-2 Annex B, most significant bit first.
struct VlcCode {
    const char* bits;
    int16_t value;
};

// B.1 macroblock_address_increment
constexpr VlcCode kAddressIncrement[] = {
    {"1", 1}, {"011", 2}, {"010", 3}, {"0011", 4}, {"0010", 5}, {"00011", 6}, {"00010", 7},
    {"0000111", 8}, {"0000110", 9}, {"00001011", 10}, {"00001010", 11}, {"00001001", 12}, {"00001000", 13},
    {"00000111", 14}, {"00000110", 15}, {"0000010111", 16}, {"0000010110", 17}, {"0000010101", 18},
    {"0000010100", 19}, {"0000010011", 20}, {"0000010010", 21}, {"00000100011", 22}, {"00000100010", 23},
    {"00000100001", 24}, {"00000100000", 25}, {"00000011111", 26}, {"00000011110", 27}, {"00000011101", 28},
    {"00000011100", 29}, {"00000011011", 30}, {"00000011010", 31}, {"00000011001", 32}, {"00000011000", 33},
    {"00000001111", 0x22}, {"00000001000", 0x23},
};
// B.2-B.4 macroblock_type; D pictures have only intra macroblocks.
constexpr VlcCode kTypeI[] = {{"1", 0x01}, {"01", 0x11}};
constexpr VlcCode kTypeP[] = {{"1", 0x0A}, {"01", 0x02}, {"001", 0x08}, {"00011", 0x01},
                              {"00010", 0x1A}, {"00001", 0x12}, {"000001", 0x11}};
constexpr VlcCode kTypeB[] = {{"10", 0x0C}, {"11", 0x0E}, {"010", 0x04}, {"011", 0x06}, {"0010", 0x08}, {"0011", 0x0A},
                              {"00011", 0x01}, {"00010", 0x1E}, {"000011", 0x1A}, {"000010", 0x16}, {"000001", 0x11}};
constexpr VlcCode kTypeD[] = {{"1", 0x01}};
// B.10 motion_code (the sign bit is part of the code)
constexpr VlcCode kMotionCode[] = {
    {"1", 0}, {"010", 1}, {"011", -1}, {"0010", 2}, {"0011", -2}, {"00010", 3}, {"00011", -3},
    {"0000110", 4}, {"0000111", -4}, {"00001010", 5}, {"00001011", -5}, {"00001000", 6}, {"00001001", -6},
    {"00000110", 7}, {"00000111", -7}, {"0000010110", 8}, {"0000010111", -8}, {"0000010100", 9},
    {"0000010101", -9}, {"0000010010", 10}, {"0000010011", -10}, {"00000100010", 11}, {"00000100011", -11},
    {"00000100000", 12}, {"00000100001", -12}, {"00000011110", 13}, {"00000011111", -13},
    {"00000011100", 14}, {"00000011101", -14}, {"00000011010", 15}, {"00000011011", -15},
    {"00000011000", 16}, {"00000011001", -16},
};
// B.11 dmvector
constexpr VlcCode kDmVector[] = {{"0", 0}, {"10", 1}, {"11", -1}};
// B.9 coded_block_pattern
constexpr VlcCode kCodedBlockPattern[] = {
    {"111", 60}, {"1101", 4}, {"1100", 8}, {"1011", 16}, {"1010", 32}, {"10011", 12}, {"10010", 48},
    {"10001", 20}, {"10000", 40}, {"01111", 28}, {"01110", 44}, {"01101", 52}, {"01100", 56}, {"01011", 1},
    {"01010", 61}, {"01001", 2}, {"01000", 62}, {"001111", 24}, {"001110", 36}, {"001101", 3}, {"001100", 63},
    {"0010111", 5}, {"0010110", 9}, {"0010101", 17}, {"0010100", 33}, {"0010011", 6}, {"0010010", 10},
    {"0010001", 18}, {"0010000", 34}, {"00011111", 7}, {"00011110", 11}, {"00011101", 19}, {"00011100", 35},
    {"00011011", 13}, {"00011010", 49}, {"00011001", 21}, {"00011000", 41}, {"00010111", 14}, {"00010110", 50},
    {"00010101", 22}, {"00010100", 42}, {"00010011", 15}, {"00010010", 51}, {"00010001", 23}, {"00010000", 43},
    {"00001111", 25}, {"00001110", 37}, {"00001101", 26}, {"00001100", 38}, {"00001011", 29}, {"00001010", 45},
    {"00001001", 53}, {"00001000", 57}, {"00000111", 30}, {"00000110", 46}, {"00000101", 54}, {"00000100", 58},
    {"000000111", 31}, {"000000110", 47}, {"000000101", 55}, {"000000100", 59}, {"000000011", 27},
    {"000000010", 39}, {"000000001", 0},
};
// B.12 / B.13 dct_dc_size_luminance / dct_dc_size_chrominance
constexpr VlcCode kDcSizeLuminance[] = {
    {"100", 0}, {"00", 1}, {"01", 2}, {"101", 3}, {"110", 4}, {"1110", 5}, {"11110", 6},
    {"111110", 7}, {"1111110", 8}, {"11111110", 9}, {"111111110", 10}, {"111111111", 11},
};
constexpr VlcCode kDcSizeChrominance[] = {
    {"00", 0}, {"01", 1}, {"10", 2}, {"110", 3}, {"1110", 4}, {"11110", 5}, {"111110", 6},
    {"1111110", 7}, {"11111110", 8}, {"111111110", 9}, {"1111111110", 10}, {"1111111111", 11},
};

// DCT coefficients: run/level codes (each followed by a sign bit).
struct RunLevel {
    const char* bits;
    uint8_t run, level;
};
// B.14 (table zero) up to 13 bits. A block's first non-intra coefficient
// uses "1s" for run 0 level 1 instead; the decoder handles that.
constexpr RunLevel kDctZero[] = {
    {"11", 0, 1}, {"011", 1, 1}, {"0100", 0, 2}, {"0101", 2, 1}, {"00101", 0, 3}, {"00111", 3, 1}, {"00110", 4, 1},
    {"000110", 1, 2}, {"000111", 5, 1}, {"000101", 6, 1}, {"000100", 7, 1}, {"0000110", 0, 4}, {"0000100", 2, 2},
    {"0000111", 8, 1}, {"0000101", 9, 1}, {"00100110", 0, 5}, {"00100001", 0, 6}, {"00100101", 1, 3},
    {"00100100", 3, 2}, {"00100111", 10, 1}, {"00100011", 11, 1}, {"00100010", 12, 1}, {"00100000", 13, 1},
    {"0000001010", 0, 7}, {"0000001100", 1, 4}, {"0000001011", 2, 3}, {"0000001111", 4, 2}, {"0000001001", 5, 2},
    {"0000001110", 14, 1}, {"0000001101", 15, 1}, {"0000001000", 16, 1}, {"000000011101", 0, 8},
    {"000000011000", 0, 9}, {"000000010011", 0, 10}, {"000000010000", 0, 11}, {"000000011011", 1, 5},
    {"000000010100", 2, 4}, {"0000000011010", 0, 12}, {"0000000011001", 0, 13}, {"0000000011000", 0, 14},
    {"0000000010111", 0, 15},
};
// B.15 (table one, intra blocks with intra_vlc_format) up to 10 bits.
constexpr RunLevel kDctOne[] = {
    {"10", 0, 1}, {"010", 1, 1}, {"110", 0, 2}, {"00101", 2, 1}, {"0111", 0, 3}, {"00111", 3, 1},
    {"000110", 4, 1}, {"00110", 1, 2}, {"000111", 5, 1}, {"0000110", 6, 1}, {"0000100", 7, 1}, {"11100", 0, 4},
    {"0000111", 2, 2}, {"0000101", 8, 1}, {"1111000", 9, 1}, {"11101", 0, 5}, {"000101", 0, 6}, {"1111001", 1, 3},
    {"00100110", 3, 2}, {"1111010", 10, 1}, {"00100001", 11, 1}, {"00100101", 12, 1}, {"00100100", 13, 1},
    {"000100", 0, 7}, {"00100111", 1, 4}, {"11111100", 2, 3}, {"11111101", 4, 2}, {"000000100", 5, 2},
    {"000000101", 14, 1}, {"000000111", 15, 1}, {"0000001101", 16, 1}, {"1111011", 0, 8}, {"1111100", 0, 9},
    {"00100011", 0, 10}, {"00100010", 0, 11}, {"00100000", 1, 5}, {"0000001100", 2, 4}, {"11111010", 0, 12},
    {"11111011", 0, 13}, {"11111110", 0, 14}, {"11111111", 0, 15},
};
// The 12- to 16-bit codes both tables share.
constexpr RunLevel kDctShared[] = {
    {"000000011100", 3, 3}, {"000000010010", 4, 3}, {"000000011110", 6, 2}, {"000000010101", 7, 2},
    {"000000010001", 8, 2}, {"000000011111", 17, 1}, {"000000011010", 18, 1}, {"000000011001", 19, 1},
    {"000000010111", 20, 1}, {"000000010110", 21, 1}, {"0000000010110", 1, 6}, {"0000000010101", 1, 7},
    {"0000000010100", 2, 5}, {"0000000010011", 3, 4}, {"0000000010010", 5, 3}, {"0000000010001", 9, 2},
    {"0000000010000", 10, 2}, {"0000000011111", 22, 1}, {"0000000011110", 23, 1}, {"0000000011101", 24, 1},
    {"0000000011100", 25, 1}, {"0000000011011", 26, 1}, {"00000000011111", 0, 16}, {"00000000011110", 0, 17},
    {"00000000011101", 0, 18}, {"00000000011100", 0, 19}, {"00000000011011", 0, 20}, {"00000000011010", 0, 21},
    {"00000000011001", 0, 22}, {"00000000011000", 0, 23}, {"00000000010111", 0, 24}, {"00000000010110", 0, 25},
    {"00000000010101", 0, 26}, {"00000000010100", 0, 27}, {"00000000010011", 0, 28}, {"00000000010010", 0, 29},
    {"00000000010001", 0, 30}, {"00000000010000", 0, 31}, {"000000000011000", 0, 32}, {"000000000010111", 0, 33},
    {"000000000010110", 0, 34}, {"000000000010101", 0, 35}, {"000000000010100", 0, 36}, {"000000000010011", 0, 37},
    {"000000000010010", 0, 38}, {"000000000010001", 0, 39}, {"000000000010000", 0, 40}, {"000000000011111", 1, 8},
    {"000000000011110", 1, 9}, {"000000000011101", 1, 10}, {"000000000011100", 1, 11}, {"000000000011011", 1, 12},
    {"000000000011010", 1, 13}, {"000000000011001", 1, 14}, {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16},
    {"0000000000010001", 1, 17}, {"0000000000010000", 1, 18}, {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2},
    {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2}, {"0000000000010111", 14, 2}, {"0000000000010110", 15, 2},
    {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1}, {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1},
    {"0000000000011100", 30, 1}, {"0000000000011011", 31, 1},
};

// Fills the entries of `table` (indexed by its first `width` bits) that start
// with the code `bits`.
template <typename T>
void fill(std::vector<T>& table, int width, const char* bits, const T& entry) {
    const int len = static_cast<int>(std::strlen(bits));
    uint32_t code = 0;
    for (int i = 0; i < len; ++i) code = (code << 1) | (bits[i] == '1' ? 1u : 0u);
    const uint32_t first = code << (width - len);
    std::fill_n(table.begin() + first, size_t{1} << (width - len), entry);
}

class Lut {
public:
    explicit Lut(std::span<const VlcCode> codes) {
        for (const VlcCode& c : codes) width_ = std::max(width_, static_cast<int>(std::strlen(c.bits)));
        entries_.resize(size_t{1} << width_);
        for (const VlcCode& c : codes) fill(entries_, width_, c.bits, Code{c.value, static_cast<uint8_t>(std::strlen(c.bits))});
    }
    Code operator()(uint32_t top) const { return entries_[top >> (32 - width_)]; }
    int width() const { return width_; }

private:
    int width_ = 0;
    std::vector<Code> entries_;
};

class DctLut {
public:
    DctLut(const char* endOfBlock, std::span<const RunLevel> own) : entries_(size_t{1} << 16) {
        fill(entries_, 16, endOfBlock, Coefficient{Coefficient::EndOfBlock, 0, 0, static_cast<uint8_t>(std::strlen(endOfBlock))});
        fill(entries_, 16, "000001", Coefficient{Coefficient::Escape, 0, 0, 6});
        for (auto list : {own, std::span<const RunLevel>(kDctShared)})
            for (const RunLevel& c : list)
                fill(entries_, 16, c.bits, Coefficient{Coefficient::RunLevel, c.run, c.level, static_cast<uint8_t>(std::strlen(c.bits))});
    }
    Coefficient operator()(uint32_t top) const { return entries_[top >> 16]; }

private:
    std::vector<Coefficient> entries_;
};

const Lut& lut(Vlc table) {
    static const Lut luts[] = {Lut(kAddressIncrement), Lut(kTypeI), Lut(kTypeP), Lut(kTypeB), Lut(kTypeD),
                               Lut(kMotionCode), Lut(kDmVector), Lut(kCodedBlockPattern), Lut(kDcSizeLuminance),
                               Lut(kDcSizeChrominance)};
    return luts[static_cast<int>(table)];
}

// IDCT basis scaled by 2^20: c(k) * cos((2n + 1) * k * pi / 16), with
// c(0) = 1 / sqrt(8) and c(k) = 1 / 2.
struct Basis {
    int64_t v[8][8];
    Basis() {
        int64_t half[9];   // 2^19 * cos(m * pi / 16)
        for (int m = 0; m <= 8; ++m) half[m] = std::llround(std::ldexp(std::cos(m * std::numbers::pi / 16), 19));
        for (int k = 0; k < 8; ++k)
            for (int n = 0; n < 8; ++n) {
                int m = ((2 * n + 1) * k) % 32;   // cos is even and has period 32 here
                if (m > 16) m = 32 - m;
                v[k][n] = k == 0 ? half[4] : (m <= 8 ? half[m] : -half[16 - m]);   // 2^20 / sqrt(8) = 2^19 cos(pi / 4)
            }
    }
};
const Basis kBasis;

uint32_t clamp8(int v) { return static_cast<uint32_t>(std::clamp(v, 0, 255)); }

} // namespace

Code decode(Vlc table, uint32_t top) { return lut(table)(top); }
int maxLength(Vlc table) { return lut(table).width(); }

Coefficient decodeCoefficient(bool tableOne, uint32_t top) {
    static const DctLut zero("10", kDctZero), one("0110", kDctOne);
    return (tableOne ? one : zero)(top);
}

const uint8_t kZigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};
const uint8_t kAlternate[64] = {
    0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63,
};
const uint8_t kDefaultIntraMatrix[64] = {
    8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

int quantiserScale(bool nonLinear, int code) {
    static constexpr uint8_t kNonLinear[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22,
                                               24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};
    return nonLinear ? kNonLinear[code & 31] : 2 * (code & 31);
}

void idct(const int16_t in[64], int16_t out[64]) {
    bool dcOnly = true;
    for (int i = 1; i < 64 && dcOnly; ++i) dcOnly = in[i] == 0;
    if (dcOnly) {   // flat block: in[0] / 8, rounded
        const int16_t v = static_cast<int16_t>(std::clamp((in[0] + 4) >> 3, -256, 255));
        std::fill_n(out, 64, v);
        return;
    }
    // Rows, then columns; exact integer sums at scale 2^40 (below 2^55 for
    // coefficients in -2048..2047), rounded once.
    int64_t rows[64];
    for (int y = 0; y < 8; ++y) {
        const int16_t* f = in + y * 8;
        for (int x = 0; x < 8; ++x) {
            int64_t s = 0;
            for (int u = 0; u < 8; ++u) s += f[u] * kBasis.v[u][x];
            rows[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y) {
            int64_t s = 0;
            for (int v = 0; v < 8; ++v) s += rows[v * 8 + x] * kBasis.v[v][y];
            const int64_t r = (s + (int64_t{1} << 39)) >> 40;
            out[y * 8 + x] = static_cast<int16_t>(std::clamp<int64_t>(r, -256, 255));
        }
}

void yuvToRgb32(const uint8_t raw8[384], uint32_t out[256], uint32_t th0, uint32_t th1) {
    const uint8_t* cb = raw8 + 256;
    const uint8_t* cr = raw8 + 320;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int lum = 149 * std::max(raw8[y * 16 + x] - 16, 0);   // 1.164 (Y - 16)
            const int b = cb[(y / 2) * 8 + x / 2] - 128;
            const int r = cr[(y / 2) * 8 + x / 2] - 128;
            const uint32_t red = clamp8((lum + 204 * r + 64) >> 7);              // + 1.596 Cr
            const uint32_t green = clamp8((lum - 104 * r - 50 * b + 64) >> 7);   // - 0.813 Cr - 0.391 Cb
            const uint32_t blue = clamp8((lum + 258 * b + 64) >> 7);             // + 2.016 Cb
            uint32_t& px = out[y * 16 + x];
            if (red < th0 && green < th0 && blue < th0) {
                px = 0;
                continue;
            }
            const uint32_t alpha = red < th1 && green < th1 && blue < th1 ? 0x40u : 0x80u;
            px = red | green << 8 | blue << 16 | alpha << 24;
        }
}

void rgb32ToRgb16(const uint32_t in[256], uint16_t out[256], bool dither) {
    static constexpr int kDither[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
    for (int i = 0; i < 256; ++i) {
        const int d = dither ? kDither[(i / 16) & 3][i & 3] : 0;
        const auto channel = [&](int shift) { return clamp8(static_cast<int>((in[i] >> shift) & 0xFF) + d) >> 3; };
        const uint32_t a = (in[i] >> 24) == 0x40 ? 1u : 0u;
        out[i] = static_cast<uint16_t>(channel(0) | channel(8) << 5 | channel(16) << 10 | a << 15);
    }
}

uint8_t nearestColour(uint16_t pixel, const uint16_t clut[16]) {
    uint8_t best = 0;
    int bestDistance = INT_MAX;
    for (uint8_t i = 0; i < 16; ++i) {
        int distance = 0;
        for (int shift = 0; shift < 15; shift += 5) {
            const int d = ((pixel >> shift) & 31) - ((clut[i] >> shift) & 31);
            distance += d * d;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

} // namespace ps2::mpeg
