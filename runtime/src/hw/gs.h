// Graphics Synthesizer: privileged registers, GIF packet processing, local
// memory and a small software rasterizer.
//
// Coverage (enough for 2D, debug text and simple 3D sent through GIF/VIF1):
// points, lines, triangles (flat/Gouraud), sprites; textures in CT32/24/16
// and T8/T4 with CSM1 CLUTs (nearest sampling); TFX modulate/decal/
// highlight; alpha test, Z test, alpha blending, FBMSK; host->local and
// local->local image transfers. Not modelled: bilinear/mipmapping, dithering,
// destination alpha test, local->host readback.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2::gs {

void init();

uint64_t privRead(uint32_t addr);
void privWrite(uint32_t addr, uint64_t value);
void privWrite32(uint32_t addr, uint32_t value);
uint64_t csr();
uint64_t imr();
uint64_t putImr(uint64_t value);   // returns the previous IMR

void onVblankStart();
void onVblankEnd();
void setVideoMode(bool interlace, bool pal);

uint32_t gifRegRead(uint32_t addr);
void gifRegWrite(uint32_t addr, uint32_t value);
void gifFifoWrite(const uint64_t q[2]);
void gifTransfer(const uint64_t* qwords, size_t count, int path);
void setPath3Masked(bool masked);

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;   // R, G, B, A bytes in memory order
};
// The image the display circuits currently show. False if the display is off.
bool currentFrame(Frame& out);

// Direct access for tests and tools.
uint32_t readPixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y);
// Index of the memory unit holding pixel (x, y): a word for 32/24-bit
// formats, halfword for 16-bit, byte for T8 and nibble for T4.
uint32_t debugAddress(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);

} // namespace ps2::gs
