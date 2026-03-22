#include "render.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <cstddef>
#include <cstdint>
#include <quill/Logger.h>
#include <stdexcept>
#include <vector>
#include <iostream>

#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>
#include <vulkan/vulkan_core.h>

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

#define APP_TITLE "Game"
#define ENGINE_TITLE "Hephaestus"
#define WIDTH 800
#define HEIGHT 600

App::App(quill::Logger* logger) : logger_(logger) {
  createWindow();

  createInstance();
  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();

  selectPhysicalDevice();

  createDeviceAndQueues();
}

App::~App() {
  vkDestroyDevice(device_, NULL);
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

bool App::createInstance() {
  quill::info(logger_, "Creating instance...");

  uint32_t num_instance_extensions;
  const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(&num_instance_extensions);
  if (instance_extensions == NULL) {
    quill::error(logger_, "Instance extensions should not be null");
    return false;
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
  instance_create_info.enabledLayerCount = 0;
  instance_create_info.ppEnabledLayerNames = NULL;

  VkResult result = vkCreateInstance(&instance_create_info, NULL, &instance_);
  SDL_free(extensions);
  if (result != VK_SUCCESS) {
    quill::error(logger_, "VkInstance should have been created successfully");
    throw std::runtime_error("Failed to create VkInstance");
    return false;
  }

  return true;
}

bool App::selectPhysicalDevice() {
  quill::info(logger_, "Selecting physical device...");

  uint32_t num_devices;
  vkEnumeratePhysicalDevices(instance_, &num_devices, NULL);
  std::vector<VkPhysicalDevice> devices(num_devices);
  vkEnumeratePhysicalDevices(instance_, &num_devices, devices.data());

  for (uint32_t i = 0; i < devices.size(); i++) {
    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(devices[i], &device_props);
    if (device_props.deviceType & (VK_PHYSICAL_DEVICE_TYPE_CPU | VK_PHYSICAL_DEVICE_TYPE_OTHER)) {
      continue;
    }

    physical_device_ = devices[i];
    return true;
  }

  return false;
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

bool App::createDeviceAndQueues() {
  quill::info(logger_, "Creating logical device and queues...");

  graphics_qf_idx_ = chooseQueueFamily();
  const float queue_priority = 1.0f;
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

  VkResult result = vkCreateDevice(physical_device_, &device_create_info, NULL, &device_);
  if (result != VK_SUCCESS) {
    std::cerr << "ERROR: Failed to create logical device" << std::endl;
    return false;
  }

  vkGetDeviceQueue(device_, graphics_qf_idx_, 0, &graphics_queue_);

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