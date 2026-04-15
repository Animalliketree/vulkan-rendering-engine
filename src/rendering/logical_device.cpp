/* Copyright 2026 Alix Boivin */

#include "../rendering/logical_device.hpp"
#include "vulkan/vulkan.hpp"

#include <cassert>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace {
}  // namespace

namespace rendering::internal_logical_device {
LogicalDevice::LogicalDevice(const internal_physical_device::PhysicalDevice& physical_device) noexcept :
        physical_device_(physical_device) {

    createCommandPool();
}

LogicalDevice::~LogicalDevice() noexcept {
    device_.waitIdle();
    device_.destroyBuffer(vertex_buffer_.buffer);
    device_.freeMemory(vertex_buffer_.memory);
    device_.destroyCommandPool(command_pool_);
    device_.destroy();
}


void LogicalDevice::createCommandPool() noexcept {
    vk::CommandPoolCreateInfo pool_info = {};

    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = graphics_qf_idx_;

    command_pool_ = device_.createCommandPool(pool_info);
}

std::vector<vk::CommandBuffer> LogicalDevice::createCommandBuffers(
        vk::CommandBufferAllocateInfo& alloc_info) const noexcept {
    alloc_info.commandPool = command_pool_;

    return device_.allocateCommandBuffers(alloc_info);
}


void LogicalDevice::copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) noexcept {
    assert(src != nullptr && dst != nullptr && size > 0);

    using UsageFlags = vk::CommandBufferUsageFlagBits;

    vk::CommandBufferAllocateInfo alloc_info;
    vk::CommandBuffer command_copy_buffer;
    vk::CommandBufferBeginInfo begin_info;
    vk::SubmitInfo submit_info;

    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 1;
    command_copy_buffer = createCommandBuffers(alloc_info)[0];

    begin_info.flags = UsageFlags::eOneTimeSubmit;
    command_copy_buffer.begin(begin_info);

    command_copy_buffer.copyBuffer(src, dst, vk::BufferCopy(0, 0, size));
    command_copy_buffer.end();

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_copy_buffer;
    submitCommands(1, &submit_info, nullptr);
    graphics_queue_.waitIdle();
}

}  // namespace rendering::internal_logical_device
