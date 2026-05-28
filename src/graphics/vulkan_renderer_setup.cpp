#include <volk.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "vulkan_renderer.hpp"

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

constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR cap) noexcept {
    if (cap.currentExtent.width != UINT32_MAX)
        return cap.currentExtent;
    else return {
        std::clamp<uint32_t>(kWindowWidth, cap.minImageExtent.width,
                            cap.maxImageExtent.width),
        std::clamp<uint32_t>(kWindowHeight, cap.minImageExtent.height,
                            cap.maxImageExtent.height)
    };
}

VkSurfaceFormatKHR chooseFromList(const std::vector<VkSurfaceFormatKHR> fmts)
noexcept {
    assert(fmts.size() > 0);

    const VkFormat kDesiredFmt = VK_FORMAT_B8G8R8A8_SRGB;
    for (VkSurfaceFormatKHR format : fmts) {
        if (format.format == kDesiredFmt) return format;
    }

    return fmts[0];
}

VkPresentModeKHR chooseFromList(const std::vector<VkPresentModeKHR> modes)
noexcept {
    assert(modes.size() > 0);

    const VkPresentModeKHR kDesiredMode = VK_PRESENT_MODE_MAILBOX_KHR;
    for (VkPresentModeKHR mode : modes) {
        if (mode == kDesiredMode) return mode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
}  // namespace

namespace graphics::vk_renderer {
void VulkanRenderer::createDescriptorPool() noexcept {
    const VkDescriptorPoolSize size{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = kMaxFramesInFlight
    };
    const VkDescriptorPoolCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &size
    };

    assert(vkCreateDescriptorPool(device_, &create_info, nullptr,
                                  &descriptor_pool_) == VK_SUCCESS);
}

void VulkanRenderer::createDescriptorSetLayout() noexcept {
    constexpr VkDescriptorSetLayoutBinding ubo_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = nullptr
    };
    const VkDescriptorSetLayoutCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &ubo_binding
    };

    assert(vkCreateDescriptorSetLayout(device_, &create_info, nullptr,
                                       &descriptor_set_layout_) == VK_SUCCESS);
}

void VulkanRenderer::createDescriptorSets() noexcept {
    assert(descriptor_pool_ != nullptr);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight,
                                               descriptor_set_layout_);

    const VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = kMaxFramesInFlight,
        .pSetLayouts = layouts.data()
    };
    descriptor_sets_.resize(kMaxFramesInFlight);
    assert(vkAllocateDescriptorSets(device_, &alloc_info,
                                    descriptor_sets_.data()) == VK_SUCCESS);

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        const VkDescriptorBufferInfo buf_info{
            .buffer = uniform_buffers_[i].buffer,
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
        const VkWriteDescriptorSet write_info{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_sets_[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &buf_info,
            .pTexelBufferView = nullptr
        };

        vkUpdateDescriptorSets(device_, 1, &write_info, 0, nullptr);
    }

    assert(descriptor_sets_.size() == kMaxFramesInFlight);
}

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

void VulkanRenderer::createUniformBuffers() noexcept {
    if (!uniform_buffers_.empty()) {
        for (BufferHandle buf : uniform_buffers_) {
            vkDestroyBuffer(device_, buf.buffer, nullptr);
            vkUnmapMemory(device_, buf.memory);
            vkFreeMemory(device_, buf.memory, nullptr);
        }
        uniform_buffers_.clear();
        uniform_buffer_maps_.clear();
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        VkDeviceSize buf_size = sizeof(ShaderData);
        BufferHandle buf = createBuffer(buf_size,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        uniform_buffers_.emplace_back(std::move(buf));
        void* data;
        vkMapMemory(device_, uniform_buffers_[i].memory,
                    uniform_buffers_[i].offset, buf_size, 0, &data);
        uniform_buffer_maps_.emplace_back(std::move(data));
    }
}

void VulkanRenderer::createCommandPool() noexcept {
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_qf_idx_
    };
    assert(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_)
        == VK_SUCCESS);
}

void VulkanRenderer::createDepthResources() noexcept {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    const uint32_t queue_i = graphics_qf_idx_;

    if (depth_image_.image != nullptr) {
        vkDestroyImageView(device_, depth_image_.view, nullptr);
        vkFreeMemory(device_, depth_image_.memory, nullptr);
        vkDestroyImage(device_, depth_image_.image, nullptr);
    }

    depth_image_.format = findDesiredFormat(candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    const VkImageCreateInfo img_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_image_.format,
        .extent = {swapchain_.extent.width, swapchain_.extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_i,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    assert(vkCreateImage(device_, &img_info, nullptr, &depth_image_.image)
        == VK_SUCCESS);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device_, depth_image_.image, &mem_req);

    const VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    assert(vkAllocateMemory(device_, &alloc_info, nullptr,
                            &depth_image_.memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device_, depth_image_.image,
                             depth_image_.memory, 0) == VK_SUCCESS);
    depth_image_.view = createImageView(depth_image_.image,
                                        depth_image_.format,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanRenderer::createCommandBuffers() noexcept {
    assert(command_buffers_.empty());

    command_buffers_.resize(kMaxFramesInFlight);
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(command_buffers_.size())
    };

    assert(vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data())
        == VK_SUCCESS);
}

void VulkanRenderer::createSyncObjects() noexcept {
    assert(sem_present_done_.empty()
           && sem_render_done_.empty() && draw_fences_.empty());

    constexpr VkSemaphoreCreateInfo sem_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };
    constexpr VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (uint32_t i = 0; i < swapchain_.images.size(); i++) {
        VkSemaphore sem;
        assert(vkCreateSemaphore(device_, &sem_info, nullptr, &sem)
            == VK_SUCCESS);
        sem_render_done_.push_back(sem);
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        VkSemaphore sem;
        VkFence fence;
        assert(vkCreateSemaphore(device_, &sem_info, nullptr, &sem)
            == VK_SUCCESS);
        sem_present_done_.push_back(sem);
        assert(vkCreateFence(device_, &fence_info, nullptr, &fence)
            == VK_SUCCESS);
        draw_fences_.push_back(fence);
    }

    assert(sem_render_done_.size() == swapchain_.images.size());
    assert(sem_present_done_.size() == kMaxFramesInFlight);
}

BufferHandle VulkanRenderer::createBuffer(const VkDeviceSize size,
        const VkMemoryPropertyFlags props,
        const VkBufferUsageFlags usage) noexcept {
    BufferHandle buf;
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr
    };
    assert(vkCreateBuffer(device_, &info, nullptr, &buf.buffer) == VK_SUCCESS);

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device_, buf.buffer, &mem_req);

    const VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, props)
    };
    assert(vkAllocateMemory(device_, &alloc_info, nullptr, &buf.memory)
        == VK_SUCCESS);

    assert(vkBindBufferMemory(device_, buf.buffer, buf.memory, buf.offset)
        == VK_SUCCESS);
    return buf;
}

VkImageView VulkanRenderer::createImageView(const VkImage& img,
                                            const VkFormat fmt,
                                            const VkImageAspectFlags aspect)
noexcept {
    VkImageView view;

    const VkImageViewCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {aspect, 0, 1, 0, 1}
    };
    vkCreateImageView(device_, &info, nullptr, &view);
    return view;
}

void VulkanRenderer::createImageViews() noexcept {
    for (VkImageView view : swapchain_.image_views)
        vkDestroyImageView(device_, view, nullptr);
    swapchain_.image_views.clear();

    for (VkImage img : swapchain_.images) {
        swapchain_.image_views.push_back(createImageView(img,
                swapchain_.format.format, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    assert(swapchain_.image_views.size() == swapchain_.images.size());
}

void VulkanRenderer::createSwapchain(VkSwapchainKHR old_swapchain) noexcept {
    assert(surface_ != nullptr);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);
    const uint32_t min_images = caps.maxImageCount > 0
        ? std::min(caps.minImageCount + 1, caps.maxImageCount)
        : caps.minImageCount + 1;

    uint32_t num_fmts;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_fmts,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(num_fmts);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_fmts,
                                         fmts.data());

    uint32_t num_pms;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                              &num_pms, nullptr);
    std::vector<VkPresentModeKHR> pms(num_pms);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                              &num_pms, pms.data());

    const VkSurfaceFormatKHR format = chooseFromList(fmts);

    const VkSwapchainCreateInfoKHR swapchain_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface_,
        .minImageCount = min_images,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = chooseSwapExtent(caps),
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = chooseFromList(pms),
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain};

    assert(vkCreateSwapchainKHR(device_, &swapchain_info, nullptr,
                         &swapchain_.swapchain) == VK_SUCCESS);

    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);

    swapchain_.extent = swapchain_info.imageExtent;
    swapchain_.format = format;

    uint32_t num_imgs;
    vkGetSwapchainImagesKHR(device_, swapchain_.swapchain, &num_imgs, nullptr);
    swapchain_.images.resize(num_imgs);
    vkGetSwapchainImagesKHR(device_, swapchain_.swapchain, &num_imgs,
                            swapchain_.images.data());
}
}  // namespace graphics::vk_renderer
