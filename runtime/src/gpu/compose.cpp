#include "gpu/compose.h"

#include "gpu/device.h"

#include "ps2n_spirv.h"

#include <algorithm>

namespace ps2::gpu {

namespace {

VkDevice dev() { return device().handle; }

VkShaderModule shaderModule(const uint32_t* code, size_t bytes) {
    auto info = make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev(), &info, nullptr, &m);
    return m;
}

} // namespace

bool Composer::init(VkFormat format) {
    shutdown();
    // One combined image sampler: the picture.
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    auto setInfo = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(dev(), &setInfo, nullptr, &setLayout_);
    const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24};
    auto layoutInfo = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    vkCreatePipelineLayout(dev(), &layoutInfo, nullptr, &layout_);
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
    auto poolInfo = make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 32;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    vkCreateDescriptorPool(dev(), &poolInfo, nullptr, &pool_);
    for (int i = 0; i < 2; ++i) {
        auto sampler = make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        sampler.magFilter = sampler.minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(dev(), &sampler, nullptr, i ? &linear_ : &nearest_);
    }
    if (!setLayout_ || !layout_ || !pool_ || !nearest_ || !linear_) return false;

    const VkShaderModule vs = shaderModule(spirv::present_vert, sizeof spirv::present_vert);
    const VkShaderModule fs = shaderModule(spirv::present_frag, sizeof spirv::present_frag);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (size_t i = 0; i < 2; ++i) {
        stages[i] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    auto vertexInput = make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    auto multisample = make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    auto rendering = make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    auto info = make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering);
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_;
    const VkResult r = vkCreateGraphicsPipelines(dev(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    vkDestroyShaderModule(dev(), vs, nullptr);
    vkDestroyShaderModule(dev(), fs, nullptr);
    if (r != VK_SUCCESS) pipeline_ = VK_NULL_HANDLE;
    return pipeline_ != VK_NULL_HANDLE;
}

void Composer::shutdown() {
    if (!dev()) return;
    if (pipeline_) vkDestroyPipeline(dev(), pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(dev(), layout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(dev(), setLayout_, nullptr);
    if (pool_) vkDestroyDescriptorPool(dev(), pool_, nullptr);   // and every set from it
    if (nearest_) vkDestroySampler(dev(), nearest_, nullptr);
    if (linear_) vkDestroySampler(dev(), linear_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    nearest_ = linear_ = VK_NULL_HANDLE;
}

bool Composer::bind(ComposeSource& s, VkImageView view, VkImageLayout layout, int width, int height) {
    s.width = width;
    s.height = height;
    if (s.view == view && s.sets[0]) return true;
    release(s);
    for (size_t i = 0; i < 2; ++i) {
        auto alloc = make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        alloc.descriptorPool = pool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(dev(), &alloc, &s.sets[i]) != VK_SUCCESS) return false;
        const VkDescriptorImageInfo image{i ? linear_ : nearest_, view, layout};
        auto write = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = s.sets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(dev(), 1, &write, 0, nullptr);
    }
    s.view = view;
    return true;
}

void Composer::release(ComposeSource& s) {
    for (VkDescriptorSet& set : s.sets) {
        if (set && pool_) vkFreeDescriptorSets(dev(), pool_, 1, &set);
        set = VK_NULL_HANDLE;
    }
    s.view = VK_NULL_HANDLE;
}

void Composer::draw(VkCommandBuffer cmd, const ComposeSource& s, int x, int y, int w, int h, uint32_t clipW, uint32_t clipH,
                    Filter filter, float brightness) const {
    if (!s.sets[0] || w <= 0 || h <= 0 || s.width <= 0 || s.height <= 0) return;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, static_cast<int>(clipW)), y1 = std::min(y + h, static_cast<int>(clipH));
    if (x1 <= x0 || y1 <= y0) return;
    const VkViewport vp{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    const VkRect2D scissor{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &s.sets[filter == Filter::Nearest ? 0 : 1], 0, nullptr);
    struct {
        float size[2];
        float scale[2];
        uint32_t sharp;
        float brightness;
    } params{{static_cast<float>(s.width), static_cast<float>(s.height)},
             {static_cast<float>(w) / static_cast<float>(s.width), static_cast<float>(h) / static_cast<float>(s.height)},
             filter == Filter::Sharp ? 1u : 0u,
             brightness};
    static_assert(sizeof params == 24);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof params, &params);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace ps2::gpu
