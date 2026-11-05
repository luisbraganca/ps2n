// The pictures the Vulkan renderer hands to the window: three RGBA8 images,
// so that one can be on screen, one wait to be shown and the renderer draw
// the next into the third. They stay in VK_IMAGE_LAYOUT_GENERAL: the
// renderer's compute shaders write them, the window samples them.
//
// The renderer (the GS thread, or the guest thread in deterministic runs)
// takes a slot, draws into it and publishes it; its submission signals its
// timeline (pictureTimeline) when the picture is complete. The window takes
// the newest published picture and tells which of its submissions read it,
// so the renderer reuses a slot only after the window is done with it.
#pragma once

#include "gpu/resources.h"

#include <cstdint>

namespace ps2::gpu {

constexpr uint32_t kPictureSlots = 3;

// Renderer's side. A slot for the next picture, its image at least
// width x height (made again when the size changes): neither the one on
// screen nor the one waiting. Waits until the window finished reading it.
// `cmd` moves a new image to the GENERAL layout. -1 when it cannot be made.
int acquirePicture(VkCommandBuffer cmd, Commands& commands, uint32_t width, uint32_t height);
Image& pictureImage(int slot);
// The picture in `slot` is complete once the renderer's timeline reaches the
// value video::presentPicture is given with it.
void publishPicture(int slot);
void setPictureTimeline(VkSemaphore timeline);
VkSemaphore pictureTimeline();
// At shutdown, with the window closed: destroys the images.
void releasePictures();

// Window's side.
VkImageView pictureView(int slot);
void takePicture(int slot);   // the window shows it from now on
// The window's submission `value` on `timeline` reads the picture in `slot`.
void pictureShown(int slot, VkSemaphore timeline, uint64_t value);

} // namespace ps2::gpu
