/* Copyright 2026 Alix Boivin */

#include <fstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

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

void VulkanRenderer::createGraphicsPipeline() noexcept {
    std::vector<char> code =
        readFile(std::string(kProjectRoot) + "/" + kShaderFile);
    createShaderModule(code);

    vk::PipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
    vert_stage_info.module = graphics_pipeline_.shader_module;
    vert_stage_info.pName = "vertMain";

    vk::PipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
    frag_stage_info.module = graphics_pipeline_.shader_module;
    frag_stage_info.pName = "fragMain";

    vk::PipelineShaderStageCreateInfo stage_info[] = {vert_stage_info,
                                                      frag_stage_info};

    vk::VertexInputBindingDescription binding_description =
        Vertex::getBindingDescription();
    auto attribute_descriptions = Vertex::getAttributeDescription();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.vertexAttributeDescriptionCount =
        attribute_descriptions.size();
    vertex_input_info.pVertexAttributeDescriptions =
        attribute_descriptions.data();
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;

    vk::PipelineInputAssemblyStateCreateInfo assembly_info = {};
    assembly_info.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport viewport{0.0f, 0.0f,
                          static_cast<float>(swapchain_.extent.width),
                          static_cast<float>(swapchain_.extent.height),
                          0.0f, 1.0f};

    vk::Rect2D scissor{vk::Offset2D{0, 0}, swapchain_.extent};

    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dyn_state_info = {};
    dyn_state_info.dynamicStateCount =
        static_cast<uint32_t>(dynamic_states.size());
    dyn_state_info.pDynamicStates = dynamic_states.data();

    vk::PipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.viewportCount = 1;
    viewport_info.pViewports = &viewport;
    viewport_info.scissorCount = 1;
    viewport_info.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer_info =
        buildRasterizerInfo();

    vk::PipelineMultisampleStateCreateInfo multisample_info = {};
    multisample_info.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisample_info.sampleShadingEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState color_blend_attach = {};
    color_blend_attach.blendEnable = VK_FALSE;
    color_blend_attach.colorWriteMask = vk::ColorComponentFlagBits::eR
        | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo color_blend_info = {};
    color_blend_info.logicOpEnable = VK_FALSE;
    color_blend_info.logicOp = vk::LogicOp::eCopy;
    color_blend_info.attachmentCount = 1;
    color_blend_info.pAttachments = &color_blend_attach;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info{{},
        vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False};

    graphics_pipeline_.layout = createGraphicsPipelineLayout();

    vk::PipelineRenderingCreateInfo rendering_info = {};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swapchain_.format.format;
    rendering_info.depthAttachmentFormat = depth_image_.format;

    vk::GraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stage_info;
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
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    graphics_pipeline_.pipeline = device_.createGraphicsPipelines(
        nullptr, pipeline_info).value[0];
    assert(graphics_pipeline_.pipeline != nullptr);
}
}  // namespace graphics::vk_renderer
