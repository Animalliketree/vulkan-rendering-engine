#include "render.hpp"

#include <fcntl.h>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

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

const std::vector<char const*> kValidationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

constexpr uint32_t kMaxFramesInFlight = 2;

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

const char* kAppTitle = "Game";
const char* kEngineTitle = "Hephaestus";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

App::App(quill::Logger* logger) : logger_(logger) {
  createWindow();
  createInstance();

  bool success = SDL_Vulkan_CreateSurface(window_, instance_, NULL, &surface_);
  if (!success) {
    throw std::runtime_error(
      std::string("Failed to create Vulkan surface: ")
      + std::string(SDL_GetError()));
  }

  selectPhysicalDevice();
  createLogicalDevice();
  createSwapchain();
  createImageViews();
  createGraphicsPipeline();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();
}

App::~App() {
  for (VkFence fence : draw_fences_) vkDestroyFence(device_, fence, NULL);
  for (VkSemaphore semaphore : present_complete_semaphores_)
      vkDestroySemaphore(device_, semaphore, NULL);
  for (VkSemaphore semaphore : render_finished_semaphores_)
      vkDestroySemaphore(device_, semaphore, NULL);
  vkDestroyCommandPool(device_, command_pool_, NULL);
  vkDestroyPipeline(device_, graphics_pipeline_, NULL);
  vkDestroyPipelineLayout(device_, graphics_pipeline_layout_, NULL);
  vkDestroyShaderModule(device_, shader_module_, NULL);
  for (VkImageView view : swapchain_image_views_) {
    vkDestroyImageView(device_, view, NULL);
  }
  vkDestroySwapchainKHR(device_, swapchain_, NULL);
  vkDestroyDevice(device_, NULL);
  SDL_Vulkan_DestroySurface(instance_, surface_, NULL);
  vkDestroyInstance(instance_, NULL);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool App::createWindow() {
  SDL_SetAppMetadata(kAppTitle, "0.0.1", "");
  SDL_Init(SDL_INIT_VIDEO);

  window_ = SDL_CreateWindow(kAppTitle, kWindowWidth, kWindowHeight,
                             SDL_WINDOW_VULKAN);

  if (window_ == NULL) {
    quill::error(logger_, "Failed to create SDL window: {}", SDL_GetError());
    throw std::runtime_error("Failed to create SDL window");
  }

  return true;
}

/*
Initialise Vulkan
*/
bool App::checkValidationLayerSupport(const char* const* p_layers,
                                      uint32_t num_layers) {
  uint32_t num_layer_props;
  vkEnumerateInstanceLayerProperties(&num_layer_props, NULL);
  std::vector<VkLayerProperties> layer_props(num_layer_props);
  vkEnumerateInstanceLayerProperties(&num_layer_props, layer_props.data());

  bool all_layers_available = true;
  for (uint32_t i = 0; i < num_layers; i++) {
    bool layer_available = false;
    for (VkLayerProperties layer : layer_props) {
      if (SDL_strcmp(p_layers[i], layer.layerName) == 0) {
        layer_available = true;
        break;
      }
    }

    if (!layer_available) {
      quill::warning(logger_, "Missing layer: {}", p_layers[i]);
      all_layers_available = false;
      break;
    }
  }

  return all_layers_available;
}

bool App::createInstance() {
  std::vector<char const*> required_layers = {};
  if (kEnableValidationLayers) {
    required_layers.assign(kValidationLayers.begin(), kValidationLayers.end());
  }

  bool validation_layers_supported = checkValidationLayerSupport(
      required_layers.data(),
      required_layers.size());

  if (!validation_layers_supported) {
    throw std::runtime_error("Requested validation layers are not available");
  }

  // Handle extensions
  uint32_t num_instance_extensions;
  const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(
      &num_instance_extensions);
  if (instance_extensions == NULL) {
    throw std::runtime_error("Instance extensions should not be null");
  }

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = kAppTitle;
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = kEngineTitle;
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_4;

  uint32_t num_extensions = num_instance_extensions + 1;
  const char** extensions = (const char**)(SDL_malloc(
      num_extensions * sizeof(const char*)));
  extensions[0] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
  SDL_memcpy(&extensions[1], instance_extensions,
             num_instance_extensions * sizeof(const char*));

  VkInstanceCreateInfo instance_create_info = {};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pApplicationInfo = &app_info;
  instance_create_info.enabledExtensionCount = num_extensions;
  instance_create_info.ppEnabledExtensionNames = extensions;
  instance_create_info.enabledLayerCount = static_cast<uint32_t>(
      required_layers.size());
  instance_create_info.ppEnabledLayerNames = required_layers.data();

  VkResult result = vkCreateInstance(&instance_create_info, NULL, &instance_);
  SDL_free(extensions);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create VkInstance");

  return true;
}

bool App::selectPhysicalDevice() {
  uint32_t num_devices;
  vkEnumeratePhysicalDevices(instance_, &num_devices, NULL);
  if (num_devices == 0) {
    throw std::runtime_error(
        "No physical devices with Vulkan support available");
  }

  std::vector<VkPhysicalDevice> devices(num_devices);
  vkEnumeratePhysicalDevices(instance_, &num_devices, devices.data());

  for (uint32_t i = 0; i < devices.size(); i++) {
    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(devices[i], &device_props);
    if (device_props.apiVersion < VK_API_VERSION_1_4) continue;

    uint32_t num_qf_props;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &num_qf_props, NULL);
    std::vector<VkQueueFamilyProperties> qf_props(num_qf_props);

    uint32_t graphics_qf_idx = 0;
    bool supports_graphics = false;
    for (uint32_t j = 0; j < qf_props.size(); j++) {
      if (SDL_Vulkan_GetPresentationSupport(instance_, devices[i], j)) {
        supports_graphics = true;
        graphics_qf_idx = j;
        break;
      }
    }

    if (!supports_graphics) continue;

    uint32_t num_extensions;
    vkEnumerateDeviceExtensionProperties(devices[i], NULL,
                                         &num_extensions, NULL);
    std::vector<VkExtensionProperties> extensions(num_extensions);
    vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_extensions,
                                         extensions.data());

    bool desired_extensions_available = true;
    for (const char* desired_extension : kRequiredDeviceExtensions) {
      bool extension_available = false;
      for (VkExtensionProperties prop : extensions) {
        if (strcmp(desired_extension, prop.extensionName) == 0) {
          extension_available = true;
          break;
        }
      }

      if (!extension_available) {
        desired_extensions_available = false;
        break;
      }
    }

    if (!desired_extensions_available) continue;


    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
    dynamic_state_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

    VkPhysicalDeviceVulkan13Features vk_1_3_features;
    vk_1_3_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk_1_3_features.pNext = &dynamic_state_features;

    VkPhysicalDeviceFeatures2 features;
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vk_1_3_features;
    vkGetPhysicalDeviceFeatures2(devices[i], &features);
    if (dynamic_state_features.extendedDynamicState == VK_FALSE
        || vk_1_3_features.dynamicRendering == VK_FALSE) continue;

    physical_device_ = devices[i];
    graphics_qf_idx_ = graphics_qf_idx;
    return true;
  }

  throw std::runtime_error("Failed to find a suitable Vulkan physical device");
}

uint32_t App::chooseQueueFamily() {
  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_,
                                           &queue_family_count, NULL);
  std::vector<VkQueueFamilyProperties> props(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_,
                                           &queue_family_count, props.data());

  for (uint32_t i = 0; i < props.size(); i++) {
    if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) return i;
  }

  return UINT32_MAX;
}

bool App::createLogicalDevice() {
  graphics_qf_idx_ = chooseQueueFamily();
  constexpr float kQueuePriority = 0.5f;
  VkDeviceQueueCreateInfo queue_create_info = {};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueCount = 1;
  queue_create_info.queueFamilyIndex = graphics_qf_idx_;
  queue_create_info.pQueuePriorities = &kQueuePriority;

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_features = {};
  extended_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  extended_features.extendedDynamicState = VK_TRUE;

  VkPhysicalDeviceVulkan13Features vk_1_3_features = {};
  vk_1_3_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vk_1_3_features.pNext = &extended_features;
  vk_1_3_features.dynamicRendering = VK_TRUE;

  VkPhysicalDeviceFeatures2 features_2 = {};
  features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features_2.pNext = &vk_1_3_features;

  VkDeviceCreateInfo device_create_info = {};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = &features_2;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.enabledExtensionCount = static_cast<uint32_t>(
      kRequiredDeviceExtensions.size());
  device_create_info.ppEnabledExtensionNames =
      kRequiredDeviceExtensions.data();

  VkResult result = vkCreateDevice(physical_device_, &device_create_info, NULL,
                                   &device_);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create logical device!");

  vkGetDeviceQueue(device_, graphics_qf_idx_, 0, &graphics_queue_);

  return true;
}

/*
Presentation stuff
*/
VkSurfaceFormatKHR App::chooseSwapSurfaceFormat() {
  uint32_t num_formats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &num_formats, NULL);
  std::vector<VkSurfaceFormatKHR> formats(num_formats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &num_formats, formats.data());

  assert(num_formats > 0);
  VkFormat desired_format = VK_FORMAT_B8G8R8A8_SRGB;
  for (VkSurfaceFormatKHR format : formats) {
    if (format.format == desired_format) return format;
  }
  return formats[0];
}

VkPresentModeKHR App::chooseSwapPresentMode() {
  uint32_t num_modes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                            &num_modes, NULL);
  std::vector<VkPresentModeKHR> modes(num_modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                            &num_modes, modes.data());

  assert(num_modes > 0);
  VkPresentModeKHR desired_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  for (uint32_t i = 0; i < num_modes; i++) {
    if (modes[i] == desired_mode) return modes[i];
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D App::chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  return {
    std::clamp<uint32_t>(kWindowWidth, capabilities.minImageExtent.width,
                                capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(kWindowHeight, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)
  };
}

bool App::createSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_,
                                            &capabilities);

  VkExtent2D extent = chooseSwapExtent(capabilities);

  VkSurfaceFormatKHR format = chooseSwapSurfaceFormat();

  uint32_t min_images = capabilities.maxImageCount > 0
    ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
    : capabilities.minImageCount + 1;

  VkSwapchainCreateInfoKHR create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface_;
  create_info.minImageCount = min_images;
  create_info.imageFormat = format.format;
  create_info.imageColorSpace = format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = chooseSwapPresentMode();
  create_info.clipped = true;
  create_info.oldSwapchain = NULL;

  vkCreateSwapchainKHR(device_, &create_info, NULL, &swapchain_);

  uint32_t num_images;
  vkGetSwapchainImagesKHR(device_, swapchain_, &num_images, NULL);
  swapchain_images_.resize(num_images);
  vkGetSwapchainImagesKHR(device_, swapchain_, &num_images,
                          swapchain_images_.data());

  swapchain_format_ = format;
  swapchain_extent_ = extent;

  return true;
}

bool App::createImageViews() {
  assert(swapchain_image_views_.empty());

  VkImageViewCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  create_info.pNext = NULL;
  create_info.flags = 0;
  create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  create_info.format = swapchain_format_.format;
  create_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  create_info.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY
  };

  swapchain_image_views_.resize(swapchain_images_.size());
  for (uint32_t i = 0; i < swapchain_images_.size(); i++) {
    create_info.image = swapchain_images_[i];

    vkCreateImageView(device_, &create_info, NULL, &swapchain_image_views_[i]);
  }

  return true;
}

/*
Graphics pipeline
*/
static std::vector<char> readFile(const char* file_name) {
  std::ifstream file;
  file.open(file_name, std::ios::ate | std::ios::binary);

  if (!file.is_open()) throw std::runtime_error("Failed to open file!");

  std::vector<char> buffer(static_cast<std::vector<char>::size_type>(
      file.tellg()));

  file.seekg(0, std::ios::beg);
  file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  file.close();

  return buffer;
}

VkShaderModule App::createShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo shader_info = {};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.codeSize = code.size() * sizeof(char);
  shader_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule module;
  VkResult result = vkCreateShaderModule(device_, &shader_info, NULL, &module);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create shader module!");
  return module;
}

bool App::createGraphicsPipeline() {
  // Shader Stages
  shader_module_ = createShaderModule(readFile("shaders/slang.spv"));

  VkPipelineShaderStageCreateInfo vert_stage_info = {};
  vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_stage_info.module = shader_module_;
  vert_stage_info.pName = "vertMain";

  VkPipelineShaderStageCreateInfo frag_stage_info = {};
  frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_stage_info.module = shader_module_;
  frag_stage_info.pName = "fragMain";

  VkPipelineShaderStageCreateInfo stage_info[] = {vert_stage_info,
                                                  frag_stage_info};

  // Inputs
  VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo assembly_info = {};
  assembly_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly_info.flags = 0;
  assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // Viewport
  VkViewport viewport{
    0.0f, 0.0f, static_cast<float>(swapchain_extent_.width),
    static_cast<float>(swapchain_extent_.height),
    0.0f, 1.0f};

  VkRect2D scissor{VkOffset2D{0, 0}, swapchain_extent_};

  std::vector<VkDynamicState> dynamic_states = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = static_cast<uint32_t>(
      dynamic_states.size());
  dynamic_state.pDynamicStates = dynamic_states.data();

  VkPipelineViewportStateCreateInfo viewport_info = {};
  viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_info.viewportCount = 1;
  viewport_info.pViewports = &viewport;
  viewport_info.scissorCount = 1;
  viewport_info.pScissors = &scissor;

  // Rasterizer
  VkPipelineRasterizationStateCreateInfo rasterizer_info = {};
  rasterizer_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer_info.depthClampEnable = VK_FALSE;
  rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
  rasterizer_info.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer_info.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer_info.depthBiasEnable = VK_FALSE;
  rasterizer_info.lineWidth = 1.0f;

  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisample_info = {};
  multisample_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisample_info.sampleShadingEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState color_blend_attach = {};
  color_blend_attach.blendEnable = VK_FALSE;
  color_blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
    | VK_COLOR_COMPONENT_G_BIT
    | VK_COLOR_COMPONENT_B_BIT
    | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blend_info = {};
  color_blend_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_info.logicOpEnable = VK_FALSE;
  color_blend_info.logicOp = VK_LOGIC_OP_COPY;
  color_blend_info.attachmentCount = 1;
  color_blend_info.pAttachments = &color_blend_attach;

  VkPipelineLayout pipeline_layout = NULL;
  VkPipelineLayoutCreateInfo pipeline_layout_info = {};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 0;
  pipeline_layout_info.pushConstantRangeCount = 0;

  VkResult result = vkCreatePipelineLayout(device_, &pipeline_layout_info,
                                           NULL, &pipeline_layout);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create pipeline layout!");

  VkPipelineRenderingCreateInfo rendering_info = {};
  rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &swapchain_format_.format;

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &rendering_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stage_info;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &assembly_info;
  pipeline_info.pViewportState = &viewport_info;
  pipeline_info.pRasterizationState = &rasterizer_info;
  pipeline_info.pMultisampleState = &multisample_info;
  pipeline_info.pColorBlendState = &color_blend_info;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = NULL;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  result = vkCreateGraphicsPipelines(device_, NULL, 1, &pipeline_info, NULL,
                                     &graphics_pipeline_);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create pipeline!");

  graphics_pipeline_layout_ = pipeline_layout;

  return true;
}

/*
Drawing
*/
bool App::createCommandPool() {
  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = graphics_qf_idx_;

  VkResult result = vkCreateCommandPool(device_, &pool_info, NULL,
                                        &command_pool_);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create command pool!");

  return true;
}

bool App::createCommandBuffers() {
  assert(command_buffers_.empty());

  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool_;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = kMaxFramesInFlight;

  command_buffers_.resize(kMaxFramesInFlight);

  VkResult result = vkAllocateCommandBuffers(device_, &alloc_info,
                                             command_buffers_.data());
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to create command buffer!");

  return true;
}

void App::transitionImageLayout(uint32_t image_index, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags2 src_access_mask,
    VkAccessFlags2 dst_access_mask, VkPipelineStageFlags2 src_stage_mask,
    VkPipelineStageFlags2 dst_stage_mask) {
  VkImageMemoryBarrier2 barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = src_stage_mask;
  barrier.srcAccessMask = src_access_mask;
  barrier.dstStageMask = dst_stage_mask;
  barrier.dstAccessMask = dst_access_mask;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = swapchain_images_[image_index];
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkDependencyInfo dependency_info = {};
  dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency_info.dependencyFlags = 0;
  dependency_info.imageMemoryBarrierCount = 1;
  dependency_info.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(command_buffers_[frame_index_], &dependency_info);
}

bool App::recordCommandBuffer(uint32_t image_index) {
  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(command_buffers_[frame_index_], &begin_info);

  transitionImageLayout(
    image_index,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    {},
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

  VkClearValue clear_color;
  clear_color.color = {0.0f, 0.0f, 0.0f, 1.0f};
  VkRenderingAttachmentInfo attachment_info = {};
  attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  attachment_info.imageView = swapchain_image_views_[image_index],
  attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
  attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
  attachment_info.clearValue = clear_color;

  VkRenderingInfo rendering_info = {};
  rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering_info.renderArea = {{0, 0}, swapchain_extent_};
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &attachment_info;
  vkCmdBeginRendering(command_buffers_[frame_index_], &rendering_info);

  vkCmdBindPipeline(command_buffers_[frame_index_], VK_PIPELINE_BIND_POINT_GRAPHICS,
                    graphics_pipeline_);

  VkViewport viewport = {
    0.0f, 0.0f,
    static_cast<float>(swapchain_extent_.width),
    static_cast<float>(swapchain_extent_.height),
    0.0f, 1.0f
  };
  VkRect2D scissor = {VkOffset2D{0, 0}, swapchain_extent_};
  vkCmdSetViewport(command_buffers_[frame_index_], 0, 1, &viewport);
  vkCmdSetScissor(command_buffers_[frame_index_], 0, 1, &scissor);
  vkCmdDraw(command_buffers_[frame_index_], 3, 1, 0, 0);
  vkCmdEndRendering(command_buffers_[frame_index_]);

  transitionImageLayout(
    image_index,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    {},
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

  vkEndCommandBuffer(command_buffers_[frame_index_]);
  return true;
}

bool App::createSyncObjects() {
  assert(present_complete_semaphores_.empty()
         && render_finished_semaphores_.empty()
         && draw_fences_.empty());
  VkSemaphoreCreateInfo semaphore_info = {};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_info.flags = 0;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  render_finished_semaphores_.resize(swapchain_images_.size());
  for (size_t i = 0; i < swapchain_images_.size(); i++) {
    vkCreateSemaphore(device_, &semaphore_info, NULL,
                      &render_finished_semaphores_[i]);
  }

  present_complete_semaphores_.resize(kMaxFramesInFlight);
  draw_fences_.resize(kMaxFramesInFlight);
  for (size_t i = 0; i < kMaxFramesInFlight; i++) {
    vkCreateSemaphore(device_, &semaphore_info, NULL,
                      &present_complete_semaphores_[i]);
    vkCreateFence(device_, &fence_info, NULL, &draw_fences_[i]);
  }

  return true;
}

bool App::drawFrame() {
  VkResult result = vkWaitForFences(device_, 1, &draw_fences_[frame_index_],
                                    VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to wait for fence!");
  result = vkResetFences(device_, 1, &draw_fences_[frame_index_]);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to reset fence!");

  uint32_t image_index;
  result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                        present_complete_semaphores_[frame_index_], NULL,
                        &image_index);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to acquire next image!");

  vkResetCommandBuffer(command_buffers_[frame_index_], 0);
  recordCommandBuffer(image_index);

  VkPipelineStageFlags wait_dst_stage_mask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &present_complete_semaphores_[frame_index_];
  submitInfo.pWaitDstStageMask = &wait_dst_stage_mask;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &command_buffers_[frame_index_];
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &render_finished_semaphores_[frame_index_];

  vkQueueSubmit(graphics_queue_, 1, &submitInfo, draw_fences_[frame_index_]);

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished_semaphores_[frame_index_];
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain_;
  present_info.pImageIndices = &image_index;

  result = vkQueuePresentKHR(graphics_queue_, &present_info);
  if (result != VK_SUCCESS) throw std::runtime_error(
      "Failed to present image to swapchain!");

  frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
  return true;
}
