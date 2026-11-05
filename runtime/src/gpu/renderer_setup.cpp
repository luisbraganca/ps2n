// The Vulkan renderer's set-up (renderer.h): its buffers, its descriptor
// set, its pipelines and the tables its shaders address GS memory with.
#include "gpu/renderer_internal.h"

#include "gpu/device.h"
#include "gpu/picture.h"
#include "gpu/shaders/display_layout.h"
#include "gpu/shaders/layout.h"
#include "log.h"

#include "ps2n_spirv.h"

#include <algorithm>
#include <cstring>

namespace ps2::gpu::renderer {

using namespace ps2::gs::detail;

namespace {

VkShaderModule shaderModule(const uint32_t* code, size_t bytes) {
    auto info = make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev(), &info, nullptr, &m);
    return m;
}

VkPipeline computePipeline(const uint32_t* code, size_t bytes) {
    const VkShaderModule m = shaderModule(code, bytes);
    auto stage = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = m;
    stage.pName = "main";
    auto info = make<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    info.stage = stage;
    info.layout = g.layout;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(dev(), VK_NULL_HANDLE, 1, &info, nullptr, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(dev(), m, nullptr);
    return p;
}

// The pixel pipeline: no attachments (the shader writes the targets), the
// whole render area, the GS's coordinates in a viewport of +-viewHalf.
VkPipeline drawPipeline(const uint32_t* frag, size_t fragBytes) {
    const VkShaderModule vs = shaderModule(spirv::gs_vert, sizeof spirv::gs_vert);
    const VkShaderModule fs = shaderModule(frag, fragBytes);
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
    auto blend = make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    auto rendering = make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
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
    info.layout = g.layout;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev(), VK_NULL_HANDLE, 1, &info, nullptr, &p) != VK_SUCCESS) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(dev(), vs, nullptr);
    vkDestroyShaderModule(dev(), fs, nullptr);
    return p;
}

bool createDescriptors() {
    const VkDescriptorBindingFlags images = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kImageSlots, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kPictureBindings, VK_SHADER_STAGE_ALL, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
    };
    const VkDescriptorBindingFlags flags[] = {images, 0, 0, 0, 0, 0, images, 0};
    auto flagInfo = make<VkDescriptorSetLayoutBindingFlagsCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    flagInfo.bindingCount = 8;
    flagInfo.pBindingFlags = flags;
    auto layoutInfo = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &flagInfo);
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 8;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(dev(), &layoutInfo, nullptr, &g.setLayout) != VK_SUCCESS) return false;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kImageSlots + kPictureBindings},
                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6}};
    auto poolInfo = make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(dev(), &poolInfo, nullptr, &g.pool) != VK_SUCCESS) return false;
    auto alloc = make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    alloc.descriptorPool = g.pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &g.setLayout;
    if (vkAllocateDescriptorSets(dev(), &alloc, &g.set) != VK_SUCCESS) return false;
    const Buffer* buffers[] = {&g.mirror, &g.tables, &g.states, &g.prims, &g.upload, &g.control};
    const uint32_t bindingOf[] = {1, 2, 3, 4, 5, 7};
    std::array<VkDescriptorBufferInfo, 6> infos{};
    std::array<VkWriteDescriptorSet, 6> writes{};
    for (size_t i = 0; i < 6; ++i) {
        infos[i] = {buffers[i]->handle, 0, VK_WHOLE_SIZE};
        writes[i] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[i].dstSet = g.set;
        writes[i].dstBinding = bindingOf[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(dev(), 6, writes.data(), 0, nullptr);
    const VkPushConstantRange push{VK_SHADER_STAGE_ALL, 0, 128};
    auto pipelineLayout = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &g.setLayout;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &push;
    return vkCreatePipelineLayout(dev(), &pipelineLayout, nullptr, &g.layout) == VK_SUCCESS;
}

// gs_internal.h's tables, as the shaders read them (layout.h's T_*).
std::vector<uint32_t> tableWords() {
    std::vector<uint32_t> t(T_WORDS, 0);
    for (uint32_t z = 0; z < 2; ++z)
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                const uint32_t w = g_swizzle.c32[z][y][x];
                t[T_C32 + (z * 32 + y) * 64 + x] = w;
                t[T_INV32 + z * 2048 + w] = y * 64 + x;
            }
    for (uint32_t v = 0; v < 4; ++v)
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                const uint32_t h = g_swizzle.c16[v][y][x];
                t[T_C16 + (v * 64 + y) * 64 + x] = h;
                t[T_INV16 + v * 4096 + h] = y * 64 + x;
            }
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 128; ++x) t[T_C8 + y * 128 + x] = g_swizzle.c8[y][x];
    for (uint32_t y = 0; y < 128; ++y)
        for (uint32_t x = 0; x < 128; ++x) t[T_C4 + y * 128 + x] = g_swizzle.c4[y][x];
    for (uint32_t k = 0; k < 3; ++k) {
        for (uint32_t i = 0; i < 256; ++i) t[T_CLUT8 + k * 256 + i] = g_clutOffsets.t8[k][i];
        for (uint32_t i = 0; i < 512; ++i) t[T_CLUT4 + k * 512 + i] = g_clutOffsets.t4[k][i];
    }
    for (uint32_t b = 0; b < 256; ++b) {
        t[T_LOG2BEFORE + b] = g_log2Steps.before[b];
        t[T_LOG2START + b] = g_log2Steps.start[b];
    }
    return t;
}

void destroyAll() {
    if (!dev()) return;
    // The window's last frames may still read a picture, and wait on this
    // renderer's timeline for it: nobody submits by now.
    vkDeviceWaitIdle(dev());
    if (g.cmds.ready()) g.cmds.shutdown();
    if (g.drawPipelines[1] == g.drawPipelines[0]) g.drawPipelines[1] = VK_NULL_HANDLE;
    for (VkPipeline p : {g.drawPipelines[0], g.drawPipelines[1], g.resolve, g.expand, g.push, g.copy, g.display, g.deinterlace})
        if (p) vkDestroyPipeline(dev(), p, nullptr);
    if (g.layout) vkDestroyPipelineLayout(dev(), g.layout, nullptr);
    if (g.pool) vkDestroyDescriptorPool(dev(), g.pool, nullptr);
    if (g.setLayout) vkDestroyDescriptorSetLayout(dev(), g.setLayout, nullptr);
    for (Buffer* b : {&g.mirror, &g.tables, &g.states, &g.prims, &g.upload, &g.control, &g.readback}) destroy(*b);
    for (Image& i : g.history.images) destroy(i);
    for (Image& i : g.scratch) destroy(i);
    for (auto& t : g.targets) destroy(t->image);
    releasePictures();
    g = Renderer{};
}

} // namespace

bool start(int scale, std::string& why) {
    stop(true);   // a new scale: what the card drew goes back to GS memory, to be drawn on at the new one
    if (!available()) {
        why = status().reason.empty() ? "no graphics card is open" : status().reason;
        return false;
    }
    g.scale = std::clamp(scale, 1, 8);
    const VkPhysicalDeviceLimits& limits = device().properties.limits;
    g.renderSize = std::min({limits.maxFramebufferWidth, limits.maxFramebufferHeight, 32768u});
    g.viewHalf = static_cast<float>(std::min(device().maxViewport, 32768u) / 2);
    if (!g.cmds.init(*device().render, "GS", kSlots)) {
        why = "cannot make the renderer's command buffers";
        destroyAll();
        return false;
    }
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const bool buffers =
        createBuffer(g.mirror, VkDeviceSize(kPages) * kPageWords * 4,
                     storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Device, "GS memory") &&
        createBuffer(g.tables, VkDeviceSize(T_WORDS) * 4, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Device, "GS tables") &&
        createBuffer(g.states, VkDeviceSize(kSlots) * kMaxStates * STATE_WORDS * 4, storage, Memory::Stream, "GS states") &&
        createBuffer(g.prims, VkDeviceSize(kSlots) * kMaxPrims * PRIM_WORDS * 4, storage, Memory::Stream, "GS primitives") &&
        createBuffer(g.upload, VkDeviceSize(kSlots) * kUploadWords * 4, storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Memory::Stream,
                     "GS uploads") &&
        createBuffer(g.control, VkDeviceSize(kControlWords) * 4, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Device,
                     "deinterlacing") &&
        createBuffer(g.readback, VkDeviceSize(kPages) * kPageWords * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Memory::Readback,
                     "GS memory read back");
    if (!buffers || !createDescriptors()) {
        why = "cannot make the renderer's buffers";
        destroyAll();
        return false;
    }
    // Rounding as the guest thread does: toward zero where the card can,
    // doubles where it has them (the depth of triangles whose corners differ).
    const Device& d = device();
    if (d.rtz32 && d.float64 && d.rte64) g.drawPipelines[1] = drawPipeline(spirv::gs_frag_rtz_f64, sizeof spirv::gs_frag_rtz_f64);
    else if (d.rtz32) g.drawPipelines[1] = drawPipeline(spirv::gs_frag_rtz, sizeof spirv::gs_frag_rtz);
    if (d.float64) g.drawPipelines[0] = drawPipeline(spirv::gs_frag_rte_f64, sizeof spirv::gs_frag_rte_f64);
    else g.drawPipelines[0] = drawPipeline(spirv::gs_frag_rte, sizeof spirv::gs_frag_rte);
    if (!g.drawPipelines[1] && g.drawPipelines[0]) {
        g.drawPipelines[1] = g.drawPipelines[0];   // shared: destroyed once
        PS2_WARN("gpu", "the graphics card's shaders cannot round toward zero, as the EE does: some pixels may come out a step off "
                 "the software renderer's");
    }
    if (!d.float64)
        PS2_WARN("gpu", "the graphics card has no doubles in its shaders: the depth across a triangle may differ from the software "
                 "renderer's in its last bits");
    g.resolve = computePipeline(spirv::resolve_comp, sizeof spirv::resolve_comp);
    g.expand = computePipeline(spirv::expand_comp, sizeof spirv::expand_comp);
    g.push = computePipeline(spirv::push_comp, sizeof spirv::push_comp);
    g.copy = computePipeline(spirv::copy_comp, sizeof spirv::copy_comp);
    g.display = computePipeline(spirv::display_comp, sizeof spirv::display_comp);
    g.deinterlace = computePipeline(spirv::deinterlace_comp, sizeof spirv::deinterlace_comp);
    if (!g.drawPipelines[0] || !g.resolve || !g.expand || !g.push || !g.copy || !g.display || !g.deinterlace) {
        why = "cannot make the renderer's pipelines";
        destroyAll();
        return false;
    }
    for (uint32_t i = kImageSlots; i > 0; --i) g.freeSlots.push_back(i - 1);
    // The tables and the deinterlacer's control buffer, before anything reads them.
    const std::vector<uint32_t> tables = tableWords();
    std::memcpy(uploadWords(), tables.data(), tables.size() * 4);
    VkCommandBuffer cmd = g.cmds.current();
    const VkBufferCopy region{0, 0, tables.size() * 4};
    vkCmdCopyBuffer(cmd, g.upload.handle, g.tables.handle, 1, &region);
    vkCmdFillBuffer(cmd, g.control.handle, 0, VK_WHOLE_SIZE, 0);
    fullBarrier(cmd);
    g.cmds.wait(g.cmds.submit());
    g.uploadUsed = 0;
    g.primCount = g.stateWords = 0;
    g.pages.fill(PageState{});
    setPictureTimeline(g.cmds.timeline());
    g.started = true;
    return true;
}

void stop(bool readBack) {
    if (!g.started) {
        if (g.cmds.ready() || g.setLayout || g.mirror.handle) destroyAll();
        return;
    }
    if (readBack) syncAll();
    g.started = false;
    destroyAll();
}

} // namespace ps2::gpu::renderer
