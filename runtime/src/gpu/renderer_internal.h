// The Vulkan renderer's state, shared by its files: renderer.cpp (pages,
// targets, transfers, drawing), renderer_setup.cpp (pipelines, buffers,
// start and stop) and renderer_display.cpp (the vsync's picture).
#pragma once

#include "gpu/renderer.h"
#include "gpu/resources.h"

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ps2::gpu::renderer {

constexpr uint32_t kSlots = 3;                 // submissions in flight, each with its share of the rings
constexpr uint32_t kMaxPrims = 1u << 16;       // primitives a submission holds
constexpr uint32_t kMaxStates = 1u << 12;      // drawing states a submission holds
constexpr uint32_t kUploadWords = 2u << 20;    // 8 MB of a submission's uploads and entries
constexpr uint32_t kImageSlots = 4096;         // binding 0
constexpr uint32_t kPictureBindings = 16;      // binding 6: 0-2 the pictures, 3-8 the field history
constexpr uint32_t kHistoryBinding = 3;
constexpr uint32_t kPages = 512;
constexpr uint32_t kPageWords = 2048;
constexpr uint32_t kControlWords = 64;

struct Target {
    uint16_t id = 0;
    uint32_t basePage = 0, fbw = 1;   // its first page, and pages across (at least 1)
    int family = 0;                   // as bufferFamily
    uint32_t pageH = 32;
    uint32_t rows = 0;                // page rows its image holds
    Image image;
    uint32_t slot = UINT32_MAX;       // binding 0
    uint32_t owned = 0;               // pages it owns
};

struct PageState {
    uint16_t owner = 0;               // a target's id; 0: the CPU (g_vram)
    bool vramValid = true;            // g_vram holds the page's content
    bool mirrorValid = false;         // the mirror does
};

// Where a texture level is read: a target at its scale (its first page at
// xOff, yOff of it), or GS memory through the mirror.
struct LevelSource {
    bool target = false;
    Target* from = nullptr;
    uint32_t xOff = 0, yOff = 0;
    bool snapshot = false;            // the target is the frame or depth buffer: read from a copy
    gs::detail::Pages held;           // the level's pages the target holds; the mirror has the rest
};

// The drawing state primitives are drawn with now, and what preparing it found.
struct StateInfo {
    uint64_t gen = ~uint64_t(0);
    int rounding = -1;
    bool declined = false;
    bool toCpu = false;               // declined, its buffers past what the card holds: the software rasterizer draws
    bool selfRead = false;            // reads memory it draws into: prepared again for each primitive
    bool noDepth = false;             // the depth buffer is the frame's memory
    bool anyFormat = false;           // a texture format the GS cannot read
    gs::detail::Access access;
    Target* frame = nullptr;
    Target* depth = nullptr;
    std::array<LevelSource, 7> levels{};
    std::array<gs::detail::Pages, 7> levelPages{};
    gs::detail::Pages clutPages;
    gs::detail::Pages writes;         // frame and depth pages
    gs::detail::Pages targetReads;    // pages read from targets at scale
    uint64_t epoch = 0;               // the ownership it was prepared under
    uint64_t dirtyEpoch = ~uint64_t(0);
    uint32_t recordWord = 0;          // its record in this submission's states
    uint64_t recordSubmission = ~uint64_t(0);
    uint64_t recordEpoch = ~uint64_t(0);
};

// A transfer whose rectangle reaches targets as it is written.
struct PendingTransfer {
    bool active = false;
    uint32_t dir = 0;
    uint32_t dbp = 0, dbw = 0, dpsm = 0, dsax = 0, dsay = 0, w = 0, h = 0;
    uint32_t pushed = 0;              // pixels sent to the targets so far
    std::vector<uint16_t> targets;    // targets of its layout it writes into
    gs::detail::Pages pages;          // the pages it writes
};

// The field history at scale (renderer_display.cpp): 4 fields, the free
// image the next field goes into, and the last picture.
struct History {
    uint32_t width = 0, lines = 0, height = 0;
    std::array<Image, 6> images;
    bool reset = true;
};

struct Renderer {
    bool started = false;
    int scale = 1;
    Commands cmds;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> drawPipelines{};   // [the guest rounds toward zero]
    VkPipeline resolve = VK_NULL_HANDLE, expand = VK_NULL_HANDLE, push = VK_NULL_HANDLE, copy = VK_NULL_HANDLE;
    VkPipeline display = VK_NULL_HANDLE, deinterlace = VK_NULL_HANDLE;
    Buffer mirror, tables, states, prims, upload, control, readback;
    float viewHalf = 16384.0f;
    uint32_t renderSize = 32768;

    std::array<PageState, kPages> pages{};
    std::vector<std::unique_ptr<Target>> targets;
    uint16_t nextId = 1;
    std::vector<uint32_t> freeSlots;
    std::array<VkImageView, kPictureBindings> pictureViews{};
    uint64_t ownershipEpoch = 1;      // moves when a page changes owner or a target its image
    uint64_t cleanEpoch = 1;          // moves when pages become valid in GS memory or the mirror

    // What this submission has recorded.
    bool rendering = false;
    uint32_t primCount = 0, stateWords = 0, uploadUsed = 0;
    uint32_t runFirst = 0, runCount = 0;
    int pipelineBound = -1;
    gs::detail::Pages drawnSinceCut, sampledSinceCut;
    bool spilled = false;             // the last primitive drew past its frame's width: a barrier before the next
    // Work queued for the next cut, outside a rendering instance.
    std::vector<uint32_t> uploadPagesQueued;   // pages of g_vram to the mirror
    std::vector<uint32_t> resolveEntries;      // upload words of PAGE_* entries
    std::vector<uint32_t> expandEntries;
    std::vector<std::array<uint32_t, 3>> pushEntries;   // upload word of a PUSH_* entry, its width and height
    std::vector<std::pair<Image*, Image*>> snapshots;

    StateInfo state;
    PendingTransfer transfer;
    History history;
    std::vector<Image> scratch;       // copies of targets that primitives read while drawing into them
    std::vector<uint32_t> scratchSlots;
};
extern Renderer g;

// renderer.cpp
VkDevice dev();
uint32_t slotOfSubmission();
uint32_t* uploadWords();
uint32_t uploadAlloc(uint32_t words);
void reserve(uint32_t words);
VkCommandBuffer commandBuffer();
void fullBarrier(VkCommandBuffer cmd);
void pushConstants(VkCommandBuffer cmd, const uint32_t* words, uint32_t count);
void flushOps();
void endRendering();
void submit();
void mirrorPage(uint32_t page);
void mirrorPages(const gs::detail::Pages& pages);
// Pages the card drew, back in GS memory (g_vram); `why` names the report.
void download(const gs::detail::Pages& pages, const char* why);
void flushTransfer();
uint32_t takeSlot(const Image& image);
void retire(Image image, uint32_t slot);
void writePictureBinding(uint32_t index, VkImageView view);
bool makeStorageImage(Image& image, VkFormat format, uint32_t w, uint32_t h, const char* name);
Target* targetById(uint16_t id);
int bufferFamily(uint32_t psm);
void freeAllTargets();

template <typename F>
void forEachPage(const gs::detail::Pages& pages, F f) {
    for (uint32_t i = 0; i < 8; ++i)
        for (uint64_t b = pages.bits[i]; b; b &= b - 1) f(i * 64 + static_cast<uint32_t>(std::countr_zero(b)));
}
inline void addPage(gs::detail::Pages& pages, uint32_t page) { pages.bits[page >> 6] |= uint64_t(1) << (page & 63); }
inline bool hasPage(const gs::detail::Pages& pages, uint32_t page) { return (pages.bits[page >> 6] >> (page & 63)) & 1; }

// renderer_display.cpp
void releaseHistory();

} // namespace ps2::gpu::renderer
