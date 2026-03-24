#include "render.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <quill/Logger.h>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <cassert>
#include <algorithm>

#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>
#include <vulkan/vulkan_core.h>

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

const std::vector<char const*> kValidationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> required_device_extensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

#define APP_TITLE "Game"
#define ENGINE_TITLE "Hephaestus"
#define WIDTH 800
#define HEIGHT 600

App::App(quill::Logger* logger) : logger_(logger) {
  createWindow();

  createInstance();
  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();

  bool success = SDL_Vulkan_CreateSurface(window_, instance_, NULL, &surface_);
  if (!success) {
    throw std::runtime_error(std::string("Failed to create Vulkan surface: ") + std::string(SDL_GetError()));
  }

  selectPhysicalDevice();

  createLogicalDevice();

  createSwapchain();
}

App::~App() {
  vkDestroySwapchainKHR(device_, swapchain_, NULL);
  vkDestroyDevice(device_, NULL);
  SDL_Vulkan_DestroySurface(instance_, surface_, NULL);
  vkDestroyInstance(instance_, NULL);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool App::createWindow() {
  quill::info(logger_, "Initializing SDL...");
  SDL_SetAppMetadata(APP_TITLE, "0.0.1", "");
  SDL_Init(SDL_INIT_VIDEO);

  quill::info(logger_, "Creating window...");
  window_ = SDL_CreateWindow(APP_TITLE, WIDTH, HEIGHT, SDL_WINDOW_VULKAN);

  if (window_ == NULL) {
    quill::error(logger_, "Failed to create SDL window: {}", SDL_GetError());
    throw std::runtime_error("Failed to create SDL window");
  }

  return true;
}

/*
Initialise Vulkan
*/
bool App::checkValidationLayers(const char* const* p_layers, uint32_t num_layers) {
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
  quill::info(logger_, "Creating instance...");

  std::vector<char const*> required_layers = {};
  if (kEnableValidationLayers) {
    required_layers.assign(kValidationLayers.begin(), kValidationLayers.end());
  }

  if (checkValidationLayers(required_layers.data(), required_layers.size()) == false) {
    throw std::runtime_error("Requested validation layers are not available");
  }

  // Handle extensions
  uint32_t num_instance_extensions;
  const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(&num_instance_extensions);
  if (instance_extensions == NULL) {
    throw std::runtime_error("Instance extensions should not be null");
  }

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = NULL;
  app_info.pApplicationName = APP_TITLE;
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = ENGINE_TITLE;
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_4;

  int num_extensions = num_instance_extensions + 1;
  const char** extensions = (const char**)(SDL_malloc(num_extensions * sizeof(const char*)));
  extensions[0] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
  SDL_memcpy(&extensions[1], instance_extensions, num_instance_extensions * sizeof(const char*));

  VkInstanceCreateInfo instance_create_info = {};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = NULL;
  instance_create_info.pApplicationInfo = &app_info;
  instance_create_info.enabledExtensionCount = num_extensions;
  instance_create_info.ppEnabledExtensionNames = extensions;
  instance_create_info.enabledLayerCount = required_layers.size();
  instance_create_info.ppEnabledLayerNames = required_layers.data();

  VkResult result = vkCreateInstance(&instance_create_info, NULL, &instance_);
  SDL_free(extensions);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to create VkInstance");
    return false;
  }

  return true;
}

bool App::selectPhysicalDevice() {
  quill::info(logger_, "Selecting physical device...");

  uint32_t num_devices;
  vkEnumeratePhysicalDevices(instance_, &num_devices, NULL);
  if (num_devices == 0) {
    throw std::runtime_error("No physical devices with Vulkan support available");
  }
  std::vector<VkPhysicalDevice> devices(num_devices);
  vkEnumeratePhysicalDevices(instance_, &num_devices, devices.data());

  for (uint32_t i = 0; i < devices.size(); i++) {
    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(devices[i], &device_props);
    if (device_props.apiVersion < VK_API_VERSION_1_4) continue;
    if (device_props.deviceType & (VK_PHYSICAL_DEVICE_TYPE_CPU | VK_PHYSICAL_DEVICE_TYPE_OTHER)) {
      continue;
    }

    uint32_t num_qf_props;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &num_qf_props, NULL);
    std::vector<VkQueueFamilyProperties> qf_props(num_qf_props);

    uint32_t graphics_qf_idx = 0;
    bool supports_graphics = false;
    for (uint32_t i = 0; i < qf_props.size(); i++) {
      if (SDL_Vulkan_GetPresentationSupport(instance_, devices[i], i)) {
        supports_graphics = true;
        uint32_t graphics_qf_idx = i;
        break;
      }
    }

    if (!supports_graphics) continue;

    uint32_t num_extensions;
    vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_extensions, NULL);
    std::vector<VkExtensionProperties> extensions(num_extensions);
    vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_extensions, extensions.data());

    bool desired_extensions_available = true;
    for (const char* desired_extension: required_device_extensions) {
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
    dynamic_state_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

    VkPhysicalDeviceVulkan13Features vk_1_3_features;
    vk_1_3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk_1_3_features.pNext = &dynamic_state_features;

    VkPhysicalDeviceFeatures2 features;
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vk_1_3_features;
    vkGetPhysicalDeviceFeatures2(devices[i], &features);
    if (dynamic_state_features.extendedDynamicState == VK_FALSE || vk_1_3_features.dynamicRendering == VK_FALSE) continue;

    physical_device_ = devices[i];
    return true;
  }

  throw std::runtime_error("Failed to find a suitable Vulkan physical device");
}

uint32_t App::chooseQueueFamily() {
  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, NULL);
  std::vector<VkQueueFamilyProperties> props(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, props.data());

  for (uint32_t i = 0; i < props.size(); i++) {
    if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      return i;
    }
  }

  std::cerr << "ERROR: Could not find the graphics queue family index" << std::endl;
  return UINT32_MAX;
}

bool App::createLogicalDevice() {
  quill::info(logger_, "Creating logical device and queues...");

  graphics_qf_idx_ = chooseQueueFamily();
  const float queue_priority = 0.5f;
  VkDeviceQueueCreateInfo queue_create_info = {};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.pNext = NULL;
  queue_create_info.flags = 0;
  queue_create_info.queueCount = 1;
  queue_create_info.queueFamilyIndex = graphics_qf_idx_;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkDeviceCreateInfo device_create_info = {};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = NULL;
  device_create_info.flags = 0;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.enabledExtensionCount = required_device_extensions.size();
  device_create_info.ppEnabledExtensionNames = required_device_extensions.data();

  VkResult result = vkCreateDevice(physical_device_, &device_create_info, NULL, &device_);
  if (result != VK_SUCCESS) {
    std::cerr << "ERROR: Failed to create logical device" << std::endl;
    return false;
  }

  vkGetDeviceQueue(device_, graphics_qf_idx_, 0, &graphics_queue_);

  return true;
}

/*
Presentation stuff
*/
VkSurfaceFormatKHR App::chooseSwapSurfaceFormat() {
  uint32_t num_formats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_formats, NULL);
  std::vector<VkSurfaceFormatKHR> formats(num_formats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_formats, formats.data());

  assert(num_formats > 0);
  VkFormat desired_format = VK_FORMAT_B8G8R8A8_SRGB;
  for (VkSurfaceFormatKHR format : formats) {
    if (format.format == desired_format) {
      return format;
    }
  }
  return formats[0];
}

VkPresentModeKHR App::chooseSwapPresentMode() {
  uint32_t num_modes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &num_modes, NULL);
  std::vector<VkPresentModeKHR> modes(num_modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &num_modes, modes.data());

  assert(num_modes > 0);
  VkPresentModeKHR desired_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  for (uint32_t i = 0; i < num_modes; i++) {
    if (modes[i] == desired_mode) return modes[i];
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D App::chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  return {
    std::clamp<uint32_t>(WIDTH, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(HEIGHT, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
  };
}

bool App::createSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

  VkExtent2D extent = chooseSwapExtent(capabilities);

  VkSurfaceFormatKHR format = chooseSwapSurfaceFormat();

  uint32_t min_images = capabilities.maxImageCount > 0
    ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
    : capabilities.minImageCount + 1;

  VkSwapchainCreateInfoKHR create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.pNext = NULL;
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
  images_.resize(num_images);
  vkGetSwapchainImagesKHR(device_, swapchain_, &num_images, images_.data());

  swapchain_format_ = format;
  swapchain_extent_ = extent;

  return true;
}

void App::mainLoop() {
  bool done = false;

  while (!done) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      }
    }
  }
}