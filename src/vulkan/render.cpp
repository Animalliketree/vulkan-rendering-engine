#include "render.hpp"
#include "vulkan/vulkan.hpp"

#include <array>
#include <fcntl.h>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

namespace {
#ifdef NDEBUG
const std::vector<char const*> kValidationLayers = {};
#else
const std::vector<char const*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
#endif

constexpr char kAppTitle[] = "Game";
constexpr char kEngineTitle[] = "Hephaestus";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kMaxFramesInFlight = 2;
const std::string kProjectRoot = "/home/arboivin/alix-baque/maison/projets/voxel-engine";
const std::string kShaderFile = kProjectRoot + "/appdata/slang.spv";

struct Vertex {
  glm::vec2 pos;
  glm::vec3 color;

  static vk::VertexInputBindingDescription getBindingDescription() {
    return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescription() {
    return {{{0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)},
             {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)}}};
  }
};

const std::vector<Vertex> vertices = {
  {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
  {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
  {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

bool validationLayersSupported() {
    std::vector<vk::LayerProperties> layer_props = vk::enumerateInstanceLayerProperties();

    for (const char* target : kValidationLayers) {
        bool layer_available = false;
        for (vk::LayerProperties layer : layer_props) {
            if (SDL_strcmp(target, layer.layerName) == 0) {
                layer_available = true;
                break;
            }
        }

        if (!layer_available) {
            return false;
        }
    }

    return true;
}

bool evaluatePhysicalDeviceProperties(vk::PhysicalDevice device) {
    assert(device != nullptr);

    vk::PhysicalDeviceProperties props = device.getProperties();

    if (props.apiVersion < VK_API_VERSION_1_4) return false;
    else return true;
}

bool evaluateDeviceExtensions(vk::PhysicalDevice device) {
    assert(device != nullptr);

    std::vector<vk::ExtensionProperties> extensions = device.enumerateDeviceExtensionProperties();

    for (const char* extension : kRequiredDeviceExtensions) {
        bool available = false;
        for (vk::ExtensionProperties prop : extensions) {
            if (strcmp(extension, prop.extensionName) == 0) {
                available = true;
                break;
            }
        }

        if (!available) {
            return false;
        }
    }

    return true;
}

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
}  // namespace

VulkanRenderer::VulkanRenderer(SDL_Window* window)  {
    assert(window != nullptr);

    createInstance();

    bool success = SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_);
    if (!success) throw std::runtime_error(
        "Failed to create Vulkan surface: " + std::to_string(*SDL_GetError()));

    selectPhysicalDevice();
    createLogicalDevice();
    createSwapchain(nullptr);
    createImageViews();
    createVertexBuffer();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() {
    device_.waitIdle();
    for (VkFence fence : draw_fences_) device_.destroyFence(fence);
    for (VkSemaphore semaphore : present_complete_semaphores_)
        device_.destroySemaphore(semaphore);
    for (VkSemaphore semaphore : render_finished_semaphores_)
        device_.destroySemaphore(semaphore);
    device_.destroyCommandPool(command_pool_);
    device_.destroyPipeline(graphics_pipeline_);
    device_.destroyPipelineLayout(graphics_pipeline_layout_);
    device_.destroyShaderModule(shader_module_);
    for (VkImageView view : swapchain_image_views_)
        device_.destroyImageView(view);
    device_.destroyBuffer(vertex_buffer_);
    device_.freeMemory(vertex_buffer_memory_);
    device_.destroySwapchainKHR(swapchain_);
    device_.destroy();
    SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    instance_.destroy();
}

vk::ApplicationInfo buildAppInfo() {
    vk::ApplicationInfo app_info = {};
    app_info.pApplicationName = kAppTitle;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = kEngineTitle;
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_4;
    return app_info;
}

/* Initialise Vulkan */
bool VulkanRenderer::createInstance() {
    bool layers_supported = validationLayersSupported();

    // Handle extensions
    uint32_t num_instance_extensions;
    const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(
        &num_instance_extensions);
    assert(instance_extensions != nullptr);

    uint32_t num_extensions;
    const char** extensions;
    if (layers_supported) {
        num_extensions = num_instance_extensions + 1;
        extensions = (const char**)(SDL_malloc(
            num_extensions * sizeof(const char*)));
        extensions[0] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        SDL_memcpy(&extensions[1], instance_extensions,
                num_instance_extensions * sizeof(const char*));
    } else {
        num_extensions = num_instance_extensions;
        extensions = (const char**)(SDL_malloc(
            num_extensions * sizeof(const char*)));
        SDL_memcpy(&extensions[0], instance_extensions,
            num_instance_extensions * sizeof(const char*));
    }

    vk::ApplicationInfo app_info = buildAppInfo();

    vk::InstanceCreateInfo instance_info = {};
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = num_extensions;
    instance_info.ppEnabledExtensionNames = extensions;
    if (layers_supported) {
        instance_info.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        instance_info.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        instance_info.enabledLayerCount = 0;
        instance_info.ppEnabledLayerNames = nullptr;
    }

    instance_ = vk::createInstance(instance_info);
    SDL_free(extensions);
    assert(instance_ != nullptr);

    return true;
}

uint32_t VulkanRenderer::getQueueFamilyIndex(vk::PhysicalDevice device) {
    assert(device != nullptr);

    std::vector<vk::QueueFamilyProperties> props = device.getQueueFamilyProperties();

    bool supports_graphics = false;
    for (uint32_t i = 0; i < props.size(); i++) {
        supports_graphics = SDL_Vulkan_GetPresentationSupport(instance_, device, i);
        if (supports_graphics) return i;
    }

    return UINT32_MAX;
}

bool VulkanRenderer::selectPhysicalDevice() {
    assert(instance_ != nullptr);

    std::vector<vk::PhysicalDevice> devices = instance_.enumeratePhysicalDevices();
    assert(devices.size() > 0);

    for (vk::PhysicalDevice device : devices) {
        if (!evaluatePhysicalDeviceProperties(device) ||
            !evaluateDeviceExtensions(device)) continue;

        uint32_t graphics_qf_idx = getQueueFamilyIndex(device);
        if (graphics_qf_idx == UINT32_MAX) continue;

        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
        vk::PhysicalDeviceVulkan13Features vk_1_3_features;
        vk_1_3_features.pNext = &dynamic_state_features;

        vk::PhysicalDeviceFeatures2 features;
        features.pNext = &vk_1_3_features;
        device.getFeatures2(&features);
        if (dynamic_state_features.extendedDynamicState == VK_FALSE
            || vk_1_3_features.dynamicRendering == VK_FALSE) continue;

        physical_device_ = device;
        graphics_qf_idx_ = graphics_qf_idx;
        return true;
    }

    throw std::runtime_error("Failed to find a suitable Vulkan physical device!");
}

bool VulkanRenderer::createLogicalDevice() {
  graphics_qf_idx_ = getQueueFamilyIndex(physical_device_);
  constexpr float kQueuePriority = 0.5f;
  vk::DeviceQueueCreateInfo queue_info = {};
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = graphics_qf_idx_;
  queue_info.pQueuePriorities = &kQueuePriority;

  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extended_features = {};
  extended_features.extendedDynamicState = VK_TRUE;

  vk::PhysicalDeviceVulkan13Features vk_1_3_features = {};
  vk_1_3_features.pNext = &extended_features;
  vk_1_3_features.dynamicRendering = VK_TRUE;
  vk_1_3_features.synchronization2 = VK_TRUE;

  vk::PhysicalDeviceFeatures2 features_2 = {};
  features_2.pNext = &vk_1_3_features;

  vk::DeviceCreateInfo device_info = {};
  device_info.pNext = &features_2;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledExtensionCount = static_cast<uint32_t>(
      kRequiredDeviceExtensions.size());
  device_info.ppEnabledExtensionNames =
      kRequiredDeviceExtensions.data();

  vk::Result r = physical_device_.createDevice(&device_info, nullptr, &device_);
  assert(r == vk::Result::eSuccess);

  device_.getQueue(graphics_qf_idx_, 0, &graphics_queue_);

  return true;
}

/* Presentation stuff */
vk::SurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat() {
    std::vector<vk::SurfaceFormatKHR> formats = physical_device_.getSurfaceFormatsKHR(surface_);
    assert(formats.size() > 0);

    vk::Format desired_format = vk::Format::eB8G8R8A8Srgb;
    for (vk::SurfaceFormatKHR format : formats) {
        if (format.format == desired_format) return format;
    }

    return formats[0];
}

vk::PresentModeKHR VulkanRenderer::chooseSwapPresentMode() {
    std::vector<vk::PresentModeKHR> modes = physical_device_.getSurfacePresentModesKHR(surface_);
    assert(modes.size() > 0);

    vk::PresentModeKHR desired_mode = vk::PresentModeKHR::eMailbox;
    for (vk::PresentModeKHR mode : modes) {
        if (mode == desired_mode) return mode;
    }

    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR cap) {
    if (cap.currentExtent.width != UINT32_MAX)
        return cap.currentExtent;
    else return {
        std::clamp<uint32_t>(kWindowWidth, cap.minImageExtent.width,
                            cap.maxImageExtent.width),
        std::clamp<uint32_t>(kWindowHeight, cap.minImageExtent.height,
                            cap.maxImageExtent.height)
    };
}

bool VulkanRenderer::createSwapchain(vk::SwapchainKHR old_swapchain) {
    vk::SurfaceCapabilitiesKHR capabilities = physical_device_.getSurfaceCapabilitiesKHR(surface_);
    assert(capabilities != nullptr);

    swapchain_extent_ = chooseSwapExtent(capabilities);

    swapchain_format_ = chooseSwapSurfaceFormat();

    uint32_t min_images = capabilities.maxImageCount > 0
        ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
        : capabilities.minImageCount + 1;

    vk::SwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface = surface_;
    swapchain_info.minImageCount = min_images;
    swapchain_info.imageFormat = swapchain_format_.format;
    swapchain_info.imageColorSpace = swapchain_format_.colorSpace;
    swapchain_info.imageExtent = swapchain_extent_;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapchain_info.presentMode = chooseSwapPresentMode();
    swapchain_info.clipped = true;
    swapchain_info.oldSwapchain = old_swapchain;

    vk::Result r = device_.createSwapchainKHR(&swapchain_info, nullptr, &swapchain_);
    assert(r == vk::Result::eSuccess);

    device_.destroySwapchainKHR(old_swapchain, nullptr);

    swapchain_images_ = device_.getSwapchainImagesKHR(swapchain_);

    return true;
}

bool VulkanRenderer::createImageViews() {
    assert(swapchain_image_views_.empty());

    vk::ImageViewCreateInfo view_info = {};
    view_info.pNext = nullptr;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = swapchain_format_.format;
    view_info.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    view_info.components = {
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity
    };

    swapchain_image_views_.resize(swapchain_images_.size());
    for (uint32_t i = 0; i < swapchain_images_.size(); i++) {
        view_info.image = swapchain_images_[i];

        swapchain_image_views_[i] = device_.createImageView(view_info);
        assert(swapchain_image_views_[i] != nullptr);
    }

    return true;
}

bool VulkanRenderer::recreateSwapchain() {
    device_.waitIdle();

    // Wipe current swapchain
    for (vk::ImageView view : swapchain_image_views_)
        device_.destroyImageView(view);
    swapchain_image_views_.clear();

    createSwapchain(swapchain_);
    createImageViews();

    return true;
}

/* Graphics pipeline */
vk::ShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    assert(device_ != nullptr);

    vk::ShaderModuleCreateInfo module_info = {};
    module_info.codeSize = code.size() * sizeof(char);
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    vk::ShaderModule module = device_.createShaderModule(module_info);

    assert(module != nullptr);
    return module;
}

bool VulkanRenderer::createVertexBuffer() {
  assert(device_ != nullptr);

  vk::BufferCreateInfo buffer_info = {};
  buffer_info.size = sizeof(vertices[0]) * vertices.size();
  buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_info.sharingMode = vk::SharingMode::eExclusive;
  vertex_buffer_ = device_.createBuffer(buffer_info, nullptr);
  assert(vertex_buffer_ != nullptr);

  vk::MemoryRequirements mem_req = device_.getBufferMemoryRequirements(vertex_buffer_);

  vk::MemoryAllocateInfo mem_alloc_info = {};
  mem_alloc_info.allocationSize = mem_req.size;
  mem_alloc_info.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits,
      vk::MemoryPropertyFlagBits::eHostVisible
      | vk::MemoryPropertyFlagBits::eHostCoherent);
  
  vertex_buffer_memory_ = device_.allocateMemory(mem_alloc_info);
  assert(vertex_buffer_memory_ != nullptr);

  device_.bindBufferMemory(vertex_buffer_, vertex_buffer_memory_, 0);

  void* data = device_.mapMemory(vertex_buffer_memory_, 0, buffer_info.size);
  memcpy(data, vertices.data(), buffer_info.size);
  device_.unmapMemory(vertex_buffer_memory_);

  return true;
}

uint32_t VulkanRenderer::findMemoryType(uint32_t type_filter, vk::MemoryPropertyFlags prop_flags) {
  assert(physical_device_ != nullptr);

  vk::PhysicalDeviceMemoryProperties props = physical_device_.getMemoryProperties();

  for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
    if (type_filter & (1 << i) &&
        (props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
      return i;
  }

  throw std::runtime_error("Failed to find suitable memory type!");
}

vk::PipelineLayout VulkanRenderer::createGraphicsPipelineLayout() {
    assert(device_ != nullptr);

    vk::PipelineLayout layout;
    vk::PipelineLayoutCreateInfo layout_info = {};
    layout_info.setLayoutCount = 0;
    layout_info.pushConstantRangeCount = 0;

    layout = device_.createPipelineLayout(layout_info);
    assert(layout != nullptr);
    return layout;
}

vk::PipelineRasterizationStateCreateInfo buildRasterizerInfo() {
    vk::PipelineRasterizationStateCreateInfo info = {};
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = vk::PolygonMode::eFill;
    info.cullMode = vk::CullModeFlagBits::eBack;
    info.frontFace = vk::FrontFace::eClockwise;
    info.depthBiasEnable = VK_FALSE;
    info.lineWidth = 1.0f;

    return info;
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
    vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_extent_.width),
                          static_cast<float>(swapchain_extent_.height), 0.0f, 1.0f};

    vk::Rect2D scissor{vk::Offset2D{0, 0}, swapchain_extent_};

    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dyn_state_info = {};
    dyn_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dyn_state_info.pDynamicStates = dynamic_states.data();

    vk::PipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.viewportCount = 1;
    viewport_info.pViewports = &viewport;
    viewport_info.scissorCount = 1;
    viewport_info.pScissors = &scissor;

    // Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer_info = buildRasterizerInfo();

    // Multisampling
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
    rendering_info.pColorAttachmentFormats = &swapchain_format_.format;

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

/* Drawing */
void VulkanRenderer::createCommandPool() {
    assert(device_ != nullptr);

    vk::CommandPoolCreateInfo pool_info = {};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = graphics_qf_idx_;

    command_pool_ = device_.createCommandPool(pool_info);
    assert(command_pool_ != nullptr);
}

void VulkanRenderer::createCommandBuffers() {
    assert(device_ != nullptr && command_buffers_.empty());

    vk::CommandBufferAllocateInfo alloc_info = {};
    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = kMaxFramesInFlight;

    command_buffers_ = device_.allocateCommandBuffers(alloc_info);
    assert(command_buffers_.size() == kMaxFramesInFlight);
}

void VulkanRenderer::transitionImageLayout(uint32_t image_index, vk::ImageLayout old_layout,
        vk::ImageLayout new_layout, vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask, vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask) {
    vk::ImageMemoryBarrier2 barrier = {};
    barrier.srcStageMask = src_stage_mask;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstStageMask = dst_stage_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_images_[image_index];
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::DependencyInfo dependency_info = {};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;
    command_buffers_[frame_index_].pipelineBarrier2(&dependency_info);
}

bool VulkanRenderer::recordCommandBuffer(uint32_t image_index) {
    vk::CommandBufferBeginInfo begin_info = {};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    vk::Result result = command_buffers_[frame_index_].begin(&begin_info);
    assert(result == vk::Result::eSuccess);

    transitionImageLayout(
        image_index,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vk::ClearValue clear_value;
    clear_value.color = {0.0f, 0.0f, 0.0f, 1.0f};
    vk::RenderingAttachmentInfo attachment_info = {};
    attachment_info.imageView = swapchain_image_views_[image_index],
    attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
    attachment_info.loadOp = vk::AttachmentLoadOp::eClear,
    attachment_info.storeOp = vk::AttachmentStoreOp::eStore,
    attachment_info.clearValue = clear_value;

    vk::RenderingInfo rendering_info = {};
    rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain_extent_};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &attachment_info;
    command_buffers_[frame_index_].beginRendering(&rendering_info);

    command_buffers_[frame_index_].bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                graphics_pipeline_);
    vk::DeviceSize vertex_buffer_offsets[] = {0};
    command_buffers_[frame_index_].bindVertexBuffers(0, 1, &vertex_buffer_, vertex_buffer_offsets);

    vk::Viewport viewport = {0.0f, 0.0f,
        static_cast<float>(swapchain_extent_.width),
        static_cast<float>(swapchain_extent_.height), 0.0f, 1.0f};

    vk::Rect2D scissor = {vk::Offset2D{0, 0}, swapchain_extent_};

    command_buffers_[frame_index_].setViewport(0, 1, &viewport);
    command_buffers_[frame_index_].setScissor(0, 1, &scissor);
    command_buffers_[frame_index_].draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    command_buffers_[frame_index_].endRendering();

    transitionImageLayout(
        image_index,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe);

    vkEndCommandBuffer(command_buffers_[frame_index_]);
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    assert(present_complete_semaphores_.empty()
            && render_finished_semaphores_.empty()
            && draw_fences_.empty());
    vk::SemaphoreCreateInfo semaphore_info = {};

    vk::FenceCreateInfo fence_info = {};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    render_finished_semaphores_.resize(swapchain_images_.size());
    for (size_t i = 0; i < swapchain_images_.size(); i++) {
        render_finished_semaphores_[i] = device_.createSemaphore(semaphore_info);
        assert(render_finished_semaphores_[i] != nullptr);
    }

    present_complete_semaphores_.resize(kMaxFramesInFlight);
    draw_fences_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++) {
        present_complete_semaphores_[i] = device_.createSemaphore(semaphore_info);
        draw_fences_[i] = device_.createFence(fence_info);
        assert(present_complete_semaphores_[i] != nullptr
               && draw_fences_[i] != nullptr);
    }

    return true;
}

bool VulkanRenderer::drawFrame() {
    assert(device_ != nullptr);

    vk::Result result = device_.waitForFences(1, &draw_fences_[frame_index_],
                                        VK_TRUE, UINT64_MAX);
    assert(result == vk::Result::eSuccess);

    uint32_t image_index;
    result = device_.acquireNextImageKHR(swapchain_, UINT64_MAX,
                            present_complete_semaphores_[frame_index_], nullptr,
                            &image_index);
    switch (result) {
        case vk::Result::eErrorOutOfDateKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuboptimalKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("Failed to acquire next image!");
    }

    result = device_.resetFences(1, &draw_fences_[frame_index_]);
    assert(result == vk::Result::eSuccess);

    command_buffers_[frame_index_].reset();
    recordCommandBuffer(image_index);

    vk::PipelineStageFlags wait_dst_stage_mask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo = {};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &present_complete_semaphores_[frame_index_];
    submitInfo.pWaitDstStageMask = &wait_dst_stage_mask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffers_[frame_index_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &render_finished_semaphores_[image_index];

    result = graphics_queue_.submit(1, &submitInfo, draw_fences_[frame_index_]);

    vk::PresentInfoKHR present_info = {};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index;

    result = graphics_queue_.presentKHR(&present_info);
    switch (result) {
        case vk::Result::eErrorOutOfDateKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuboptimalKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("Failed to present image to queue!");
    }

    if (framebuffer_resized_) {
        framebuffer_resized_ = false;
        recreateSwapchain();
    }

    frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
    return true;
}
