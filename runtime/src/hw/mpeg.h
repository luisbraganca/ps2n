// MPEG-2 video building blocks for the IPU: the variable-length codes of
// ISO/IEC 13818-2 Annex B, inverse scan, inverse quantisation, the inverse
// DCT, and the IPU's colour conversions. Nothing here knows about FIFOs or
// registers; hw/ipu.cpp feeds it bits.
#pragma once

#include <cstdint>

namespace ps2::mpeg {

// A decoded variable-length code. `length` is the number of bits the code
// takes; 0 means no code starts with the bits given.
struct Code {
    int16_t value = 0;
    uint8_t length = 0;
};

// The tables VDEC selects with its TBL field, plus the ones BDEC uses. Each
// lookup takes the next bits of the stream MSB-aligned in a 32-bit word (bits
// past the end of the data may be anything; check `length` against them).
enum class Vlc {
    MacroblockAddressIncrement,   // B.1; 0x22 = macroblock_stuffing, 0x23 = macroblock_escape
    MacroblockTypeI,              // B.2-B.4 and D pictures; value bits: 0 intra, 1 pattern,
    MacroblockTypeP,              // 2 motion backward, 3 motion forward, 4 quant
    MacroblockTypeB,
    MacroblockTypeD,
    MotionCode,                   // B.10, -16..16
    DmVector,                     // B.11, -1..1
    CodedBlockPattern,            // B.9, 0..63
    DcSizeLuminance,              // B.12, 0..11
    DcSizeChrominance,            // B.13, 0..11
};
Code decode(Vlc table, uint32_t top);
// Longest code in a table, in bits.
int maxLength(Vlc table);

// One entry of the DCT coefficient tables (B.14 "table zero", B.15 "table
// one"). `length` excludes the sign bit that follows a run/level code.
struct Coefficient {
    enum Kind : uint8_t { Invalid, RunLevel, EndOfBlock, Escape };
    Kind kind = Invalid;
    uint8_t run = 0;
    uint8_t level = 0;
    uint8_t length = 0;
};
Coefficient decodeCoefficient(bool tableOne, uint32_t top);

// Coefficient order -> raster position, for zigzag and alternate scan.
extern const uint8_t kZigzag[64];
extern const uint8_t kAlternate[64];
// quantiser_scale for quantiser_scale_code 0..31, linear and non-linear.
int quantiserScale(bool nonLinear, int code);
// MPEG-2's default intra matrix, in raster order.
extern const uint8_t kDefaultIntraMatrix[64];

// 8x8 inverse DCT, raster order in and out, the result saturated to
// -256..255 as the standard requires. Within IEEE 1180's accuracy.
void idct(const int16_t in[64], int16_t out[64]);

// The IPU's CSC: a RAW8 macroblock (16x16 Y, then 8x8 Cb and Cr) to 256
// RGBA32 pixels. ITU-R BT.601 with the coefficients the IPU uses (1/128
// steps). Pixels whose R, G and B are all below th0 become 0 (transparent
// black); below th1, alpha 0x40; otherwise alpha 0x80.
void yuvToRgb32(const uint8_t raw8[384], uint32_t out[256], uint32_t th0, uint32_t th1);
// RGBA32 to RGBA16 (5:5:5:1), optionally dithered with the 4x4 matrix the GS
// also uses. The A bit marks the semi-transparent pixels (alpha 0x40), so a
// TEXA with TA0 = 0x80, TA1 = 0x40 and AEM restores all three levels.
void rgb32ToRgb16(const uint32_t in[256], uint16_t out[256], bool dither);
// RGBA16 pixel to the index of the nearest of 16 colours (VQ, for PACK).
uint8_t nearestColour(uint16_t pixel, const uint16_t clut[16]);

} // namespace ps2::mpeg
