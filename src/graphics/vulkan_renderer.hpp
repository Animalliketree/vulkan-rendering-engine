/* Copyright 2026 Alix Boivin */

#ifndef SRC_GRAPHICS_VULKAN_RENDERER_HPP_
#define SRC_GRAPHICS_VULKAN_RENDERER_HPP_

#include <SDL3/SDL_video.h>
#include <cstdint>
#include <chrono>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

namespace graphics::vk_renderer {
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 2>
    getAttributeDescription() {
        return {{
            {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)},
            {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)}}};
    }
};

struct BufferHandle {
    vk::Buffer buffer = nullptr;
    vk::DeviceSize offset = 0;
    vk::DeviceMemory memory = nullptr;
};

struct SwapchainHandle {
    vk::SwapchainKHR swapchain = nullptr;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> image_views;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
};

struct GraphicsPipelineHandle {
    vk::ShaderModule shader_module = nullptr;
    vk::PipelineLayout layout = nullptr;
    vk::Pipeline pipeline = nullptr;
};

class VulkanRenderer {
 public:
    explicit VulkanRenderer(SDL_Window* window) noexcept;
    ~VulkanRenderer() noexcept;

    inline void flagResized() { framebuffer_resized_ = true; }

    bool drawFrame();

 private:
    const uint32_t kMaxFramesInFlight = 2;

    using Time = std::chrono::time_point<
        std::chrono::system_clock,
        std::chrono::duration<int64_t, std::ratio<1, 1000000000>>>;

    void createInstance() noexcept;

    void selectPhysicalDevice() noexcept;

    uint32_t getQueueFamilyIndex(vk::PhysicalDevice device);

    void createLogicalDevice() noexcept;

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat();

    vk::PresentModeKHR chooseSwapPresentMode();

    void createSwapchain(vk::SwapchainKHR old_swapchain) noexcept;

    void recreateSwapchain();

    void createImageViews() noexcept;

    void createShaderModule(const std::vector<char>& code) noexcept;

    BufferHandle createBuffer(
        const vk::DeviceSize size,
        const vk::MemoryPropertyFlags props,
        const vk::BufferUsageFlags usage) noexcept;

    void copyBuffer(
        const vk::Buffer& src,
        const vk::Buffer& dst,
        const vk::DeviceSize buffer_size) noexcept;

    template<typename T>
    void loadDataToDevice(
        const std::vector<T> data,
        const vk::BufferUsageFlags usage,
        BufferHandle& dst) noexcept;

    void createUniformBuffers() noexcept;

    uint32_t findMemoryType(
        const uint32_t type_filter,
        const vk::MemoryPropertyFlags prop_flags) noexcept;

    void createDescriptorSetLayout() noexcept;

    vk::PipelineLayout createGraphicsPipelineLayout() noexcept;

    void createGraphicsPipeline() noexcept;

    void createCommandPool() noexcept;

    void createCommandBuffers() noexcept;

    void createDescriptorPool() noexcept;

    void createDescriptorSets() noexcept;

    // Drawing Methods
    void transitionImageLayout(
        const uint32_t image_index,
        const vk::ImageLayout old_layout,
        const vk::ImageLayout new_layout,
        const vk::AccessFlags2 src_access_mask,
        const vk::AccessFlags2 dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask);

    void recordCommandBuffer(uint32_t image_index);

    void createSyncObjects() noexcept;

    void updateUniformBuffer(uint32_t img_idx);

    vk::Instance instance_ = nullptr;
    vk::PhysicalDevice physical_device_ = nullptr;
    vk::Device device_ = nullptr;
    uint32_t graphics_qf_idx_ = UINT32_MAX;
    vk::Queue graphics_queue_ = nullptr;

    VkSurfaceKHR surface_ = nullptr;
    SwapchainHandle swapchain_ = {};

    vk::CommandPool command_pool_ = nullptr;
    std::vector<vk::CommandBuffer> command_buffers_;

    BufferHandle vertex_buffer_ = {};
    BufferHandle index_buffer_ = {};

    std::vector<BufferHandle> uniform_buffers_;
    std::vector<void*> uniform_buffer_maps_;

    vk::DescriptorPool descriptor_pool_ = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_ = nullptr;
    std::vector<vk::DescriptorSet> descriptor_sets_;

    GraphicsPipelineHandle graphics_pipeline_;

    std::vector<vk::Semaphore> present_complete_semaphores_;
    std::vector<vk::Semaphore> render_finished_semaphores_;
    std::vector<vk::Fence> draw_fences_;

    uint32_t frame_index_ = 0;
    bool framebuffer_resized_ = false;

    Time start_time_ = std::chrono::high_resolution_clock::now();
};
}  // namespace graphics::vk_renderer

#endif  // SRC_GRAPHICS_VULKAN_RENDERER_HPP_
