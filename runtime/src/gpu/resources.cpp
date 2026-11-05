#include "gpu/resources.h"

#include "log.h"

#include <cstdint>

namespace ps2::gpu {

namespace {

// A memory type the requirements allow, with `want` (and, if it can, `prefer`).
bool memoryType(uint32_t allowed, VkMemoryPropertyFlags want, VkMemoryPropertyFlags prefer, uint32_t& index, VkMemoryPropertyFlags& got) {
    const VkPhysicalDeviceMemoryProperties& m = device().memory;
    for (int pass = 0; pass < 2; ++pass) {
        const VkMemoryPropertyFlags need = pass == 0 ? want | prefer : want;
        for (uint32_t i = 0; i < m.memoryTypeCount; ++i) {
            if (!(allowed & (1u << i)) || (m.memoryTypes[i].propertyFlags & need) != need) continue;
            index = i;
            got = m.memoryTypes[i].propertyFlags;
            return true;
        }
    }
    return false;
}

bool allocate(const VkMemoryRequirements& req, Memory where, VkDeviceMemory& memory, VkMemoryPropertyFlags& got) {
    VkMemoryPropertyFlags want = 0, prefer = 0;
    switch (where) {
    case Memory::Device: want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT; break;
    case Memory::Upload: want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; break;
    case Memory::Readback:
        want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        prefer = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    case Memory::Stream:
        want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        prefer = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    }
    uint32_t index = 0;
    if (!memoryType(req.memoryTypeBits, want, prefer, index, got)) return false;
    auto info = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    info.allocationSize = req.size;
    info.memoryTypeIndex = index;
    return vkAllocateMemory(device().handle, &info, nullptr, &memory) == VK_SUCCESS;
}

} // namespace

bool createBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, Memory where, const char* name) {
    destroy(b);
    auto info = make<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device().handle, &info, nullptr, &b.handle) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device().handle, b.handle, &req);
    VkMemoryPropertyFlags got = 0;
    if (!allocate(req, where, b.memory, got) || vkBindBufferMemory(device().handle, b.handle, b.memory, 0) != VK_SUCCESS) {
        PS2_WARN("gpu", "cannot allocate %llu bytes of memory for %s", static_cast<unsigned long long>(size), name);
        destroy(b);
        return false;
    }
    b.size = size;
    b.coherent = (got & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    if (where != Memory::Device && vkMapMemory(device().handle, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped) != VK_SUCCESS) {
        destroy(b);
        return false;
    }
    nameObject(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(b.handle), name);
    return true;
}

void destroy(Buffer& b) {
    if (b.handle) vkDestroyBuffer(device().handle, b.handle, nullptr);
    if (b.memory) vkFreeMemory(device().handle, b.memory, nullptr);
    b = Buffer{};
}

void invalidate(const Buffer& b) {
    if (b.coherent || !b.memory) return;
    auto range = make<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
    range.memory = b.memory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(device().handle, 1, &range);
}

bool createImage(Image& i, VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage, const char* name) {
    destroy(i);
    auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device().handle, &info, nullptr, &i.handle) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device().handle, i.handle, &req);
    VkMemoryPropertyFlags got = 0;
    if (!allocate(req, Memory::Device, i.memory, got) || vkBindImageMemory(device().handle, i.handle, i.memory, 0) != VK_SUCCESS) {
        PS2_WARN("gpu", "cannot allocate %ux%u image memory for %s", width, height, name);
        destroy(i);
        return false;
    }
    auto view = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = i.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device().handle, &view, nullptr, &i.view) != VK_SUCCESS) {
        destroy(i);
        return false;
    }
    i.format = format;
    i.width = width;
    i.height = height;
    nameObject(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(i.handle), name);
    return true;
}

void destroy(Image& i) {
    if (i.view) vkDestroyImageView(device().handle, i.view, nullptr);
    if (i.handle) vkDestroyImage(device().handle, i.handle, nullptr);
    if (i.memory) vkFreeMemory(device().handle, i.memory, nullptr);
    i = Image{};
}

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                   VkAccessFlags2 dstAccess) {
    auto b = make<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    auto dep = make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkImageLayout oldLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout) {
    auto b = make<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto dep = make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// ---- Commands -----------------------------------------------------------------------------------

bool Commands::init(Queue& queue, const char* name, uint32_t depth) {
    queue_ = &queue;
    auto type = make<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    auto sci = make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type);
    if (vkCreateSemaphore(device().handle, &sci, nullptr, &timeline_) != VK_SUCCESS) return false;
    nameObject(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(timeline_), name);
    slots_.resize(depth);
    for (Slot& s : slots_) {
        auto pci = make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = device().family;
        if (vkCreateCommandPool(device().handle, &pci, nullptr, &s.pool) != VK_SUCCESS) return false;
        auto cai = make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        cai.commandPool = s.pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device().handle, &cai, &s.cmd) != VK_SUCCESS) return false;
    }
    pool_ = slots_[0].pool;
    slot_ = 0;
    submitted_ = 0;
    recording_ = false;
    return true;
}

void Commands::shutdown() {
    if (!timeline_ && slots_.empty()) return;
    if (recording_) submit();
    waitIdle();
    for (Slot& s : slots_) {
        collect(s);
        if (s.pool) vkDestroyCommandPool(device().handle, s.pool, nullptr);
    }
    for (auto& release : pending_) release();
    pending_.clear();
    slots_.clear();
    if (timeline_) vkDestroySemaphore(device().handle, timeline_, nullptr);
    timeline_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    queue_ = nullptr;
}

void Commands::collect(Slot& s) {
    for (auto& release : s.releases) release();
    s.releases.clear();
}

VkCommandBuffer Commands::current() {
    Slot& s = slots_[slot_];
    if (recording_) return s.cmd;
    wait(s.submission);   // the card is done with the slot's last use
    collect(s);
    vkResetCommandPool(device().handle, s.pool, 0);
    auto begin = make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.cmd, &begin);
    recording_ = true;
    return s.cmd;
}

uint64_t Commands::submit(const std::vector<VkSemaphoreSubmitInfo>& waits, const std::vector<VkSemaphoreSubmitInfo>& signals) {
    Slot& s = slots_[slot_];
    if (!recording_) current();
    vkEndCommandBuffer(s.cmd);
    recording_ = false;
    const uint64_t number = ++submitted_;
    std::vector<VkSemaphoreSubmitInfo> signal = signals;
    auto mine = make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    mine.semaphore = timeline_;
    mine.value = number;
    mine.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signal.push_back(mine);
    auto cbi = make<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    cbi.commandBuffer = s.cmd;
    auto info = make<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    info.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    info.pWaitSemaphoreInfos = waits.data();
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = &cbi;
    info.signalSemaphoreInfoCount = static_cast<uint32_t>(signal.size());
    info.pSignalSemaphoreInfos = signal.data();
    if (const VkResult r = queue_->submit(info, VK_NULL_HANDLE); r != VK_SUCCESS)
        PS2_ERROR("gpu", "vkQueueSubmit2 failed (%s)", resultName(r));
    s.submission = number;
    for (auto& release : pending_) s.releases.push_back(std::move(release));
    pending_.clear();
    slot_ = (slot_ + 1) % slots_.size();
    return number;
}

bool Commands::finished(uint64_t submission) const {
    if (submission == 0) return true;
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(device().handle, timeline_, &value);
    return value >= submission;
}

bool Commands::wait(uint64_t submission) {
    if (submission == 0 || finished(submission)) return true;
    auto info = make<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline_;
    info.pValues = &submission;
    const VkResult r = vkWaitSemaphores(device().handle, &info, UINT64_MAX);
    if (r != VK_SUCCESS) {
        PS2_ERROR("gpu", "waiting for the graphics card failed (%s)", resultName(r));
        return false;
    }
    return true;
}

void Commands::defer(std::function<void()> release) { pending_.push_back(std::move(release)); }

} // namespace ps2::gpu
