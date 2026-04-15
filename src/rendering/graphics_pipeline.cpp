#include "graphics_pipeline.hpp"
#include "vulkan/vulkan.hpp"
#include "render.hpp"

// if using clang extension in VSCode, this library and the related function
// are flagged as 'not found', but it will still work when building CMake.
#include <array>
#include <readfile.hpp>
#include <cassert>

namespace {
using DynamicStates = std::array<vk::DynamicState, 2>;

const std::string kSrcDir = "/home/arboivin/alix-baque/maison/projets";
const std::string kAppDataDir = kSrcDir + "/voxel-engine/appdata";

struct ColorBlendInfo {
    vk::PipelineColorBlendAttachmentState attachment_state;
    vk::PipelineColorBlendStateCreateInfo create_info;
};

struct VertexInputState {
    vk::PipelineVertexInputStateCreateInfo create_info;
    std::array<vk::VertexInputAttributeDescription, 2> attribute_description;
    vk::VertexInputBindingDescription binding_description;
};

struct ViewportInfo {
    vk::Viewport viewport;
    vk::Rect2D scissor;
    vk::PipelineViewportStateCreateInfo create_info;
};

DynamicStates buildDynamicStates() noexcept {
    return {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
}

vk::PipelineRasterizationStateCreateInfo buildRasterizerInfo() noexcept {
    vk::PipelineRasterizationStateCreateInfo rasterizer_info = {};
    rasterizer_info.depthClampEnable = VK_FALSE;
    rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_info.polygonMode = vk::PolygonMode::eFill;
    rasterizer_info.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer_info.frontFace = vk::FrontFace::eClockwise;
    rasterizer_info.depthBiasEnable = VK_FALSE;
    rasterizer_info.lineWidth = 1.0f;

    return rasterizer_info;
}

vk::PipelineMultisampleStateCreateInfo buildMultisampleInfo() noexcept {
    vk::PipelineMultisampleStateCreateInfo multisample_info = {};
    multisample_info.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisample_info.sampleShadingEnable = VK_FALSE;

    return multisample_info;
}

ColorBlendInfo buildColorBlendInfo() noexcept {
    ColorBlendInfo info;
    info.attachment_state.blendEnable = VK_FALSE;
    info.attachment_state.colorWriteMask = vk::ColorComponentFlagBits::eR
        | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;

    info.create_info.logicOpEnable = VK_FALSE;
    info.create_info.logicOp = vk::LogicOp::eCopy;
    info.create_info.attachmentCount = 1;
    info.create_info.pAttachments = &info.attachment_state;
    return info;
}

VertexInputState buildVertexInputState() noexcept {
    VertexInputState info;
    info.binding_description = rendering::Vertex::bindingDescription();
    info.attribute_description = rendering::Vertex::attributeDescription();

    info.create_info.vertexAttributeDescriptionCount = info.attribute_description.size();
    info.create_info.pVertexAttributeDescriptions = info.attribute_description.data();
    info.create_info.vertexBindingDescriptionCount = 1;
    info.create_info.pVertexBindingDescriptions = &info.binding_description;

    return info;
}

ViewportInfo buildViewportInfo(vk::Extent2D extent) noexcept {
    ViewportInfo info;
    info.viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                          static_cast<float>(extent.height), 0.0f, 1.0f};
    info.scissor = vk::Rect2D{vk::Offset2D{0, 0}, extent};
    info.create_info.viewportCount = 1;
    info.create_info.pViewports = &info.viewport;
    info.create_info.scissorCount = 1;
    info.create_info.pScissors = &info.scissor;

    return info;
}
}  // namespace

namespace rendering::internal_graphics_pipeline {
using ShaderStageInfo = std::array<vk::PipelineShaderStageCreateInfo, 2>;

void GraphicsPipeline::createLayout(vk::Device& device) noexcept {
    vk::PipelineLayoutCreateInfo layout_info = {};
    layout_info.setLayoutCount = 0;
    layout_info.pushConstantRangeCount = 0;

    layout_ = device.createPipelineLayout(layout_info);
}

void GraphicsPipeline::createShaderModule(vk::Device& device) noexcept {
    vk::ShaderModuleCreateInfo module_info = {};
    auto code = readFile(kAppDataDir + "/slang.spv");

    module_info.codeSize = code.size() * sizeof(char);
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    shader_module_ = device.createShaderModule(module_info);
}

ShaderStageInfo GraphicsPipeline::buildShaderStageInfo() const noexcept {
    vk::PipelineShaderStageCreateInfo vert_stage_info = {};
    vk::PipelineShaderStageCreateInfo frag_stage_info = {};

    vert_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
    vert_stage_info.module = shader_module_;
    vert_stage_info.pName = "vertMain";

    frag_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
    frag_stage_info.module = shader_module_;
    frag_stage_info.pName = "fragMain";

    return {vert_stage_info, frag_stage_info};
}

void GraphicsPipeline::create(vk::Device& device,
                              const internal_swapchain::Swapchain& swapchain) noexcept {
    VertexInputState vertex_input_info = buildVertexInputState();
    vk::PipelineInputAssemblyStateCreateInfo assembly_info;
    auto dynamic_states = buildDynamicStates();
    vk::PipelineDynamicStateCreateInfo dyn_state_info;
    ViewportInfo viewport_info = buildViewportInfo(swapchain.extent());
    auto rasterizer_info = buildRasterizerInfo();
    vk::PipelineMultisampleStateCreateInfo multisample_info = buildMultisampleInfo();
    ColorBlendInfo color_blend_info = buildColorBlendInfo();
    vk::PipelineRenderingCreateInfo rendering_info;
    vk::GraphicsPipelineCreateInfo pipeline_info;

    createShaderModule(device);
    auto shader_stage_info = buildShaderStageInfo();
    createLayout(device);

    assembly_info.topology = vk::PrimitiveTopology::eTriangleList;

    dyn_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dyn_state_info.pDynamicStates = dynamic_states.data();

    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swapchain.format().format;

    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = shader_stage_info.size();
    pipeline_info.pStages = shader_stage_info.data();
    pipeline_info.pVertexInputState = &vertex_input_info.create_info;
    pipeline_info.pInputAssemblyState = &assembly_info;
    pipeline_info.pViewportState = &viewport_info.create_info;
    pipeline_info.pRasterizationState = &rasterizer_info;
    pipeline_info.pMultisampleState = &multisample_info;
    pipeline_info.pColorBlendState = &color_blend_info.create_info;
    pipeline_info.pDynamicState = &dyn_state_info;
    pipeline_info.layout = layout_;
    pipeline_info.renderPass = nullptr;

    auto result = device.createGraphicsPipeline(nullptr, pipeline_info);
    assert(result.has_value());
    pipeline_ = result.value;
}

void GraphicsPipeline::destroy(vk::Device& device) noexcept {
    if (pipeline_ != nullptr)
        device.destroyPipeline(pipeline_);
    if (layout_ != nullptr)
        device.destroyPipelineLayout(layout_);
    if (shader_module_ != nullptr)
        device.destroyShaderModule(shader_module_);
    pipeline_ = nullptr;
    layout_ = nullptr;
    shader_module_ = nullptr;
}

void GraphicsPipeline::bind(const vk::CommandBuffer& buffer) const noexcept {
    buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
}
}  // namespace rendering::internal_graphics_pipeline
