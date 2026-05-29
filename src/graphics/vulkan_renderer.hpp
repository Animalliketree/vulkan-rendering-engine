/* Copyright 2026 Alix Boivin */

#ifndef SRC_GRAPHICS_VULKAN_RENDERER_HPP_
#define SRC_GRAPHICS_VULKAN_RENDERER_HPP_

#include <SDL3/SDL_video.h>
#include <volk.h>

#include <cstddef>
#include <cstdint>

#include <chrono>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/glm.hpp>

#include "../../src/graphics/vulkan_context.hpp"

namespace graphics::vk_renderer {
struct ShaderData {
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 model;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;

    static VkVertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 3>
    getAttributeDescription() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}}};
    }
};

struct Voxel { glm::vec3 color; };

struct BufferHandle {
    VkBuffer buffer = nullptr;
    VkDeviceSize offset = 0;
    VkDeviceMemory memory = nullptr;
};

struct SwapchainHandle {
    VkSwapchainKHR swapchain = nullptr;
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    VkSurfaceFormatKHR format;
    VkExtent2D extent;
};

struct PipelineHandle {
    VkShaderModule shader_module = nullptr;
    VkPipelineLayout layout = nullptr;
    VkPipeline pipeline = nullptr;
};

struct DepthImage {
    VkImage image = nullptr;
    VkDeviceMemory memory = nullptr;
    VkImageView view = nullptr;
    VkFormat format;
};

struct Camera {
    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 dir = glm::vec3(0.0f);
    glm::vec3 up = glm::vec3(0.0f);
};

class VulkanRenderer : private graphics::vulkan::VulkanContext {
 public:
    explicit VulkanRenderer(SDL_Window* window) noexcept;
    ~VulkanRenderer() noexcept;

    inline void flagResized() noexcept { framebuffer_resized_ = true; }

    bool drawFrame();

 private:
    using Time = std::chrono::time_point<
        std::chrono::system_clock,
        std::chrono::duration<int64_t, std::ratio<1, 1000000000>>>;

        const uint32_t kMaxFramesInFlight = 2;
    const Time start_time_ = std::chrono::high_resolution_clock::now();

    // Requires device_
    void createSwapchain(VkSwapchainKHR old_swapchain) noexcept;

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

    VkShaderModule createShaderModule(
        const std::vector<char>& code) noexcept;

    VkImageView createImageView(
        const VkImage& img,
        const VkFormat format,
        const VkImageAspectFlags flags) noexcept;

    BufferHandle createBuffer(
        const VkDeviceSize size,
        const VkMemoryPropertyFlags props,
        const VkBufferUsageFlags usage) noexcept;

    VkPipelineLayout createGraphicsPipelineLayout() noexcept;

    void copyBuffer(
        const VkBuffer& src,
        const VkBuffer& dst,
        const VkDeviceSize buffer_size) noexcept;

    template<typename T, size_t N>
    void loadDataOntoDevice(
        const std::array<T, N> data,
        const VkBufferUsageFlags usage,
        BufferHandle& dst) noexcept;

    // Drawing Methods
    void transitionImageLayout(
        const VkImage& img,
        const VkImageLayout old_layout,
        const VkImageLayout new_layout,
        const VkAccessFlags2 src_access_mask,
        const VkAccessFlags2 dst_access_mask,
        const VkPipelineStageFlags2 src_stage_mask,
        const VkPipelineStageFlags2 dst_stage_mask,
        const VkImageAspectFlags aspect);

    void recreateSwapchain() noexcept;

    void recordCommandBuffer(uint32_t image_index);

    void updateUniformBuffer(uint32_t img_idx);

    // Command stuff
    VkCommandPool command_pool_ = nullptr;

    VkDescriptorPool descriptor_pool_ = nullptr;
    VkDescriptorSetLayout descriptor_set_layout_ = nullptr;
    std::vector<VkDescriptorSet> descriptor_sets_;

    PipelineHandle graphics_pipeline_{};

    DepthImage depth_image_;

    Camera cam_{};

    // Presentation
    uint32_t frame_i_ = 0;
    std::vector<VkCommandBuffer> command_buffers_;
    VkSurfaceKHR surface_ = nullptr;
    SwapchainHandle swapchain_{};
    bool framebuffer_resized_ = false;
    std::vector<VkSemaphore> sem_present_done_;
    std::vector<VkSemaphore> sem_render_done_;
    std::vector<VkFence> draw_fences_;

    // Memory management
    BufferHandle vertex_buffer_{};
    BufferHandle index_buffer_{};
    std::vector<BufferHandle> uniform_buffers_;
    std::vector<void*> uniform_buffer_maps_;
};
}  // namespace graphics::vk_renderer

#endif  // SRC_GRAPHICS_VULKAN_RENDERER_HPP_
