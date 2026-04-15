/* Copyright 2026 Alix Boivin */

#ifndef SRC_RENDERING_LOGICAL_DEVICE_HPP_
#define SRC_RENDERING_LOGICAL_DEVICE_HPP_

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "../rendering/physical_device.hpp"
#include "vulkan/vulkan.hpp"

namespace rendering::internal_logical_device {

class LogicalDevice {
    using PhysicalDevice = internal_physical_device::PhysicalDevice;

  public:
    LogicalDevice(const PhysicalDevice&) noexcept;
    ~LogicalDevice() noexcept;

    explicit operator const vk::Device() const { return device_; }

    void waitIdle() const noexcept { device_.waitIdle(); }

    // Fences
    vk::Fence createFence(const vk::FenceCreateInfo&) const noexcept;
    void destroyFence(vk::Fence f) const { device_.destroyFence(f); }
    void waitForFences(const std::vector<vk::Fence>&, const vk::Bool32 wait_all,
                       const uint32_t timeout = UINT32_MAX) const noexcept;
    void resetFences(const std::vector<vk::Fence>&) const noexcept;

    // Semaphores
    vk::Semaphore createSemaphore(const vk::SemaphoreCreateInfo&) const noexcept;
    void destroySemaphore(vk::Semaphore s) const noexcept { device_.destroySemaphore(s); }

    // Memory
    BufferHandle createBuffer(const vk::BufferCreateInfo&,
                              const vk::MemoryPropertyFlags) const noexcept;
    void destroyBuffer(vk::Buffer b) const { device_.destroyBuffer(b); }

    std::vector<vk::CommandBuffer> createCommandBuffers(
        vk::CommandBufferAllocateInfo&) const noexcept;

    // Command stuff
    void submitCommands(const uint32_t num_submits, const vk::SubmitInfo*,
                        const vk::Fence) const noexcept;

    vk::Result present(const vk::PresentInfoKHR&) const noexcept;

    void createVertexBuffer(const std::vector<Vertex>& vertices) noexcept;
    void bindVertexBuffer(const vk::CommandBuffer& buf) const noexcept {
        buf.bindVertexBuffers(0, 1, &vertex_buffer_.buffer, vertex_buffer_offsets_.data());
    }

  private:
    const PhysicalDevice& physical_device_;

    void createCommandPool() noexcept;

    uint32_t findMemoryType(const uint32_t type_filter,
                            const vk::MemoryPropertyFlags prop_flags) const noexcept;

    vk::MemoryRequirements bufferMemoryRequirements(const vk::Buffer) const noexcept;

    void copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize) noexcept;

    vk::Device device_;
    uint32_t graphics_qf_idx_;
    uint32_t transfer_qf_idx_;
    vk::Queue graphics_queue_;
    vk::Queue transfer_queue_;
    vk::CommandPool command_pool_;
    std::vector<vk::DeviceSize> vertex_buffer_offsets_ = {0};
    BufferHandle vertex_buffer_;
};
}  // namespace rendering::internal_logical_device

#endif  // SRC_RENDERING_LOGICAL_DEVICE_HPP_
