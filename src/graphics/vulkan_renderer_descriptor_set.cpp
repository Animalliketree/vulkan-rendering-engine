#include "vulkan_renderer.hpp"

#include <vulkan/vulkan.hpp>

namespace graphics::vk_renderer {
void VulkanRenderer::createDescriptorPool() {
    assert(device_ != nullptr);

    vk::DescriptorPoolSize size;
    size.type = vk::DescriptorType::eUniformBuffer;
    size.descriptorCount = kMaxFramesInFlight;

    vk::DescriptorPoolCreateInfo create_info;
    create_info.maxSets = kMaxFramesInFlight;
    create_info.poolSizeCount = 1;
    create_info.pPoolSizes = &size;

    descriptor_pool_ = device_.createDescriptorPool(create_info);
}

void VulkanRenderer::createDescriptorSetLayout() {
    assert(device_ != nullptr);

    vk::DescriptorSetLayoutBinding ubo_binding;
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo create_info;
    create_info.bindingCount = 1;
    create_info.pBindings = &ubo_binding;

    descriptor_set_layout_ = device_.createDescriptorSetLayout(create_info);

    assert(descriptor_set_layout_ != nullptr);
}

void VulkanRenderer::createDescriptorSets() {
    assert(device_ != nullptr && descriptor_pool_ != nullptr);

    std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight,
                                                 descriptor_set_layout_);

    vk::DescriptorSetAllocateInfo alloc_info;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = kMaxFramesInFlight;
    alloc_info.pSetLayouts = layouts.data();

    descriptor_sets_ = device_.allocateDescriptorSets(alloc_info);

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        vk::DescriptorBufferInfo buf_info;
        buf_info.buffer = uniform_buffers_[i].buffer;
        buf_info.offset = 0;
        buf_info.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write_info;
        write_info.dstSet = descriptor_sets_[i];
        write_info.dstBinding = 0;
        write_info.dstArrayElement = 0;
        write_info.descriptorCount = 1;
        write_info.descriptorType = vk::DescriptorType::eUniformBuffer;
        write_info.pBufferInfo = &buf_info;

        device_.updateDescriptorSets(write_info, {});
    }

    assert(descriptor_sets_.size() == kMaxFramesInFlight);
}
}  // namespace graphics::vk_renderer