/* Copyright 2026 Alix Boivin */

#include "../../src/graphics/vulkan_renderer.hpp"

#include <array>
#include <cstddef>
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

#include <volk.h>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

namespace {
struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

const std::array<graphics::vk_renderer::Vertex, 8> vertices = {
    graphics::vk_renderer::Vertex{glm::vec3(-0.5f, -0.5f, -0.25f), glm::vec3(1.0f, 0.0f, 0.0f)},
    graphics::vk_renderer::Vertex{glm::vec3(-0.5f,  0.5f, -0.25f), glm::vec3(0.0f, 1.0f, 0.0f)},
    graphics::vk_renderer::Vertex{glm::vec3( 0.5f, -0.5f, -0.25f), glm::vec3(0.0f, 0.0f, 1.0f)},
    graphics::vk_renderer::Vertex{glm::vec3( 0.5f,  0.5f, -0.25f), glm::vec3(1.0f, 1.0f, 1.0f)},

    {glm::vec3{-0.5f, -0.5f,  0.25f}, glm::vec3{1.0f, 0.0f, 0.0f}},
    {glm::vec3{-0.5f,  0.5f,  0.25f}, glm::vec3{0.0f, 1.0f, 0.0f}},
    {glm::vec3{ 0.5f, -0.5f,  0.25f}, glm::vec3{0.0f, 0.0f, 1.0f}},
    {glm::vec3{ 0.5f,  0.5f,  0.25f}, glm::vec3{1.0f, 1.0f, 1.0f}},
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
                     vertex_buffer_);
    quill::info(log, "Loading index data...");
    loadDataOntoDevice(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     index_buffer_);
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

BufferHandle VulkanRenderer::createBuffer(const VkDeviceSize size,
        const VkMemoryPropertyFlags props,
        const VkBufferUsageFlags usage) noexcept {
    BufferHandle buf;
    VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr};
    assert(vkCreateBuffer(device_, &info, nullptr, &buf.buffer) == VK_SUCCESS);

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device_, buf.buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, props)};
    assert(vkAllocateMemory(device_, &alloc_info, nullptr, &buf.memory)
        == VK_SUCCESS);

    assert(vkBindBufferMemory(device_, buf.buffer, buf.memory, buf.offset)
        == VK_SUCCESS);
    return buf;
}

void VulkanRenderer::copyBuffer(const VkBuffer& src, const VkBuffer& dst,
                                const VkDeviceSize buffer_size) noexcept {
    assert(buffer_size > 0);

    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Allocating command buffer...");
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VkCommandBuffer cmd_buf;
    assert(vkAllocateCommandBuffers(device_, &alloc_info, &cmd_buf)
        == VK_SUCCESS);

    quill::info(log, "Beginning command buffer recording...");
    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr};
    assert(vkBeginCommandBuffer(cmd_buf, &begin_info) == VK_SUCCESS);

    quill::info(log, "Recording copy command...");
    VkBufferCopy copy_info{0, 0, buffer_size};
    vkCmdCopyBuffer(cmd_buf, src, dst, 1, &copy_info);
    vkEndCommandBuffer(cmd_buf);

    quill::info(log, "Submitting to graphics queue...");
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr};
    assert(vkQueueSubmit(graphics_queue_, 1, &submit_info, nullptr)
        == VK_SUCCESS);
    quill::info(log, "Waiting for queue...");
    assert(vkQueueWaitIdle(graphics_queue_) == VK_SUCCESS);
}

template<typename T, size_t N>
void VulkanRenderer::loadDataOntoDevice(const std::array<T, N> data,
                                      const VkBufferUsageFlags usage,
                                      BufferHandle& buf) noexcept {
    VkDeviceSize buffer_size = sizeof(data[0]) * data.size();

    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Creating staging buffer...");
    BufferHandle staging_buf = createBuffer(buffer_size,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    quill::info(log, "Creating target buffer...");
    buf = createBuffer(buffer_size,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    quill::info(log, "Moving data onto staging buffer...");
    void* mem;
    assert(vkMapMemory(device_, staging_buf.memory, staging_buf.offset,
                       buffer_size, 0, &mem) == VK_SUCCESS);
    SDL_memcpy(mem, data.data(), buffer_size);
    vkUnmapMemory(device_, staging_buf.memory);

    quill::info(log, "Copying staging buffer onto target buffer...");
    copyBuffer(staging_buf.buffer, buf.buffer, buffer_size);
    vkDestroyBuffer(device_, staging_buf.buffer, nullptr);
    vkFreeMemory(device_, staging_buf.memory, nullptr);
}

void VulkanRenderer::createUniformBuffers() noexcept {
    if (!uniform_buffers_.empty()) {
        for (BufferHandle buf : uniform_buffers_) {
            vkDestroyBuffer(device_, buf.buffer, nullptr);
            vkUnmapMemory(device_, buf.memory);
            vkFreeMemory(device_, buf.memory, nullptr);
        }
        uniform_buffers_.clear();
        uniform_buffer_maps_.clear();
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        VkDeviceSize buf_size = sizeof(UniformBufferObject);
        BufferHandle buf = createBuffer(buf_size,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        uniform_buffers_.emplace_back(std::move(buf));
        void* data;
        vkMapMemory(device_, uniform_buffers_[i].memory,
                    uniform_buffers_[i].offset, buf_size, 0, &data);
        uniform_buffer_maps_.emplace_back(std::move(data));
    }
}

void VulkanRenderer::createCommandPool() noexcept {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_qf_idx_};
    vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
    assert(command_pool_ != nullptr);
}

void VulkanRenderer::createDepthResources() noexcept {
    if (depth_image_.image != nullptr) {
        vkDestroyImageView(device_, depth_image_.view, nullptr);
        vkFreeMemory(device_, depth_image_.memory, nullptr);
        vkDestroyImage(device_, depth_image_.image, nullptr);
    }

    const std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    depth_image_.format = findDesiredFormat(candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    uint32_t queue_i = graphics_qf_idx_;
    VkImageCreateInfo img_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_image_.format,
        .extent = {swapchain_.extent.width, swapchain_.extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_i,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    vkCreateImage(device_, &img_info, nullptr, &depth_image_.image);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device_, depth_image_.image, &mem_req);
    VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    vkAllocateMemory(device_, &alloc_info, nullptr, &depth_image_.memory);
    vkBindImageMemory(device_, depth_image_.image, depth_image_.memory, 0);
    depth_image_.view = createImageView(depth_image_.image,
                                        depth_image_.format,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanRenderer::createCommandBuffers() noexcept {
    assert(command_buffers_.empty());

    command_buffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(command_buffers_.size())};

    assert(vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data())
        == VK_SUCCESS);
}

void VulkanRenderer::transitionImageLayout(const VkImage& img,
        const VkImageLayout old_layout, const VkImageLayout new_layout,
        const VkAccessFlags2 src_access_mask,
        const VkAccessFlags2 dst_access_mask,
        const VkPipelineStageFlags2 src_stage_mask,
        const VkPipelineStageFlags2 dst_stage_mask,
        const VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 barrier{
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
        .subresourceRange = {aspect, 0, 1, 0, 1}};

    VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(command_buffers_[frame_i_], &dep_info);
}

void VulkanRenderer::recordCommandBuffer(uint32_t img_i) {
    VkCommandBuffer& cmd_buf = command_buffers_[frame_i_];
    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr};
    vkBeginCommandBuffer(cmd_buf, &begin_info);

    transitionImageLayout(swapchain_.images[img_i],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    transitionImageLayout(depth_image_.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkClearValue col_value;
        col_value.color = {0.0f, 0.0f, 0.0f, 1.0f};
    VkClearValue depth_value;
        depth_value.depthStencil = {1.0f, 0};

    VkRenderingAttachmentInfo col_attach_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = swapchain_.image_views[img_i],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = {},
        .resolveImageView = {},
        .resolveImageLayout = {},
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = col_value};

    VkRenderingAttachmentInfo depth_attach_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_image_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = {},
        .resolveImageView = {},
        .resolveImageLayout = {},
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = depth_value};

    VkRenderingInfo render_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = {{0, 0}, swapchain_.extent},
        .layerCount = 1,
        .viewMask = {},
        .colorAttachmentCount = 1,
        .pColorAttachments = &col_attach_info,
        .pDepthAttachment = &depth_attach_info,
        .pStencilAttachment = nullptr};
    vkCmdBeginRendering(cmd_buf, &render_info);

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_.pipeline);
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer_.buffer,
                           &vertex_buffer_.offset);
    vkCmdBindIndexBuffer(cmd_buf, index_buffer_.buffer, index_buffer_.offset,
                         VK_INDEX_TYPE_UINT16);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapchain_.extent.width),
        .height = static_cast<float>(swapchain_.extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f};

    VkRect2D scissor = {
        .offset = VkOffset2D{0, 0},
        .extent = swapchain_.extent};

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

    assert(vkEndCommandBuffer(cmd_buf) == VK_SUCCESS);
}

void VulkanRenderer::createSyncObjects() noexcept {
    assert(sem_present_done_.empty()
           && sem_render_done_.empty() && draw_fences_.empty());

    VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                   nullptr, 0};

    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    for (uint32_t i = 0; i < swapchain_.images.size(); i++) {
        VkSemaphore sem;
        assert(vkCreateSemaphore(device_, &sem_info, nullptr, &sem) == VK_SUCCESS);
        sem_render_done_.push_back(sem);
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        VkSemaphore sem;
        VkFence fence;
        assert(vkCreateSemaphore(device_, &sem_info, nullptr, &sem) == VK_SUCCESS);
        sem_present_done_.push_back(sem);
        assert(vkCreateFence(device_, &fence_info, nullptr, &fence)
            == VK_SUCCESS);
        draw_fences_.push_back(fence);
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
    ubo.view = glm::lookAt(cam_.pos, cam_.dir, cam_.up);
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f,
                                10.0f);

    ubo.proj[1][1] *= -1;  // GLM assumes inverted axis so we flip it

    SDL_memcpy(uniform_buffer_maps_[img_idx % kMaxFramesInFlight], &ubo,
               sizeof(ubo));
}

bool VulkanRenderer::drawFrame() {
    assert(command_buffers_.size() == kMaxFramesInFlight);
    assert(uniform_buffers_.size() == kMaxFramesInFlight);

    assert(vkWaitForFences(device_, 1, &draw_fences_[frame_i_],
                           VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    assert(vkResetFences(device_, 1, &draw_fences_[frame_i_]) == VK_SUCCESS);

    uint32_t img_i;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_.swapchain,
                                            UINT64_MAX,
                                            sem_present_done_[frame_i_],
                                            nullptr, &img_i);
    switch (result) {
        case VK_ERROR_OUT_OF_DATE_KHR:
            recreateSwapchain();
            return false;
        case VK_SUBOPTIMAL_KHR:
            recreateSwapchain();
            return false;
        case VK_SUCCESS:
            break;
        default:
            throw std::runtime_error("Failed to acquire next image!");
    }

    assert(vkResetCommandBuffer(command_buffers_[frame_i_], 0) == VK_SUCCESS);
    recordCommandBuffer(img_i);

    updateUniformBuffer(img_i);

    VkPipelineStageFlags wait_dst_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sem_present_done_[frame_i_],
        .pWaitDstStageMask = &wait_dst_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffers_[frame_i_],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sem_render_done_[img_i]};

    assert(vkQueueSubmit(graphics_queue_, 1, &submit_info,
                         draw_fences_[frame_i_]) == VK_SUCCESS);

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sem_render_done_[img_i],
        .swapchainCount = 1,
        .pSwapchains = &swapchain_.swapchain,
        .pImageIndices = &img_i,
        .pResults = nullptr};

    assert(vkQueuePresentKHR(graphics_queue_, &present_info) == VK_SUCCESS);
    switch (result) {
        case VK_ERROR_OUT_OF_DATE_KHR:
            [[fallthrough]];
        case VK_SUBOPTIMAL_KHR:
            recreateSwapchain();
            return false;
        case VK_SUCCESS:
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
