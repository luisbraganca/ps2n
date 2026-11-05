// Buffers, images, barriers and command buffers on the open device. Memory
// comes straight from the driver, one allocation per buffer or image: the
// renderer makes a few hundred at most.
#pragma once

#include "gpu/device.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ps2::gpu {

enum class Memory {
    Device,     // the card's own memory
    Upload,     // the host writes it, the card reads it (mapped)
    Readback,   // the card writes it, the host reads it (mapped, cached where it can be)
    Stream,     // the host writes it, the card's shaders read it often: the card's memory where
                // the host can map it (resizable BAR), else as Upload
};

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;   // Upload and Readback buffers stay mapped
    VkDeviceSize size = 0;
    bool coherent = true;
};

struct Image {
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

bool createBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, Memory where, const char* name);
void destroy(Buffer& b);
// Readback buffers may not be coherent: before the host reads what the card wrote.
void invalidate(const Buffer& b);
// An image in VK_IMAGE_LAYOUT_UNDEFINED, to be moved to its layout with imageBarrier().
bool createImage(Image& i, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, const char* name);
void destroy(Image& i);

// Barriers (synchronization2): one for all memory, and one for an image's layout.
void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                   VkAccessFlags2 dstAccess);
void imageBarrier(VkCommandBuffer cmd, VkImage image, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkImageLayout oldLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout);

// Command buffers one thread records at a time and submits to a queue, a few
// in flight: a timeline semaphore counts the submissions the card finished.
class Commands {
public:
    bool init(Queue& queue, const char* name, uint32_t depth = 3);
    void shutdown();   // waits for everything submitted
    bool ready() const { return pool_ != VK_NULL_HANDLE; }
    // The command buffer being recorded, begun on first use. Beginning one
    // waits until the card finished the one that used its slot before.
    VkCommandBuffer current();
    bool recording() const { return recording_; }
    // Ends and submits the current one (an empty submission if none was
    // begun), waiting for and signalling the given semaphores as well as the
    // timeline. Returns its number, which finished() and wait() take.
    uint64_t submit(const std::vector<VkSemaphoreSubmitInfo>& waits = {}, const std::vector<VkSemaphoreSubmitInfo>& signals = {});
    bool finished(uint64_t submission) const;
    bool wait(uint64_t submission);   // false: the device was lost
    void waitIdle() { wait(submitted_); }
    uint64_t submitted() const { return submitted_; }
    // Runs `release` once the card finished what was recorded so far.
    void defer(std::function<void()> release);
    VkSemaphore timeline() const { return timeline_; }

private:
    struct Slot {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        uint64_t submission = 0;   // what it was last submitted as
        std::vector<std::function<void()>> releases;
    };
    void collect(Slot& s);
    Queue* queue_ = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;   // non-null once initialised (the first slot's)
    std::vector<Slot> slots_;
    size_t slot_ = 0;
    bool recording_ = false;
    uint64_t submitted_ = 0;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    std::vector<std::function<void()>> pending_;   // releases for what is being recorded
};

} // namespace ps2::gpu
