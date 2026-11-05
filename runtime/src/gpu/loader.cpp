// Loads the graphics driver's Vulkan library at run time (see vk.h).
#include "gpu/vk.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define PS2N_VK_DEFINE(name) PFN_##name name = nullptr;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
PS2N_VK_GLOBAL_FUNCTIONS(PS2N_VK_DEFINE)
PS2N_VK_INSTANCE_FUNCTIONS(PS2N_VK_DEFINE)
PS2N_VK_SURFACE_FUNCTIONS(PS2N_VK_DEFINE)
PS2N_VK_DEBUG_FUNCTIONS(PS2N_VK_DEFINE)
PS2N_VK_DEVICE_FUNCTIONS(PS2N_VK_DEFINE)
PS2N_VK_SWAPCHAIN_FUNCTIONS(PS2N_VK_DEFINE)
#undef PS2N_VK_DEFINE

namespace ps2::gpu::loader {

namespace {

#if defined(_WIN32)
HMODULE g_library = nullptr;
#else
void* g_library = nullptr;
#endif

void* symbol(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(g_library, name));
#else
    return dlsym(g_library, name);
#endif
}

} // namespace

bool open(const char*& why) {
    if (g_library) return true;
#if defined(_WIN32)
    g_library = LoadLibraryW(L"vulkan-1.dll");
    why = "no Vulkan loader (vulkan-1.dll) on this computer: the graphics driver installs it";
#elif defined(__APPLE__)
    for (const char* name : {"libvulkan.1.dylib", "libMoltenVK.dylib"})
        if (!g_library) g_library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    why = "no Vulkan library (libvulkan.1.dylib or MoltenVK) on this computer";
#else
    for (const char* name : {"libvulkan.so.1", "libvulkan.so"})
        if (!g_library) g_library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    why = "no Vulkan loader (libvulkan.so.1) on this computer: install the graphics driver's Vulkan package";
#endif
    if (!g_library) return false;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol("vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        why = "the Vulkan library has no vkGetInstanceProcAddr";
        close();
        return false;
    }
#define PS2N_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
    PS2N_VK_GLOBAL_FUNCTIONS(PS2N_VK_LOAD)
#undef PS2N_VK_LOAD
    // vkEnumerateInstanceVersion is 1.1's: absent means a 1.0 loader, which is told apart later.
    if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties || !vkEnumerateInstanceLayerProperties) {
        why = "the Vulkan library lacks its global functions";
        close();
        return false;
    }
    return true;
}

bool loadInstance(VkInstance instance, bool surface, bool debug, const char*& missing) {
    bool ok = true;
#define PS2N_VK_LOAD(name)                                                                                             \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));                                       \
    if (!name && ok) {                                                                                                 \
        ok = false;                                                                                                    \
        missing = #name;                                                                                               \
    }
    PS2N_VK_INSTANCE_FUNCTIONS(PS2N_VK_LOAD)
    if (surface) {
        PS2N_VK_SURFACE_FUNCTIONS(PS2N_VK_LOAD)
    }
    if (debug) {
        PS2N_VK_DEBUG_FUNCTIONS(PS2N_VK_LOAD)
    }
#undef PS2N_VK_LOAD
    return ok;
}

bool loadDevice(VkDevice device, bool swapchain, const char*& missing) {
    bool ok = true;
#define PS2N_VK_LOAD(name)                                                                                             \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                           \
    if (!name && ok) {                                                                                                 \
        ok = false;                                                                                                    \
        missing = #name;                                                                                               \
    }
    PS2N_VK_DEVICE_FUNCTIONS(PS2N_VK_LOAD)
    if (swapchain) {
        PS2N_VK_SWAPCHAIN_FUNCTIONS(PS2N_VK_LOAD)
    }
#undef PS2N_VK_LOAD
    return ok;
}

PFN_vkVoidFunction lookup(const char* name, VkInstance instance, VkDevice) {
    // The instance's lookup finds device functions too (through the loader's
    // dispatch), and asking the device for an instance function is an error.
    return vkGetInstanceProcAddr(instance, name);
}

void close() {
    if (!g_library) return;
#if defined(_WIN32)
    FreeLibrary(g_library);
#else
    dlclose(g_library);
#endif
    g_library = nullptr;
#define PS2N_VK_CLEAR(name) name = nullptr;
    vkGetInstanceProcAddr = nullptr;
    PS2N_VK_GLOBAL_FUNCTIONS(PS2N_VK_CLEAR)
    PS2N_VK_INSTANCE_FUNCTIONS(PS2N_VK_CLEAR)
    PS2N_VK_SURFACE_FUNCTIONS(PS2N_VK_CLEAR)
    PS2N_VK_DEBUG_FUNCTIONS(PS2N_VK_CLEAR)
    PS2N_VK_DEVICE_FUNCTIONS(PS2N_VK_CLEAR)
    PS2N_VK_SWAPCHAIN_FUNCTIONS(PS2N_VK_CLEAR)
#undef PS2N_VK_CLEAR
}

} // namespace ps2::gpu::loader

namespace ps2::gpu {

const char* resultName(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "an unexpected VkResult";
    }
}

} // namespace ps2::gpu
