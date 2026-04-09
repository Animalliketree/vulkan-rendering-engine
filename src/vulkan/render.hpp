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
#include <vulkan/vulkan_core.h>

class Instance {
 public:
  Instance(quill::Logger*);
  ~Instance();

  SDL_WindowFlags windowFlags();
  VkSurfaceKHR surface() { return surface_; }
  const VkSurfaceKHR surface() const { return surface_; }

  std::vector<vk::PhysicalDevice> getPhysicalDevices();

  bool getPresentationSupport(vk::PhysicalDevice, uint32_t queue_family_index);

  operator vk::Instance() const { return instance_; }
 private:
  quill::Logger* logger_;
  SDL_Window* window_;
  vk::Instance instance_;
  VkSurfaceKHR surface_;

  bool checkValidationLayerSupport(char const* const* p_layers,
                                   uint32_t num_layers);

  std::vector<const char*> getInstanceExtensions(bool layers_supported);
};

class PhysicalDevice {
 public:
  PhysicalDevice(quill::Logger*, Instance&);
  ~PhysicalDevice() = default;

  uint32_t getQueueFamilyIndex();

  vk::Device createDevice(vk::DeviceCreateInfo&);

  std::vector<vk::SurfaceFormatKHR> getSurfaceFormats(vk::SurfaceKHR);
  std::vector<vk::PresentModeKHR> getSurfacePresentModes(vk::SurfaceKHR);

  vk::SurfaceCapabilitiesKHR getSurfaceCapabilities(vk::SurfaceKHR);
  vk::PhysicalDeviceMemoryProperties getMemoryProperties();

  operator vk::PhysicalDevice() const { return physical_device_; }
 private:
  quill::Logger* logger_;
  vk::PhysicalDevice physical_device_;

  void selectPhysicalDevice();

  bool evaluatePhysicalDeviceProperties(vk::PhysicalDevice device);
  bool evaluateQueueFamilies(Instance& instance, vk::PhysicalDevice device);
  bool evaluateDeviceFeatures(vk::PhysicalDevice device);
};

class LogicalDevice {
 public:
  LogicalDevice(quill::Logger*, PhysicalDevice);
  ~LogicalDevice();
  void waitIdle() { device_.waitIdle(); }
  vk::Queue graphics_queue() { return graphics_queue_; }

  const uint32_t& graphics_qf_idx() const { return graphics_qf_idx_; }

  operator vk::Device() const { return device_; }
 private:
  quill::Logger* logger_;
  vk::Device device_;
  uint32_t graphics_qf_idx_;
  vk::Queue graphics_queue_;
};

class Swapchain {
 public:
  Swapchain(PhysicalDevice&, LogicalDevice&, VkSurfaceKHR);
  Swapchain(Swapchain&);
  ~Swapchain();

  const vk::SurfaceFormatKHR& format() const { return format_; }
  const vk::Extent2D& extent() const { return extent_; }
  std::vector<vk::Image> images() { return images_; }
  std::vector<vk::ImageView> image_views() { return image_views_; }

  operator vk::SwapchainKHR() const { return swapchain_; }
  
  void refresh();

  uint32_t nextImageIndex(vk::Semaphore);

 private:
  PhysicalDevice& physical_device_;
  LogicalDevice& device_;
  VkSurfaceKHR surface_;
  vk::SurfaceCapabilitiesKHR capabilities_;
  vk::SurfaceFormatKHR format_;
  vk::Extent2D extent_;
  std::vector<vk::Image> images_;
  std::vector<vk::ImageView> image_views_;
  vk::SwapchainKHR swapchain_;

  vk::SurfaceFormatKHR chooseSurfaceFormat();

  vk::PresentModeKHR choosePresentMode();

  vk::Extent2D chooseExtent();

  vk::SwapchainCreateInfoKHR buildSwapchainInfo();

  void getImages();
  void getImageViews();
};

class ShaderModule {
 public:
  ShaderModule(LogicalDevice&, const std::vector<char>& code);
  ~ShaderModule();

  operator vk::ShaderModule() const { return shader_module_; }
 private:
  LogicalDevice& device_;
  vk::ShaderModule shader_module_;
};

class Renderer {
 public:
  explicit Renderer(quill::Logger*);
  ~Renderer();

  bool drawFrame();

  inline void flagResized() { framebuffer_resized_ = true; }

// Internal
 private:
  quill::Logger* logger_;
  // Initialisation Variables
  Instance instance_;
  PhysicalDevice physical_device_;
  LogicalDevice device_;

  // Presentation Variables
  Swapchain swapchain_;
  bool framebuffer_resized_ = false;

  // Graphics Variables
  ShaderModule shader_module_;
  vk::Buffer vertex_buffer_;
  vk::DeviceMemory vertex_buffer_memory_;
  vk::PipelineLayout graphics_pipeline_layout_;
  vk::Pipeline graphics_pipeline_;

  // Drawing Variables
  vk::CommandPool command_pool_;
  std::vector<vk::CommandBuffer> command_buffers_;

  // Synchronisation Variables
  std::vector<vk::Semaphore> present_complete_semaphores_;
  std::vector<vk::Semaphore> render_finished_semaphores_;
  std::vector<vk::Fence> draw_fences_;
  uint32_t frame_index_ = 0;

  bool createVertexBuffer();

  uint32_t findMemoryType(uint32_t type_filter, vk::MemoryPropertyFlags prop_flags);

  vk::PipelineLayout createGraphicsPipelineLayout();

  bool createGraphicsPipeline();

  bool createCommandPool();

  bool createCommandBuffers();

  void createSyncObjects();

  // Drawing Methods
  void transitionImageLayout(uint32_t image_index, vk::ImageLayout old_layout,
    vk::ImageLayout new_layout, vk::AccessFlags2 src_access_mask,
    vk::AccessFlags2 dst_access_mask, vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask);

  bool recordCommandBuffer(uint32_t image_index);
};

#endif  // SRC_VULKAN_RENDER_HPP_
