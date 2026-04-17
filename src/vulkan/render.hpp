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

class VulkanRenderer {
  public:
    explicit VulkanRenderer(SDL_Window* window);
    ~VulkanRenderer();

    inline void flagResized() { framebuffer_resized_ = true; }

    bool drawFrame();

// Internal
  private:
    vk::Instance instance_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    uint32_t graphics_qf_idx_ = UINT32_MAX;
    vk::Queue graphics_queue_;
    VkSurfaceKHR surface_;
    vk::SwapchainKHR swapchain_;
    std::vector<vk::Image> swapchain_images_;
    std::vector<vk::ImageView> swapchain_image_views_;
    vk::SurfaceFormatKHR swapchain_format_;
    vk::Extent2D swapchain_extent_;
    vk::ShaderModule shader_module_;
    vk::Buffer vertex_buffer_;
    vk::DeviceMemory vertex_buffer_memory_;
    vk::PipelineLayout graphics_pipeline_layout_;
    vk::Pipeline graphics_pipeline_;
    vk::CommandPool command_pool_;
    std::vector<vk::CommandBuffer> command_buffers_;
    std::vector<vk::Semaphore> present_complete_semaphores_;
    std::vector<vk::Semaphore> render_finished_semaphores_;
    std::vector<vk::Fence> draw_fences_;
    uint32_t frame_index_ = 0;
    bool framebuffer_resized_ = false;

    bool createInstance();

    bool selectPhysicalDevice();

    uint32_t getQueueFamilyIndex(vk::PhysicalDevice device);

    bool createLogicalDevice();

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat();

    vk::PresentModeKHR chooseSwapPresentMode();

    bool createSwapchain(vk::SwapchainKHR old_swapchain);

    bool recreateSwapchain();

    bool createImageViews();

    vk::ShaderModule createShaderModule(const std::vector<char>& code);

    bool createVertexBuffer();

    uint32_t findMemoryType(uint32_t type_filter, vk::MemoryPropertyFlags prop_flags);

    vk::PipelineLayout createGraphicsPipelineLayout();

    bool createGraphicsPipeline();

    void createCommandPool();

    void createCommandBuffers();

    // Drawing Methods
    void transitionImageLayout(uint32_t image_index, vk::ImageLayout old_layout,
        vk::ImageLayout new_layout, vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask, vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask);

    bool recordCommandBuffer(uint32_t image_index);

    bool createSyncObjects();
};

#endif  // SRC_VULKAN_RENDER_HPP_
