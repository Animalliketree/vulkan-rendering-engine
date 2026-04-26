/* Copyright 2026 Alix Boivin */

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <volk.h>

#include "../graphics/vulkan_renderer.hpp"

namespace {
constexpr char kProjectRoot[] =
"/home/arboivin/alix-baque/maison/projets/voxel-engine";

const char kShaderFile[] = "/appdata/slang.spv";

std::vector<char> readFile(std::string file_name) {
    assert(!file_name.empty());
    std::ifstream ifs;
    ifs.open(file_name, std::ios::ate | std::ios::binary);
    assert(ifs.is_open());

    std::vector<char> buffer(static_cast<std::vector<char>::size_type>(
        ifs.tellg()));

    ifs.seekg(0, std::ios::beg);
    ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    ifs.close();

    return buffer;
}

VkPipelineRasterizationStateCreateInfo buildRasterizerInfo() {
    VkPipelineRasterizationStateCreateInfo info = {};
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = VK_CULL_MODE_BACK_BIT;
    info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info.depthBiasEnable = VK_FALSE;
    info.lineWidth = 1.0f;

    return info;
}

std::vector<VkPipelineShaderStageCreateInfo>
buildShaderStageInfo(VkShaderModule module) noexcept {
    assert(module != nullptr);

    VkPipelineShaderStageCreateInfo vert_stage_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = module,
        .pName = "vertMain",
        .pSpecializationInfo = nullptr};

    VkPipelineShaderStageCreateInfo frag_stage_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = module,
        .pName = "fragMain",
        .pSpecializationInfo = nullptr};

    return {vert_stage_info, frag_stage_info};
}
}  // namespace

namespace graphics::vk_renderer {
VkShaderModule VulkanRenderer::createShaderModule(
        const std::vector<char>& code) noexcept {
    VkShaderModuleCreateInfo module_info = {};
    module_info.codeSize = code.size() * sizeof(char);
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod;
    vkCreateShaderModule(device_, &module_info, nullptr, &mod);
    return mod;

    assert(graphics_pipeline_.shader_module != nullptr);
}

VkPipelineLayout VulkanRenderer::createGraphicsPipelineLayout() noexcept {
    VkPipelineLayout layout = nullptr;
    VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout_,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr};

    vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout);
    assert(layout != nullptr);
    return layout;
}

void VulkanRenderer::createGraphicsPipeline() noexcept {
    std::vector<char> code =
        readFile(std::string(kProjectRoot) + "/" + kShaderFile);
    graphics_pipeline_.shader_module = createShaderModule(code);

    auto stage_info = buildShaderStageInfo(graphics_pipeline_.shader_module);

    auto binding_description = Vertex::getBindingDescription();
    auto attribute_descriptions = Vertex::getAttributeDescription();

    VkPipelineVertexInputStateCreateInfo vertex_input_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount = attribute_descriptions.size(),
        .pVertexAttributeDescriptions = attribute_descriptions.data()};

    VkPipelineInputAssemblyStateCreateInfo assembly_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE};

    VkViewport viewport{0.0f, 0.0f,
                        static_cast<float>(swapchain_.extent.width),
                        static_cast<float>(swapchain_.extent.height),
                        0.0f, 1.0f};

    VkRect2D scissor{{0, 0}, swapchain_.extent};

    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                  VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dyn_state_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()};

    VkPipelineViewportStateCreateInfo viewport_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor};

    auto rasterizer_info = buildRasterizerInfo();

    VkPipelineMultisampleStateCreateInfo multisample_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE};

    VkPipelineColorBlendAttachmentState color_blend_attach{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = {},
        .dstColorBlendFactor = {},
        .colorBlendOp = VK_BLEND_OP_ZERO_EXT,
        .srcAlphaBlendFactor = {},
        .dstAlphaBlendFactor = {},
        .alphaBlendOp = VK_BLEND_OP_ZERO_EXT,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    VkPipelineColorBlendStateCreateInfo color_blend_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attach,
        .blendConstants = {0.0f}};

    VkPipelineDepthStencilStateCreateInfo depth_stencil_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0,
        .maxDepthBounds = 0
    };

    graphics_pipeline_.layout = createGraphicsPipelineLayout();

    VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = {},
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain_.format.format,
        .depthAttachmentFormat = depth_image_.format,
        .stencilAttachmentFormat = {}};

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = static_cast<uint32_t>(stage_info.size());
    pipeline_info.pStages = stage_info.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &assembly_info;
    pipeline_info.pViewportState = &viewport_info;
    pipeline_info.pRasterizationState = &rasterizer_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pColorBlendState = &color_blend_info;
    pipeline_info.pDepthStencilState = &depth_stencil_info;
    pipeline_info.pDynamicState = &dyn_state_info;
    pipeline_info.layout = graphics_pipeline_.layout;
    pipeline_info.renderPass = nullptr;

    vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info, nullptr,
                              &graphics_pipeline_.pipeline);
    assert(graphics_pipeline_.pipeline != nullptr);
}
}  // namespace graphics::vk_renderer
