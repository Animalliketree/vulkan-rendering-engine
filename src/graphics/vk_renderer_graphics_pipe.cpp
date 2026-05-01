/* Copyright 2026 Alix Boivin */

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include "../graphics/vulkan_renderer.hpp"

namespace {
constexpr char kProjectRoot[] =
"/home/arboivin/alix-baque/maison/projets/voxel-engine";

const char kShaderFile[] = "/appdata/graphics.spv";

std::vector<char> readFile(const std::string file_name) {
    assert(!file_name.empty());
    std::ifstream ifs;
    ifs.open(file_name, std::ios::ate | std::ios::binary);
    assert(ifs.is_open());

    std::vector<char> buffer(static_cast<uint32_t>(ifs.tellg()));

    ifs.seekg(0, std::ios::beg);
    ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    ifs.close();

    return buffer;
}
}  // namespace

namespace graphics::vk_renderer {
VkShaderModule VulkanRenderer::createShaderModule(
        const std::vector<char>& code) noexcept {
    VkShaderModule mod;

    const VkShaderModuleCreateInfo module_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = code.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    assert(vkCreateShaderModule(device_, &module_info, nullptr, &mod)
        == VK_SUCCESS);
    return mod;
}

VkPipelineLayout VulkanRenderer::createGraphicsPipelineLayout() noexcept {
    VkPipelineLayout layout;

    const VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout_,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };
    assert(vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout)
        == VK_SUCCESS);
    return layout;
}

void VulkanRenderer::createGraphicsPipeline() noexcept {
    const std::vector<char> code =
        readFile(std::string(kProjectRoot) + "/" + kShaderFile);
    const VkShaderModule shader_module = createShaderModule(code);

    const auto binding_description = Vertex::getBindingDescription();
    const auto attribute_descriptions = Vertex::getAttributeDescription();
    const VkViewport viewport{0.0f, 0.0f,
                        static_cast<float>(swapchain_.extent.width),
                        static_cast<float>(swapchain_.extent.height),
                        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, swapchain_.extent};

    std::vector<VkPipelineShaderStageCreateInfo> stage_info;
    stage_info.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shader_module, .pName = "vertMain",
        .pSpecializationInfo = nullptr
    });
    stage_info.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shader_module, .pName = "fragMain",
        .pSpecializationInfo = nullptr
    });
    const VkPipelineVertexInputStateCreateInfo vertex_input_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount = attribute_descriptions.size(),
        .pVertexAttributeDescriptions = attribute_descriptions.data()
    };
    constexpr VkPipelineInputAssemblyStateCreateInfo assembly_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    const std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    const VkPipelineDynamicStateCreateInfo dyn_state_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()
    };
    const VkPipelineViewportStateCreateInfo viewport_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor
    };
    constexpr VkPipelineRasterizationStateCreateInfo rasterizer_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp = 0,
        .depthBiasSlopeFactor = 0,
        .lineWidth = 1.0f
    };
    constexpr VkPipelineMultisampleStateCreateInfo multisample_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };
    constexpr VkPipelineColorBlendAttachmentState color_blend_attach{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = {},
        .dstColorBlendFactor = {},
        .colorBlendOp = VK_BLEND_OP_ZERO_EXT,
        .srcAlphaBlendFactor = {},
        .dstAlphaBlendFactor = {},
        .alphaBlendOp = VK_BLEND_OP_ZERO_EXT,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    const VkPipelineColorBlendStateCreateInfo color_blend_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attach,
        .blendConstants = {0.0f}
    };
    constexpr VkPipelineDepthStencilStateCreateInfo depth_stencil_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr, .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {}, .back = {},
        .minDepthBounds = 0,
        .maxDepthBounds = 0
    };
    const VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = {},
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain_.format.format,
        .depthAttachmentFormat = depth_image_.format,
        .stencilAttachmentFormat = {}
    };

    graphics_pipeline_.layout = createGraphicsPipelineLayout();

    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info, .flags = 0,
        .stageCount = static_cast<uint32_t>(stage_info.size()),
        .pStages = stage_info.data(),
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &assembly_info,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_info,
        .pRasterizationState = &rasterizer_info,
        .pMultisampleState = &multisample_info,
        .pDepthStencilState = &depth_stencil_info,
        .pColorBlendState = &color_blend_info,
        .pDynamicState = &dyn_state_info,
        .layout = graphics_pipeline_.layout,
        .renderPass = nullptr, .subpass = {},
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1
    };

    assert(vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info,
                                     nullptr, &graphics_pipeline_.pipeline)
        == VK_SUCCESS);

    graphics_pipeline_.shader_module = shader_module;
}
}  // namespace graphics::vk_renderer
