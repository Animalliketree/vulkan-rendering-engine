/* Copyright 2026 Alix Boivin */

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "../graphics/vulkan_renderer.hpp"
#include "vulkan/vulkan.hpp"

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

vk::PipelineRasterizationStateCreateInfo buildRasterizerInfo() {
    vk::PipelineRasterizationStateCreateInfo info = {};
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = vk::PolygonMode::eFill;
    info.cullMode = vk::CullModeFlagBits::eBack;
    info.frontFace = vk::FrontFace::eCounterClockwise;
    info.depthBiasEnable = VK_FALSE;
    info.lineWidth = 1.0f;

    return info;
}
}  // namespace

namespace graphics::vk_renderer {
void
VulkanRenderer::createShaderModule(const std::vector<char>& code) noexcept {
    assert(device_ != nullptr);

    vk::ShaderModuleCreateInfo module_info = {};
    module_info.codeSize = code.size() * sizeof(char);
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    graphics_pipeline_.shader_module = device_.createShaderModule(module_info);

    assert(graphics_pipeline_.shader_module != nullptr);
}

vk::PipelineLayout VulkanRenderer::createGraphicsPipelineLayout() noexcept {
    assert(device_ != nullptr);

    vk::PipelineLayout layout;
    vk::PipelineLayoutCreateInfo layout_info = {};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_set_layout_;
    layout_info.pushConstantRangeCount = 0;

    layout = device_.createPipelineLayout(layout_info);
    assert(layout != nullptr);
    return layout;
}

std::vector<vk::PipelineShaderStageCreateInfo>
buildShaderStageInfo(vk::ShaderModule module) noexcept {
    assert(module != nullptr);

    vk::PipelineShaderStageCreateInfo vert_stage_info{{},
        vk::ShaderStageFlagBits::eVertex, module, "vertMain"};

    vk::PipelineShaderStageCreateInfo frag_stage_info{{},
        vk::ShaderStageFlagBits::eFragment, module, "fragMain"};

    return {vert_stage_info, frag_stage_info};
}

void VulkanRenderer::createGraphicsPipeline() noexcept {
    std::vector<char> code =
        readFile(std::string(kProjectRoot) + "/" + kShaderFile);
    createShaderModule(code);

    auto stage_info = buildShaderStageInfo(graphics_pipeline_.shader_module);

    auto binding_description = Vertex::getBindingDescription();
    auto attribute_descriptions = Vertex::getAttributeDescription();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{{}, 1,
        &binding_description, attribute_descriptions.size(),
        attribute_descriptions.data()};

    vk::PipelineInputAssemblyStateCreateInfo assembly_info{{},
        vk::PrimitiveTopology::eTriangleList};

    vk::Viewport viewport{0.0f, 0.0f,
                          static_cast<float>(swapchain_.extent.width),
                          static_cast<float>(swapchain_.extent.height),
                          0.0f, 1.0f};

    vk::Rect2D scissor{{0, 0}, swapchain_.extent};

    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dyn_state_info{{},
        static_cast<uint32_t>(dynamic_states.size()), dynamic_states.data()};

    vk::PipelineViewportStateCreateInfo viewport_info{{},
        1, &viewport, 1, &scissor};

    auto rasterizer_info = buildRasterizerInfo();

    vk::PipelineMultisampleStateCreateInfo multisample_info{{},
        vk::SampleCountFlagBits::e1, vk::False};

    vk::PipelineColorBlendAttachmentState color_blend_attach{vk::False};
    color_blend_attach.colorWriteMask = vk::ColorComponentFlagBits::eR
        | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo color_blend_info{{},
        vk::False, vk::LogicOp::eCopy, 1, &color_blend_attach};

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info{{},
        vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False};

    graphics_pipeline_.layout = createGraphicsPipelineLayout();

    vk::PipelineRenderingCreateInfo rendering_info{{},
        1, &swapchain_.format.format, depth_image_.format};

    vk::GraphicsPipelineCreateInfo pipeline_info = {};
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

    graphics_pipeline_.pipeline = device_.createGraphicsPipelines(
        nullptr, pipeline_info).value[0];
    assert(graphics_pipeline_.pipeline != nullptr);
}
}  // namespace graphics::vk_renderer
