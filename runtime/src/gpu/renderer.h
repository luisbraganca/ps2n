// The GS's Vulkan renderer: draws the primitives gs.cpp decodes on the
// graphics card, at N times the console's resolution, instead of the
// software rasterizer.
//
// GS memory is shared out by pages of 8 KB. A page's current contents belong
// to the CPU (g_vram holds them), to one target or to the card's mirror. A
// target is an image of one buffer layout (its first page, width and pixel
// family), N x N image pixels a GS pixel, each holding the pixel's raw unit.
// The mirror is the card's copy of GS memory at 1x, which textures, CLUTs and
// the display read in any layout. A partial cross-layout transfer merges into
// it and leaves g_vram stale until the CPU reads the page. Pages move exactly:
// a target's page goes to GS memory as its pixels N * x, N * y, which are what
// the software would have drawn there, and comes from it as each unit N x N
// times. So whatever reads GS memory (a
// texture in another layout, a CLUT, the EE, the tests) sees what the
// software rasterizer would have drawn, at any N; only targets read as
// textures in their own layout, and the display, use the extra pixels.
//
// One thread calls it at a time: the GS thread, or the guest thread where
// the GS has none (deterministic runs). Main thread for start.
#pragma once

#include "hw/gs.h"
#include "hw/gs_internal.h"

#include <cstdint>
#include <string>

namespace ps2::video {
struct Present;
}

namespace ps2::gpu::renderer {

// With the device open (gpu::init): pipelines, buffers, the mirror. False,
// with `why`, where it cannot.
bool start(int scale, std::string& why);
// Everything freed, with every page back in GS memory first when `readBack`
// (a switch to the software rasterizer). Before gpu::shutdown.
void stop(bool readBack);
bool active();
int scale();

// gs::init: GS memory was cleared, every page is the CPU's again.
void reset();
// Draws at another scale from now on: every page goes back to GS memory and
// the targets are made again as drawing needs them. The window's pictures
// and the device's objects stay.
void rescale(int scale);
// A primitive with its drawing state, as gs.cpp's kick has it. False for one
// the card cannot hold (its frame or depth buffer reaches round the end of
// GS memory, or past what the card's images hold at this scale): gs.cpp has
// the software rasterizer draw it in GS memory, between cpuDraws and cpuDrew.
bool draw(const gs::detail::Draw& d, uint64_t gen, gs::detail::Shape shape, const gs::detail::Vertex* v);
// The pages such a primitive reads come back to GS memory, and those it may
// draw into become the CPU's.
void cpuDraws(const gs::detail::Draw& d);
// What the software drew there is new to the card's copy of GS memory.
void cpuDrew(const gs::detail::Draw& d);
// An image transfer (TRXDIR) starts. True when the renderer carried it out
// itself (a local-to-local copy between targets of one layout); otherwise
// gs.cpp writes GS memory as it always does and calls transferEnded when the
// rectangle is complete (at once for local-to-local).
bool transfer(uint32_t dir, uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg);
void transferEnded();
// Whether g_vram holds a page's current contents (the transfer CLUT checks).
bool cpuPageValid(uint32_t page);
// Before the CPU reads GS memory: these pages, or all, hold what was drawn.
void syncPages(const gs::detail::Pages& pages);
void syncAll();
// A vsync's picture: `odd` is the field's parity (gs.cpp's fieldLines) when
// SMODE2 shows fields.
void vsync(const gs::detail::Display& display, const video::Present& how, int odd);
// The display's picture now, at scale, read back (gs::currentFrame). In
// FIELD mode it goes through the deinterlacer as a vsync's does.
bool picture(const gs::detail::Display& display, int odd, gs::Frame& out);

} // namespace ps2::gpu::renderer
