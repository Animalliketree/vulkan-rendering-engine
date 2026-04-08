#ifndef SRC_VULKAN_RENDER_HPP_
#define SRC_VULKAN_RENDER_HPP_

#include "vulkan/vulkan.hpp"
#include <quill/Logger.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

class App {
 public:
  explicit App(quill::Logger* logger);
  ~App();

  bool drawFrame();
  inline void flagResized() { framebuffer_resized_ = true; }

  inline void waitIdle() { vkDeviceWaitIdle(device_); }

// Internal
 private:
  // Initialisation
  SDL_Window* window_ = NULL;
  vk::Instance instance_;
  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  uint32_t graphics_qf_idx_ = UINT32_MAX;
  vk::Queue graphics_queue_;

  // Presentation
  VkSurfaceKHR surface_;
  vk::SwapchainKHR swapchain_;
  std::vector<vk::Image> swapchain_images_;
  std::vector<vk::ImageView> swapchain_image_views_;
  vk::SurfaceFormatKHR swapchain_format_;
  vk::Extent2D swapchain_extent_;
  bool framebuffer_resized_ = false;

  // Graphics
  vk::ShaderModule shader_module_;
  vk::Buffer vertex_buffer_;
  vk::DeviceMemory vertex_buffer_memory_;
  vk::PipelineLayout graphics_pipeline_layout_;
  vk::Pipeline graphics_pipeline_;

  // Drawing
  vk::CommandPool command_pool_;
  std::vector<vk::CommandBuffer> command_buffers_;

  // Synchronisation
  std::vector<vk::Semaphore> present_complete_semaphores_;
  std::vector<vk::Semaphore> render_finished_semaphores_;
  std::vector<vk::Fence> draw_fences_;
  uint32_t frame_index_ = 0;

  quill::Logger* logger_;

  bool createWindow();

  bool checkValidationLayerSupport(char const* const* p_layers,
                                   uint32_t num_layers);
  bool createInstance();

  bool selectPhysicalDevice();

  uint32_t chooseQueueFamily();

  bool createLogicalDevice();

  vk::SurfaceFormatKHR chooseSwapSurfaceFormat();

  vk::PresentModeKHR chooseSwapPresentMode();

  vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR surface_capabilities);

  bool createSwapchain(vk::SwapchainKHR old_swapchain);

  bool recreateSwapchain();

  bool createImageViews();

  vk::ShaderModule createShaderModule(const std::vector<char>& code);

  bool createVertexBuffer();

  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags prop_flags);

  bool createGraphicsPipeline();

  bool createCommandPool();

  bool createCommandBuffers();

  void transitionImageLayout(uint32_t image_index, vk::ImageLayout old_layout,
    vk::ImageLayout new_layout, vk::AccessFlags2 src_access_mask,
    vk::AccessFlags2 dst_access_mask, vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask);

  bool recordCommandBuffer(uint32_t image_index);

  bool createSyncObjects();
};

#endif  // SRC_VULKAN_RENDER_HPP_
