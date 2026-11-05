// A picture drawn into a colour image, as the window shows it: scaled into
// a rectangle, filtered as PCSX2's Bilinear Filtering setting asks (nearest,
// smooth or sharp), dimmed behind the menu. The window draws with it into
// its swapchain (video/present_vk.cpp); without SDL in it, the tests draw
// with it into an image of their own.
#pragma once

#include "gpu/vk.h"

#include <array>
#include <cstdint>

namespace ps2::gpu {

enum class Filter : uint32_t { Nearest, Smooth, Sharp };

// A picture to draw, and the descriptor sets that sample it.
struct ComposeSource {
    VkImageView view = VK_NULL_HANDLE;
    int width = 0, height = 0;
    std::array<VkDescriptorSet, 2> sets{};   // nearest, bilinear
};

class Composer {
public:
    // For colour images of `format`.
    bool init(VkFormat format);
    void shutdown();   // after the card finished every draw
    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }
    // `view` (in `layout` when drawn) as a picture of width x height.
    bool bind(ComposeSource& s, VkImageView view, VkImageLayout layout, int width, int height);
    void release(ComposeSource& s);   // its sets; after the card finished the draws that used them
    // Inside a rendering instance whose image is clipW x clipH: the picture
    // in the rectangle (x, y, w, h), which may reach past the image.
    void draw(VkCommandBuffer cmd, const ComposeSource& s, int x, int y, int w, int h, uint32_t clipW, uint32_t clipH, Filter filter,
              float brightness) const;

private:
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler nearest_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
};

} // namespace ps2::gpu
