#ifndef SRC_VULKAN_RENDER_HPP_
#define SRC_VULKAN_RENDER_HPP_

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

namespace graphics::vk_renderer {
struct BufferHandle {
    vk::Buffer buffer;
    vk::DeviceSize offset;
    vk::DeviceMemory memory;
};

struct SwapchainHandle {
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> image_views;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
};

class VulkanRenderer {
  public:
    explicit VulkanRenderer(SDL_Window* window);
    ~VulkanRenderer();

    inline void flagResized() { framebuffer_resized_ = true; }

    bool drawFrame();

  private:
    void createInstance();

    void selectPhysicalDevice();

    uint32_t getQueueFamilyIndex(vk::PhysicalDevice device);

    void createLogicalDevice();

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat();

    vk::PresentModeKHR chooseSwapPresentMode();

    void createSwapchain(vk::SwapchainKHR old_swapchain);

    void recreateSwapchain();

    void createImageViews();

    vk::ShaderModule createShaderModule(const std::vector<char>& code);

    BufferHandle createBuffer(
        const vk::DeviceSize size,
        const vk::MemoryPropertyFlags props,
        const vk::BufferUsageFlags usage);

    void copyBuffer(
        const vk::Buffer& src,
        const vk::Buffer& dst,
        const vk::DeviceSize buffer_size);

    void createVertexBuffer();

    uint32_t findMemoryType(
        const uint32_t type_filter,
        const vk::MemoryPropertyFlags prop_flags);

    vk::PipelineLayout createGraphicsPipelineLayout();

    bool createGraphicsPipeline();

    void createCommandPool();

    void createCommandBuffers();

    // Drawing Methods
    void transitionImageLayout(
        uint32_t image_index,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask);

    bool recordCommandBuffer(uint32_t image_index);

    bool createSyncObjects();

    vk::Instance instance_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    uint32_t graphics_qf_idx_ = UINT32_MAX;
    vk::Queue graphics_queue_;
    VkSurfaceKHR surface_;
    SwapchainHandle swapchain_;

    vk::ShaderModule shader_module_;
    BufferHandle vertex_buffer_;
    vk::PipelineLayout graphics_pipeline_layout_;
    vk::Pipeline graphics_pipeline_;
    vk::CommandPool command_pool_;
    std::vector<vk::CommandBuffer> command_buffers_;
    std::vector<vk::Semaphore> present_complete_semaphores_;
    std::vector<vk::Semaphore> render_finished_semaphores_;
    std::vector<vk::Fence> draw_fences_;

    uint32_t frame_index_ = 0;
    bool framebuffer_resized_ = false;
};
}  // namespace graphics::vk_renderer

#endif  // SRC_VULKAN_RENDER_HPP_
