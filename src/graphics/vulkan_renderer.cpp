/* Copyright 2026 Alix Boivin */

#include "../../src/graphics/vulkan_renderer.hpp"
#include "vulkan/vulkan.hpp"

#include <fcntl.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <quill/LogFunctions.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <vulkan/vulkan.hpp>

namespace {
struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

const std::vector<graphics::vk_renderer::Vertex> vertices = {
    {{-0.5f, -0.5f, -0.25f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.25f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.25f}, {0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.25f}, {1.0f, 1.0f, 1.0f}},

    {{-0.5f, -0.5f,  0.25f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.25f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.25f}, {0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.25f}, {1.0f, 1.0f, 1.0f}},
};

const std::vector<uint16_t> indices = {
    0, 2, 3, 3, 1, 0,
    4, 6, 7, 7, 5, 4,
};
}  // namespace

namespace graphics::vk_renderer {
VulkanRenderer::VulkanRenderer(SDL_Window* window) noexcept {
    assert(window != nullptr);

    quill::Logger* log = quill::simple_logger();

    createInstance();

    bool success = SDL_Vulkan_CreateSurface(window, instance_, nullptr,
                                            &surface_);
    if (!success) {
        quill::info(log, "Failed to create Vulkan surface: {}",
                    SDL_GetError());
        abort();
    }

    selectPhysicalDevice();
    createLogicalDevice();
    device_.getQueue(graphics_qf_idx_, 0, &graphics_queue_);

    createSwapchain(nullptr);
    createImageViews();
    createCommandPool();
    createDepthResources();
    createCommandBuffers();
    loadDataToDevice(vertices, vk::BufferUsageFlagBits::eVertexBuffer,
                     vertex_buffer_);
    loadDataToDevice(indices, vk::BufferUsageFlagBits::eIndexBuffer,
                     index_buffer_);
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSetLayout();
    createDescriptorSets();
    createGraphicsPipeline();
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() noexcept {
    device_.waitIdle();

    device_.destroyImageView(depth_image_.view);
    device_.destroyImage(depth_image_.image);
    device_.freeMemory(depth_image_.memory);

    for (VkFence fence : draw_fences_) device_.destroyFence(fence);
    for (VkSemaphore semaphore : sem_render_done_)
        device_.destroySemaphore(semaphore);
    for (VkSemaphore semaphore : sem_present_done_)
        device_.destroySemaphore(semaphore);

    device_.destroyPipeline(graphics_pipeline_.pipeline);
    device_.destroyPipelineLayout(graphics_pipeline_.layout);
    device_.destroyShaderModule(graphics_pipeline_.shader_module);

    device_.destroyDescriptorSetLayout(descriptor_set_layout_);
    device_.destroyDescriptorPool(descriptor_pool_);

    for (BufferHandle buf : uniform_buffers_) {
        device_.unmapMemory(buf.memory);
        device_.destroyBuffer(buf.buffer);
        device_.freeMemory(buf.memory);
    }

    device_.destroyBuffer(index_buffer_.buffer);
    device_.freeMemory(index_buffer_.memory);
    device_.destroyBuffer(vertex_buffer_.buffer);
    device_.freeMemory(vertex_buffer_.memory);

    device_.destroyCommandPool(command_pool_);

    for (VkImageView view : swapchain_.image_views)
        device_.destroyImageView(view);
    device_.destroySwapchainKHR(swapchain_.swapchain);
    SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);

    device_.destroy();
    instance_.destroy();
}

BufferHandle VulkanRenderer::createBuffer(const vk::DeviceSize size,
        const vk::MemoryPropertyFlags props,
        const vk::BufferUsageFlags usage) noexcept {
    BufferHandle buf;
    vk::BufferCreateInfo buf_info{{}, size, usage,
                                  vk::SharingMode::eExclusive};
    buf.buffer = device_.createBuffer(buf_info);

    vk::MemoryRequirements mem_req = device_.getBufferMemoryRequirements(
                                                                   buf.buffer);

    vk::MemoryAllocateInfo alloc_info{mem_req.size,
                                findMemoryType(mem_req.memoryTypeBits, props)};
    buf.memory = device_.allocateMemory(alloc_info);

    device_.bindBufferMemory(buf.buffer, buf.memory, buf.offset);
    return buf;
}

void VulkanRenderer::copyBuffer(const vk::Buffer& src, const vk::Buffer& dst,
                                const vk::DeviceSize buffer_size) noexcept {
    assert(device_ != nullptr && buffer_size > 0);

    vk::CommandBufferAllocateInfo alloc_info;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 1;

    vk::CommandBuffer copy_buf = device_.allocateCommandBuffers(alloc_info)[0];

    vk::CommandBufferBeginInfo begin_info;
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    copy_buf.begin(begin_info);

    copy_buf.copyBuffer(src, dst, vk::BufferCopy(0, 0, buffer_size));
    copy_buf.end();

    vk::SubmitInfo submit_info;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &copy_buf;
    graphics_queue_.submit(submit_info);
    graphics_queue_.waitIdle();
}

template<typename T>
void VulkanRenderer::loadDataToDevice(const std::vector<T> data,
                                      const vk::BufferUsageFlags usage,
                                      BufferHandle& dst) noexcept {
    assert(device_ != nullptr);

    vk::DeviceSize buffer_size = sizeof(data[0]) * data.size();

    BufferHandle staging_buf = createBuffer(buffer_size,
        vk::MemoryPropertyFlagBits::eHostVisible
        | vk::MemoryPropertyFlagBits::eHostCoherent,
        vk::BufferUsageFlagBits::eTransferSrc);
    dst = createBuffer(buffer_size,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        usage | vk::BufferUsageFlagBits::eTransferDst);

    void* mem = device_.mapMemory(staging_buf.memory, staging_buf.offset,
                                  buffer_size);
    SDL_memcpy(mem, data.data(), buffer_size);
    device_.unmapMemory(staging_buf.memory);

    copyBuffer(staging_buf.buffer, dst.buffer, buffer_size);
    device_.destroyBuffer(staging_buf.buffer);
    device_.freeMemory(staging_buf.memory);
}

void VulkanRenderer::createUniformBuffers() noexcept {
    if (!uniform_buffers_.empty()) {
        for (BufferHandle buf : uniform_buffers_) {
            device_.destroyBuffer(buf.buffer);
            device_.unmapMemory(buf.memory);
            device_.freeMemory(buf.memory);
        }
        uniform_buffers_.clear();
        uniform_buffer_maps_.clear();
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        vk::DeviceSize buf_size = sizeof(UniformBufferObject);
        BufferHandle buf = createBuffer(buf_size,
            vk::MemoryPropertyFlagBits::eHostVisible
            | vk::MemoryPropertyFlagBits::eHostCoherent,
            vk::BufferUsageFlagBits::eUniformBuffer);
        uniform_buffers_.emplace_back(std::move(buf));
        uniform_buffer_maps_.emplace_back(
            device_.mapMemory(uniform_buffers_[i].memory, 0, buf_size));
    }
}

void VulkanRenderer::createCommandPool() noexcept {
    assert(device_ != nullptr);

    vk::CommandPoolCreateInfo pool_info = {};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = graphics_qf_idx_;

    command_pool_ = device_.createCommandPool(pool_info);
    assert(command_pool_ != nullptr);
}

vk::Format VulkanRenderer::findDesiredFormat(
        const std::vector<vk::Format>& candidates,
        const vk::ImageTiling tiling,
        const vk::FormatFeatureFlags features) const noexcept {
    for (const vk::Format format : candidates) {
        vk::FormatProperties props = physical_device_.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear
                && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal
                && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    abort();  // Failed to find a valid format from the list
}

void VulkanRenderer::createDepthResources() noexcept {
    if (depth_image_.image != nullptr) {
        device_.destroyImageView(depth_image_.view);
        device_.freeMemory(depth_image_.memory);
        device_.destroyImage(depth_image_.image);
    }

    const std::vector<vk::Format> candidates = {
        vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint};
    depth_image_.format = findDesiredFormat(candidates,
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    vk::ImageCreateInfo img_info{{}, vk::ImageType::e2D, depth_image_.format,
        {swapchain_.extent.width, swapchain_.extent.height, 1},
        1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::SharingMode::eExclusive, 1, &graphics_qf_idx_,
        vk::ImageLayout::eUndefined};
    depth_image_.image = device_.createImage(img_info);

    vk::MemoryRequirements mem_req =
        device_.getImageMemoryRequirements(depth_image_.image);
    vk::MemoryAllocateInfo alloc_info{mem_req.size,
        findMemoryType(mem_req.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eHostVisible
                       | vk::MemoryPropertyFlagBits::eHostCoherent)};
    depth_image_.memory = device_.allocateMemory(alloc_info);
    device_.bindImageMemory(depth_image_.image, depth_image_.memory, 0);
    depth_image_.view = createImageView(depth_image_.image,
                                        depth_image_.format,
                                        vk::ImageAspectFlagBits::eDepth);
}

void VulkanRenderer::createCommandBuffers() noexcept {
    assert(device_ != nullptr && command_buffers_.empty());

    vk::CommandBufferAllocateInfo alloc_info = {};
    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = kMaxFramesInFlight;

    command_buffers_ = device_.allocateCommandBuffers(alloc_info);
    assert(command_buffers_.size() == kMaxFramesInFlight);
}

void VulkanRenderer::transitionImageLayout(const vk::Image& img,
        const vk::ImageLayout old_layout, const vk::ImageLayout new_layout,
        const vk::AccessFlags2 src_access_mask,
        const vk::AccessFlags2 dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask,
        const vk::ImageAspectFlags aspect) {
    vk::ImageMemoryBarrier2 barrier{
        src_stage_mask, src_access_mask, dst_stage_mask, dst_access_mask,
        old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED, img, {aspect, 0, 1, 0, 1}, nullptr};

    vk::DependencyInfo dep_info{{}, 0, {}, 0, {}, 1, &barrier, nullptr};
    command_buffers_[frame_i_].pipelineBarrier2(&dep_info);
}

void VulkanRenderer::recordCommandBuffer(uint32_t img_i) {
    vk::CommandBuffer& cmd_buf = command_buffers_[frame_i_];

    cmd_buf.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    transitionImageLayout(swapchain_.images[img_i],
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal, {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::ImageAspectFlagBits::eColor);

    transitionImageLayout(depth_image_.image, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests
        | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::PipelineStageFlagBits2::eEarlyFragmentTests
        | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::ImageAspectFlagBits::eDepth);

    vk::ClearValue clear_value = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    vk::ClearValue depth_value = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderingAttachmentInfo color_attach_info{
        swapchain_.image_views[img_i],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
        clear_value, nullptr};

    vk::RenderingAttachmentInfo depth_attach_info{depth_image_.view,
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
        depth_value, nullptr};

    vk::RenderingInfo render_info{{}, {{0, 0}, swapchain_.extent}, 1, {}, 1,
                                  &color_attach_info, &depth_attach_info};
    cmd_buf.beginRendering(&render_info);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         graphics_pipeline_.pipeline);
    cmd_buf.bindVertexBuffers(0, 1, &vertex_buffer_.buffer,
                              &vertex_buffer_.offset);
    cmd_buf.bindIndexBuffer(index_buffer_.buffer, index_buffer_.offset,
                            vk::IndexType::eUint16);

    vk::Viewport viewport = {0.0f, 0.0f,
                             static_cast<float>(swapchain_.extent.width),
                             static_cast<float>(swapchain_.extent.height),
                             0.0f, 1.0f};

    vk::Rect2D scissor = {vk::Offset2D{0, 0}, swapchain_.extent};

    cmd_buf.setViewport(0, 1, &viewport);
    cmd_buf.setScissor(0, 1, &scissor);
    cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               graphics_pipeline_.layout, 0,
                               descriptor_sets_[frame_i_], nullptr);
    cmd_buf.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    cmd_buf.endRendering();

    transitionImageLayout(swapchain_.images[img_i],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite, {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::ImageAspectFlagBits::eColor);

    vkEndCommandBuffer(cmd_buf);
}

void VulkanRenderer::createSyncObjects() noexcept {
    assert(device_ != nullptr && sem_present_done_.empty()
           && sem_render_done_.empty() && draw_fences_.empty());

    vk::FenceCreateInfo fence_info{vk::FenceCreateFlagBits::eSignaled};

    for (uint32_t i = 0; i < swapchain_.images.size(); i++) {
        sem_render_done_.push_back(device_.createSemaphore({}));
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        sem_present_done_.push_back(device_.createSemaphore({}));
        draw_fences_.push_back(device_.createFence(fence_info));
    }

    assert(sem_render_done_.size() == swapchain_.images.size());
    assert(sem_present_done_.size() == kMaxFramesInFlight);
}

using Time = std::chrono::time_point<
std::chrono::system_clock,
std::chrono::duration<int64_t, std::ratio<1, 1000000000>>>;

void VulkanRenderer::updateUniformBuffer(uint32_t img_idx) {
    assert(swapchain_.swapchain != nullptr);
    const float aspect_ratio =
        static_cast<float>(swapchain_.extent.width)
        / static_cast<float>(swapchain_.extent.height);

    Time end = std::chrono::high_resolution_clock::now();

    float time = std::chrono::duration<float, std::chrono::seconds::period>
        (end - start_time_).count();

    UniformBufferObject ubo;
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f),
        aspect_ratio, 0.1f, 10.0f);

    ubo.proj[1][1] *= -1;  // GLM assumes inverted axis so we flip it

    SDL_memcpy(uniform_buffer_maps_[img_idx % kMaxFramesInFlight], &ubo,
               sizeof(ubo));
}

bool VulkanRenderer::drawFrame() {
    assert(device_ != nullptr);
    assert(command_buffers_.size() == kMaxFramesInFlight);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    vk::Result result = device_.waitForFences(1, &draw_fences_[frame_i_],
                                              VK_TRUE, UINT64_MAX);
    assert(result == vk::Result::eSuccess);

    uint32_t img_i;
    result = device_.acquireNextImageKHR(
        swapchain_.swapchain, UINT64_MAX,
        sem_present_done_[frame_i_], nullptr, &img_i);
    switch (result) {
        case vk::Result::eErrorOutOfDateKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuboptimalKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("Failed to acquire next image!");
    }

    result = device_.resetFences(1, &draw_fences_[frame_i_]);
    assert(result == vk::Result::eSuccess);

    command_buffers_[frame_i_].reset();
    recordCommandBuffer(img_i);

    updateUniformBuffer(img_i);

    vk::PipelineStageFlags wait_dst_stage =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo{1, &sem_present_done_[frame_i_],
                              &wait_dst_stage, 1,
                              &command_buffers_[frame_i_], 1,
                              &sem_render_done_[img_i]};

    result = graphics_queue_.submit(1, &submitInfo, draw_fences_[frame_i_]);

    vk::PresentInfoKHR present_info{1, &sem_render_done_[img_i], 1,
                                    &swapchain_.swapchain, &img_i};

    result = graphics_queue_.presentKHR(&present_info);
    switch (result) {
        case vk::Result::eErrorOutOfDateKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuboptimalKHR:
            recreateSwapchain();
            return false;
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("Failed to present image to queue!");
    }

    if (framebuffer_resized_) {
        framebuffer_resized_ = false;
        recreateSwapchain();
    }

    frame_i_ = (frame_i_ + 1) % kMaxFramesInFlight;
    return true;
}
}  // namespace graphics::vk_renderer
