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
// Guest thread: raises the GS interrupt for the FINISH and SIGNAL events the
// GS's own thread reached since the last call. hw calls it wherever the
// guest can see the interrupt (safepoints, I_STAT reads).
void pollEvents();

// The GS on its own thread, as PCSX2's MTGS runs it: the guest thread queues
// GIF data and the GS thread draws it, up to two vsyncs behind. A vsync's
// picture is still the memory and display registers as they were at that
// vsync. Off by default; the runtime turns it on for real-time runs, and
// setDrawThreads(1) keeps everything on the guest thread. Guest thread.
void setOwnThread(bool on);
// Finishes what the GS thread has queued and stops it (at exit).
void shutdown();

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
// The image the display circuits currently show, once the GS thread has
// drawn what is queued. False if the display is off. onVblankStart() makes
// one for the window and the dumps each vsync, which it remembers: in FIELD
// mode (SMODE2 interlaced, FFMD 1) each vsync shows one field, half the
// picture's lines, and the other half comes from deinterlacing, as PCSX2
// offers it.
bool currentFrame(Frame& out);

enum class Deinterlace : uint8_t {
    Adaptive,   // what stayed still since the last field of this parity: the previous field's lines
                // (the whole picture, steady); what moved: interpolated from this field. A field
                // goes on the lines the game drew it for, and a picture shown twice stays as it was.
    Weave,      // the previous field's lines everywhere: steady, but moving edges comb
    Bob,        // this field's lines doubled, as the PS2 sends it: still edges shake by a line
    Count
};
void setDeinterlace(Deinterlace mode);   // any thread
Deinterlace deinterlace();
const char* deinterlaceName(Deinterlace mode);

// Threads that draw a large primitive together (each takes bands of rows;
// the picture is the same as one thread's): 0 picks half the host's threads,
// at most 8; 1 draws everything on the guest thread, with no GS thread.
void setDrawThreads(int n);

// Direct access for tests and tools. Both wait for the GS thread.
uint32_t readPixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y);
// Index of the memory unit holding pixel (x, y): a word for 32/24-bit
// formats, halfword for 16-bit, byte for T8 and nibble for T4.
uint32_t debugAddress(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);

} // namespace ps2::gs
