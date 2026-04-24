/* Copyright 2026 Alix Boivin */

#ifndef SRC_GRAPHICS_VULKAN_RENDERER_HPP_
#define SRC_GRAPHICS_VULKAN_RENDERER_HPP_

#include "vulkan/vulkan.hpp"
#include "vulkan_device_handle.hpp"
#include <SDL3/SDL_video.h>
#include <cstdint>
#include <chrono>
#include <glm/ext/vector_float3.hpp>
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

struct DepthImage {
    vk::Image image = nullptr;
    vk::DeviceMemory memory = nullptr;
    vk::ImageView view = nullptr;
    vk::Format format;
};

struct Camera {
    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 dir = glm::vec3(0.0f);
    glm::vec3 up = glm::vec3(0.0f);
};

class VulkanRenderer {
 public:
    explicit VulkanRenderer(SDL_Window* window) noexcept;
    ~VulkanRenderer() noexcept;

    inline void flagResized() noexcept { framebuffer_resized_ = true; }

    bool drawFrame();

 private:
    const vulkan::device::VulkanDeviceHandle device_;
    const uint32_t kMaxFramesInFlight = 2;

    using Time = std::chrono::time_point<
        std::chrono::system_clock,
        std::chrono::duration<int64_t, std::ratio<1, 1000000000>>>;

    // Requires device_
    void createSwapchain(vk::SwapchainKHR old_swapchain) noexcept;

    void createImageViews() noexcept;

    void createCommandPool() noexcept;

    void createDepthResources() noexcept;

    void createCommandBuffers() noexcept;

    void createUniformBuffers() noexcept;

    void createDescriptorPool() noexcept;

    void createDescriptorSetLayout() noexcept;

    void createDescriptorSets() noexcept;

    void createGraphicsPipeline() noexcept;

    void createSyncObjects() noexcept;

    void createShaderModule(const std::vector<char>& code) noexcept;

    vk::ImageView createImageView(
        const vk::Image& img,
        const vk::Format format,
        const vk::ImageAspectFlags flags) noexcept;

    BufferHandle createBuffer(
        const vk::DeviceSize size,
        const vk::MemoryPropertyFlags props,
        const vk::BufferUsageFlags usage) noexcept;

    vk::PipelineLayout createGraphicsPipelineLayout() noexcept;

    void copyBuffer(
        const vk::Buffer& src,
        const vk::Buffer& dst,
        const vk::DeviceSize buffer_size) noexcept;

    template<typename T, size_t N>
    void loadDataOntoDevice(
        const std::array<T, N> data,
        const vk::BufferUsageFlags usage,
        BufferHandle& dst) noexcept;

    // Drawing Methods
    void transitionImageLayout(
        const vk::Image& img,
        const vk::ImageLayout old_layout,
        const vk::ImageLayout new_layout,
        const vk::AccessFlags2 src_access_mask,
        const vk::AccessFlags2 dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask,
        const vk::ImageAspectFlags aspect);

    void recreateSwapchain();

    void recordCommandBuffer(uint32_t image_index);

    void updateUniformBuffer(uint32_t img_idx);

    uint32_t frame_i_ = 0;
    VkSurfaceKHR surface_ = nullptr;
    SwapchainHandle swapchain_{};

    vk::CommandPool command_pool_ = nullptr;
    std::vector<vk::CommandBuffer> command_buffers_;

    BufferHandle vertex_buffer_{};
    BufferHandle index_buffer_{};

    std::vector<BufferHandle> uniform_buffers_;
    std::vector<void*> uniform_buffer_maps_;

    vk::DescriptorPool descriptor_pool_ = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_ = nullptr;
    std::vector<vk::DescriptorSet> descriptor_sets_;

    GraphicsPipelineHandle graphics_pipeline_ = {};

    std::vector<vk::Semaphore> sem_present_done_;
    std::vector<vk::Semaphore> sem_render_done_;
    std::vector<vk::Fence> draw_fences_;

    DepthImage depth_image_;

    Time start_time_ = std::chrono::high_resolution_clock::now();

    bool framebuffer_resized_ = false;

    Camera cam_;
};
}  // namespace graphics::vk_renderer

#endif  // SRC_GRAPHICS_VULKAN_RENDERER_HPP_
