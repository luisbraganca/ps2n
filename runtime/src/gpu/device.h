// The open device, for the runtime's Vulkan code (gpu.h is the interface
// the rest of the runtime sees).
#pragma once

#include "gpu/gpu.h"
#include "gpu/vk.h"

#include <mutex>

namespace ps2::gpu {

// A queue and the lock its users hold while they submit or present to it.
// The GS's renderer and the window each have one; where the device offers a
// single queue they share it.
class Queue {
public:
    VkResult submit(const VkSubmitInfo2& info, VkFence fence);
    VkResult present(const VkPresentInfoKHR& info);
    VkResult waitIdle();
    VkQueue handle() const { return handle_; }
    std::mutex& mutex() { return mutex_; }   // also held around ImGui's drawing, which submits itself

    VkQueue handle_ = VK_NULL_HANDLE;

private:
    std::mutex mutex_;
};

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice handle = VK_NULL_HANDLE;
    uint32_t family = 0;
    Queue* render = nullptr;    // the GS's
    Queue* present = nullptr;   // the window's
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    bool surfaces = false;      // the instance can make surfaces
    bool swapchain = false;     // the device has VK_KHR_swapchain
    bool debugUtils = false;
    bool rtz32 = false;         // shaders can round float32 toward zero, as the EE does
    bool float64 = false;       // shaderFloat64
    bool rtz64 = false;         // and round it toward zero
    bool rte64 = false;         // or to nearest, set explicitly
    uint32_t maxViewport = 16384;
};

// Valid after init() succeeded, until shutdown().
Device& device();
// A name the validation layer and debuggers show, with --gpu-debug.
void nameObject(VkObjectType type, uint64_t handle, const char* name);

} // namespace ps2::gpu
