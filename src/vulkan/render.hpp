#ifndef SRC_VULKAN_RENDER_HPP_
#define SRC_VULKAN_RENDER_HPP_

#include <quill/Logger.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

class App {
 public:
  explicit App(quill::Logger* logger);
  ~App();

  bool drawFrame();

  inline void waitIdle() { vkDeviceWaitIdle(device_); }

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

  // Graphics
  VkShaderModule shader_module_;
  VkPipeline graphics_pipeline_;
  VkPipelineLayout graphics_pipeline_layout_;

  // Drawing
  VkCommandPool command_pool_;
  std::vector<VkCommandBuffer> command_buffers_;

  // Synchronisation
  std::vector<VkSemaphore> present_complete_semaphores_;
  std::vector<VkSemaphore> render_finished_semaphores_;
  std::vector<VkFence> draw_fences_;
  uint32_t frame_index_ = 0;

  quill::Logger* logger_;

  bool createWindow();

  bool checkValidationLayerSupport(char const* const* p_layers,
                                   uint32_t num_layers);
  bool createInstance();

  bool selectPhysicalDevice();

  uint32_t chooseQueueFamily();

  bool createLogicalDevice();

  VkSurfaceFormatKHR chooseSwapSurfaceFormat();

  VkPresentModeKHR chooseSwapPresentMode();

  VkExtent2D chooseSwapExtent(VkSurfaceCapabilitiesKHR surface_capabilities);

  bool createSwapchain(VkSwapchainKHR old_swapchain = NULL);

  bool recreateSwapchain();

  bool createImageViews();

  VkShaderModule createShaderModule(const std::vector<char>& code);

  bool createGraphicsPipeline();

  bool createCommandPool();

  bool createCommandBuffers();

  void transitionImageLayout(uint32_t image_index, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags2 src_access_mask,
    VkAccessFlags2 dst_access_mask, VkPipelineStageFlags2 src_stage_mask,
    VkPipelineStageFlags2 dst_stage_mask);

  bool recordCommandBuffer(uint32_t image_index);

  bool createSyncObjects();
};

#endif  // SRC_VULKAN_RENDER_HPP_
