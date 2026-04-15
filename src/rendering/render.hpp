/* Copyright 2026 Alix Boivin */

#ifndef SRC_VULKAN_RENDER_HPP_
#define SRC_VULKAN_RENDER_HPP_

#include "graphics_pipeline.hpp"
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
#include "swapchain.hpp"

namespace rendering {
struct BufferHandle {
    vk::Buffer buffer;
    vk::DeviceMemory memory;
};

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription bindingDescription() {
        return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 2>
        attributeDescription() {
        return {{{0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)},
            {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)}}};
    }
};

class Renderer {
  public:
    Renderer(const quill::Logger*) noexcept;
    Renderer(Renderer&) = delete;
    ~Renderer() noexcept;

    void drawFrame() noexcept;

    void flagResized() noexcept { window_resized_ = true; }

  private:
    const quill::Logger* logger_;
    SDL_Window* window_;
    vk::Instance instance_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    internal_swapchain::Swapchain swapchain_;
    internal_graphics_pipeline::GraphicsPipeline graphics_pipeline_;

    void createInstance() noexcept;
    void choosePhysicalDevice() noexcept;
    void createDevice() noexcept;
    void createVertexBuffer(const std::vector<Vertex>& vertices) noexcept;
    BufferHandle createBuffer(const vk::BufferCreateInfo&,
                              const vk::MemoryPropertyFlags flags) const noexcept;
    void createCommandPool() noexcept;
    void createCommandBuffers();
    void createSyncObjects();

    void getGraphicsQueueFamilyIndex() noexcept;
    uint32_t findMemoryType(const uint32_t type_filter,
                            const vk::MemoryPropertyFlags prop_flags) const noexcept;
    void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) noexcept;

    // Drawing Methods
    void transitionImageLayout(
        const uint32_t image_index,
        const vk::ImageLayout old_layout,
        const vk::ImageLayout new_layout,
        const vk::AccessFlags2 src_access_mask,
        const vk::AccessFlags2 dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask) noexcept;

    void recordCommandBuffer(uint32_t image_index);

    void beginRendering(vk::RenderingAttachmentInfo);

    std::vector<vk::CommandBuffer> command_buffers_;
    std::vector<vk::Semaphore> present_complete_semaphores_;
    std::vector<vk::Semaphore> render_finished_semaphores_;
    std::vector<vk::Fence> draw_fences_;
    uint32_t graphics_qf_idx_ = UINT32_MAX;
    uint32_t transfer_qf_idx_ = UINT32_MAX;
    bool window_resized_ = false;
    uint32_t frame_index_ = 0;

    vk::Queue graphics_queue_;
    vk::Queue transfer_queue_;
    vk::CommandPool command_pool_;
    vk::DeviceSize vertex_buffer_offset_ = 0;
    BufferHandle vertex_buffer_;
};
}  // namespace rendering

#endif  // SRC_VULKAN_RENDER_HPP_
