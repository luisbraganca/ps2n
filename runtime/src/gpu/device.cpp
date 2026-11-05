// The Vulkan instance and device (gpu.h, device.h).
#include "gpu/device.h"

#include "log.h"

#include "ps2n/util.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace ps2::gpu {

namespace {

Device g_device;
Status g_status;
Queue g_queues[2];
VkDebugUtilsMessengerEXT g_messenger = VK_NULL_HANDLE;
std::atomic<uint64_t> g_validationMessages{0};

// What the renderer needs from a device, and the sentence that says it lacks it.
constexpr const char* kInterlock = "VK_EXT_fragment_shader_interlock";

bool listed(const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

std::string apiText(uint32_t v) { return ps2n::strprintf("%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v)); }

VKAPI_ATTR VkBool32 VKAPI_CALL onValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* text = data && data->pMessage ? data->pMessage : "";
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        g_validationMessages.fetch_add(1);
        PS2_WARN("gpu", "validation: %s", text);
    } else {
        PS2_DEBUG("gpu", "validation: %s", text);
    }
    return VK_FALSE;
}

// One physical device and whether it can draw the GS.
struct Candidate {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::string name;
    std::string lacks;   // why not, empty if it can
    int score = 0;
    uint32_t family = 0, queues = 0;
    bool swapchain = false, float64 = false, rtz32 = false, rtz64 = false, rte64 = false;
    uint32_t maxViewport = 16384;
    std::string driver;
    uint32_t api = 0;
};

Candidate examine(VkPhysicalDevice pd, bool wantSwapchain) {
    Candidate c;
    c.physical = pd;
    auto p12 = make<VkPhysicalDeviceVulkan12Properties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
    auto props = make<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12);
    vkGetPhysicalDeviceProperties2(pd, &props);
    c.name = props.properties.deviceName;
    c.api = props.properties.apiVersion;
    c.driver = std::string(p12.driverName) + (p12.driverInfo[0] ? std::string(" ") + p12.driverInfo : std::string());
    c.maxViewport = std::min(props.properties.limits.maxViewportDimensions[0], props.properties.limits.maxViewportDimensions[1]);
    switch (props.properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: c.score = 3; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: c.score = 2; break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: c.score = 1; break;
    default: c.score = 0; break;
    }
    if (c.api < VK_API_VERSION_1_3) {
        c.lacks = "reports Vulkan " + apiText(c.api) + "; the renderer needs 1.3";
        return c;
    }
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, families.data());
    bool found = false;
    for (uint32_t i = 0; i < n && !found; ++i) {
        const VkQueueFlags want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[i].queueFlags & want) == want && families[i].queueCount) {
            c.family = i;
            c.queues = families[i].queueCount;
            found = true;
        }
    }
    if (!found) {
        c.lacks = "has no queue that both draws and computes";
        return c;
    }
    n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    if (!listed(exts, kInterlock)) {
        c.lacks = "lacks VK_EXT_fragment_shader_interlock, which drawing the GS's blending in order needs";
        return c;
    }
    c.swapchain = wantSwapchain && listed(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    auto interlock = make<VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT);
    auto f13 = make<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &interlock);
    auto f12 = make<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13);
    auto f = make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12);
    vkGetPhysicalDeviceFeatures2(pd, &f);
    const struct {
        bool has;
        const char* name;
    } needs[] = {
        {interlock.fragmentShaderPixelInterlock == VK_TRUE, "fragmentShaderPixelInterlock"},
        {f.features.fragmentStoresAndAtomics == VK_TRUE, "fragmentStoresAndAtomics"},
        {f12.timelineSemaphore == VK_TRUE, "timelineSemaphore"},
        {f12.runtimeDescriptorArray == VK_TRUE, "runtimeDescriptorArray"},
        {f12.descriptorBindingPartiallyBound == VK_TRUE, "descriptorBindingPartiallyBound"},
        {f12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE, "descriptorBindingStorageImageUpdateAfterBind"},
        {f12.shaderStorageImageArrayNonUniformIndexing == VK_TRUE, "shaderStorageImageArrayNonUniformIndexing"},
        {f12.descriptorBindingUpdateUnusedWhilePending == VK_TRUE, "descriptorBindingUpdateUnusedWhilePending"},
        {f13.synchronization2 == VK_TRUE, "synchronization2"},
        {f13.dynamicRendering == VK_TRUE, "dynamicRendering"},
        {f13.shaderTerminateInvocation == VK_TRUE, "shaderTerminateInvocation"},
    };
    for (const auto& need : needs) {
        if (need.has) continue;
        c.lacks = std::string("lacks ") + need.name + ", which the renderer needs";
        return c;
    }
    c.float64 = f.features.shaderFloat64 == VK_TRUE;
    c.rtz32 = p12.shaderRoundingModeRTZFloat32 == VK_TRUE && p12.shaderSignedZeroInfNanPreserveFloat32 == VK_TRUE;
    c.rte64 = c.float64 && p12.shaderRoundingModeRTEFloat64 == VK_TRUE;
    c.rtz64 = c.float64 && p12.shaderRoundingModeRTZFloat64 == VK_TRUE && p12.shaderSignedZeroInfNanPreserveFloat64 == VK_TRUE &&
              p12.roundingModeIndependence != VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
    return c;
}

bool fail(std::string why) {
    g_status.reason = std::move(why);
    shutdown();
    return false;
}

} // namespace

VkResult Queue::submit(const VkSubmitInfo2& info, VkFence fence) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return vkQueueSubmit2(handle_, 1, &info, fence);
}

VkResult Queue::present(const VkPresentInfoKHR& info) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return vkQueuePresentKHR(handle_, &info);
}

VkResult Queue::waitIdle() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return vkQueueWaitIdle(handle_);
}

Device& device() { return g_device; }

void nameObject(VkObjectType type, uint64_t handle, const char* name) {
    if (!g_device.debugUtils || !vkSetDebugUtilsObjectNameEXT || !handle || !name) return;
    auto info = make<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(g_device.handle, &info);
}

bool init(const Options& options) {
    if (g_status.available) return true;
    g_status = Status{};
    const char* why = nullptr;
    if (!loader::open(why)) return fail(why);
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_3)
        return fail("the Vulkan loader is version " + apiText(version) + "; 1.3 is needed: update the graphics driver");

    // The instance: surfaces when a window will show the pictures (every
    // kind the loader knows, as the window does not exist yet), the
    // validation layer and its messages with --gpu-debug.
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> available(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, available.data());
    std::vector<const char*> extensions;
    const bool surfaces = options.window && listed(available, VK_KHR_SURFACE_EXTENSION_NAME);
    if (surfaces) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        for (const char* platform : {"VK_KHR_win32_surface", "VK_KHR_xlib_surface", "VK_KHR_xcb_surface", "VK_KHR_wayland_surface",
                                     "VK_EXT_metal_surface", "VK_MVK_macos_surface"})
            if (listed(available, platform)) extensions.push_back(platform);
    }
    const bool debugUtils = options.debug && listed(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkInstanceCreateFlags flags = 0;
    if (listed(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    std::vector<const char*> layers;
    if (options.debug) {
        n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> found(n);
        vkEnumerateInstanceLayerProperties(&n, found.data());
        bool validation = false;
        for (const VkLayerProperties& l : found) {
            PS2_DEBUG("gpu", "Vulkan layer %s: %s", l.layerName, l.description);
            validation = validation || std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        }
        if (validation) layers.push_back("VK_LAYER_KHRONOS_validation");
        else PS2_WARN("gpu", "--gpu-debug: the Vulkan validation layer is not installed (it comes with the Vulkan SDK); going on without it");
    }
    auto app = make<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "PS2Native";
    app.pEngineName = "ps2n";
    app.apiVersion = VK_API_VERSION_1_3;
    auto ici = make<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ici.flags = flags;
    ici.pApplicationInfo = &app;
    ici.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    if (const VkResult r = vkCreateInstance(&ici, nullptr, &g_device.instance); r != VK_SUCCESS)
        return fail(std::string("cannot create a Vulkan instance (") + resultName(r) + ")");
    const char* missing = nullptr;
    if (!loader::loadInstance(g_device.instance, surfaces, debugUtils, missing))
        return fail(std::string("the Vulkan loader lacks ") + missing);
    g_device.surfaces = surfaces;
    g_device.debugUtils = debugUtils;
    g_status.validation = !layers.empty();
    if (debugUtils) {
        auto mci = make<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = onValidation;
        vkCreateDebugUtilsMessengerEXT(g_device.instance, &mci, nullptr, &g_messenger);
    }

    // The device: the best kind (discrete, integrated, virtual, a CPU) among
    // those that have everything the renderer needs.
    n = 0;
    vkEnumeratePhysicalDevices(g_device.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> physical(n);
    vkEnumeratePhysicalDevices(g_device.instance, &n, physical.data());
    if (physical.empty()) return fail("no graphics card has a Vulkan driver");
    std::vector<Candidate> candidates;
    for (VkPhysicalDevice pd : physical) candidates.push_back(examine(pd, surfaces));
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates)
        if (c.lacks.empty() && (!best || c.score > best->score)) best = &c;
    if (!best) {
        std::string reasons;
        for (const Candidate& c : candidates) reasons += (reasons.empty() ? "" : "; ") + c.name + " " + c.lacks;
        return fail(reasons);
    }
    const Candidate& c = *best;
    g_device.physical = c.physical;
    g_device.family = c.family;
    const uint32_t queueCount = std::min(c.queues, 2u);
    const float priorities[2] = {1.0f, 1.0f};
    auto qci = make<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qci.queueFamilyIndex = c.family;
    qci.queueCount = queueCount;
    qci.pQueuePriorities = priorities;
    std::vector<const char*> deviceExtensions = {kInterlock};
    if (c.swapchain) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    auto interlock = make<VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT);
    interlock.fragmentShaderPixelInterlock = VK_TRUE;
    auto f13 = make<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &interlock);
    f13.synchronization2 = VK_TRUE;
    f13.dynamicRendering = VK_TRUE;
    f13.shaderTerminateInvocation = VK_TRUE;
    auto f12 = make<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13);
    f12.timelineSemaphore = VK_TRUE;
    f12.runtimeDescriptorArray = VK_TRUE;
    f12.descriptorBindingPartiallyBound = VK_TRUE;
    f12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    f12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    f12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    auto features = make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12);
    features.features.fragmentStoresAndAtomics = VK_TRUE;
    features.features.shaderFloat64 = c.float64 ? VK_TRUE : VK_FALSE;
    auto dci = make<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features);
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    dci.ppEnabledExtensionNames = deviceExtensions.data();
    if (const VkResult r = vkCreateDevice(c.physical, &dci, nullptr, &g_device.handle); r != VK_SUCCESS)
        return fail(c.name + ": cannot create its Vulkan device (" + resultName(r) + ")");
    if (!loader::loadDevice(g_device.handle, c.swapchain, missing))
        return fail(c.name + ": its driver lacks " + missing);
    for (uint32_t i = 0; i < queueCount; ++i) vkGetDeviceQueue(g_device.handle, c.family, i, &g_queues[i].handle_);
    g_device.render = &g_queues[0];
    g_device.present = &g_queues[queueCount - 1];
    vkGetPhysicalDeviceProperties(c.physical, &g_device.properties);
    vkGetPhysicalDeviceMemoryProperties(c.physical, &g_device.memory);
    g_device.swapchain = c.swapchain;
    g_device.rtz32 = c.rtz32;
    g_device.float64 = c.float64;
    g_device.rtz64 = c.rtz64;
    g_device.rte64 = c.rte64;
    g_device.maxViewport = c.maxViewport;
    g_status.available = true;
    g_status.canPresent = c.swapchain;
    g_status.deviceName = c.name;
    g_status.driver = c.driver;
    g_status.apiVersion = c.api;
    PS2_DEBUG("gpu", "Vulkan device %s (%s, Vulkan %s): %u queue(s), rounding toward zero: %s, float64: %s", c.name.c_str(),
              c.driver.c_str(), apiText(c.api).c_str(), queueCount, c.rtz32 ? "yes" : "no", c.float64 ? "yes" : "no");
    return true;
}

const Status& status() { return g_status; }
bool available() { return g_status.available; }

std::string describe() {
    if (!g_status.available) return {};
    return g_status.deviceName + " (" + g_status.driver + ", Vulkan " + apiText(g_status.apiVersion) + ")";
}

void shutdown() {
    if (g_device.handle) {
        vkDeviceWaitIdle(g_device.handle);
        vkDestroyDevice(g_device.handle, nullptr);
    }
    if (g_messenger && vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(g_device.instance, g_messenger, nullptr);
    g_messenger = VK_NULL_HANDLE;
    if (g_device.instance) vkDestroyInstance(g_device.instance, nullptr);
    g_device = Device{};
    for (Queue& q : g_queues) q.handle_ = VK_NULL_HANDLE;
    g_status.available = false;
    g_status.canPresent = false;
    loader::close();
}

uint64_t validationMessages() { return g_validationMessages.load(); }

} // namespace ps2::gpu
