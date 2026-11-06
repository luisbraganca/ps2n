// The GS's Vulkan renderer (renderer.h): pages and targets, the work the
// card does between draws, transfers, and the primitives themselves.
#include "gpu/renderer_internal.h"

#include "gpu/device.h"
#include "gpu/shaders/layout.h"
#include "log.h"

#include <algorithm>
#include <cfenv>
#include <cstring>

namespace ps2::gpu::renderer {

using namespace ps2::gs::detail;

Renderer g;

namespace {

// A texture format's family: the 32-bit colour layout holds T8H, T4HL and
// T4HH too; T8 and T4 have layouts of their own, which no target holds.
int textureFamily(uint32_t psm) {
    switch (psm) {
    case PSMCT32: case PSMCT24: case PSMT8H: case PSMT4HL: case PSMT4HH: return 0;
    case PSMCT16: return 1;
    case PSMCT16S: return 2;
    default: return -1;
    }
}
// A transfer's format: the family of the target it can write at scale.
int transferFamily(uint32_t psm) { return bufferFamily(psm) >= 0 ? bufferFamily(psm) : textureFamily(psm); }

uint32_t pageHeightOf(int family) { return family == 1 || family == 2 || family == 4 || family == 5 ? 64 : 32; }

// What a transfer's unit writes of a target's word (PUSH_UNIT_*).
uint32_t pushUnitOf(uint32_t psm) {
    switch (psm) {
    case PSMCT24: case PSMZ24: return PUSH_UNIT_LOW24;
    case PSMT8H: return PUSH_UNIT_T8H;
    case PSMT4HL: return PUSH_UNIT_T4HL;
    case PSMT4HH: return PUSH_UNIT_T4HH;
    default: return PUSH_UNIT_WHOLE;
    }
}

bool everyPage(const Pages& pages) {
    for (uint64_t b : pages.bits)
        if (b != ~uint64_t(0)) return false;
    return true;
}

uint32_t primBase() { return slotOfSubmission() * kMaxPrims; }
uint32_t stateBase() { return slotOfSubmission() * kMaxStates * STATE_WORDS; }
uint32_t uploadBase() { return slotOfSubmission() * kUploadWords; }

void beginRendering(int pipeline) {
    VkCommandBuffer cmd = commandBuffer();
    if (!g.rendering) {
        auto info = make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        info.renderArea = {{0, 0}, {g.renderSize, g.renderSize}};
        info.layerCount = 1;
        vkCmdBeginRendering(cmd, &info);
        const VkViewport vp{-g.viewHalf, -g.viewHalf, 2.0f * g.viewHalf, 2.0f * g.viewHalf, 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {g.renderSize, g.renderSize}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        g.rendering = true;
        g.pipelineBound = -1;
    }
    if (g.pipelineBound != pipeline) {
        if (g.runCount) {
            vkCmdDraw(cmd, 6, g.runCount, 0, g.runFirst);
            g.runFirst += g.runCount;
            g.runCount = 0;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.drawPipelines[size_t(pipeline)]);
        const uint32_t push[2] = {static_cast<uint32_t>(g.scale), std::bit_cast<uint32_t>(g.viewHalf)};
        pushConstants(cmd, push, 2);
        g.pipelineBound = pipeline;
    }
}

// Rendering ends and the next draws see everything drawn before.
void cut() {
    if (!g.rendering) return;
    endRendering();
    fullBarrier(commandBuffer());
    g.drawnSinceCut = Pages{};
    g.sampledSinceCut = Pages{};
}

uint32_t pageEntry(const Target& t, uint32_t page) {
    reserve(PAGE_WORDS);
    const uint32_t e = uploadAlloc(PAGE_WORDS);
    uint32_t* w = uploadWords() + e;
    const uint32_t rel = page - t.basePage;
    w[PAGE_IMAGE] = t.slot;
    w[PAGE_X] = (rel % t.fbw) * 64;
    w[PAGE_Y] = (rel / t.fbw) * t.pageH;
    w[PAGE_KIND] = pageKindOf(t.family);
    w[PAGE_PAGE] = page;
    return e;
}

Target* findTarget(uint32_t basePage, uint32_t fbw, int family) {
    for (auto& t : g.targets)
        if (t->basePage == basePage && t->fbw == fbw && t->family == family) return t.get();
    return nullptr;
}

void freeTargetIfEmpty(Target* t) {
    if (!t || t->owned) return;
    if (t->image.handle) retire(t->image, t->slot);
    t->image = Image{};
    if (g.state.frame == t || g.state.depth == t) g.state.gen = ~uint64_t(0);   // prepared again
    for (LevelSource& l : g.state.levels)
        if (l.from == t) g.state.gen = ~uint64_t(0);
    std::erase_if(g.targets, [&](const std::unique_ptr<Target>& p) { return p.get() == t; });
    ++g.ownershipEpoch;
}

// The target's image holds `rows` page rows at least: made larger, keeping its content.
bool growTarget(Target& t, uint32_t rows) {
    if (rows <= t.rows) return true;
    const uint32_t most = (kPages - t.basePage + t.fbw - 1) / t.fbw;
    const uint32_t want = std::min(most, std::max(rows, t.rows ? t.rows * 2 : rows));
    const auto n = static_cast<uint32_t>(g.scale);
    const uint32_t w = t.fbw * 64 * n, h = want * t.pageH * n;
    if (w > g.renderSize || h > g.renderSize) return false;
    endRendering();
    Image image;
    if (!makeStorageImage(image, VK_FORMAT_R32_UINT, w, h, "target")) return false;
    const uint32_t slot = takeSlot(image);
    if (slot == UINT32_MAX) {
        destroy(image);
        return false;
    }
    if (t.image.handle) {
        // What the work queued so far does to the old image comes first.
        flushOps();
        VkCommandBuffer cmd = commandBuffer();
        fullBarrier(cmd);
        const VkImageCopy region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0},
                                 {t.image.width, t.image.height, 1}};
        vkCmdCopyImage(cmd, t.image.handle, VK_IMAGE_LAYOUT_GENERAL, image.handle, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        fullBarrier(cmd);
        retire(t.image, t.slot);
    }
    t.image = image;
    t.slot = slot;
    t.rows = want;
    ++g.ownershipEpoch;
    return true;
}

Target* targetFor(uint32_t basePage, uint32_t fbw, int family) {
    fbw = std::max(fbw, 1u);
    if (Target* t = findTarget(basePage, fbw, family)) return t;
    auto t = std::make_unique<Target>();
    t->id = g.nextId++;
    if (g.nextId == 0) g.nextId = 1;
    t->basePage = basePage;
    t->fbw = fbw;
    t->family = family;
    t->pageH = pageHeightOf(family);
    g.targets.push_back(std::move(t));
    return g.targets.back().get();
}

// The pages become the target's: their content comes, through the mirror,
// from wherever it is. False where the target cannot hold them.
bool acquire(Target& t, const Pages& pages) {
    bool fits = true;
    uint32_t rows = 0;
    forEachPage(pages, [&](uint32_t page) {
        if (page < t.basePage) fits = false;
        else rows = std::max(rows, (page - t.basePage) / t.fbw + 1);
    });
    if (!fits || !growTarget(t, rows)) return false;
    bool moved = false;
    std::vector<Target*> losers;
    forEachPage(pages, [&](uint32_t page) {
        PageState& p = g.pages[page];
        if (p.owner == t.id) return;
        mirrorPage(page);
        if (Target* old = p.owner ? targetById(p.owner) : nullptr) {
            --old->owned;
            losers.push_back(old);
        }
        g.expandEntries.push_back(pageEntry(t, page));
        p.owner = t.id;
        ++t.owned;
        moved = true;
    });
    if (moved) ++g.ownershipEpoch;
    for (Target* old : losers)
        if (targetById(old->id)) freeTargetIfEmpty(old);
    return true;
}

// Whether a buffer (bp in blocks, bw, family) is laid out as target t's pages,
// and where its first page sits in t, in pixels.
bool sameLayout(const Target& t, uint32_t bp, uint32_t bw, int family, uint32_t& originX, uint32_t& originY) {
    if (family != t.family || std::max(bw, 1u) != t.fbw || bp % 32 != 0 || bp / 32 < t.basePage) return false;
    const uint32_t rel = bp / 32 - t.basePage;
    originX = (rel % t.fbw) * 64;
    originY = (rel / t.fbw) * t.pageH;
    return true;
}

// The pages become the CPU's, as GS memory holds them: what targets drew
// there is dropped.
void drop(const Pages& pages) {
    std::vector<Target*> losers;
    forEachPage(pages, [&](uint32_t page) {
        PageState& p = g.pages[page];
        if (!p.owner) return;
        if (Target* t = targetById(p.owner)) {
            --t->owned;
            losers.push_back(t);
        }
        p.owner = 0;
    });
    for (Target* t : losers)
        if (targetById(t->id)) freeTargetIfEmpty(t);
    ++g.ownershipEpoch;
}

// The pages become the CPU's: what targets drew in them read back first
// (`why`: the read back's report).
void release(const Pages& pages, const char* why) {
    bool any = false;
    forEachPage(pages, [&](uint32_t page) { any = any || g.pages[page].owner != 0; });
    if (!any) return;
    download(pages, why);
    drop(pages);
}

// Of `pages`, those a host-to-local rectangle overwrites whole: every unit
// of the page is in it, in a format that writes whole units (CT24 keeps
// each word's top byte, T8H and T4H keep the rest of the word). The
// rectangle starts on a page and does not wrap.
Pages wholePages(const Pages& pages, uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t dsax, uint32_t dsay, uint32_t w, uint32_t h) {
    Pages whole;
    uint32_t pw = 64, ph = 32, across = std::max(dbw, 1u);
    switch (dpsm) {
    case PSMCT32: case PSMZ32: break;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: ph = 64; break;
    case PSMT8: pw = 128, ph = 64, across = std::max(dbw >> 1, 1u); break;
    case PSMT4: pw = 128, ph = 128, across = std::max(dbw >> 1, 1u); break;
    default: return whole;
    }
    if (dbp % 32 || across * pw != std::max(dbw, 1u) * 64) return whole;   // pages that do not tile the buffer's width
    const uint32_t firstX = (dsax + pw - 1) / pw, lastX = (dsax + w) / pw;   // pages [first, last) inside the rectangle
    const uint32_t firstY = (dsay + ph - 1) / ph, lastY = (dsay + h) / ph;
    for (uint32_t py = firstY; py < lastY; ++py)
        for (uint32_t px = firstX; px < lastX && px < across; ++px) {
            const uint32_t page = (dbp / 32 + py * across + px) & (kPages - 1);
            if (hasPage(pages, page)) addPage(whole, page);
        }
    return whole;
}

// ---- Transfers ---------------------------------------------------------------------------------------

// A transfer's destination: pages of targets of its layout take its pixels
// at their scale as they come. A partial transfer in another layout resolves
// the old pixels to the mirror, where its own pixels are merged as they come.
void beginDestination(uint32_t dir, uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t dsax, uint32_t dsay, uint32_t w, uint32_t h) {
    PendingTransfer& x = g.transfer;
    x = PendingTransfer{};
    x.active = true;
    x.dir = dir;
    x.dbp = dbp;
    x.dbw = dbw;
    x.dpsm = dpsm;
    x.dsax = dsax;
    x.dsay = dsay;
    x.w = w;
    x.h = h;
    x.pages.add(footprint(dbp, dbw, dpsm, dsax + w, dsay + h));
    // A rectangle that wraps round 2048, memory or its buffer's width takes the CPU's way.
    const bool rectangular = dsax + w <= 2048 && dsay + h <= 2048 && dsax + w <= std::max(dbw, 1u) * 64 && !everyPage(x.pages);
    const int family = transferFamily(dpsm);
    const bool plain = family >= 0 && rectangular;
    Pages others;
    bool told = false;
    forEachPage(x.pages, [&](uint32_t page) {
        const PageState& p = g.pages[page];
        if (!p.owner) {
            if (!p.vramValid) addPage(others, page);   // the mirror holds what an earlier transfer left there
            return;
        }
        Target* t = targetById(p.owner);
        uint32_t ox = 0, oy = 0;
        if (plain && t && sameLayout(*t, dbp, dbw, family, ox, oy)) {
            if (std::find(x.targets.begin(), x.targets.end(), p.owner) == x.targets.end()) x.targets.push_back(p.owner);
        } else {
            if (!told && t)
                PS2_DEBUG("gpu", "a transfer to block 0x%x (%u wide, format 0x%x, %ux%u at %u,%u) takes page %u from the target at page %u "
                          "(%u wide, family %d)", dbp, dbw, dpsm, w, h, dsax, dsay, page, t->basePage, t->fbw, t->family);
            told = true;
            addPage(others, page);
        }
    });
    // A page the rectangle writes whole needs nothing back: the transfer
    // replaces all of it, unless it ends early (reported then).
    if (dir == 0 && rectangular) {
        x.replaced = wholePages(others, dbp, dbw, dpsm, dsax, dsay, w, h);
        bool any = false;
        forEachPage(x.replaced, [&](uint32_t page) {
            others.bits[page >> 6] &= ~(uint64_t(1) << (page & 63));
            g.pages[page].vramValid = true;   // what the transfer writes is the page
            any = true;
        });
        if (any) {   // what the card has queued for those targets goes first, as before a read back
            flushOps();
            endRendering();
            drop(x.replaced);
        }
        forEachPage(others, [&](uint32_t page) {
            mirrorPage(page);
            g.pages[page].vramValid = false;   // the merge changes the mirror, not the CPU's copy
            addPage(x.merged, page);
        });
        drop(others);
    } else {
        download(others, "pages read back: written in another layout");
        drop(others);
    }
    // The CPU's pages it writes: the mirror's copies go out of date, except
    // where the transfer's pixels are merged into the mirror below.
    forEachPage(x.pages, [&](uint32_t page) {
        if (!g.pages[page].owner && !hasPage(x.merged, page)) g.pages[page].mirrorValid = false;
    });
    ++g.ownershipEpoch;
}

// Both sides of a local-to-local copy are targets of its layout: the card copies them at scale.
bool copyOnCard(uint32_t sbp, uint32_t sbw, uint32_t spsm, uint32_t ssax, uint32_t ssay, uint32_t dbp, uint32_t dbw, uint32_t dpsm,
                uint32_t dsax, uint32_t dsay, uint32_t w, uint32_t h) {
    if (spsm != dpsm) return false;
    switch (spsm) {
    case PSMCT32: case PSMCT24: case PSMCT16: case PSMCT16S: case PSMZ32: case PSMZ24: case PSMZ16: case PSMZ16S: break;
    default: return false;
    }
    const int family = bufferFamily(spsm);
    if (ssax + w > std::min(2048u, std::max(sbw, 1u) * 64) || ssay + h > 2048 || dsax + w > std::min(2048u, std::max(dbw, 1u) * 64) ||
        dsay + h > 2048)
        return false;
    Pages src, dst;
    src.add(footprint(sbp, sbw, spsm, ssax + w, ssay + h));
    dst.add(footprint(dbp, dbw, dpsm, dsax + w, dsay + h));
    if (everyPage(src) || everyPage(dst)) return false;
    auto soleOwner = [](const Pages& pages) -> Target* {
        uint16_t id = 0;
        bool one = true;
        forEachPage(pages, [&](uint32_t page) {
            const uint16_t o = g.pages[page].owner;
            if (!o || (id && o != id)) one = false;
            id = o;
        });
        return one && id ? targetById(id) : nullptr;
    };
    Target* s = soleOwner(src);
    Target* d = soleOwner(dst);
    uint32_t sx = 0, sy = 0, dx = 0, dy = 0;
    if (!s || !d || !sameLayout(*s, sbp, sbw, family, sx, sy) || !sameLayout(*d, dbp, dbw, family, dx, dy)) return false;
    flushOps();
    endRendering();
    VkCommandBuffer cmd = commandBuffer();
    fullBarrier(cmd);
    const auto n = static_cast<uint32_t>(g.scale);
    uint32_t from = s->slot;
    if (s == d) {
        // Overlapping rectangles read the image as it was: from a copy of it.
        const bool overlaps = sx + ssax < dx + dsax + w && dx + dsax < sx + ssax + w && sy + ssay < dy + dsay + h && dy + dsay < sy + ssay + h;
        if (overlaps) {
            Image copy;
            if (!makeStorageImage(copy, VK_FORMAT_R32_UINT, s->image.width, s->image.height, "copy source")) return false;
            const uint32_t slot = takeSlot(copy);
            if (slot == UINT32_MAX) {
                destroy(copy);
                return false;
            }
            const VkImageCopy region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0},
                                     {s->image.width, s->image.height, 1}};
            vkCmdCopyImage(cmd, s->image.handle, VK_IMAGE_LAYOUT_GENERAL, copy.handle, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            fullBarrier(cmd);
            retire(copy, slot);
            from = slot;
            // Copying in raster order, the console reads pixels it has just
            // written where the destination comes after the source.
            const uint32_t fromY = sy + ssay, toY = dy + dsay;
            if (toY > fromY || (toY == fromY && dx + dsax > sx + ssax))
                PS2_FALLBACK("gpu", "transfer reads what it writes",
                             "a local-to-local transfer copies a rectangle over itself, down or right: the card copies it from a copy "
                             "of the source, where the console reads the pixels it has just written");
        }
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.copy);
    const uint32_t push[10] = {n, from, d->slot, (sx + ssax) * n, (sy + ssay) * n, (dx + dsax) * n, (dy + dsay) * n, w * n, h * n,
                               spsm == PSMCT24 || spsm == PSMZ24 ? 1u : 0u};
    pushConstants(cmd, push, 10);
    vkCmdDispatch(cmd, (w * n + 7) / 8, (h * n + 7) / 8, 1);
    fullBarrier(cmd);
    forEachPage(dst, [&](uint32_t page) {
        g.pages[page].vramValid = false;
        g.pages[page].mirrorValid = false;
    });
    ++g.ownershipEpoch;
    return true;
}

} // namespace

// ---- Shared helpers ------------------------------------------------------------------------------------

VkDevice dev() { return device().handle; }
uint32_t slotOfSubmission() { return static_cast<uint32_t>(g.cmds.submitted() % kSlots); }
uint32_t* uploadWords() { return static_cast<uint32_t*>(g.upload.mapped); }

int bufferFamily(uint32_t psm) {
    switch (psm) {
    case PSMCT32: case PSMCT24: return 0;
    case PSMCT16: return 1;
    case PSMCT16S: return 2;
    case PSMZ32: case PSMZ24: return 3;
    case PSMZ16: return 4;
    case PSMZ16S: return 5;
    default: return -1;
    }
}

uint32_t uploadAlloc(uint32_t words) {
    const uint32_t at = uploadBase() + g.uploadUsed;
    g.uploadUsed += (words + 3) & ~3u;
    return at;
}

void reserve(uint32_t words) {
    // Queued page uploads take their room when they are recorded.
    const auto queued = static_cast<uint32_t>(g.uploadPagesQueued.size()) * kPageWords;
    if (g.uploadUsed + queued + words + 64 <= kUploadWords) return;
    submit();
}

VkCommandBuffer commandBuffer() {
    const bool fresh = !g.cmds.recording();
    VkCommandBuffer cmd = g.cmds.current();
    if (fresh) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1, &g.set, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.layout, 0, 1, &g.set, 0, nullptr);
        g.pipelineBound = -1;
    }
    return cmd;
}

void fullBarrier(VkCommandBuffer cmd) {
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

void pushConstants(VkCommandBuffer cmd, const uint32_t* words, uint32_t count) {
    vkCmdPushConstants(cmd, g.layout, VK_SHADER_STAGE_ALL, 0, count * 4, words);
}

void endRendering() {
    if (!g.rendering) return;
    VkCommandBuffer cmd = commandBuffer();
    if (g.runCount) {
        vkCmdDraw(cmd, 6, g.runCount, 0, g.runFirst);
        g.runFirst += g.runCount;
        g.runCount = 0;
    }
    vkCmdEndRendering(cmd);
    g.rendering = false;
    g.pipelineBound = -1;
}

void flushOps() {
    if (g.uploadPagesQueued.empty() && g.resolveEntries.empty() && g.expandEntries.empty() && g.pushEntries.empty() && g.mergeEntries.empty() &&
        g.snapshots.empty())
        return;
    endRendering();
    VkCommandBuffer cmd = commandBuffer();
    fullBarrier(cmd);
    if (!g.uploadPagesQueued.empty()) {
        std::vector<VkBufferCopy> regions;
        for (uint32_t page : g.uploadPagesQueued) {
            const uint32_t at = uploadAlloc(kPageWords);   // reserve() kept room for it
            std::memcpy(uploadWords() + at, g_vram + size_t(page) * kPageWords, kPageWords * 4);
            regions.push_back({VkDeviceSize(at) * 4, VkDeviceSize(page) * kPageWords * 4, kPageWords * 4});
        }
        vkCmdCopyBuffer(cmd, g.upload.handle, g.mirror.handle, static_cast<uint32_t>(regions.size()), regions.data());
        fullBarrier(cmd);
    }
    const auto n = static_cast<uint32_t>(g.scale);
    // A transfer's pixels reach their targets before anything copies those
    // targets' pages to the mirror. A cross-layout transfer then merges into
    // the pages just resolved, before anything expands them into a new target.
    if (!g.pushEntries.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.push);
        for (const auto& [e, w, h] : g.pushEntries) {
            const uint32_t push[2] = {n, e};
            pushConstants(cmd, push, 2);
            vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
        }
        fullBarrier(cmd);
    }
    if (!g.resolveEntries.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.resolve);
        for (uint32_t e : g.resolveEntries) {
            const uint32_t push[2] = {n, e};
            pushConstants(cmd, push, 2);
            vkCmdDispatch(cmd, 1, 1, 1);
        }
        fullBarrier(cmd);
    }
    if (!g.mergeEntries.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.merge);
        for (const auto& [e, w, h] : g.mergeEntries) {
            const uint32_t push[1] = {e};
            pushConstants(cmd, push, 1);
            vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
            fullBarrier(cmd);   // transfers may overlap: keep their order
        }
    }
    if (!g.expandEntries.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.expand);
        for (uint32_t e : g.expandEntries) {
            const uint32_t push[2] = {n, e};
            pushConstants(cmd, push, 2);
            vkCmdDispatch(cmd, 8, 8, 1);
        }
        fullBarrier(cmd);
    }
    for (const auto& [from, to] : g.snapshots) {
        const VkImageCopy region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0},
                                 {from->width, from->height, 1}};
        vkCmdCopyImage(cmd, from->handle, VK_IMAGE_LAYOUT_GENERAL, to->handle, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    }
    if (!g.snapshots.empty()) fullBarrier(cmd);
    g.uploadPagesQueued.clear();
    g.resolveEntries.clear();
    g.expandEntries.clear();
    g.pushEntries.clear();
    g.mergeEntries.clear();
    g.snapshots.clear();
    g.drawnSinceCut = Pages{};
    g.sampledSinceCut = Pages{};
}

void submit() {
    flushOps();
    endRendering();
    g.cmds.submit();
    g.primCount = g.stateWords = g.uploadUsed = 0;
    g.runFirst = primBase();
    g.runCount = 0;
    g.drawnSinceCut = Pages{};
    g.sampledSinceCut = Pages{};
}

Target* targetById(uint16_t id) {
    for (auto& t : g.targets)
        if (t->id == id) return t.get();
    return nullptr;
}

void mirrorPage(uint32_t page) {
    PageState& p = g.pages[page];
    if (p.mirrorValid) return;
    if (p.owner == 0) {
        reserve(kPageWords);
        g.uploadPagesQueued.push_back(page);
    } else {
        g.resolveEntries.push_back(pageEntry(*targetById(p.owner), page));
    }
    p.mirrorValid = true;
    ++g.cleanEpoch;
}

void mirrorPages(const Pages& pages) { forEachPage(pages, mirrorPage); }

void download(const Pages& pages, const char* why) {
    std::vector<uint32_t> wanted;
    forEachPage(pages, [&](uint32_t page) {
        if (!g.pages[page].vramValid) wanted.push_back(page);
    });
    if (wanted.empty()) return;
    for (uint32_t page : wanted) mirrorPage(page);
    flushOps();
    endRendering();
    VkCommandBuffer cmd = commandBuffer();
    fullBarrier(cmd);
    std::vector<VkBufferCopy> regions;
    for (size_t i = 0; i < wanted.size(); ++i)
        regions.push_back({VkDeviceSize(wanted[i]) * kPageWords * 4, VkDeviceSize(i) * kPageWords * 4, kPageWords * 4});
    vkCmdCopyBuffer(cmd, g.mirror.handle, g.readback.handle, static_cast<uint32_t>(regions.size()), regions.data());
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                  VK_ACCESS_2_HOST_READ_BIT);
    submit();
    g.cmds.wait(g.cmds.submitted());
    invalidate(g.readback);
    for (size_t i = 0; i < wanted.size(); ++i) {
        std::memcpy(g_vram + size_t(wanted[i]) * kPageWords, static_cast<const uint32_t*>(g.readback.mapped) + i * kPageWords,
                    kPageWords * 4);
        g.pages[wanted[i]].vramValid = true;
    }
    ++g.cleanEpoch;
    PS2_FALLBACK("gpu", why, "%s: %zu page(s) of GS memory from the graphics card, which waits for it to finish (the first: page %u)", why,
                 wanted.size(), wanted[0]);
}

void flushTransfer() {
    PendingTransfer& x = g.transfer;
    if (!x.active) return;
    const uint32_t total = x.w * x.h;
    const uint32_t upTo = x.dir == 0 && g_xfer.active ? std::min(total, g_xfer.y * x.w + g_xfer.x) : total;
    if (upTo <= x.pushed) return;
    // The CPU's pages it wrote: the mirror's copies are out of date, except
    // where this transfer's pixels are merged into the mirror below.
    forEachPage(x.pages, [&](uint32_t page) {
        if (!g.pages[page].owner && !hasPage(x.merged, page)) g.pages[page].mirrorValid = false;
    });
    const bool merges = std::any_of(x.merged.bits.begin(), x.merged.bits.end(), [](uint64_t b) { return b != 0; });
    if (!x.targets.empty() || merges) {
        // The pixels sent since the last flush, and no others (the rest of a
        // row is not in GS memory yet): the end of a row begun, whole rows,
        // and the start of a row not finished, as rectangles of the transfer's.
        struct Span {
            uint32_t col, row, w, h;
        };
        std::vector<Span> spans;
        uint32_t from = x.pushed;
        if (from % x.w) {
            const uint32_t end = std::min(upTo, (from / x.w + 1) * x.w);
            spans.push_back({from % x.w, from / x.w, end - from, 1});
            from = end;
        }
        if (upTo - from >= x.w) {
            const uint32_t rows = (upTo - from) / x.w;
            spans.push_back({0, from / x.w, x.w, rows});
            from += rows * x.w;
        }
        if (upTo > from) spans.push_back({0, from / x.w, upTo - from, 1});
        const int family = transferFamily(x.dpsm);
        for (const Span& sp : spans) {
            reserve(sp.w * sp.h + static_cast<uint32_t>(x.targets.size()) * (PUSH_WORDS + 4) + (merges ? MERGE_WORDS : 0) + 64);
            const uint32_t src = uploadAlloc(sp.w * sp.h);
            uint32_t* units = uploadWords() + src;
            for (uint32_t r = 0; r < sp.h; ++r)
                for (uint32_t c = 0; c < sp.w; ++c)
                    units[r * sp.w + c] = readRaw(x.dbp, x.dbw, x.dpsm, (x.dsax + sp.col + c) & 2047, (x.dsay + sp.row + r) & 2047);
            for (uint16_t id : x.targets) {
                Target* t = targetById(id);
                uint32_t ox = 0, oy = 0;
                if (!t || !sameLayout(*t, x.dbp, x.dbw, family, ox, oy)) continue;
                const uint32_t e = uploadAlloc(PUSH_WORDS);
                uint32_t* w = uploadWords() + e;
                w[PUSH_IMAGE] = t->slot;
                w[PUSH_X] = x.dsax + sp.col;   // push.comp wraps it at 2048
                w[PUSH_Y] = (x.dsay + sp.row) & 2047;
                w[PUSH_W] = sp.w;
                w[PUSH_H] = sp.h;
                w[PUSH_SRC] = src;
                w[PUSH_UNIT] = pushUnitOf(x.dpsm);
                w[PUSH_BASEPAGE] = t->basePage;
                w[PUSH_FBW] = t->fbw;
                w[PUSH_PAGEH] = t->pageH;
                for (uint32_t i = 0; i < 16; ++i) w[PUSH_MASK + i] = 0;
                forEachPage(x.pages, [&](uint32_t page) {
                    if (g.pages[page].owner == id) {
                        w[PUSH_MASK + (page >> 5)] |= 1u << (page & 31);
                        g.pages[page].mirrorValid = false;
                    }
                });
                w[PUSH_ORIGIN] = ox;
                w[PUSH_ORIGINY] = oy;
                g.pushEntries.push_back({e, sp.w, sp.h});
            }
            if (merges) {
                const uint32_t e = uploadAlloc(MERGE_WORDS);
                uint32_t* m = uploadWords() + e;
                m[MERGE_X] = x.dsax + sp.col;   // merge.comp wraps both coordinates at 2048
                m[MERGE_Y] = x.dsay + sp.row;
                m[MERGE_W] = sp.w;
                m[MERGE_H] = sp.h;
                m[MERGE_SRC] = src;
                m[MERGE_DBP] = x.dbp;
                m[MERGE_DBW] = x.dbw;
                m[MERGE_PSM] = x.dpsm;
                for (uint32_t i = 0; i < 16; ++i) m[MERGE_MASK + i] = static_cast<uint32_t>(x.merged.bits[i / 2] >> (32 * (i % 2)));
                g.mergeEntries.push_back({e, sp.w, sp.h});
            }
        }
    }
    x.pushed = upTo;
    ++g.ownershipEpoch;   // states that read these pages prepare again
}

uint32_t takeSlot(const Image& image) {
    if (g.freeSlots.empty()) return UINT32_MAX;
    const uint32_t slot = g.freeSlots.back();
    g.freeSlots.pop_back();
    const VkDescriptorImageInfo info{VK_NULL_HANDLE, image.view, VK_IMAGE_LAYOUT_GENERAL};
    auto write = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = g.set;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(dev(), 1, &write, 0, nullptr);
    return slot;
}

void retire(Image image, uint32_t slot) {
    g.cmds.defer([image, slot]() mutable {
        destroy(image);
        if (slot != UINT32_MAX) g.freeSlots.push_back(slot);
    });
}

void writePictureBinding(uint32_t index, VkImageView view) {
    if (g.pictureViews[index] == view) return;
    g.pictureViews[index] = view;
    const VkDescriptorImageInfo info{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    auto write = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = g.set;
    write.dstBinding = 6;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(dev(), 1, &write, 0, nullptr);
}

bool makeStorageImage(Image& image, VkFormat format, uint32_t w, uint32_t h, const char* name) {
    if (!createImage(image, format, w, h, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     name))
        return false;
    endRendering();
    imageBarrier(commandBuffer(), image.handle, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_IMAGE_LAYOUT_GENERAL);
    return true;
}

void freeAllTargets() {
    for (auto& t : g.targets)
        if (t->image.handle) retire(t->image, t->slot);
    g.targets.clear();
    for (size_t i = 0; i < g.scratch.size(); ++i) retire(g.scratch[i], g.scratchSlots[i]);
    g.scratch.clear();
    g.scratchSlots.clear();
    for (PageState& p : g.pages) p = PageState{};
    g.state = StateInfo{};
    g.transfer = PendingTransfer{};
    ++g.ownershipEpoch;
    ++g.cleanEpoch;
}

// ---- Drawing ------------------------------------------------------------------------------------------

namespace {

void decline(StateInfo& s, const char* key, const char* why) {
    s.declined = true;
    PS2_FALLBACK("gpu", key, "%s: the Vulkan renderer leaves these primitives out", why);
}

// A copy of target t that primitives read while they draw into t: one per size.
uint32_t snapshotOf(Target& t) {
    for (size_t i = 0; i < g.scratch.size(); ++i)
        if (g.scratch[i].width == t.image.width && g.scratch[i].height == t.image.height) {
            g.snapshots.push_back({&t.image, &g.scratch[i]});
            return g.scratchSlots[i];
        }
    Image copy;
    if (!makeStorageImage(copy, VK_FORMAT_R32_UINT, t.image.width, t.image.height, "snapshot")) return UINT32_MAX;
    const uint32_t slot = takeSlot(copy);
    if (slot == UINT32_MAX) {
        destroy(copy);
        return UINT32_MAX;
    }
    g.scratch.push_back(copy);
    g.scratchSlots.push_back(slot);
    g.snapshots.push_back({&t.image, &g.scratch.back()});
    return slot;
}

// Its targets own the pages it draws into, its textures and CLUT are where
// the shaders read them, and what the card must do first is queued.
void prepare(const Draw& d, StateInfo& s) {
    const Env& e = d.e;
    s.access = accessOf(d);
    s.writes = Pages{};
    s.targetReads = Pages{};
    s.selfRead = false;
    // Buffers reaching round the end of GS memory, or past what the card's
    // images hold: the software rasterizer draws the primitives that reach
    // into the scissor (draw() reports them; games park the scissor past the
    // end of memory to draw nothing).
    const auto toCpu = [&s] {
        s.declined = true;
        s.toCpu = true;
    };
    if (everyPage(s.access.frame) || (d.zte && everyPage(s.access.depth))) return toCpu();
    const int frameFamily = bufferFamily(e.fpsm);
    if (frameFamily < 0) {
        decline(s, "frame format", "the frame buffer's format is not one the GS draws in");
        return;
    }
    s.frame = targetFor(e.fbp / 32, e.fbw, frameFamily);
    if (!acquire(*s.frame, s.access.frame)) return toCpu();
    s.writes.add(s.access.frame);
    s.noDepth = d.zte && s.access.frame.meets(s.access.depth);
    // Without a depth test that reads or writes depth, leaving it out changes nothing.
    if (s.noDepth && d.ztst != 0 && (d.ztst >= 2 || !e.zmsk))
        PS2_UNIMPL("gpu", "frame and depth overlap", "the depth buffer is in the frame buffer's memory: the Vulkan renderer draws these "
                   "primitives without their depth test (FRAME 0x%x, ZBUF 0x%x)", e.fbp, e.zbp);
    s.depth = nullptr;
    if (d.zte && !s.noDepth) {
        const int depthFamily = bufferFamily(e.zpsm);
        if (depthFamily < 0) {
            decline(s, "depth format", "the depth buffer's format is not one the GS has");
            return;
        }
        s.depth = targetFor(e.zbp / 32, e.fbw, depthFamily);
        if (!acquire(*s.depth, s.access.depth)) return toCpu();
        s.writes.add(s.access.depth);
    }
    s.anyFormat = false;
    s.clutPages = Pages{};
    if (d.tex) {
        const int family = textureFamily(d.tpsm);
        const bool clut = d.tpsm == PSMT8 || d.tpsm == PSMT4 || d.tpsm == PSMT8H || d.tpsm == PSMT4HL || d.tpsm == PSMT4HH;
        s.anyFormat = family < 0 && !clut;
        if (s.anyFormat)
            PS2_UNIMPL("gs", "texture format", "texture format 0x%x is not supported", d.tpsm);
        for (int n = d.firstLevel; n <= d.lastLevel && !s.anyFormat; ++n) {
            const Level& l = d.level[n];
            const auto tw = static_cast<uint32_t>(d.wms >= 2 ? 1024 >> n : l.tw);
            const auto th = static_cast<uint32_t>(d.wmt >= 2 ? 1024 >> n : l.th);
            Pages pages;
            pages.add(footprint(l.tbp, l.tbw, d.tpsm, tw, th));
            s.levelPages[size_t(n)] = pages;
            LevelSource& src = s.levels[size_t(n)];
            src = LevelSource{};
            // The first level of a target's own layout is read from it, at its
            // scale, in the pages it holds; GS memory (the mirror) has the
            // rest, as a texture declared larger than the picture the target
            // drew reaches into the buffer after it.
            const uint32_t first = l.tbp / 32;
            Target* t = n == 0 && family >= 0 && l.tbp % 32 == 0 && first < kPages && g.pages[first].owner ? targetById(g.pages[first].owner)
                                                                                                            : nullptr;
            uint32_t ox = 0, oy = 0;
            if (t && sameLayout(*t, l.tbp, l.tbw, family, ox, oy) && !everyPage(pages)) {
                src.target = true;
                src.from = t;
                src.xOff = ox;
                src.yOff = oy;
                src.snapshot = t == s.frame || t == s.depth;
                Pages rest;
                forEachPage(pages, [&](uint32_t page) { addPage(g.pages[page].owner == t->id ? src.held : rest, page); });
                if (src.snapshot) s.selfRead = true;
                else s.targetReads.add(src.held);
                mirrorPages(rest);
                if (rest.meets(s.writes)) s.selfRead = true;
            } else {
                mirrorPages(pages);
                if (pages.meets(s.writes)) s.selfRead = true;
            }
        }
        if (clut && !s.anyFormat) {
            s.clutPages.add(clutSpan(d));
            mirrorPages(s.clutPages);
            if (s.clutPages.meets(s.writes)) s.selfRead = true;
        }
    }
    s.epoch = g.ownershipEpoch;
    s.dirtyEpoch = ~uint64_t(0);
}

// The state's record in this submission's states.
void record(const Draw& d, StateInfo& s) {
    if (s.recordSubmission == g.cmds.submitted() && s.recordEpoch == s.epoch && !s.selfRead) return;
    if (g.stateWords + STATE_WORDS > kMaxStates * STATE_WORDS) submit();
    const Env& e = d.e;
    s.recordWord = stateBase() + g.stateWords;
    g.stateWords += STATE_WORDS;
    s.recordSubmission = g.cmds.submitted();
    s.recordEpoch = s.epoch;
    uint32_t* w = static_cast<uint32_t*>(g.states.mapped) + s.recordWord;
    std::memset(w, 0, STATE_WORDS * 4);
    uint32_t flags = 0;
    auto set = [&](bool on, uint32_t bit) {
        if (on) flags |= bit;
    };
    set(d.tex, F_TEX);
    set(d.fst, F_FST);
    set(d.gouraud, F_GOURAUD);
    set(d.zte, F_ZTE);
    set(e.zmsk, F_ZMSK);
    set(d.ate, F_ATE);
    set(d.fge, F_FGE);
    set(d.abe, F_ABE);
    set(d.pabe, F_PABE);
    set(d.colclamp, F_COLCLAMP);
    set(e.fba, F_FBA);
    set(d.fb16, F_FB16);
    set(d.tcc, F_TCC);
    set(d.lodByQ, F_LODBYQ);
    set(d.mipLinear, F_MIPLINEAR);
    set(d.magLinear, F_MAGLINEAR);
    set(d.minLinear, F_MINLINEAR);
    set(d.linear, F_LINEAR);
    set(d.clutWide, F_CLUTWIDE);
    set(d.aem, F_AEM);
    set(s.anyFormat, F_ANYFORMAT);
    set(s.noDepth, F_NODEPTH);
    w[S_FLAGS] = flags;
    w[S_FRAME] = s.frame->slot;
    w[S_DEPTH] = s.depth ? s.depth->slot : s.frame->slot;
    w[S_FPSM] = e.fpsm;
    w[S_ZPSM] = e.zpsm;
    w[S_FBMSK] = e.fbmsk;
    w[S_FBMSK16] = d.fbmsk16;
    w[S_TPSM] = d.tpsm;
    w[S_TFX] = d.tfx;
    w[S_WMS] = d.wms;
    w[S_WMT] = d.wmt;
    w[S_TWF] = std::bit_cast<uint32_t>(d.twf);
    w[S_THF] = std::bit_cast<uint32_t>(d.thf);
    w[S_LODK] = static_cast<uint32_t>(d.lodK);
    w[S_LODL] = static_cast<uint32_t>(d.lodL);
    w[S_MAGFROM] = d.magnifiedFrom;
    w[S_MIPLEVELS] = static_cast<uint32_t>(d.mipLevels);
    w[S_TA0] = d.ta0;
    w[S_TA1] = d.ta1;
    w[S_CLUTBASE] = d.clutBase;
    // Where the texel's CLUT offsets start, as an index into the tables.
    const uint16_t* t8 = &g_clutOffsets.t8[0][0];
    const uint16_t* t4 = &g_clutOffsets.t4[0][0];
    if (d.clutOffsets >= t8 && d.clutOffsets < t8 + 3 * 256) w[S_CLUTTABLE] = T_CLUT8 + static_cast<uint32_t>(d.clutOffsets - t8);
    else if (d.clutOffsets >= t4 && d.clutOffsets < t4 + 3 * 512) w[S_CLUTTABLE] = T_CLUT4 + static_cast<uint32_t>(d.clutOffsets - t4);
    else w[S_CLUTTABLE] = T_CLUT8;
    w[S_ATST] = d.atst;
    w[S_AREF] = d.aref;
    w[S_AFAIL] = d.afail;
    w[S_ZTST] = d.ztst;
    w[S_ZMAX] = d.zmax;
    w[S_FOGR] = d.fogr;
    w[S_FOGG] = d.fogg;
    w[S_FOGB] = d.fogb;
    w[S_BLEND] = static_cast<uint32_t>(d.ba | d.bb << 2 | d.bc << 4 | d.bd << 6);
    w[S_FIX] = d.fix;
    w[S_SCX0] = static_cast<uint32_t>(e.scx0);
    w[S_SCX1] = static_cast<uint32_t>(e.scx1);
    w[S_SCY0] = static_cast<uint32_t>(e.scy0);
    w[S_SCY1] = static_cast<uint32_t>(e.scy1);
    w[S_FRAMEW] = s.frame->image.width;
    w[S_FRAMEH] = s.frame->image.height;
    w[S_FBW] = s.frame->fbw;
    w[S_DEPTHW] = s.depth ? s.depth->image.width : 0;
    w[S_DEPTHH] = s.depth ? s.depth->image.height : 0;
    if (d.tex && !s.anyFormat) {
        for (int n = d.firstLevel; n <= d.lastLevel; ++n) {
            const Level& l = d.level[n];
            const LevelSource& src = s.levels[size_t(n)];
            uint32_t* lw = w + S_LEVELS + n * L_WORDS;
            if (src.target) {
                lw[L_SOURCE] = L_FROM_TARGET | (src.snapshot ? snapshotOf(*src.from) : src.from->slot);
                lw[L_BASE] = src.xOff;
                lw[L_ROW] = src.yOff;
                lw[L_SCALE] = static_cast<uint32_t>(g.scale);
                w[S_SOURCEFBW] = src.from->fbw;
                w[S_SOURCEBASE] = src.from->basePage;
                w[S_SOURCEUNIT] = l.base;
                w[S_SOURCEROW] = l.rowOfPages;
                for (uint32_t i = 0; i < 16; ++i) w[S_SOURCEPAGES + i] = static_cast<uint32_t>(src.held.bits[i / 2] >> (32 * (i % 2)));
            } else {
                lw[L_SOURCE] = 0;
                lw[L_BASE] = l.base;
                lw[L_ROW] = l.rowOfPages;
                lw[L_SCALE] = 1;
            }
            lw[L_TW] = static_cast<uint32_t>(l.tw);
            lw[L_TH] = static_cast<uint32_t>(l.th);
            lw[L_MINU] = static_cast<uint32_t>(l.minu);
            lw[L_MAXU] = static_cast<uint32_t>(l.maxu);
            lw[L_MINV] = static_cast<uint32_t>(l.minv);
            lw[L_MAXV] = static_cast<uint32_t>(l.maxv);
        }
    }
}

void putVertex(uint32_t* p, int i, const Vertex& v) {
    uint32_t* o = p + P_V0 + i * V_WORDS;
    o[V_X] = std::bit_cast<uint32_t>(v.x);
    o[V_Y] = std::bit_cast<uint32_t>(v.y);
    o[V_Z] = v.z;
    o[V_R] = std::bit_cast<uint32_t>(v.r);
    o[V_G] = std::bit_cast<uint32_t>(v.g);
    o[V_B] = std::bit_cast<uint32_t>(v.b);
    o[V_A] = std::bit_cast<uint32_t>(v.a);
    o[V_S] = std::bit_cast<uint32_t>(v.s);
    o[V_T] = std::bit_cast<uint32_t>(v.t);
    o[V_Q] = std::bit_cast<uint32_t>(v.q);
    o[V_U] = std::bit_cast<uint32_t>(v.u);
    o[V_V] = std::bit_cast<uint32_t>(v.v);
    o[V_FOG] = std::bit_cast<uint32_t>(v.fog);
}

// A new primitive record in this submission, drawn with the current run.
uint32_t* newPrim(StateInfo& s, uint32_t kind, int pipeline) {
    if (g.primCount == kMaxPrims) {
        submit();
        g.state.recordSubmission = ~uint64_t(0);
    }
    if (s.recordSubmission != g.cmds.submitted()) return nullptr;   // the caller records the state again
    beginRendering(pipeline);
    const uint32_t index = primBase() + g.primCount++;
    if (!g.runCount) g.runFirst = index;
    ++g.runCount;
    uint32_t* p = static_cast<uint32_t*>(g.prims.mapped) + size_t(index) * PRIM_WORDS;
    p[P_KIND] = kind;
    p[P_STATE] = s.recordWord;
    return p;
}

// The rightmost window x a primitive's vertices reach.
float rightmost(Shape shape, const Vertex* v) {
    const int count = shape == Shape::Triangle ? 3 : shape == Shape::Point ? 1 : 2;
    float x = v[0].x;
    for (int i = 1; i < count; ++i) x = std::max(x, v[i].x);
    return x;
}

// Whether a primitive's bounding box meets the scissor: whether it can draw
// at all.
bool meetsScissor(const Env& e, Shape shape, const Vertex* v) {
    const int count = shape == Shape::Triangle ? 3 : shape == Shape::Point ? 1 : 2;
    float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
    for (int i = 1; i < count; ++i) {
        x0 = std::min(x0, v[i].x);
        x1 = std::max(x1, v[i].x);
        y0 = std::min(y0, v[i].y);
        y1 = std::max(y1, v[i].y);
    }
    return x1 >= static_cast<float>(e.scx0) && x0 < static_cast<float>(e.scx1 + 1) && y1 >= static_cast<float>(e.scy0) &&
           y0 < static_cast<float>(e.scy1 + 1);
}

// gs.cpp's texCoord, on the CPU as there (under the guest's rounding).
void texCoord(const Draw& d, float s, float t, float q, float u, float v, float& outU, float& outV) {
    if (d.fst) {
        outU = u;
        outV = v;
        return;
    }
    const float iq = q != 0 ? 1.0f / q : 1.0f;
    outU = s * iq * d.twf;
    outV = t * iq * d.thf;
}

} // namespace

bool draw(const Draw& d, uint64_t gen, Shape shape, const Vertex* v) {
    if (!g.started) return true;
    if (g.transfer.active) flushTransfer();
    const int rounding = std::fegetround();
    StateInfo& s = g.state;
    if (gen != s.gen || rounding != s.rounding) {
        s = StateInfo{};
        s.gen = gen;
        s.rounding = rounding;
        s.epoch = 0;
    }
    if (s.epoch != g.ownershipEpoch || s.selfRead)
        if (!s.declined) prepare(d, s);
    // A state whose buffers the card cannot hold: a primitive that reaches
    // into its scissor goes to the software rasterizer (one outside it draws
    // nothing, on the console either); the others are left out, reported.
    if (s.declined) {
        if (!s.toCpu || !meetsScissor(d.e, shape, v)) return true;
        PS2_FALLBACK("gpu", "drawn by the software rasterizer",
                     "a primitive whose frame or depth buffer goes round the end of GS memory, or past what the card's images hold, "
                     "is drawn by the software rasterizer in GS memory (FRAME at block 0x%x, %u wide; scissor %d-%d x %d-%d)",
                     d.e.fbp, d.e.fbw, d.e.scx0, d.e.scx1, d.e.scy0, d.e.scy1);
        return false;
    }
    if (s.selfRead)
        PS2_FALLBACK("gpu", "primitive reads what it writes",
                     "a primitive reads memory it draws into: the card reads it as it was before the primitive, the console as "
                     "the primitive changes it");
    // What it draws into is no longer what GS memory or the mirror hold.
    if (s.dirtyEpoch != g.cleanEpoch) {
        forEachPage(s.writes, [](uint32_t page) {
            g.pages[page].vramValid = false;
            g.pages[page].mirrorValid = false;
        });
        s.dirtyEpoch = g.cleanEpoch;
    }
    const int pipeline = rounding == FE_TOWARDZERO ? 1 : 0;
    // A primitive drawing past the frame's width writes pixels that GS memory
    // keeps on the next row of pages, where the interlock does not order them
    // with other primitives' pixels: it comes between two barriers.
    const auto width = static_cast<int>(s.frame->fbw * 64);
    const bool spills = d.e.scx1 >= width && rightmost(shape, v) >= static_cast<float>(width);
    // Each primitive: its state's record, the GS memory the card must move
    // first (and a snapshot it reads), a barrier where it reads what earlier
    // draws wrote or writes what they read, then its own record.
    auto prim = [&](uint32_t kind) -> uint32_t* {
        for (int tries = 0; tries < 2; ++tries) {
            record(d, s);
            flushOps();
            if (spills || g.spilled || s.targetReads.meets(g.drawnSinceCut) || s.writes.meets(g.sampledSinceCut)) cut();
            if (uint32_t* p = newPrim(s, kind, pipeline)) {
                g.drawnSinceCut.add(s.writes);
                g.sampledSinceCut.add(s.targetReads);
                g.spilled = spills;
                return p;
            }
        }
        return nullptr;
    };
    switch (shape) {
    case Shape::Triangle: {
        const float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (area == 0) return true;
        uint32_t* p = prim(KIND_TRIANGLE);
        if (!p) return true;
        for (int i = 0; i < 3; ++i) putVertex(p, i, v[i]);
        p[P_AREA] = std::bit_cast<uint32_t>(area);
        break;
    }
    case Shape::Sprite: {
        uint32_t* p = prim(KIND_SPRITE);
        if (!p) return true;
        putVertex(p, 0, v[0]);
        putVertex(p, 1, v[1]);
        float ua = 0, va = 0, ub = 0, vb = 0;
        texCoord(d, v[0].s, v[0].t, v[0].q, v[0].u, v[0].v, ua, va);
        texCoord(d, v[1].s, v[1].t, v[1].q, v[1].u, v[1].v, ub, vb);
        if (d.tex && g.scale > 1 && gs::roundSprite() == 2 && (d.fst || v[0].q == v[1].q)) {
            auto edge = [](float a, float& b, float span) {
                if (std::abs(b - a) > 1.0f && std::abs(b - a) <= std::abs(span) + 2.0f) b += b > a ? -1.0f : 1.0f;
            };
            edge(ua, ub, v[1].x - v[0].x);
            edge(va, vb, v[1].y - v[0].y);
        }
        p[P_UA] = std::bit_cast<uint32_t>(ua);
        p[P_VA] = std::bit_cast<uint32_t>(va);
        p[P_UB] = std::bit_cast<uint32_t>(ub);
        p[P_VB] = std::bit_cast<uint32_t>(vb);
        break;
    }
    case Shape::Point: {
        uint32_t* p = prim(KIND_PIXEL);
        if (!p) return true;
        Vertex at = v[0];
        at.x = static_cast<float>(static_cast<int>(v[0].x));
        at.y = static_cast<float>(static_cast<int>(v[0].y));
        putVertex(p, 0, at);
        break;
    }
    case Shape::Line: {
        // gs.cpp's drawLine: a pixel per step, each its own primitive.
        const Vertex& a = v[0];
        const Vertex& b = v[1];
        const int steps = static_cast<int>(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)));
        for (int i = 0; i <= steps; ++i) {
            const float f = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0;
            const Vertex& c = d.gouraud ? (f < 0.5f ? a : b) : b;
            Vertex at = b;
            at.x = static_cast<float>(static_cast<int>(a.x + (b.x - a.x) * f));
            at.y = static_cast<float>(static_cast<int>(a.y + (b.y - a.y) * f));
            at.r = c.r;
            at.g = c.g;
            at.b = c.b;
            at.a = c.a;
            uint32_t* p = prim(KIND_PIXEL);
            if (!p) return true;
            putVertex(p, 0, at);
        }
        break;
    }
    }
    return true;
}

void cpuDraws(const Draw& d) {
    if (!g.started) return;
    if (g.transfer.active) flushTransfer();
    const Access a = accessOf(d);
    download(a.reads, "pages read back: the software rasterizer draws a primitive");
    Pages writes = a.frame;
    writes.add(a.depth);
    release(writes, "pages read back: the software rasterizer draws a primitive");
}

void cpuDrew(const Draw& d) {
    if (!g.started) return;
    const Access a = accessOf(d);
    Pages writes = a.frame;
    writes.add(a.depth);
    forEachPage(writes, [](uint32_t page) { g.pages[page].mirrorValid = false; });
    ++g.cleanEpoch;
    ++g.ownershipEpoch;
}

bool transfer(uint32_t dir, uint64_t bb, uint64_t pos, uint64_t reg) {
    if (!g.started) return false;
    if (g.transfer.active) {   // the last host-to-local transfer stopped short of its rectangle
        bool any = false;
        forEachPage(g.transfer.replaced, [&](uint32_t) { any = true; });
        if (any)
            PS2_UNIMPL("gpu", "transfer ended early over drawn pages",
                       "a transfer to block 0x%x (%ux%u) stopped before its end: the rest of the pages it was to replace whole lost "
                       "what the card drew there",
                       g.transfer.dbp, g.transfer.w, g.transfer.h);
        transferEnded();
    }
    const auto field = [](uint64_t v, int lo, int bits) { return static_cast<uint32_t>((v >> lo) & ((uint64_t(1) << bits) - 1)); };
    const uint32_t dbp = field(bb, 32, 14), dbw = field(bb, 48, 6), dpsm = field(bb, 56, 6);
    const uint32_t dsax = field(pos, 32, 11), dsay = field(pos, 48, 11);
    const uint32_t w = field(reg, 0, 12), h = field(reg, 32, 12);
    if (!w || !h) return false;
    if (dir == 0) {
        beginDestination(0, dbp, dbw, dpsm, dsax, dsay, w, h);
        return false;
    }
    if (dir != 2) return false;
    const uint32_t sbp = field(bb, 0, 14), sbw = field(bb, 16, 6), spsm = field(bb, 24, 6);
    const uint32_t ssax = field(pos, 0, 11), ssay = field(pos, 16, 11);
    if (copyOnCard(sbp, sbw, spsm, ssax, ssay, dbp, dbw, dpsm, dsax, dsay, w, h)) return true;
    Pages source;
    source.add(footprint(sbp, sbw, spsm, ssax + w, ssay + h));
    download(source, "pages read back: a local copy reads them in another layout");   // gs.cpp's loop reads them from g_vram
    beginDestination(2, dbp, dbw, dpsm, dsax, dsay, w, h);
    return false;
}

void transferEnded() {
    if (!g.transfer.active) return;
    flushTransfer();
    g.transfer.active = false;
}

void syncPages(const Pages& pages) {
    if (!g.started) return;
    download(pages, "pages read back: the CPU reads GS memory");
}

void syncAll() {
    if (!g.started) return;
    if (g.transfer.active) flushTransfer();
    Pages all;
    all.bits.fill(~uint64_t(0));
    download(all, "pages read back: the CPU reads GS memory");
}

void reset() {
    if (!g.started) return;
    submit();
    freeAllTargets();
    releaseHistory();
}

void rescale(int scale) {
    if (!g.started) return;
    syncAll();   // GS memory holds every page: the targets go, and come again at the new scale as drawing needs them
    submit();
    freeAllTargets();
    releaseHistory();
    g.scale = std::clamp(scale, 1, 8);
}

bool active() { return g.started; }
bool cpuPageValid(uint32_t page) { return !g.started || g.pages[page].vramValid; }
int scale() { return g.scale; }

} // namespace ps2::gpu::renderer
