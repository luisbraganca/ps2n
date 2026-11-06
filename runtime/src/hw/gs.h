// Graphics Synthesizer: privileged registers, GIF packet processing, local
// memory and a small software rasterizer.
//
// Coverage (enough for 2D, debug text and simple 3D sent through GIF/VIF1):
// points, lines, triangles (flat/Gouraud), sprites; textures in CT32/24/16
// and T8/T4 with CSM1 CLUTs (nearest, bilinear or trilinear sampling and
// mipmaps, as TEX1 asks); TFX modulate/decal/highlight; alpha test, Z test,
// alpha blending, FBMSK; host->local, local->local and local->host image
// transfers. Not modelled: dithering and destination alpha test.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2::gs {

void init();

// The Vulkan renderer (gpu/renderer.h) draws instead of the software
// rasterizer, at `scale` (1 to 8) times the console's resolution. With the
// device open (gpu::init), before the guest starts or on its thread; called
// again while the card draws, it goes on at the new scale. False, with
// `why`, where it cannot.
bool useVulkan(int scale, std::string& why);
// The software rasterizer draws again, with every page the card drew back in
// GS memory. Guest thread, or a test's.
void useSoftware();
bool vulkanDraws();
int vulkanScale();   // 1 while the software rasterizer draws; any thread
// The settings page's resolution, which the Vulkan renderer takes at the next
// vsync (useVulkan again, on the guest thread). Any thread.
void requestScale(int scale);

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
// GIF data and the GS thread decodes it for the draw threads, up to two
// vsyncs behind. A vsync's
// picture is still the memory and display registers as they were at that
// vsync. Off by default; the runtime turns it on for real-time runs, and
// setDrawThreads(1) keeps everything on the guest thread. Guest thread.
void setOwnThread(bool on);
// Whether the GS decodes on its own thread now.
bool ownThread();
// Finishes what VU1's thread and the GS thread have queued and stops them (at exit).
void shutdown();

// PATH1 from VU1 on its own thread (vu::setOwnThread). The guest thread
// queues each program where its XGKICK packets go among the GIF data
// (path1Run); the VU1 thread sends them (path1Kick), then the program's end
// (path1End), and the GS thread decodes them there as they come.
void path1Run(uint64_t program);
void path1Kick(const uint64_t* qwords, size_t count);
void path1End(uint64_t program);
// At exit: ends the waits of both threads on PATH1 for good.
void path1Close();

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

// Threads that draw: they share every primitive, each drawing its own bands
// of rows, and the picture is the same as one thread's. 0 picks half the
// host's threads, at most 8; 1 draws everything on the guest thread, with no
// GS thread.
void setDrawThreads(int n);

// Direct access for tests and tools. readPixel32 waits for the GS thread and
// the draw threads.
uint32_t readPixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y);
// Up to `count` 128-bit quadwords of the local-to-host transfer TRXDIR started.
// The guest thread's first read waits for everything the GS had queued.
size_t readTransfer(uint64_t* out, size_t count);
// A host-side result buffer for reverse VIF1 DMA. The GS thread fills it;
// the guest publishes it to RAM only after `done` is set.
struct ReadbackJob {
    std::vector<uint64_t> data;
    uint32_t moved = 0;
    std::atomic<bool> done{false};
};
void queueReadback(ReadbackJob* job);
// Primitives the draw threads did not share, drawn by the queuing thread
// alone (they read memory they draw into, or in an unsupported format), since
// start-up. Waits like readPixel32.
uint64_t primitivesDrawnAlone();
// Index of the memory unit holding pixel (x, y): a word for 32/24-bit
// formats, halfword for 16-bit, byte for T8 and nibble for T4.
uint32_t debugAddress(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);

} // namespace ps2::gs
