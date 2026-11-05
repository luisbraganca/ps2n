#include "gpu/picture.h"

#include "log.h"

#include <array>
#include <atomic>
#include <mutex>

namespace ps2::gpu {

namespace {

struct Slots {
    std::mutex mutex;
    std::array<Image, kPictureSlots> images;
    int shown = -1, waiting = -1;
    std::array<uint64_t, kPictureSlots> lastRead{};   // the window's submissions that read each slot
    VkSemaphore windowTimeline = VK_NULL_HANDLE;
    std::atomic<VkSemaphore> rendererTimeline{VK_NULL_HANDLE};
};
Slots g;

} // namespace

int acquirePicture(VkCommandBuffer cmd, Commands& commands, uint32_t width, uint32_t height) {
    int slot = -1;
    uint64_t read = 0;
    VkSemaphore timeline = VK_NULL_HANDLE;
    {
        const std::lock_guard<std::mutex> lock(g.mutex);
        for (int s = 0; s < static_cast<int>(kPictureSlots) && slot < 0; ++s)
            if (s != g.shown && s != g.waiting) slot = s;
        read = g.lastRead[size_t(slot)];
        timeline = g.windowTimeline;
    }
    if (read && timeline) {   // the window's last frame that showed it
        auto info = make<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        info.semaphoreCount = 1;
        info.pSemaphores = &timeline;
        info.pValues = &read;
        vkWaitSemaphores(device().handle, &info, UINT64_MAX);
    }
    Image& image = g.images[size_t(slot)];
    if (image.width != width || image.height != height) {
        // Earlier pictures in it may still be read back by this renderer's
        // submissions in flight: its image goes once they are done.
        if (image.handle) {
            Image old = image;
            image = Image{};
            commands.defer([old]() mutable { destroy(old); });
        }
        if (!createImage(image, VK_FORMAT_R8G8B8A8_UNORM, width, height,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         "picture"))
            return -1;
        imageBarrier(cmd, image.handle, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
    }
    return slot;
}

Image& pictureImage(int slot) { return g.images[size_t(slot)]; }

void publishPicture(int slot) {
    const std::lock_guard<std::mutex> lock(g.mutex);
    g.waiting = slot;
}

void setPictureTimeline(VkSemaphore timeline) { g.rendererTimeline = timeline; }
VkSemaphore pictureTimeline() { return g.rendererTimeline; }

void releasePictures() {
    const std::lock_guard<std::mutex> lock(g.mutex);
    for (Image& image : g.images) destroy(image);
    g.shown = g.waiting = -1;
    g.lastRead = {};
    g.windowTimeline = VK_NULL_HANDLE;
    g.rendererTimeline = VK_NULL_HANDLE;
}

VkImageView pictureView(int slot) { return g.images[size_t(slot)].view; }

void takePicture(int slot) {
    const std::lock_guard<std::mutex> lock(g.mutex);
    g.shown = slot;
    if (g.waiting == slot) g.waiting = -1;
}

void pictureShown(int slot, VkSemaphore timeline, uint64_t value) {
    const std::lock_guard<std::mutex> lock(g.mutex);
    g.lastRead[size_t(slot)] = value;
    g.windowTimeline = timeline;
}

} // namespace ps2::gpu
