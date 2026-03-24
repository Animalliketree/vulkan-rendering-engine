#ifndef RENDER_HPP_
#define RENDER_HPP_

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <quill/Logger.h>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>

class App {
public:
  App(quill::Logger* logger);
  ~App();

  void mainLoop();

// Internal
private:
  // Initialisation
  SDL_Window* window_ = NULL;
  VkInstance instance_ = NULL;
  VkPhysicalDevice physical_device_ = NULL;
  VkDevice device_ = NULL;
  uint32_t graphics_qf_idx_ = UINT32_MAX;
  VkQueue graphics_queue_ = NULL;

  // Presentation
  VkSurfaceKHR surface_;
  VkSwapchainKHR swapchain_;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;
  VkSurfaceFormatKHR swapchain_format_;
  VkExtent2D swapchain_extent_;

  // Vulkan functions to load
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

  quill::Logger* logger_;

  bool createWindow();

  bool checkValidationLayers(char const* const* p_layers, uint32_t num_layers);
  bool createInstance();

  bool selectPhysicalDevice();

  uint32_t chooseQueueFamily();

  bool createLogicalDevice();

  VkSurfaceFormatKHR chooseSwapSurfaceFormat();

  VkPresentModeKHR chooseSwapPresentMode();

  VkExtent2D chooseSwapExtent(VkSurfaceCapabilitiesKHR surface_capabilities);

  bool createSwapchain();

  bool createImageViews();
};

#endif // RENDER_HPP_