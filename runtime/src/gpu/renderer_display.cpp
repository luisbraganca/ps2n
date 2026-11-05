// The Vulkan renderer's vsync (renderer.h): the display circuits' picture at
// scale, FIELD mode's fields through the card's deinterlacer, the picture to
// the window, and read back for --dump-frames.
#include "gpu/renderer_internal.h"

#include "gpu/device.h"
#include "gpu/picture.h"
#include "gpu/shaders/display_layout.h"
#include "gpu/shaders/layout.h"
#include "hw/gs.h"
#include "log.h"
#include "video/video.h"

#include <algorithm>
#include <cstring>

namespace ps2::gpu::renderer {

using namespace ps2::gs::detail;

namespace {

// The picture in slot `slot` (width x height), copied back to the CPU.
bool readPicture(int slot, uint32_t width, uint32_t height, gs::Frame& out) {
    Buffer back;
    if (!createBuffer(back, VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Readback, "picture read back"))
        return false;
    endRendering();
    VkCommandBuffer cmd = commandBuffer();
    fullBarrier(cmd);
    const VkBufferImageCopy copy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
    vkCmdCopyImageToBuffer(cmd, pictureImage(slot).handle, VK_IMAGE_LAYOUT_GENERAL, back.handle, 1, &copy);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                  VK_ACCESS_2_HOST_READ_BIT);
    submit();
    const bool ok = g.cmds.wait(g.cmds.submitted());
    if (ok) {
        invalidate(back);
        out.width = static_cast<int>(width);
        out.height = static_cast<int>(height);
        out.pixels.resize(size_t(width) * height);
        std::memcpy(out.pixels.data(), back.mapped, out.pixels.size() * 4);
    }
    destroy(back);
    return ok;
}

// The field history at a field's size (and the picture's): made again, and
// its bookkeeping reset, when the size changes, as gs.cpp's FieldHistory is.
bool historyFor(uint32_t width, uint32_t lines, uint32_t height) {
    History& h = g.history;
    if (h.width == width && h.lines == lines && h.height == height && h.images[0].handle) return true;
    for (size_t i = 0; i < h.images.size(); ++i) {
        if (h.images[i].handle) retire(h.images[i], UINT32_MAX);
        h.images[i] = Image{};
        const bool picture = i == 5;
        if (!createImage(h.images[i], VK_FORMAT_R8G8B8A8_UNORM, width, picture ? height : lines,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "field history"))
            return false;
        endRendering();
        imageBarrier(commandBuffer(), h.images[i].handle, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
        writePictureBinding(kHistoryBinding + static_cast<uint32_t>(i), h.images[i].view);
    }
    h.width = width;
    h.lines = lines;
    h.height = height;
    // The bookkeeping: no field yet, the images in their places.
    uint32_t control[C_WORDS] = {};
    for (uint32_t i = 0; i < 4; ++i) control[C_IMAGE + i] = kHistoryBinding + i;
    control[C_FREE] = kHistoryBinding + 4;
    control[C_PICIMAGE] = kHistoryBinding + 5;
    VkCommandBuffer cmd = commandBuffer();
    fullBarrier(cmd);
    vkCmdUpdateBuffer(cmd, g.control.handle, 0, sizeof control, control);
    fullBarrier(cmd);
    return true;
}

// Where the display reads a page of a buffer of pixel family `family`: the
// target that holds it, when its pixels are laid out as the buffer's (any
// width: a page is a page), at scale (CIRCUIT_PAGES' word); else GS memory,
// at 1x (0).
uint32_t pageAt(uint32_t page, int family) {
    const PageState& p = g.pages[page];
    const Target* t = p.owner ? targetById(p.owner) : nullptr;
    if (!t || family < 0 || t->family != family) return 0;
    const uint32_t rel = page - t->basePage;
    return CIRCUIT_PAGE_AT | t->slot << 15 | (rel / t->fbw) << 6 | rel % t->fbw;
}

Pages shownPages(const Circuit& c) {
    Pages shown;
    shown.add(footprint(c.fbp * 32, c.fbw, c.psm, static_cast<uint32_t>(c.dbx + c.width / c.magh + 1),
                        static_cast<uint32_t>(c.dby + c.height / c.magv + 1)));
    return shown;
}

// The pages a shown circuit reads from GS memory, made current in the
// mirror. Before the circuits' entries are allocated: it may submit.
void prepareCircuit(const Circuit& c, bool enabled) {
    if (!enabled) return;
    const int family = bufferFamily(c.psm);
    uint32_t native = 0;
    forEachPage(shownPages(c), [&](uint32_t page) {
        if (pageAt(page, family)) return;
        if (g.pages[page].owner) ++native;   // drawn by the card in another layout: its pixels at scale are lost here
        mirrorPage(page);
    });
    if (native && g.scale > 1)
        PS2_FALLBACK("gpu", "picture shown at the console's resolution",
                     "the display shows %u page(s) the card drew in another pixel layout: they are shown at the console's resolution",
                     native);
}

// The circuit's entry (CIRCUIT_*). `enabled`: the circuit is shown.
void circuitEntry(uint32_t* e, const Circuit& c, bool enabled) {
    std::fill(e, e + CIRCUIT_WORDS, 0u);
    e[CIRCUIT_PSM] = c.psm;
    e[CIRCUIT_FBP] = c.fbp;
    e[CIRCUIT_FBW] = c.fbw;
    e[CIRCUIT_DBX] = static_cast<uint32_t>(c.dbx);
    e[CIRCUIT_DBY] = static_cast<uint32_t>(c.dby);
    e[CIRCUIT_DX] = static_cast<uint32_t>(c.dx);
    e[CIRCUIT_DY] = static_cast<uint32_t>(c.dy);
    e[CIRCUIT_MAGH] = static_cast<uint32_t>(c.magh);
    e[CIRCUIT_MAGV] = static_cast<uint32_t>(c.magv);
    e[CIRCUIT_WIDTH] = static_cast<uint32_t>(c.width);
    e[CIRCUIT_HEIGHT] = static_cast<uint32_t>(c.height);
    if (!enabled) return;
    const int family = bufferFamily(c.psm);
    forEachPage(shownPages(c), [&](uint32_t page) { e[CIRCUIT_PAGES + page] = pageAt(page, family); });
}

} // namespace

void releaseHistory() {
    for (Image& i : g.history.images)
        if (i.handle) retire(i, UINT32_MAX);
    g.history = History{};
}

namespace {

struct Picture {
    int slot = -1;   // -1: nothing shows
    uint32_t width = 0, height = 0;
};

// The display's picture, in FIELD mode through the deinterlacer, into a
// picture slot, recorded in this submission. gs.cpp's buildFrame at scale.
Picture makePicture(const Display& disp, int odd) {
    const uint64_t pmode = disp.priv[0x0], smode2 = disp.priv[0x2];
    const Circuit c1 = readCircuit(disp, 0), c2 = readCircuit(disp, 1);
    const bool en1 = (pmode & 1) && c1.valid, en2 = (pmode & 2) && c2.valid;
    if (!en1 && !en2) return {};
    // The screen both circuits cover, a pixel the narrower circuit's.
    const bool perField = (smode2 & 1) && (smode2 & 2);
    int x0 = 1 << 20, y0 = 1 << 20, x1 = 0, y1 = 0, clocks = 16;
    for (const Circuit* c : {en1 ? &c1 : nullptr, en2 ? &c2 : nullptr}) {
        if (!c) continue;
        x0 = std::min(x0, c->dx);
        y0 = std::min(y0, c->dy);
        x1 = std::max(x1, c->dx + c->width);
        y1 = std::max(y1, c->dy + c->height);
        clocks = std::min(clocks, c->magh);
    }
    const int w = (x1 - x0) / clocks, h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return {};
    const auto n = static_cast<uint32_t>(g.scale);
    const uint32_t width = static_cast<uint32_t>(w) * n, height = static_cast<uint32_t>(h) * n;
    const auto lines = static_cast<uint32_t>((h + 1) / 2);
    prepareCircuit(c1, en1);
    prepareCircuit(c2, en2);
    reserve(2 * CIRCUIT_WORDS + 64);
    const uint32_t entries = uploadAlloc(2 * CIRCUIT_WORDS);
    circuitEntry(uploadWords() + entries, c1, en1);
    circuitEntry(uploadWords() + entries + CIRCUIT_WORDS, c2, en2);
    flushOps();
    endRendering();
    VkCommandBuffer cmd = commandBuffer();
    const int slot = acquirePicture(cmd, g.cmds, width, height);
    if (slot < 0 || (perField && !historyFor(width, lines * n, height))) return {};
    writePictureBinding(static_cast<uint32_t>(slot), pictureView(slot));
    fullBarrier(cmd);
    uint32_t flags = 0;
    if (en1) flags |= DISPLAY_EN1;
    if (en2) flags |= DISPLAY_EN2;
    if (en1 && (en2 || ((pmode >> 7) & 1))) flags |= DISPLAY_BLEND;
    if ((pmode >> 5) & 1) flags |= DISPLAY_MMOD;
    if ((pmode >> 7) & 1) flags |= DISPLAY_SLBG;
    if (perField) flags |= DISPLAY_FIELD;
    const uint32_t outHeight = perField ? lines * n : height;
    const uint32_t display[13] = {n,
                                  perField ? 0xFFFFFFFFu : static_cast<uint32_t>(slot),
                                  width,
                                  outHeight,
                                  flags,
                                  static_cast<uint32_t>((pmode >> 8) & 0xFF),
                                  static_cast<uint32_t>(disp.priv[0xE]) & 0xFFFFFFu,
                                  static_cast<uint32_t>(x0),
                                  static_cast<uint32_t>(y0),
                                  static_cast<uint32_t>(clocks),
                                  static_cast<uint32_t>(perField ? odd : 0),
                                  static_cast<uint32_t>(h),
                                  entries};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.display);
    pushConstants(cmd, display, 13);
    vkCmdDispatch(cmd, (width + 7) / 8, (outHeight + 7) / 8, 1);
    fullBarrier(cmd);
    if (perField) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.deinterlace);
        uint32_t params[10] = {n,
                               DEINTERLACE_COMPARE,
                               width,
                               lines * n,
                               static_cast<uint32_t>(odd),
                               static_cast<uint32_t>(disp.fieldCount),
                               static_cast<uint32_t>(gs::deinterlace()),
                               lines,
                               static_cast<uint32_t>(h),
                               static_cast<uint32_t>(slot)};
        pushConstants(cmd, params, 10);
        vkCmdDispatch(cmd, (width + 7) / 8, (lines * n + 7) / 8, 1);
        fullBarrier(cmd);
        params[1] = DEINTERLACE_CONTROL;
        pushConstants(cmd, params, 10);
        vkCmdDispatch(cmd, 1, 1, 1);
        fullBarrier(cmd);
        params[1] = DEINTERLACE_PIXELS;
        params[3] = height;
        pushConstants(cmd, params, 10);
        vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        fullBarrier(cmd);
    }
    return {slot, width, height};
}

} // namespace

void vsync(const Display& disp, const video::Present& how, int odd) {
    if (!g.started) return;
    flushTransfer();
    const Picture p = how.dump || how.show ? makePicture(disp, odd) : Picture{};
    submit();   // what the vsync's drawing did, on its way
    if (p.slot < 0) return;
    const uint64_t ready = g.cmds.submitted();
    if (how.show) {
        if (video::windowShowsGpuPictures()) {
            publishPicture(p.slot);
            video::presentPicture(p.slot, ready, static_cast<int>(p.width), static_cast<int>(p.height));
        } else {
            gs::Frame picture;
            if (readPicture(p.slot, p.width, p.height, picture))
                video::present(std::move(picture), video::Present{how.index, false, true});
        }
    }
    if (how.dump) {
        gs::Frame picture;
        if (readPicture(p.slot, p.width, p.height, picture)) video::writeDump(picture, how.index);
    }
}

bool picture(const Display& disp, int odd, gs::Frame& out) {
    if (!g.started) return false;
    flushTransfer();
    const Picture p = makePicture(disp, odd);
    if (p.slot < 0) {
        submit();
        return false;
    }
    return readPicture(p.slot, p.width, p.height, out);
}

} // namespace ps2::gpu::renderer
