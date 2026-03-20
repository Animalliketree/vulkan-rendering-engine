#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <cstddef>
#include <vector>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <iostream>
#include <vulkan/vulkan_core.h>

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

class App {
public:
  App() {
    SDL_SetAppMetadata("Game Engine", "0.0.1", "");
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow("Game Engien", 600, 400, SDL_WINDOW_VULKAN);

    createInstance();

    selectPhysicalDevice();

    createDeviceAndQueues();
  }
  ~App() {
    vkDestroyInstance(instance_, NULL);
    SDL_DestroyWindow(window_);
    SDL_Quit();
  }

private:
  SDL_Window* window_;
  VkInstance instance_;
  VkPhysicalDevice physical_device_;
  VkDevice device_;
  uint32_t graphics_qf_idx_;
  VkQueue graphics_queue_;

  bool createInstance() {
    uint32_t num_instance_extensions;
    const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(&num_instance_extensions);

    if (instance_extensions == NULL) {
      SDL_DestroyWindow(window_);
      std::cout << "ERROR: Failed to get Vulkan instance extensions: " << SDL_GetError() << std::endl;
      return false;
    }

    int num_extensions = num_instance_extensions + 1;
    const char** extensions = (const char**)(SDL_malloc(num_extensions * sizeof(const char*)));
    extensions[0] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
    SDL_memcpy(&extensions[1], instance_extensions, num_instance_extensions * sizeof(const char*));

    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pNext = NULL;
    instance_create_info.enabledExtensionCount = num_extensions;
    instance_create_info.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&instance_create_info, NULL, &instance_);
    SDL_free(extensions);
    if (result != VK_SUCCESS) {
      std::cout << "ERROR: Failed to create Vulkan instance!" << std::endl;
      return false;
    }

    return true;
  }

  bool selectPhysicalDevice() {
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

  uint32_t chooseQueueFamily() {
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

  bool createDeviceAndQueues() {
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
};