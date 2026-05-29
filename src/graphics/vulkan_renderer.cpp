/* Copyright 2026 Alix Boivin */

#include "../../src/graphics/vulkan_renderer.hpp"

#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <volk.h>

#include <cassert>
#include <chrono>
#include <cstdint>

#include <array>
#include <memory>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

namespace {
using Vertex = graphics::vk_renderer::Vertex;


const std::array<Vertex, 8> vertices = {Vertex
    {glm::vec3(-0.5f, -0.5f, -0.25f), glm::vec3(1.0f, 0.0f, 0.0f), {.0f, .0f}},
    {glm::vec3(-0.5f, 0.5f, -0.25f), glm::vec3(0.0f, 1.0f, 0.0f), {.0f, .0f}},
    {glm::vec3(0.5f, -0.5f, -0.25f), glm::vec3(0.0f, 0.0f, 1.0f), {.0f, .0f}},
    {glm::vec3(0.5f, 0.5f, -0.25f), glm::vec3(1.0f, 1.0f, 1.0f), {.0f, .0f}},

    {glm::vec3{-0.5f, -0.5f, 0.25f}, glm::vec3{1.0f, 0.0f, 0.0f}, {.0f, .0f}},
    {glm::vec3{-0.5f, 0.5f, 0.25f}, glm::vec3{0.0f, 1.0f, 0.0f}, {.0f, .0f}},
    {glm::vec3{0.5f, -0.5f, 0.25f}, glm::vec3{0.0f, 0.0f, 1.0f}, {.0f, .0f}},
    {glm::vec3{0.5f, 0.5f, 0.25f}, glm::vec3{1.0f, 1.0f, 1.0f}, {.0f, .0f}},
};

const std::array<uint16_t, 12> indices = {
    0, 2, 3, 3, 1, 0,
    4, 6, 7, 7, 5, 4,
};
}  // namespace

namespace graphics::vk_renderer {
VulkanRenderer::VulkanRenderer(SDL_Window* window) noexcept {
    assert(window != nullptr);

    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Creating surface...");
    bool success = SDL_Vulkan_CreateSurface(window, instance_,
                                            nullptr, &surface_);
    if (!success) {
        quill::info(log, "Failed to create Vulkan surface: {}",
                    SDL_GetError());
        abort();
    }

    quill::info(log, "Creating swapchain...");
    createSwapchain(nullptr);
    quill::info(log, "Creating image views...");
    createImageViews();
    quill::info(log, "Creating command pool...");
    createCommandPool();
    quill::info(log, "Creating depth resources...");
    createDepthResources();
    quill::info(log, "Creating command buffers...");
    createCommandBuffers();
    quill::info(log, "Loading vertex data...");
    loadDataOntoDevice(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       std::make_unique<BufferHandle>(vertex_buffer_));
    quill::info(log, "Loading index data...");
    loadDataOntoDevice(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       std::make_unique<BufferHandle>(index_buffer_));
    quill::info(log, "Creating uniform buffers...");
    createUniformBuffers();
    quill::info(log, "Creating descriptor pool...");
    createDescriptorPool();
    quill::info(log, "Creating descriptor set layout...");
    createDescriptorSetLayout();
    quill::info(log, "Creating descriptor sets...");
    createDescriptorSets();
    quill::info(log, "Creating graphics pipeline...");
    createGraphicsPipeline();
    quill::info(log, "Creating synchronization objects...");
    createSyncObjects();

    cam_ = Camera{glm::vec3{2.0f}, glm::vec3{0.0f}, {0.0f, 0.0f, 1.0f}};
}

VulkanRenderer::~VulkanRenderer() noexcept {
    vkDeviceWaitIdle(device_);

    vkDestroyImageView(device_, depth_image_.view, nullptr);
    vkDestroyImage(device_, depth_image_.image, nullptr);
    vkFreeMemory(device_, depth_image_.memory, nullptr);

    for (VkFence fence : draw_fences_) vkDestroyFence(device_, fence, nullptr);
    for (VkSemaphore sem : sem_render_done_)
        vkDestroySemaphore(device_, sem, nullptr);
    for (VkSemaphore sem : sem_present_done_)
        vkDestroySemaphore(device_, sem, nullptr);

    vkDestroyPipeline(device_, graphics_pipeline_.pipeline, nullptr);
    vkDestroyPipelineLayout(device_, graphics_pipeline_.layout, nullptr);
    vkDestroyShaderModule(device_, graphics_pipeline_.shader_module, nullptr);

    vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);

    for (BufferHandle buf : uniform_buffers_) {
        vkUnmapMemory(device_, buf.memory);
        vkDestroyBuffer(device_, buf.buffer, nullptr);
        vkFreeMemory(device_, buf.memory, nullptr);
    }

    vkDestroyBuffer(device_, index_buffer_.buffer, nullptr);
    vkFreeMemory(device_, index_buffer_.memory, nullptr);
    vkDestroyBuffer(device_, vertex_buffer_.buffer, nullptr);
    vkFreeMemory(device_, vertex_buffer_.memory, nullptr);

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    for (VkImageView view : swapchain_.image_views)
        vkDestroyImageView(device_, view, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_.swapchain, nullptr);
    SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
}

void VulkanRenderer::copyBuffer(const VkBuffer& src, const VkBuffer& dst,
                                const VkDeviceSize buffer_size) noexcept {
    assert(buffer_size > 0);

    VkCommandBuffer cmd_buf;
    const VkBufferCopy copy_info{0, 0, buffer_size};

    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Allocating command buffer...");
    const VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    constexpr VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr
    };

    VkResult r = vkAllocateCommandBuffers(device_, &alloc_info, &cmd_buf);
    assert(r == VK_SUCCESS);

    quill::info(log, "Beginning command buffer recording...");
    r = vkBeginCommandBuffer(cmd_buf, &begin_info);
    assert(r == VK_SUCCESS);

    quill::info(log, "Recording copy command...");
    vkCmdCopyBuffer(cmd_buf, src, dst, 1, &copy_info);
    vkEndCommandBuffer(cmd_buf);

    quill::info(log, "Submitting to graphics queue...");
    r = vkQueueSubmit(graphics_queue_, 1, &submit_info, nullptr);
    assert(r == VK_SUCCESS);

    quill::info(log, "Waiting for queue...");
    r = vkQueueWaitIdle(graphics_queue_);
    assert(r == VK_SUCCESS);
}

template<typename T, size_t N>
void VulkanRenderer::loadDataOntoDevice(
    const std::array<T, N> data,
    const VkBufferUsageFlags usage,
    std::shared_ptr<BufferHandle> buf
) noexcept {
    VkDeviceSize buffer_size = sizeof(data[0]) * data.size();

    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Creating staging buffer...");
    BufferHandle staging_buf = createBuffer(buffer_size,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    quill::info(log, "Creating target buffer...");
    *buf = createBuffer(buffer_size,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    quill::info(log, "Moving data onto staging buffer...");
    void* mem;
    VkResult r = vkMapMemory(device_, staging_buf.memory, staging_buf.offset,
                       buffer_size, 0, &mem);
    assert(r == VK_SUCCESS);
    SDL_memcpy(mem, data.data(), buffer_size);
    vkUnmapMemory(device_, staging_buf.memory);

    quill::info(log, "Copying staging buffer onto target buffer...");
    copyBuffer(staging_buf.buffer, buf->buffer, buffer_size);
    vkDestroyBuffer(device_, staging_buf.buffer, nullptr);
    vkFreeMemory(device_, staging_buf.memory, nullptr);
}

void VulkanRenderer::recreateSwapchain() noexcept {
    vkDeviceWaitIdle(device_);
    createSwapchain(swapchain_.swapchain);
    createImageViews();
    createDepthResources();
}

void VulkanRenderer::transitionImageLayout(const VkImage& img,
        const VkImageLayout old_layout, const VkImageLayout new_layout,
        const VkAccessFlags2 src_access_mask,
        const VkAccessFlags2 dst_access_mask,
        const VkPipelineStageFlags2 src_stage_mask,
        const VkPipelineStageFlags2 dst_stage_mask,
        const VkImageAspectFlags aspect) {
    assert(frame_i_ < command_buffers_.size()
           && command_buffers_[frame_i_] != nullptr);

    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img,
        .subresourceRange = {aspect, 0, 1, 0, 1}
    };
    const VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(command_buffers_[frame_i_], &dep_info);
}

void VulkanRenderer::recordCommandBuffer(uint32_t img_i) {
    const VkCommandBuffer& cmd_buf = command_buffers_[frame_i_];
    VkClearValue col_value;
        col_value.color = {0.0f, 0.0f, 0.0f, 1.0f};
    VkClearValue depth_value;
        depth_value.depthStencil = {1.0f, 0};

    constexpr VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    const VkRenderingAttachmentInfo col_attach_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = swapchain_.image_views[img_i],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = {},
        .resolveImageView = {},
        .resolveImageLayout = {},
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = col_value
    };
    const VkRenderingAttachmentInfo depth_attach_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_image_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = {},
        .resolveImageView = {},
        .resolveImageLayout = {},
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = depth_value
    };
    const VkRenderingInfo render_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr, .flags = 0,
        .renderArea = {{0, 0}, swapchain_.extent},
        .layerCount = 1,
        .viewMask = {},
        .colorAttachmentCount = 1,
        .pColorAttachments = &col_attach_info,
        .pDepthAttachment = &depth_attach_info,
        .pStencilAttachment = nullptr
    };
    const VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = static_cast<float>(swapchain_.extent.width),
        .height = static_cast<float>(swapchain_.extent.height),
        .minDepth = 0.0f, .maxDepth = 1.0f
    };
    const VkRect2D scissor = {VkOffset2D{0, 0}, swapchain_.extent};

    VkResult r = vkBeginCommandBuffer(cmd_buf, &begin_info);
    assert(r == VK_SUCCESS);

    transitionImageLayout(swapchain_.images[img_i],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    transitionImageLayout(depth_image_.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    vkCmdBeginRendering(cmd_buf, &render_info);

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_.pipeline);
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer_.buffer,
                           &vertex_buffer_.offset);
    vkCmdBindIndexBuffer(cmd_buf, index_buffer_.buffer, index_buffer_.offset,
                         VK_INDEX_TYPE_UINT16);

    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_pipeline_.layout, 0, 1,
                            &descriptor_sets_[frame_i_], 0, nullptr);
    vkCmdDrawIndexed(cmd_buf, static_cast<uint32_t>(indices.size()),
                     1, 0, 0, 0);
    vkCmdEndRendering(cmd_buf);

    transitionImageLayout(swapchain_.images[img_i],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, {},
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    r = vkEndCommandBuffer(cmd_buf);
    assert(r == VK_SUCCESS);
}

using Time = std::chrono::system_clock::time_point;

void VulkanRenderer::updateUniformBuffer(uint32_t img_idx) {
    assert(swapchain_.swapchain != nullptr);
    const float aspect_ratio =
        static_cast<float>(swapchain_.extent.width)
        / static_cast<float>(swapchain_.extent.height);

    const Time end = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(end - start_time_).count();

    ShaderData data{
        .proj = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f,
                                 10.0f),
        .view = glm::lookAt(cam_.pos, cam_.dir, cam_.up),
        .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                             glm::vec3(0.0f, 0.0f, 1.0f)),
    };

    data.proj[1][1] *= -1;  // GLM assumes inverted axis so we flip it

    SDL_memcpy(uniform_buffer_maps_[img_idx % kMaxFramesInFlight], &data,
               sizeof(data));
}

bool VulkanRenderer::drawFrame() {
    assert(command_buffers_.size() == kMaxFramesInFlight);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    uint32_t img_i;
    VkResult r;

    r = vkWaitForFences(device_, 1, &draw_fences_[frame_i_],
                           VK_TRUE, UINT64_MAX);
    assert(r == VK_SUCCESS);
    r = vkResetFences(device_, 1, &draw_fences_[frame_i_]);
    assert(r == VK_SUCCESS);

    r = vkAcquireNextImageKHR(device_, swapchain_.swapchain, UINT64_MAX,
                                   sem_present_done_[frame_i_], nullptr,
                                   &img_i);
    if ((r & (VK_ERROR_OUT_OF_DATE_KHR | VK_SUBOPTIMAL_KHR)) != 0) {
        recreateSwapchain();
        return false;
    }
    assert(r == VK_SUCCESS);

    constexpr VkPipelineStageFlags wait_dst_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sem_present_done_[frame_i_],
        .pWaitDstStageMask = &wait_dst_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffers_[frame_i_],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sem_render_done_[img_i]
    };
    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sem_render_done_[img_i],
        .swapchainCount = 1,
        .pSwapchains = &swapchain_.swapchain,
        .pImageIndices = &img_i,
        .pResults = nullptr
    };

    r = vkResetCommandBuffer(command_buffers_[frame_i_], 0);
    assert(r == VK_SUCCESS);

    updateUniformBuffer(img_i);
    recordCommandBuffer(img_i);
    r = vkQueueSubmit(graphics_queue_, 1, &submit_info,
                      draw_fences_[frame_i_]);
    assert(r == VK_SUCCESS);

    r = vkQueuePresentKHR(graphics_queue_, &present_info);
    assert(r == VK_SUCCESS);
    if ((r & (VK_ERROR_OUT_OF_DATE_KHR | VK_SUBOPTIMAL_KHR)) != 0) {
        recreateSwapchain();
        return false;
    }
    assert(r == VK_SUCCESS);

    if (framebuffer_resized_) {
        framebuffer_resized_ = false;
        recreateSwapchain();
    }

    frame_i_ = (frame_i_ + 1) % kMaxFramesInFlight;
    return true;
}
}  // namespace graphics::vk_renderer
