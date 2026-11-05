// The Vulkan API as the runtime calls it: the vendored headers without their
// prototypes (VK_NO_PROTOTYPES, set by CMake), and a pointer for each
// function the runtime uses, which loader.cpp fills from the graphics
// driver's Vulkan library when the runtime starts. Nothing links against
// Vulkan, so a game starts where there is none and draws in software.
//
// A function the runtime needs goes in one of the lists below: global ones
// (before an instance exists), instance ones, device ones; the optional lists
// may stay null (extensions the device may lack).
#pragma once

#include <vulkan/vulkan.h>

#define PS2N_VK_GLOBAL_FUNCTIONS(X)                                                                                    \
    X(vkCreateInstance)                                                                                                \
    X(vkEnumerateInstanceExtensionProperties)                                                                          \
    X(vkEnumerateInstanceLayerProperties)                                                                              \
    X(vkEnumerateInstanceVersion)

#define PS2N_VK_INSTANCE_FUNCTIONS(X)                                                                                  \
    X(vkDestroyInstance)                                                                                               \
    X(vkEnumeratePhysicalDevices)                                                                                      \
    X(vkGetPhysicalDeviceProperties)                                                                                   \
    X(vkGetPhysicalDeviceProperties2)                                                                                  \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkEnumerateDeviceExtensionProperties)                                                                            \
    X(vkCreateDevice)                                                                                                  \
    X(vkGetDeviceProcAddr)

// Surfaces exist only where the instance has VK_KHR_surface (a window).
#define PS2N_VK_SURFACE_FUNCTIONS(X)                                                                                   \
    X(vkDestroySurfaceKHR)                                                                                             \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

// Only with --gpu-debug, where the instance has VK_EXT_debug_utils.
#define PS2N_VK_DEBUG_FUNCTIONS(X)                                                                                     \
    X(vkCreateDebugUtilsMessengerEXT)                                                                                  \
    X(vkDestroyDebugUtilsMessengerEXT)                                                                                 \
    X(vkSetDebugUtilsObjectNameEXT)

#define PS2N_VK_DEVICE_FUNCTIONS(X)                                                                                    \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueWaitIdle)                                                                                                 \
    X(vkQueueSubmit2)                                                                                                  \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)                                                                                                   \
    X(vkInvalidateMappedMemoryRanges)                                                                                  \
    X(vkFlushMappedMemoryRanges)                                                                                       \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkResetCommandPool)                                                                                              \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCreateFence)                                                                                                   \
    X(vkDestroyFence)                                                                                                  \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkWaitSemaphores)                                                                                                \
    X(vkSignalSemaphore)                                                                                               \
    X(vkGetSemaphoreCounterValue)                                                                                      \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkCreateComputePipelines)                                                                                        \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkFreeDescriptorSets)                                                                                            \
    X(vkResetDescriptorPool)                                                                                           \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCmdPipelineBarrier2)                                                                                           \
    X(vkCmdBeginRendering)                                                                                             \
    X(vkCmdEndRendering)                                                                                               \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdSetViewport)                                                                                                \
    X(vkCmdSetScissor)                                                                                                 \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdDispatch)                                                                                                   \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdCopyImage)                                                                                                  \
    X(vkCmdClearColorImage)                                                                                            \
    X(vkCmdFillBuffer)                                                                                                 \
    X(vkCmdUpdateBuffer)

// Only where a window shows the pictures (VK_KHR_swapchain).
#define PS2N_VK_SWAPCHAIN_FUNCTIONS(X)                                                                                 \
    X(vkCreateSwapchainKHR)                                                                                            \
    X(vkDestroySwapchainKHR)                                                                                           \
    X(vkGetSwapchainImagesKHR)                                                                                         \
    X(vkAcquireNextImageKHR)                                                                                           \
    X(vkQueuePresentKHR)

#define PS2N_VK_DECLARE(name) extern PFN_##name name;
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PS2N_VK_GLOBAL_FUNCTIONS(PS2N_VK_DECLARE)
PS2N_VK_INSTANCE_FUNCTIONS(PS2N_VK_DECLARE)
PS2N_VK_SURFACE_FUNCTIONS(PS2N_VK_DECLARE)
PS2N_VK_DEBUG_FUNCTIONS(PS2N_VK_DECLARE)
PS2N_VK_DEVICE_FUNCTIONS(PS2N_VK_DECLARE)
PS2N_VK_SWAPCHAIN_FUNCTIONS(PS2N_VK_DECLARE)
#undef PS2N_VK_DECLARE

namespace ps2::gpu::loader {

// Loads the Vulkan library and its global functions. False, with `why` set,
// where there is none.
bool open(const char*& why);
// The instance's and the device's functions: false names the first missing
// (a driver older than the runtime needs).
bool loadInstance(VkInstance instance, bool surface, bool debug, const char*& missing);
bool loadDevice(VkDevice device, bool swapchain, const char*& missing);
// Any function by name, for ImGui's Vulkan backend.
PFN_vkVoidFunction lookup(const char* name, VkInstance instance, VkDevice device);
void close();

} // namespace ps2::gpu::loader

namespace ps2::gpu {

const char* resultName(VkResult r);

// A Vulkan structure with its type and its chain set, every other field zero.
template <typename T>
T make(VkStructureType type, void* next = nullptr) {
    T t{};
    t.sType = type;
    t.pNext = next;
    return t;
}

} // namespace ps2::gpu
