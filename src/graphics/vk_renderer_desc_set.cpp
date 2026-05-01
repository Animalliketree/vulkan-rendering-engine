/* Copyright 2026 Alix Boivin */

#include <cassert>
#include <vector>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#include "../graphics/vulkan_renderer.hpp"

namespace graphics::vk_renderer {
void VulkanRenderer::createDescriptorPool() noexcept {
    const VkDescriptorPoolSize size{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = kMaxFramesInFlight
    };
    const VkDescriptorPoolCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &size
    };

    assert(vkCreateDescriptorPool(device_, &create_info, nullptr,
                                  &descriptor_pool_) == VK_SUCCESS);
}

void VulkanRenderer::createDescriptorSetLayout() noexcept {
    constexpr VkDescriptorSetLayoutBinding ubo_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = nullptr
    };
    const VkDescriptorSetLayoutCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &ubo_binding
    };

    assert(vkCreateDescriptorSetLayout(device_, &create_info, nullptr,
                                       &descriptor_set_layout_) == VK_SUCCESS);
}

void VulkanRenderer::createDescriptorSets() noexcept {
    assert(descriptor_pool_ != nullptr);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight,
                                               descriptor_set_layout_);

    const VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = kMaxFramesInFlight,
        .pSetLayouts = layouts.data()
    };
    descriptor_sets_.resize(kMaxFramesInFlight);
    assert(vkAllocateDescriptorSets(device_, &alloc_info,
                                    descriptor_sets_.data()) == VK_SUCCESS);

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        const VkDescriptorBufferInfo buf_info{
            .buffer = uniform_buffers_[i].buffer,
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
        const VkWriteDescriptorSet write_info{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_sets_[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &buf_info,
            .pTexelBufferView = nullptr
        };

        vkUpdateDescriptorSets(device_, 1, &write_info, 0, nullptr);
    }

    assert(descriptor_sets_.size() == kMaxFramesInFlight);
}
}  // namespace graphics::vk_renderer
