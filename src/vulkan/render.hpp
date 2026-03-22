#ifndef RENDER_HPP_
#define RENDER_HPP_

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <quill/Logger.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>

class App {
public:
  App(quill::Logger* logger);
  ~App();

  void mainLoop();
private:
  SDL_Window* window_ = NULL;
  VkInstance instance_ = NULL;
  VkPhysicalDevice physical_device_ = NULL;
  VkDevice device_ = NULL;
  uint32_t graphics_qf_idx_ = UINT32_MAX;
  VkQueue graphics_queue_ = NULL;

  // Vulkan functions to load
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

  quill::Logger* logger_;

  bool createWindow();

  bool createInstance();

  bool selectPhysicalDevice();

  uint32_t chooseQueueFamily();

  bool createDeviceAndQueues();
};

#endif // RENDER_HPP_