#include "vulkan_renderer.hpp"

#include <vulkan/vulkan.hpp>

#include <fstream>

namespace {
const std::string kProjectRoot = "/home/arboivin/alix-baque/maison/projets/voxel-engine";
const std::string kShaderFile = kProjectRoot + "/appdata/slang.spv";

std::vector<char> readFile(std::string file_name) {
    assert(!file_name.empty());
    std::ifstream ifs;
    ifs.open(file_name, std::ios::ate | std::ios::binary);
    assert(ifs.is_open());

    std::vector<char> buffer(static_cast<std::vector<char>::size_type>(ifs.tellg()));

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
vk::ShaderModule
VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    assert(device_ != nullptr);

    vk::ShaderModuleCreateInfo module_info = {};
    module_info.codeSize = code.size() * sizeof(char);
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    vk::ShaderModule module = device_.createShaderModule(module_info);

    assert(module != nullptr);
    return module;
}

vk::PipelineLayout VulkanRenderer::createGraphicsPipelineLayout() {
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

bool VulkanRenderer::createGraphicsPipeline() {
    // Shader Stages
    shader_module_ = createShaderModule(readFile(kShaderFile));

    vk::PipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
    vert_stage_info.module = shader_module_;
    vert_stage_info.pName = "vertMain";

    vk::PipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
    frag_stage_info.module = shader_module_;
    frag_stage_info.pName = "fragMain";

    vk::PipelineShaderStageCreateInfo stage_info[] = {vert_stage_info, frag_stage_info};

    // Inputs
    vk::VertexInputBindingDescription binding_description = Vertex::getBindingDescription();
    auto attribute_descriptions = Vertex::getAttributeDescription();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.vertexAttributeDescriptionCount = attribute_descriptions.size();
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;

    vk::PipelineInputAssemblyStateCreateInfo assembly_info = {};
    assembly_info.topology = vk::PrimitiveTopology::eTriangleList;

    // Viewport
    vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_.extent.width),
                          static_cast<float>(swapchain_.extent.height), 0.0f, 1.0f};

    vk::Rect2D scissor{vk::Offset2D{0, 0}, swapchain_.extent};

    std::vector<vk::DynamicState> dynamic_states = {vk::DynamicState::eViewport,
                                                    vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dyn_state_info = {};
    dyn_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dyn_state_info.pDynamicStates = dynamic_states.data();

    vk::PipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.viewportCount = 1;
    viewport_info.pViewports = &viewport;
    viewport_info.scissorCount = 1;
    viewport_info.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer_info = buildRasterizerInfo();

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

    graphics_pipeline_layout_ = createGraphicsPipelineLayout();

    vk::PipelineRenderingCreateInfo rendering_info = {};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swapchain_.format.format;

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
    pipeline_info.pDynamicState = &dyn_state_info;
    pipeline_info.layout = graphics_pipeline_layout_;
    pipeline_info.renderPass = nullptr;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    graphics_pipeline_ = device_.createGraphicsPipelines(nullptr, pipeline_info).value[0];
    assert(graphics_pipeline_ != nullptr);

    return true;
}
}  // namespace graphics::vk_renderer