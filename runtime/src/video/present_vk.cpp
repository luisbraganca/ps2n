#include "video/present_vk.h"

#if defined(PS2N_HAVE_SDL2) && defined(PS2N_HAVE_VULKAN)

#include "gpu/compose.h"
#include "gpu/device.h"
#include "gpu/picture.h"
#include "gpu/resources.h"
#include "hw/gs.h"
#include "log.h"

#include <SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace ps2::video::vulkan {

namespace {

constexpr uint32_t kFrames = 2;   // frames the card may work on while the next is recorded

struct State {
    SDL_Window* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> rendered;              // per swapchain image: drawn, for the presentation to wait for
    std::array<VkSemaphore, kFrames> acquired{};   // per frame in flight: the image is free to draw into
    bool stale = false;                            // the swapchain no longer fits the window
    gpu::Commands cmds;
    gpu::Composer composer;

    // The software renderer's picture: its pixels until the next draw
    // uploads them, through that frame's staging buffer, into `upload`.
    std::vector<uint32_t> pending;
    int pendingW = 0, pendingH = 0;
    bool hasPending = false;
    gpu::Image upload;
    bool uploadReady = false;   // `upload` was written: it is in SHADER_READ_ONLY_OPTIMAL
    std::array<gpu::Buffer, kFrames> staging;
    gpu::ComposeSource uploadSource;

    // A picture the Vulkan renderer drew: one of its picture slots.
    int pictureSlot = -1;
    uint64_t pictureReady = 0;
    std::array<gpu::ComposeSource, gpu::kPictureSlots> pictureSources{};

    const gpu::ComposeSource* current = nullptr;   // what the next draw shows
};
State g;

VkDevice dev() { return gpu::device().handle; }

bool fail(std::string& why, std::string text) {
    why = std::move(text);
    return false;
}

void destroySwapchainParts() {
    for (VkImageView v : g.views) vkDestroyImageView(dev(), v, nullptr);
    for (VkSemaphore s : g.rendered) vkDestroySemaphore(dev(), s, nullptr);
    g.views.clear();
    g.rendered.clear();
    g.images.clear();
}

// A swapchain for the window as it is now; false while it has no area (minimised).
bool createSwapchain(std::string& why) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu::device().physical, g.surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(g.window, &w, &h);
        extent = {static_cast<uint32_t>(std::max(w, 0)), static_cast<uint32_t>(std::max(h, 0))};
        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;
    const VkFormat before = g.format;
    if (!g.format) {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu::device().physical, g.surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu::device().physical, g.surface, &n, formats.data());
        if (formats.empty()) return fail(why, "the window's surface offers no format");
        g.format = formats[0].format;
        for (const VkSurfaceFormatKHR& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                g.format = f.format;
                break;
            }
    }
    uint32_t count = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
    auto info = gpu::make<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = g.surface;
    info.minImageCount = count;
    info.imageFormat = g.format;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // vsync, as the SDL window had it
    info.clipped = VK_TRUE;
    info.oldSwapchain = g.swapchain;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateSwapchainKHR(dev(), &info, nullptr, &next); r != VK_SUCCESS)
        return fail(why, std::string("cannot create a swapchain (") + gpu::resultName(r) + ")");
    destroySwapchainParts();
    if (g.swapchain) vkDestroySwapchainKHR(dev(), g.swapchain, nullptr);
    g.swapchain = next;
    g.extent = extent;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev(), g.swapchain, &n, nullptr);
    g.images.resize(n);
    vkGetSwapchainImagesKHR(dev(), g.swapchain, &n, g.images.data());
    for (VkImage image : g.images) {
        auto view = gpu::make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = g.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        vkCreateImageView(dev(), &view, nullptr, &v);
        g.views.push_back(v);
        auto sem = gpu::make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore s = VK_NULL_HANDLE;
        vkCreateSemaphore(dev(), &sem, nullptr, &s);
        g.rendered.push_back(s);
    }
    g.stale = false;
    if (g.composer.ready() && g.format == before) return true;
    return g.composer.init(g.format) || fail(why, "cannot create the presentation pipeline");
}

bool recreate() {
    gpu::device().present->waitIdle();
    std::string why;
    if (createSwapchain(why)) return true;
    if (!why.empty()) PS2_WARN("video", "%s", why.c_str());
    return false;
}

// The software renderer's picture, into `upload` through this frame's staging buffer.
void uploadPending(VkCommandBuffer cmd, size_t frame) {
    const int w = g.pendingW, h = g.pendingH;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4;
    gpu::Buffer& staging = g.staging[frame];
    if (staging.size < bytes &&
        !gpu::createBuffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, gpu::Memory::Upload, "window picture staging"))
        return;
    std::memcpy(staging.mapped, g.pending.data(), static_cast<size_t>(bytes));
    g.hasPending = false;
    VkImageLayout from = g.uploadReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    if (g.upload.width != static_cast<uint32_t>(w) || g.upload.height != static_cast<uint32_t>(h)) {
        // The old image may still be read by the frame in flight.
        if (g.upload.handle) {
            gpu::Image old = g.upload;
            gpu::ComposeSource oldSource = g.uploadSource;
            g.upload = gpu::Image{};
            g.uploadSource = gpu::ComposeSource{};
            g.cmds.defer([old, oldSource]() mutable {
                g.composer.release(oldSource);
                gpu::destroy(old);
            });
        }
        if (!gpu::createImage(g.upload, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              "window picture"))
            return;
        from = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    gpu::imageBarrier(cmd, g.upload.handle, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE, from,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy copy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0},
                                 {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1}};
    vkCmdCopyBufferToImage(cmd, staging.handle, g.upload.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    gpu::imageBarrier(cmd, g.upload.handle, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.uploadReady = true;
    if (g.composer.bind(g.uploadSource, g.upload.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, w, h)) g.current = &g.uploadSource;
}

} // namespace

bool open(SDL_Window* window, std::string& why) {
    close();
    g.window = window;
    if (!gpu::available() || !gpu::device().swapchain) return fail(why, "the graphics card has no swapchains here");
    if (!SDL_Vulkan_CreateSurface(window, gpu::device().instance, &g.surface)) {
        g.surface = VK_NULL_HANDLE;
        return fail(why, std::string("SDL cannot make a Vulkan surface for the window (") + SDL_GetError() + ")");
    }
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu::device().physical, gpu::device().family, g.surface, &supported);
    if (!supported) return fail(why, "the graphics card's queue cannot show pictures in this window");
    if (!g.cmds.init(*gpu::device().present, "window", kFrames)) return fail(why, "cannot make the window's command buffers");
    for (VkSemaphore& s : g.acquired) {
        auto sem = gpu::make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(dev(), &sem, nullptr, &s);
    }
    // A minimised window has no swapchain yet: the first draw that has an area makes it.
    std::string swapchainWhy;
    if (!createSwapchain(swapchainWhy) && !swapchainWhy.empty()) return fail(why, swapchainWhy);
    if (!g.format) return fail(why, "the window has no area to show pictures in");
    return true;
}

void close() {
    if (!g.window && !g.surface) return;
    if (g.cmds.ready()) g.cmds.shutdown();
    if (gpu::available()) {
        gpu::device().present->waitIdle();
        g.composer.release(g.uploadSource);
        for (gpu::ComposeSource& s : g.pictureSources) g.composer.release(s);
        g.composer.shutdown();
        gpu::destroy(g.upload);
        for (gpu::Buffer& b : g.staging) gpu::destroy(b);
        destroySwapchainParts();
        if (g.swapchain) vkDestroySwapchainKHR(dev(), g.swapchain, nullptr);
        for (VkSemaphore s : g.acquired)
            if (s) vkDestroySemaphore(dev(), s, nullptr);
        if (g.surface) vkDestroySurfaceKHR(gpu::device().instance, g.surface, nullptr);
    }
    g = State{};
}

bool isOpen() { return g.surface != VK_NULL_HANDLE; }

void waitIdle() {
    if (!g.cmds.ready()) return;
    if (g.cmds.recording()) g.cmds.submit();
    g.cmds.waitIdle();
    gpu::device().present->waitIdle();
}

void showFrame(const gs::Frame& frame) {
    g.pending.assign(frame.pixels.begin(), frame.pixels.end());
    g.pendingW = frame.width;
    g.pendingH = frame.height;
    g.hasPending = frame.width > 0 && frame.height > 0;
    g.pictureSlot = -1;
}

void showPicture(int slot, uint64_t ready, int width, int height) {
    if (slot < 0 || slot >= static_cast<int>(gpu::kPictureSlots)) return;
    gpu::takePicture(slot);
    gpu::ComposeSource& s = g.pictureSources[size_t(slot)];
    // A slot's image is made again when the picture's size changes.
    if (s.view != gpu::pictureView(slot)) g.composer.release(s);
    if (!g.composer.bind(s, gpu::pictureView(slot), VK_IMAGE_LAYOUT_GENERAL, width, height)) return;
    g.pictureSlot = slot;
    g.pictureReady = ready;
    g.hasPending = false;
    g.current = &s;
}

bool shownPixels(gs::Frame& out) {
    if (!g.current || (g.current != &g.uploadSource && !gpu::pictureTimeline())) return false;
    if (g.current == &g.uploadSource) {
        out.width = g.pendingW;
        out.height = g.pendingH;
        out.pixels = g.pending;
        return !out.pixels.empty();
    }
    // The renderer's picture, copied back once it is complete.
    const auto w = static_cast<uint32_t>(g.current->width), h = static_cast<uint32_t>(g.current->height);
    gpu::Buffer back;
    if (!gpu::createBuffer(back, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, gpu::Memory::Readback, "screenshot"))
        return false;
    VkCommandBuffer cmd = g.cmds.current();
    gpu::memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferImageCopy copy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {w, h, 1}};
    vkCmdCopyImageToBuffer(cmd, gpu::pictureImage(g.pictureSlot).handle, VK_IMAGE_LAYOUT_GENERAL, back.handle, 1, &copy);
    gpu::memoryBarrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                       VK_ACCESS_2_HOST_READ_BIT);
    auto ready = gpu::make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    ready.semaphore = gpu::pictureTimeline();
    ready.value = g.pictureReady;
    ready.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    const uint64_t submission = g.cmds.submit({ready});
    gpu::pictureShown(g.pictureSlot, g.cmds.timeline(), submission);
    const bool ok = g.cmds.wait(submission);
    if (ok) {
        gpu::invalidate(back);
        out.width = static_cast<int>(w);
        out.height = static_cast<int>(h);
        out.pixels.resize(size_t(w) * h);
        std::memcpy(out.pixels.data(), back.mapped, out.pixels.size() * 4);
    }
    gpu::destroy(back);
    return ok;
}

bool draw(const Look& look, const std::function<void(VkCommandBuffer)>& overlay) {
    if (!g.surface) return false;
    if ((g.stale || !g.swapchain) && !recreate()) return false;
    // The renderer stopped (at exit): its pictures and their timeline are gone.
    if (g.current && g.current != &g.uploadSource && !gpu::pictureTimeline()) g.current = nullptr;
    VkCommandBuffer cmd = g.cmds.current();   // the card finished this frame slot's last use
    const size_t frame = static_cast<size_t>(g.cmds.submitted() % kFrames);
    uint32_t index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(dev(), g.swapchain, UINT64_MAX, g.acquired[frame], VK_NULL_HANDLE, &index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        g.stale = true;
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        PS2_WARN("video", "cannot take a swapchain image (%s)", gpu::resultName(acquire));
        return false;
    }
    if (g.hasPending) uploadPending(cmd, frame);

    const VkImage image = g.images[index];
    gpu::imageBarrier(cmd, image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    auto colour = gpu::make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    colour.imageView = g.views[index];
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    auto rendering = gpu::make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea = {{0, 0}, g.extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colour;
    vkCmdBeginRendering(cmd, &rendering);
    if (g.current) {
        const gpu::Filter filter = look.filter == BilinearMode::Off     ? gpu::Filter::Nearest
                                   : look.filter == BilinearMode::Sharp ? gpu::Filter::Sharp
                                                                        : gpu::Filter::Smooth;
        g.composer.draw(cmd, *g.current, look.rect.x, look.rect.y, look.rect.w, look.rect.h, g.extent.width, g.extent.height, filter,
                        look.brightness);
    }
    {
        // ImGui's backend submits its texture uploads itself, to this queue.
        const std::lock_guard<std::mutex> lock(gpu::device().present->mutex());
        overlay(cmd);
    }
    vkCmdEndRendering(cmd);
    gpu::imageBarrier(cmd, image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    std::vector<VkSemaphoreSubmitInfo> waits, signals;
    auto acquiredWait = gpu::make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    acquiredWait.semaphore = g.acquired[frame];
    acquiredWait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    waits.push_back(acquiredWait);
    const bool picture = g.current && g.current != &g.uploadSource && g.pictureSlot >= 0;
    if (picture) {
        // The renderer's picture is complete when its timeline reaches the value it gave.
        auto ready = gpu::make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        ready.semaphore = gpu::pictureTimeline();
        ready.value = g.pictureReady;
        ready.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        waits.push_back(ready);
    }
    auto renderedSignal = gpu::make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    renderedSignal.semaphore = g.rendered[index];
    renderedSignal.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    signals.push_back(renderedSignal);
    const uint64_t submission = g.cmds.submit(waits, signals);
    if (picture) gpu::pictureShown(g.pictureSlot, g.cmds.timeline(), submission);
    auto present = gpu::make<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &g.rendered[index];
    present.swapchainCount = 1;
    present.pSwapchains = &g.swapchain;
    present.pImageIndices = &index;
    const VkResult shown = gpu::device().present->present(present);
    if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) g.stale = true;
    else if (shown != VK_SUCCESS) PS2_WARN("video", "cannot show the picture (%s)", gpu::resultName(shown));
    return true;
}

void resized() { g.stale = true; }

void outputSize(int& w, int& h) {
    w = static_cast<int>(g.extent.width);
    h = static_cast<int>(g.extent.height);
}

uint32_t imageCount() { return static_cast<uint32_t>(std::max<size_t>(g.images.size(), kFrames)); }
VkFormat format() { return g.format; }
VkQueue queue() { return gpu::device().present->handle(); }
std::mutex& queueMutex() { return gpu::device().present->mutex(); }

} // namespace ps2::video::vulkan

#endif
