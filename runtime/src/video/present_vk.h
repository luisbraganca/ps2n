// The window's pictures through Vulkan (builds with SDL2 and the Vulkan
// renderer): a swapchain on the window, the game's picture drawn into it at
// the shape and with the filtering the settings ask for, the overlay on top.
// Main thread only, like the window. video.cpp falls back to SDL's renderer
// where this cannot open.
#pragma once

#if defined(PS2N_HAVE_SDL2) && defined(PS2N_HAVE_VULKAN)

#include "gpu/vk.h"
#include "video/video.h"

#include <SDL.h>

#include <functional>
#include <mutex>
#include <string>

namespace ps2::gs {
struct Frame;
}

namespace ps2::video::vulkan {

// The window's surface and swapchain. False, with `why`, where the device
// cannot show pictures in this window.
bool open(SDL_Window* window, std::string& why);
void close();
bool isOpen();
// Until the card finished every frame the window sent.
void waitIdle();

// What the window shows next: the software renderer's picture (copied now),
// or one the Vulkan renderer drew (`slot` of its pictures, ready once its
// timeline reaches `ready`).
void showFrame(const gs::Frame& frame);
void showPicture(int slot, uint64_t ready, int width, int height);
// The picture on screen, read back (screenshots).
bool shownPixels(gs::Frame& out);

struct Look {
    Rect rect;            // where the picture goes, in the window's pixels
    BilinearMode filter;
    float brightness;     // 1, or less behind the menu
};
// Draws a frame: black, the picture in `look.rect`, then `overlay`
// (ImGui's draw data, recorded into the same command buffer) and presents
// it. False when nothing could be drawn (a minimised window).
bool draw(const Look& look, const std::function<void(VkCommandBuffer)>& overlay);
// The window changed size: the swapchain follows at the next draw.
void resized();
// The window's size in pixels, as the swapchain has it.
void outputSize(int& w, int& h);

// For ImGui's Vulkan backend.
uint32_t imageCount();
VkFormat format();
VkQueue queue();
std::mutex& queueMutex();

} // namespace ps2::video::vulkan

#endif
