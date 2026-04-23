/* Copyright 2026 Alix Boivin */

#include <cassert>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "../graphics/vulkan_renderer.hpp"
#include "vulkan/vulkan.hpp"

namespace graphics::vk_renderer {
void VulkanRenderer::createDescriptorPool() noexcept {
    vk::DescriptorPoolSize size{vk::DescriptorType::eUniformBuffer,
                                kMaxFramesInFlight};

    vk::DescriptorPoolCreateInfo create_info{{}, kMaxFramesInFlight, 1, &size};

    descriptor_pool_ = device_.device().createDescriptorPool(create_info);
    assert(descriptor_pool_ != nullptr);
}

void VulkanRenderer::createDescriptorSetLayout() noexcept {
    vk::DescriptorSetLayoutBinding ubo_binding{0,
        vk::DescriptorType::eUniformBuffer, 1,
        vk::ShaderStageFlagBits::eVertex};

    vk::DescriptorSetLayoutCreateInfo create_info{{}, 1, &ubo_binding};

    descriptor_set_layout_ = device_.device().createDescriptorSetLayout(create_info);

    assert(descriptor_set_layout_ != nullptr);
}

void VulkanRenderer::createDescriptorSets() noexcept {
    assert(descriptor_pool_ != nullptr);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight,
                                                 descriptor_set_layout_);

    vk::DescriptorSetAllocateInfo alloc_info{descriptor_pool_,
        kMaxFramesInFlight, layouts.data()};

    descriptor_sets_ = device_.device().allocateDescriptorSets(alloc_info);

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        vk::DescriptorBufferInfo buf_info{uniform_buffers_[i].buffer, 0,
                                          vk::WholeSize};

        vk::WriteDescriptorSet write_info{descriptor_sets_[i], 0, 0, 1,
                                          vk::DescriptorType::eUniformBuffer,
                                          {}, &buf_info};

        device_.device().updateDescriptorSets(write_info, {});
    }

    assert(descriptor_sets_.size() == kMaxFramesInFlight);
}
}  // namespace graphics::vk_renderer
