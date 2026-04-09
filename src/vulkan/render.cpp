#include "render.hpp"
#include "vulkan/vulkan.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_surface.h>
#include <array>
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

const std::vector<char const*> kValidationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

const char* kAppTitle = "Game";
const char* kEngineTitle = "Hephaestus";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kMaxFramesInFlight = 2;

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

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

static std::vector<char> readFile(const char* file_name) {
  std::ifstream ifs;
  ifs.open(file_name, std::ios::ate | std::ios::binary);
  if (!ifs.is_open()) throw std::runtime_error("Failed to open file!");

  std::vector<char> buffer(static_cast<std::vector<char>::size_type>(
      ifs.tellg()));

  ifs.seekg(0, std::ios::beg);
  ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  ifs.close();

  return buffer;
}

Renderer::Renderer(quill::Logger* logger) :
    logger_(logger),
    instance_(logger),
    physical_device_(logger, instance_),
    device_(logger, physical_device_),
    swapchain_(physical_device_, device_, instance_.surface()),
    shader_module_(device_, readFile("shaders/slang.spv"))
{
  quill::info(logger_, "Initializing renderer...");
  createVertexBuffer();
  createGraphicsPipeline();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();
}

Renderer::~Renderer() {
  device_.waitIdle();
  for (VkFence fence : draw_fences_) static_cast<vk::Device>(device_).destroyFence(fence, nullptr);
  for (VkSemaphore semaphore : present_complete_semaphores_)
      static_cast<vk::Device>(device_).destroySemaphore(semaphore, nullptr);
  for (VkSemaphore semaphore : render_finished_semaphores_)
      static_cast<vk::Device>(device_).destroySemaphore(semaphore, nullptr);
  static_cast<vk::Device>(device_).destroyCommandPool(command_pool_, nullptr);
  static_cast<vk::Device>(device_).destroyPipeline(graphics_pipeline_, nullptr);
  static_cast<vk::Device>(device_).destroyPipelineLayout(graphics_pipeline_layout_, nullptr);
  static_cast<vk::Device>(device_).destroyBuffer(vertex_buffer_);
  static_cast<vk::Device>(device_).freeMemory(vertex_buffer_memory_);
}

SDL_WindowFlags Instance::windowFlags() {
  return SDL_GetWindowFlags(window_);
}

std::vector<vk::LayerProperties> getInstanceLayerProperties() {
  uint32_t num_props;
  vk::Result result = vk::enumerateInstanceLayerProperties(&num_props, nullptr);
  std::vector<vk::LayerProperties> layer_props(num_props);
  result = vk::enumerateInstanceLayerProperties(&num_props, layer_props.data());

  if (result != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to enumerate instance layer properties!");
  return layer_props;
}

/* Initialise Vulkan */
bool Instance::checkValidationLayerSupport(const char* const* layers,
                                      uint32_t num_layers) {
  std::vector<vk::LayerProperties> layer_props = getInstanceLayerProperties();

  for (uint32_t i = 0; i < num_layers; i++) {
    bool layer_available = false;
    for (vk::LayerProperties layer : layer_props) {
      if (SDL_strcmp(layers[i], layer.layerName) == 0) {
        layer_available = true;
        break;
      }
    }

    if (!layer_available) {
      quill::warning(logger_, "Layer not supported: {}", layers[i]);
      return false;
    }
  }

  return true;
}

vk::ApplicationInfo buildApplicationInfo() noexcept {
  vk::ApplicationInfo app_info = {};
  app_info.pApplicationName = kAppTitle;
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = kEngineTitle;
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_4;
  return app_info;
}

std::vector<const char*> Instance::getInstanceExtensions(bool layers_supported) {
  quill::info(logger_, "Getting instance extensions...");
  uint32_t num_instance_extensions;
  const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(
      &num_instance_extensions);
  if (instance_extensions == nullptr) throw std::runtime_error(
      "Instance extensions should not be null");

  std::vector<const char*> extensions;
  extensions.assign(instance_extensions,
                    instance_extensions + num_instance_extensions);
  if (layers_supported) {
    extensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
  }

  return extensions;
}

Instance::Instance(quill::Logger* logger) : logger_(logger) {
  SDL_SetAppMetadata(kAppTitle, "0.0.1", "");
  SDL_Init(SDL_INIT_VIDEO);
  window_ = SDL_CreateWindow(kAppTitle, kWindowWidth, kWindowHeight,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (window_ == nullptr) throw std::runtime_error(
      "Failed to create SDL window: " + std::to_string(*SDL_GetError()));
  

  std::vector<char const*> required_layers = {};
  if (kEnableValidationLayers) {
    required_layers.assign(kValidationLayers.begin(), kValidationLayers.end());
  }

  bool layers_supported = checkValidationLayerSupport(required_layers.data(),
      static_cast<uint32_t>(required_layers.size()));

  vk::ApplicationInfo app_info = buildApplicationInfo();

  std::vector<const char*> extensions = getInstanceExtensions(layers_supported);

  vk::InstanceCreateInfo instance_info = {};
  instance_info.pApplicationInfo = &app_info;
  instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  instance_info.ppEnabledExtensionNames = extensions.data();
  instance_info.enabledLayerCount = static_cast<uint32_t>(required_layers.size());
  instance_info.ppEnabledLayerNames = required_layers.data();

  vk::Result result = vk::createInstance(&instance_info, nullptr, &instance_);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create VkInstance!");

  SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_);
}

Instance::~Instance() {
  SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
  instance_.destroy();
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool Instance::getPresentationSupport(vk::PhysicalDevice device, uint32_t qf_idx) {
  return SDL_Vulkan_GetPresentationSupport(instance_, device, qf_idx);
}

bool PhysicalDevice::evaluatePhysicalDeviceProperties(vk::PhysicalDevice device) {
  quill::info(logger_, "Evaluating device properties...");
  vk::PhysicalDeviceProperties props;
  device.getProperties(&props);
  if (props.apiVersion < VK_API_VERSION_1_4) return false;
  else return true;
}

bool PhysicalDevice::evaluateQueueFamilies(Instance& instance, vk::PhysicalDevice device) {
  std::vector<vk::QueueFamilyProperties> props = device.getQueueFamilyProperties();

  for (uint32_t i = 0; i < props.size(); i++) {
    bool supports_graphics = instance.getPresentationSupport(device, i);
    if (supports_graphics) return true;
  }

  quill::warning(logger_, "Failed to find a valid queue family");
  return false;
}

bool evaluateDeviceExtensions(vk::PhysicalDevice device, std::vector<const char*> target_extensions) {
  std::vector<vk::ExtensionProperties> props = device.enumerateDeviceExtensionProperties();
  for (const char* extension : target_extensions) {
    bool available = false;
    for (vk::ExtensionProperties prop : props) {
      if (strcmp(extension, prop.extensionName) == 0) {
        available = true;
        break;
      }
    }

    if (!available) return false;
  }

  return true;
}

bool PhysicalDevice::evaluateDeviceFeatures(vk::PhysicalDevice device) {
  quill::info(logger_, "Evaluating physical device features...");
  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
  vk::PhysicalDeviceVulkan13Features vk_1_3_features;
  vk::PhysicalDeviceFeatures2 features;
  vk_1_3_features.pNext = &dynamic_state_features;
  features.pNext = &vk_1_3_features;

  device.getFeatures2(&features);
  quill::info(logger_, "Extended dynamic state supported: {}", dynamic_state_features.extendedDynamicState);
  quill::info(logger_, "Dynamic rendering supported: {}", vk_1_3_features.dynamicRendering);
  if (!dynamic_state_features.extendedDynamicState || !vk_1_3_features.dynamicRendering) {
    quill::info(logger_, "required features not available");
    return false;
  }
  else return true;
}

std::vector<vk::PhysicalDevice> Instance::getPhysicalDevices() {
  uint32_t num_devices;
  vk::Result r = instance_.enumeratePhysicalDevices(&num_devices, nullptr);
  if (num_devices == 0 || r != vk::Result::eSuccess)
      throw std::runtime_error(
          "No physical devices with Vulkan support available");

  std::vector<vk::PhysicalDevice> devices(num_devices);
  r = instance_.enumeratePhysicalDevices(&num_devices, devices.data());
  return devices;
}

uint32_t PhysicalDevice::getQueueFamilyIndex() {
  std::vector<vk::QueueFamilyProperties> props = physical_device_.getQueueFamilyProperties();

  for (uint32_t i = 0; i < props.size(); i++) {
    if ((props[i].queueFlags & vk::QueueFlagBits::eGraphics) == vk::QueueFlagBits::eGraphics) return i;
  }

  return UINT32_MAX;
}

PhysicalDevice::PhysicalDevice(quill::Logger* logger, Instance& instance) : logger_(logger) {
  assert(static_cast<vk::Instance>(instance) != nullptr);
  std::vector<vk::PhysicalDevice> devices = instance.getPhysicalDevices();
  assert(devices.size() > 0);

  bool valid_device_present = false;
  for (vk::PhysicalDevice device : devices) {
    bool properties_good = evaluatePhysicalDeviceProperties(device);
    bool extensions_good = evaluateDeviceExtensions(device, kRequiredDeviceExtensions);
    bool queue_families_good = evaluateQueueFamilies(instance, device);
    bool features_good = evaluateDeviceFeatures(device);
    if (!properties_good || !extensions_good || !queue_families_good ||
        !features_good) continue;

    physical_device_ = device;
    valid_device_present = true;
    break;
  }

  if (!valid_device_present) throw std::runtime_error(
      "Failed to find a suitable Vulkan physical device");
}

LogicalDevice::LogicalDevice(quill::Logger* logger, PhysicalDevice physical_device) : logger_(logger) {
  quill::info(logger_, "Creating logical device...");
  graphics_qf_idx_ = physical_device.getQueueFamilyIndex();
  assert(graphics_qf_idx_ != UINT32_MAX);
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

  vk::Result r = static_cast<vk::PhysicalDevice>(physical_device).createDevice(
      &device_info, nullptr, &device_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create logical device!");

  device_.getQueue(graphics_qf_idx_, 0, &graphics_queue_);
}

LogicalDevice::~LogicalDevice() {
  device_.waitIdle();
  device_.destroy();
}

std::vector<vk::SurfaceFormatKHR> PhysicalDevice::getSurfaceFormats(vk::SurfaceKHR surface) {
  uint32_t num_formats;
  vk::Result r = physical_device_.getSurfaceFormatsKHR(surface, &num_formats,
                                                       nullptr);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to get surface formats!");

  std::vector<vk::SurfaceFormatKHR> formats(num_formats);
  r = physical_device_.getSurfaceFormatsKHR(surface,
                                            &num_formats, formats.data());
  if (r != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to get surface formats!");
  
  return formats;
}

std::vector<vk::PresentModeKHR> PhysicalDevice::getSurfacePresentModes(vk::SurfaceKHR surface) {
  uint32_t num_modes;
  vk::Result r = physical_device_.getSurfacePresentModesKHR(surface,
                                                            &num_modes, nullptr);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to get surface present modes!");

  std::vector<vk::PresentModeKHR> modes(num_modes);
  r = physical_device_.getSurfacePresentModesKHR(surface, &num_modes,
                                                 modes.data());
  if (r != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to get surface present modes!");
  return modes;
}

vk::SurfaceCapabilitiesKHR PhysicalDevice::getSurfaceCapabilities(vk::SurfaceKHR surface) {
  vk::SurfaceCapabilitiesKHR capabilities;
  vk::Result r = physical_device_.getSurfaceCapabilitiesKHR(surface, &capabilities);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to get surface capabilities!");
  return capabilities;
}

vk::PhysicalDeviceMemoryProperties PhysicalDevice::getMemoryProperties() {
  vk::PhysicalDeviceMemoryProperties properties;
  physical_device_.getMemoryProperties(&properties);
  return properties;
}

/* Presentation stuff */
vk::SurfaceFormatKHR Swapchain::chooseSurfaceFormat() {
  std::vector<vk::SurfaceFormatKHR> formats = physical_device_.getSurfaceFormats(surface_);

  assert(formats.size() > 0);
  vk::Format desired_format = vk::Format::eB8G8R8A8Srgb;
  for (vk::SurfaceFormatKHR format : formats) {
    if (format.format == desired_format) return format;
  }
  return formats[0];
}

vk::PresentModeKHR Swapchain::choosePresentMode() {
  std::vector<vk::PresentModeKHR> modes = physical_device_.getSurfacePresentModes(surface_);

  assert(modes.size() > 0);
  vk::PresentModeKHR desired_mode = vk::PresentModeKHR::eMailbox;
  for (uint32_t i = 0; i < modes.size(); i++) {
    if (modes[i] == desired_mode) return modes[i];
  }

  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseExtent() {
  if (capabilities_.currentExtent.width != UINT32_MAX)
      return capabilities_.currentExtent;
  else return {
    std::clamp<uint32_t>(kWindowWidth, capabilities_.minImageExtent.width,
                                capabilities_.maxImageExtent.width),
    std::clamp<uint32_t>(kWindowHeight, capabilities_.minImageExtent.height,
                                 capabilities_.maxImageExtent.height)
  };
}

vk::SwapchainCreateInfoKHR Swapchain::buildSwapchainInfo() {
  uint32_t min_images = capabilities_.maxImageCount > 0
    ? std::min(capabilities_.minImageCount + 1, capabilities_.maxImageCount)
    : capabilities_.minImageCount + 1;

  vk::PresentModeKHR present_mode = choosePresentMode();

  vk::SwapchainCreateInfoKHR swapchain_info = {};
  swapchain_info.surface = surface_;
  swapchain_info.minImageCount = min_images;
  swapchain_info.imageFormat = format_.format;
  swapchain_info.imageColorSpace = format_.colorSpace;
  swapchain_info.imageExtent = extent_;
  swapchain_info.imageArrayLayers = 1;
  swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
  swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
  swapchain_info.preTransform = capabilities_.currentTransform;
  swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  swapchain_info.presentMode = present_mode;
  swapchain_info.clipped = true;
  swapchain_info.oldSwapchain = nullptr;
  return swapchain_info;
}

void Swapchain::getImages() {
  uint32_t num_images;
  vk::Result r = static_cast<vk::Device>(device_).getSwapchainImagesKHR(swapchain_, &num_images, nullptr);
  images_.resize(num_images);
  r = static_cast<vk::Device>(device_).getSwapchainImagesKHR(swapchain_, &num_images,
                          images_.data());
  assert(r == vk::Result::eSuccess);
}

void Swapchain::getImageViews() {
  vk::ImageViewCreateInfo view_info = {};
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = format_.format;
  view_info.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  view_info.components = {
    vk::ComponentSwizzle::eIdentity,
    vk::ComponentSwizzle::eIdentity,
    vk::ComponentSwizzle::eIdentity,
    vk::ComponentSwizzle::eIdentity
  };

  image_views_.resize(images_.size());
  for (uint32_t i = 0; i < images_.size(); i++) {
    view_info.image = images_[i];

    vk::Result r = static_cast<vk::Device>(device_).createImageView(&view_info, nullptr,
                                                &image_views_[i]);
    if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create image views!");
  }
}

Swapchain::Swapchain(PhysicalDevice& physical_device, LogicalDevice& device, VkSurfaceKHR surface) :
    physical_device_(physical_device),
    device_(device),
    surface_(surface),
    capabilities_(physical_device_.getSurfaceCapabilities(surface_)),
    format_(chooseSurfaceFormat()),
    extent_(chooseExtent())
{
  vk::SwapchainCreateInfoKHR swapchain_info = buildSwapchainInfo();

  vk::Result r = static_cast<vk::Device>(device_).createSwapchainKHR(
      &swapchain_info, nullptr, &swapchain_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create swapchain!");
  
  getImages();
  getImageViews();
}

Swapchain::~Swapchain() {
  for (VkImageView view : image_views_)
    static_cast<vk::Device>(device_).destroyImageView(view, nullptr);
  static_cast<vk::Device>(device_).destroySwapchainKHR(swapchain_);
}

void Swapchain::refresh() {
  vk::SwapchainCreateInfoKHR swapchain_info = buildSwapchainInfo();
  swapchain_info.oldSwapchain = swapchain_;

  vk::Result r = static_cast<vk::Device>(device_).createSwapchainKHR(
      &swapchain_info, nullptr, &swapchain_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create swapchain!");
  
  getImages();
  getImageViews();
}

/* Graphics pipeline */

ShaderModule::ShaderModule(LogicalDevice& device,
                           const std::vector<char>& code) :
    device_(device)
{
  vk::ShaderModuleCreateInfo module_info = {};
  module_info.codeSize = code.size() * sizeof(char);
  module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

  vk::Result r = static_cast<vk::Device>(device_).createShaderModule(
      &module_info, nullptr, &shader_module_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create shader module!");
}

ShaderModule::~ShaderModule() {
  static_cast<vk::Device>(device_).destroyShaderModule(shader_module_);
}

bool Renderer::createVertexBuffer() {
  vk::BufferCreateInfo buffer_info = {};
  buffer_info.size = sizeof(vertices[0]) * vertices.size();
  buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_info.sharingMode = vk::SharingMode::eExclusive;
  vk::Result r = static_cast<vk::Device>(device_).createBuffer(&buffer_info, nullptr, &vertex_buffer_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create vertex buffer!");

  vk::MemoryRequirements mem_req;
  static_cast<vk::Device>(device_).getBufferMemoryRequirements(vertex_buffer_, &mem_req);

  vk::MemoryAllocateInfo mem_alloc_info = {};
  mem_alloc_info.allocationSize = mem_req.size;
  mem_alloc_info.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits,
      vk::MemoryPropertyFlagBits::eHostVisible
      | vk::MemoryPropertyFlagBits::eHostCoherent);
  
  r = static_cast<vk::Device>(device_).allocateMemory(&mem_alloc_info, nullptr, &vertex_buffer_memory_);
  if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to allocate memory for vertex buffer!");

  static_cast<vk::Device>(device_).bindBufferMemory(vertex_buffer_, vertex_buffer_memory_, 0);

  void* data = static_cast<vk::Device>(device_).mapMemory(vertex_buffer_memory_, 0, buffer_info.size);
  memcpy(data, vertices.data(), buffer_info.size);
  static_cast<vk::Device>(device_).unmapMemory(vertex_buffer_memory_);

  return true;
}

uint32_t Renderer::findMemoryType(uint32_t type_filter, vk::MemoryPropertyFlags prop_flags) {
  vk::PhysicalDeviceMemoryProperties mem_props = physical_device_.getMemoryProperties();

  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if (type_filter & (1 << i) &&
        (mem_props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
      return i;
  }

  throw std::runtime_error("Failed to find suitable memory type!");
}

vk::PipelineLayout Renderer::createGraphicsPipelineLayout() {
  vk::PipelineLayout layout;
  vk::PipelineLayoutCreateInfo layout_info = {};
  layout_info.setLayoutCount = 0;
  layout_info.pushConstantRangeCount = 0;

  vk::Result result = static_cast<vk::Device>(device_).createPipelineLayout(&layout_info, nullptr,
                                                   &layout);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create pipeline layout!");
  return layout;
}

bool Renderer::createGraphicsPipeline() {
  // Shader Stages
  vk::PipelineShaderStageCreateInfo vert_stage_info = {};
  vert_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
  vert_stage_info.module = shader_module_;
  vert_stage_info.pName = "vertMain";

  vk::PipelineShaderStageCreateInfo frag_stage_info = {};
  frag_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
  frag_stage_info.module = shader_module_;
  frag_stage_info.pName = "fragMain";

  vk::PipelineShaderStageCreateInfo stage_info[] = {vert_stage_info,
                                                  frag_stage_info};

  // Inputs
  vk::VertexInputBindingDescription binding_description = Vertex::getBindingDescription();
  std::array<vk::VertexInputAttributeDescription, 2> attribute_descriptions = Vertex::getAttributeDescription();

  vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.vertexAttributeDescriptionCount = attribute_descriptions.size();
  vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &binding_description;

  vk::PipelineInputAssemblyStateCreateInfo assembly_info = {};
  assembly_info.topology = vk::PrimitiveTopology::eTriangleList;

  // Viewport
  vk::Viewport viewport{
    0.0f, 0.0f, static_cast<float>(swapchain_.extent().width),
    static_cast<float>(swapchain_.extent().height),
    0.0f, 1.0f};

  vk::Rect2D scissor{vk::Offset2D{0, 0}, swapchain_.extent()};

  std::vector<vk::DynamicState> dynamic_states = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor};

  vk::PipelineDynamicStateCreateInfo dyn_state_info = {};
  dyn_state_info.dynamicStateCount = static_cast<uint32_t>(
      dynamic_states.size());
  dyn_state_info.pDynamicStates = dynamic_states.data();

  vk::PipelineViewportStateCreateInfo viewport_info = {};
  viewport_info.viewportCount = 1;
  viewport_info.pViewports = &viewport;
  viewport_info.scissorCount = 1;
  viewport_info.pScissors = &scissor;

  // Rasterizer
  vk::PipelineRasterizationStateCreateInfo rasterizer_info = {};
  rasterizer_info.depthClampEnable = VK_FALSE;
  rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
  rasterizer_info.polygonMode = vk::PolygonMode::eFill;
  rasterizer_info.cullMode = vk::CullModeFlagBits::eBack;
  rasterizer_info.frontFace = vk::FrontFace::eClockwise;
  rasterizer_info.depthBiasEnable = VK_FALSE;
  rasterizer_info.lineWidth = 1.0f;

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

  vk::PipelineLayout pipeline_layout = createGraphicsPipelineLayout();

  vk::PipelineRenderingCreateInfo rendering_info = {};
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &swapchain_.format().format;

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
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = nullptr;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  vk::Result result = static_cast<vk::Device>(device_).createGraphicsPipelines(nullptr, 1, &pipeline_info, nullptr,
                                           &graphics_pipeline_);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create pipeline!");

  graphics_pipeline_layout_ = pipeline_layout;

  return true;
}

/* Drawing */
bool Renderer::createCommandPool() {
  vk::CommandPoolCreateInfo pool_info = {};
  pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  pool_info.queueFamilyIndex = device_.graphics_qf_idx();

  vk::Result result = static_cast<vk::Device>(device_).createCommandPool(&pool_info, nullptr,
                                                &command_pool_);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create command pool!");

  return true;
}

bool Renderer::createCommandBuffers() {
  assert(command_buffers_.empty());

  vk::CommandBufferAllocateInfo alloc_info = {};
  alloc_info.commandPool = command_pool_;
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  alloc_info.commandBufferCount = kMaxFramesInFlight;

  command_buffers_.resize(kMaxFramesInFlight);

  vk::Result result = static_cast<vk::Device>(device_).allocateCommandBuffers(&alloc_info,
                                                     command_buffers_.data());
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create command buffer!");

  return true;
}

void Renderer::transitionImageLayout(uint32_t image_index, vk::ImageLayout old_layout,
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
  barrier.image = swapchain_.images()[image_index];
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

bool Renderer::recordCommandBuffer(uint32_t image_index) {
  vk::CommandBufferBeginInfo begin_info = {};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

  vk::Result result = command_buffers_[frame_index_].begin(&begin_info);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
    "Failed to begin command buffer!");

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
  attachment_info.imageView = swapchain_.image_views()[image_index],
  attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
  attachment_info.loadOp = vk::AttachmentLoadOp::eClear,
  attachment_info.storeOp = vk::AttachmentStoreOp::eStore,
  attachment_info.clearValue = clear_value;

  vk::RenderingInfo rendering_info = {};
  rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain_.extent()};
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &attachment_info;
  command_buffers_[frame_index_].beginRendering(&rendering_info);

  command_buffers_[frame_index_].bindPipeline(vk::PipelineBindPoint::eGraphics,
                                              graphics_pipeline_);
  vk::DeviceSize vertex_buffer_offsets[] = {0};
  command_buffers_[frame_index_].bindVertexBuffers(0, 1, &vertex_buffer_, vertex_buffer_offsets);

  vk::Viewport viewport = {0.0f, 0.0f,
    static_cast<float>(swapchain_.extent().width),
    static_cast<float>(swapchain_.extent().height),
    0.0f, 1.0f
  };
  vk::Rect2D scissor = {vk::Offset2D{0, 0}, swapchain_.extent()};
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

void Renderer::createSyncObjects() {
  assert(present_complete_semaphores_.empty()
         && render_finished_semaphores_.empty()
         && draw_fences_.empty());
  vk::SemaphoreCreateInfo semaphore_info = {};

  vk::FenceCreateInfo fence_info = {};
  fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

  render_finished_semaphores_.resize(swapchain_.images().size());
  for (size_t i = 0; i < swapchain_.images().size(); i++) {
    vk::Result r = static_cast<vk::Device>(device_).createSemaphore(&semaphore_info, nullptr,
                      &render_finished_semaphores_[i]);
    if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create semaphores!");
  }

  present_complete_semaphores_.resize(kMaxFramesInFlight);
  draw_fences_.resize(kMaxFramesInFlight);
  for (size_t i = 0; i < kMaxFramesInFlight; i++) {
    vk::Result r = static_cast<vk::Device>(device_).createSemaphore(&semaphore_info, nullptr,
                      &present_complete_semaphores_[i]);
    if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create semaphores!");
    r = static_cast<vk::Device>(device_).createFence(&fence_info, nullptr, &draw_fences_[i]);
    if (r != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to create fences!");
  }
}

uint32_t Swapchain::nextImageIndex(vk::Semaphore semaphore) {
  uint32_t image_index;
  vk::Result result = static_cast<vk::Device>(device_).acquireNextImageKHR(swapchain_, UINT64_MAX,
                        semaphore, nullptr,
                        &image_index);
  switch (result) {
    case vk::Result::eErrorOutOfDateKHR:
      refresh();
      return false;
    case vk::Result::eSuboptimalKHR:
      refresh();
      return false;
    case vk::Result::eSuccess:
      break;
    default:
      throw std::runtime_error("Failed to acquire next image!");
  }
  return image_index;
}

bool Renderer::drawFrame() {
  vk::Result result = static_cast<vk::Device>(device_).waitForFences(1, &draw_fences_[frame_index_],
                                    VK_TRUE, UINT64_MAX);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to wait for fence!");

  uint32_t image_index = swapchain_.nextImageIndex(present_complete_semaphores_[frame_index_]);

  result = static_cast<vk::Device>(device_).resetFences(1, &draw_fences_[frame_index_]);
  if (result != vk::Result::eSuccess) throw std::runtime_error(
      "Failed to reset fence!");

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

  result = device_.graphics_queue().submit(1, &submitInfo, draw_fences_[frame_index_]);

  vk::SwapchainKHR swapchain = swapchain_;
  vk::PresentInfoKHR present_info = {};
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished_semaphores_[image_index];
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain;
  present_info.pImageIndices = &image_index;

  result = device_.graphics_queue().presentKHR(&present_info);
  switch (result) {
    case vk::Result::eErrorOutOfDateKHR:
      swapchain_.refresh();
      return false;
    case vk::Result::eSuboptimalKHR:
      swapchain_.refresh();
      return false;
    case vk::Result::eSuccess:
      break;
    default:
      throw std::runtime_error("Failed to present image to queue!");
  }

  if (framebuffer_resized_) {
    framebuffer_resized_ = false;
    swapchain_.refresh();
  }

  frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
  return true;
}
