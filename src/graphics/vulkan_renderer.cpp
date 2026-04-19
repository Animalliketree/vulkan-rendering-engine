#include "vulkan_renderer.hpp"
#include "vulkan/vulkan.hpp"

#include <fcntl.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <ratio>
#include <vulkan/vulkan.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

const std::vector<graphics::vk_renderer::Vertex> vertices = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{-0.5f, 0.5f},  {0.0f, 1.0f, 0.0f}},
    {{0.5f,  -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{0.5f,  0.5f},  {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {0, 2, 3, 3, 1, 0};
}  // namespace

namespace graphics::vk_renderer {
VulkanRenderer::VulkanRenderer(SDL_Window* window)  {
    assert(window != nullptr);

    quill::Logger* log = quill::simple_logger();

    createInstance();

    bool success = SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_);
    if (!success) throw std::runtime_error(
        "Failed to create Vulkan surface: " + std::to_string(*SDL_GetError()));

    selectPhysicalDevice();
    createLogicalDevice();
    device_.getQueue(graphics_qf_idx_, 0, &graphics_queue_);

    createSwapchain(nullptr);
    createImageViews();
    createCommandPool();
    loadDataToDevice(vertices, vk::BufferUsageFlagBits::eVertexBuffer, vertex_buffer_);
    loadDataToDevice(indices, vk::BufferUsageFlagBits::eIndexBuffer, index_buffer_);
    quill::info(log, "Creating descriptor pool...");
    createDescriptorPool();
    quill::info(log, "Creating uniform buffers...");
    createUniformBuffers();
    quill::info(log, "Creating descriptor set layout...");
    createDescriptorSetLayout();
    quill::info(log, "Creating descriptor sets...");
    createDescriptorSets();
    quill::info(log, "Creating graphics pipeline...");
    createGraphicsPipeline();
    quill::info(log, "Creating command buffers...");
    createCommandBuffers();
    quill::info(log, "Creating synchronization objects...");
    createSyncObjects();
}

VulkanRenderer::~VulkanRenderer() {
    device_.waitIdle();
    for (VkFence fence : draw_fences_) device_.destroyFence(fence);
    for (VkSemaphore semaphore : present_complete_semaphores_)
        device_.destroySemaphore(semaphore);
    for (VkSemaphore semaphore : render_finished_semaphores_)
        device_.destroySemaphore(semaphore);
    device_.destroyCommandPool(command_pool_);
    device_.destroyPipeline(graphics_pipeline_);
    device_.destroyPipelineLayout(graphics_pipeline_layout_);
    device_.destroyShaderModule(shader_module_);
    for (VkImageView view : swapchain_.image_views)
        device_.destroyImageView(view);
    device_.destroyBuffer(index_buffer_.buffer);
    device_.freeMemory(index_buffer_.memory);
    device_.destroyBuffer(vertex_buffer_.buffer);
    device_.freeMemory(vertex_buffer_.memory);
    device_.destroySwapchainKHR(swapchain_.swapchain);
    device_.destroy();
    SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    instance_.destroy();
}

BufferHandle VulkanRenderer::createBuffer(const vk::DeviceSize size,
                                          const vk::MemoryPropertyFlags props,
                                          const vk::BufferUsageFlags usage) noexcept {
    BufferHandle buf;
    vk::BufferCreateInfo buf_info;
    buf_info.size = size;
    buf_info.usage = usage;
    buf_info.sharingMode = vk::SharingMode::eExclusive;
    buf.buffer = device_.createBuffer(buf_info);

    vk::MemoryRequirements mem_req = device_.getBufferMemoryRequirements(buf.buffer);

    vk::MemoryAllocateInfo alloc_info;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, props);
    buf.memory = device_.allocateMemory(alloc_info);

    buf.offset = 0;
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
void VulkanRenderer::loadDataToDevice(const std::vector<T> data, const vk::BufferUsageFlags usage,
                                      BufferHandle& dst) noexcept {
    assert(device_ != nullptr);

    vk::DeviceSize buffer_size = sizeof(data[0]) * data.size();

    BufferHandle staging_buf = createBuffer(buffer_size,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        vk::BufferUsageFlagBits::eTransferSrc);
    dst = createBuffer(buffer_size,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        usage | vk::BufferUsageFlagBits::eTransferDst);

    void* mem = device_.mapMemory(staging_buf.memory, staging_buf.offset, buffer_size);
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
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
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

void VulkanRenderer::createCommandBuffers() noexcept {
    assert(device_ != nullptr && command_buffers_.empty());

    vk::CommandBufferAllocateInfo alloc_info = {};
    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = kMaxFramesInFlight;

    command_buffers_ = device_.allocateCommandBuffers(alloc_info);
    assert(command_buffers_.size() == kMaxFramesInFlight);
}

void VulkanRenderer::transitionImageLayout(const uint32_t image_index,
        const vk::ImageLayout old_layout, const vk::ImageLayout new_layout,
        const vk::AccessFlags2 src_access_mask, const vk::AccessFlags2 dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask) {
    vk::ImageMemoryBarrier2 barrier = {};
    barrier.srcStageMask = src_stage_mask;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstStageMask = dst_stage_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_.images[image_index];
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::DependencyInfo dependency_info = {};
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;
    command_buffers_[frame_index_].pipelineBarrier2(&dependency_info);
}

bool VulkanRenderer::recordCommandBuffer(uint32_t image_index) {
    vk::CommandBufferBeginInfo begin_info = {};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    vk::Result result = command_buffers_[frame_index_].begin(&begin_info);
    assert(result == vk::Result::eSuccess);

    transitionImageLayout(image_index, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal, {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    vk::ClearValue clear_value;
    clear_value.color = {0.0f, 0.0f, 0.0f, 1.0f};
    vk::RenderingAttachmentInfo attachment_info = {};
    attachment_info.imageView = swapchain_.image_views[image_index],
    attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
    attachment_info.loadOp = vk::AttachmentLoadOp::eClear,
    attachment_info.storeOp = vk::AttachmentStoreOp::eStore,
    attachment_info.clearValue = clear_value;

    vk::RenderingInfo rendering_info = {};
    rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain_.extent};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &attachment_info;
    command_buffers_[frame_index_].beginRendering(&rendering_info);

    command_buffers_[frame_index_].bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                graphics_pipeline_);
    command_buffers_[frame_index_].bindVertexBuffers(0, 1, &vertex_buffer_.buffer,
                                                     &vertex_buffer_.offset);
    command_buffers_[frame_index_].bindIndexBuffer(index_buffer_.buffer, index_buffer_.offset,
                                                   vk::IndexType::eUint16);

    vk::Viewport viewport = {0.0f, 0.0f, static_cast<float>(swapchain_.extent.width),
                             static_cast<float>(swapchain_.extent.height), 0.0f, 1.0f};

    vk::Rect2D scissor = {vk::Offset2D{0, 0}, swapchain_.extent};

    command_buffers_[frame_index_].setViewport(0, 1, &viewport);
    command_buffers_[frame_index_].setScissor(0, 1, &scissor);
    command_buffers_[frame_index_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                      graphics_pipeline_layout_, 0,
                                                      descriptor_sets_[frame_index_], nullptr);
    command_buffers_[frame_index_].drawIndexed(static_cast<uint32_t>(indices.size()),
                                               1, 0, 0, 0);
    command_buffers_[frame_index_].endRendering();

    transitionImageLayout(image_index, vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eColorAttachmentWrite,
        {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe);

    vkEndCommandBuffer(command_buffers_[frame_index_]);
    return true;
}

bool VulkanRenderer::createSyncObjects() noexcept {
    assert(present_complete_semaphores_.empty()
            && render_finished_semaphores_.empty()
            && draw_fences_.empty());
    vk::SemaphoreCreateInfo semaphore_info = {};

    vk::FenceCreateInfo fence_info = {};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    render_finished_semaphores_.resize(swapchain_.images.size());
    for (size_t i = 0; i < swapchain_.images.size(); i++) {
        render_finished_semaphores_[i] = device_.createSemaphore(semaphore_info);
        assert(render_finished_semaphores_[i] != nullptr);
    }

    present_complete_semaphores_.resize(kMaxFramesInFlight);
    draw_fences_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++) {
        present_complete_semaphores_[i] = device_.createSemaphore(semaphore_info);
        draw_fences_[i] = device_.createFence(fence_info);
        assert(present_complete_semaphores_[i] != nullptr
               && draw_fences_[i] != nullptr);
    }

    return true;
}

using Time = std::chrono::time_point<
    std::chrono::system_clock,
    std::chrono::duration<long, std::ratio<1, 1000000000>>>;

void VulkanRenderer::updateUniformBuffer(uint32_t img_idx, Time start) {
    assert(swapchain_.swapchain != nullptr);

    Time end = std::chrono::high_resolution_clock::now();

    float time = std::chrono::duration<float, std::chrono::seconds::period>(end - start).count();

    UniformBufferObject ubo;
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f),
        static_cast<float>(swapchain_.extent.width) / static_cast<float>(swapchain_.extent.height),
        0.1f, 10.0f);

    SDL_memcpy(uniform_buffer_maps_[img_idx], &ubo, sizeof(ubo));
}

bool VulkanRenderer::drawFrame() {
    assert(device_ != nullptr);
    assert(command_buffers_.size() == kMaxFramesInFlight);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Drawing frame...");

    Time start_time = std::chrono::high_resolution_clock::now();

    vk::Result result = device_.waitForFences(1, &draw_fences_[frame_index_],
                                              VK_TRUE, UINT64_MAX);
    assert(result == vk::Result::eSuccess);

    uint32_t image_index;
    result = device_.acquireNextImageKHR(swapchain_.swapchain, UINT64_MAX,
                                         present_complete_semaphores_[frame_index_],
                                         nullptr, &image_index);
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

    result = device_.resetFences(1, &draw_fences_[frame_index_]);
    assert(result == vk::Result::eSuccess);

    command_buffers_[frame_index_].reset();
    recordCommandBuffer(image_index);

    updateUniformBuffer(image_index, start_time);

    vk::PipelineStageFlags wait_dst_stage_mask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo = {};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &present_complete_semaphores_[frame_index_];
    submitInfo.pWaitDstStageMask = &wait_dst_stage_mask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffers_[frame_index_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &render_finished_semaphores_[image_index];

    result = graphics_queue_.submit(1, &submitInfo, draw_fences_[frame_index_]);

    vk::PresentInfoKHR present_info = {};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_.swapchain;
    present_info.pImageIndices = &image_index;

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

    frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
    return true;
}
}  // namespace graphics::vk_renderer
