/* Copyright 2026 Alix Boivin */

#include "render.hpp"
#include "vulkan/vulkan.hpp"
#include "graphics_pipeline.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_surface.h>
#include <fcntl.h>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

namespace {
#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

constexpr char kAppTitle[] = "Voxel Engine";
const char* kEngineTitle = "Hephaestus";
constexpr int kWindowWidth = 600;
constexpr int kWindowHeight = 400;
constexpr uint32_t kMaxFramesInFlight = 2;

const std::vector<char const*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};

const std::vector<rendering::Vertex> vertices = {
    {{0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

SDL_Window* setupWindow() {
    SDL_Window* window;
    SDL_SetAppMetadata(kAppTitle, "0.0.1", "");
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(kAppTitle, kWindowWidth, kWindowHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    assert(window != nullptr && SDL_GetError());
    return window;
}

vk::ApplicationInfo buildApplicationInfo() noexcept {
    vk::ApplicationInfo app_info = {};
    app_info.pApplicationName = kAppTitle;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = kEngineTitle;
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_4;

    return app_info;
}

bool validationLayersSupported() noexcept {
    auto layer_props = vk::enumerateInstanceLayerProperties();

    for (const char* target : kValidationLayers) {
        bool layer_available = false;
        for (vk::LayerProperties layer : layer_props) {
            if (SDL_strcmp(target, layer.layerName) == 0) {
                layer_available = true;
                break;
            }
        }

        if (!layer_available) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> instanceExtensions(const bool layers_supported) noexcept {
    uint32_t num_instance_extensions;
    const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(
    &num_instance_extensions);
    assert(instance_extensions != nullptr && SDL_GetError());

    std::vector<const char*> extensions;
    extensions.assign(instance_extensions, instance_extensions + num_instance_extensions);
    if (layers_supported) {
        extensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }

    return extensions;
}

bool evaluateDeviceExtensions(const vk::PhysicalDevice device,
                              const std::vector<const char*> target_extensions) noexcept {
    assert(device != nullptr);

    auto props = device.enumerateDeviceExtensionProperties();
    for (const char* extension : target_extensions) {
        bool available = false;
        for (vk::ExtensionProperties prop : props) {
            if (strcmp(extension, prop.extensionName) == 0) {
                available = true;
                break;
            }
        }

        if (!available) return false;
    }

    return true;
}

bool evaluatePhysicalDeviceProperties(const vk::PhysicalDevice device) noexcept {
    assert(device != nullptr);

    vk::PhysicalDeviceProperties props = device.getProperties();

    if (props.apiVersion < VK_API_VERSION_1_4) return false;
    else return true;
}

bool evaluateQueueFamilies(const vk::Instance& instance,
                           const vk::PhysicalDevice device) noexcept {
    assert(instance != nullptr && device != nullptr);

    auto props = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < props.size(); i++) {
        bool supports_graphics = SDL_Vulkan_GetPresentationSupport(instance, device, i);
        if (supports_graphics) return true;
    }

    return false;
}

bool evaluateDeviceFeatures(const vk::PhysicalDevice device) noexcept {
    assert(device != nullptr);

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
    vk::PhysicalDeviceVulkan13Features vk_1_3_features;
    vk::PhysicalDeviceFeatures2 features;
    vk_1_3_features.pNext = &dynamic_state_features;
    features.pNext = &vk_1_3_features;

    device.getFeatures2(&features);
    if (!dynamic_state_features.extendedDynamicState || !vk_1_3_features.dynamicRendering) {
        return false;
    }
    else return true;
}

struct PhysicalDeviceFeatures {
    vk::PhysicalDeviceFeatures2 features_2;
    vk::PhysicalDeviceVulkan13Features vk_1_3;
    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state;

    void build() {
        features_2.pNext = &vk_1_3;
        vk_1_3.pNext = &extended_dynamic_state;
        vk_1_3.dynamicRendering = VK_TRUE;
        vk_1_3.synchronization2 = VK_TRUE;
        extended_dynamic_state.extendedDynamicState = VK_TRUE;
    }
};
}  // namespace

namespace rendering {
void Renderer::createInstance() noexcept {
    vk::ApplicationInfo app_info;
    vk::InstanceCreateInfo instance_info;
    std::vector<const char*> extensions;

    bool layers_supported = validationLayersSupported();

    app_info = buildApplicationInfo();
    extensions = instanceExtensions(layers_supported);

    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();

    if (kEnableValidationLayers) {
        instance_info.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        instance_info.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        instance_info.enabledLayerCount = 0;
        instance_info.ppEnabledLayerNames = nullptr;
    }

    instance_ = vk::createInstance(instance_info);
}

void Renderer::choosePhysicalDevice() noexcept {
    assert(instance_ != nullptr);
    std::vector<vk::PhysicalDevice> devices = instance_.enumeratePhysicalDevices();
    assert(devices.size() > 0);

    bool valid_device_found = false;
    for (vk::PhysicalDevice device : devices) {
        bool properties_good = evaluatePhysicalDeviceProperties(device);
        bool extensions_good = evaluateDeviceExtensions(device, kRequiredDeviceExtensions);
        bool queue_families_good = evaluateQueueFamilies(instance_, device);
        bool features_good = evaluateDeviceFeatures(device);
        if (!properties_good || !extensions_good || !queue_families_good ||
            !features_good) continue;

        physical_device_ = device;
        valid_device_found = true;
        break;
    }

    assert(valid_device_found);
}

void Renderer::getGraphicsQueueFamilyIndex() noexcept {
    auto props = physical_device_.getQueueFamilyProperties();

    for (uint32_t i = 0; i < props.size(); i++) {
        if ((props[i].queueFlags & vk::QueueFlagBits::eGraphics)
            == vk::QueueFlagBits::eGraphics)
                graphics_qf_idx_ = i;
    }

    abort();
}

void Renderer::createDevice() noexcept {
    assert(physical_device_ != nullptr);

    getGraphicsQueueFamilyIndex();

    constexpr float kQueuePriority = 0.5f;
    vk::DeviceQueueCreateInfo queue_info = {};
    queue_info.queueCount = 1;
    queue_info.queueFamilyIndex = graphics_qf_idx_;
    queue_info.pQueuePriorities = &kQueuePriority;

    PhysicalDeviceFeatures features;
    features.build();

    vk::DeviceCreateInfo device_info = {};
    device_info.pNext = &features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<uint32_t>(
    kRequiredDeviceExtensions.size());
    device_info.ppEnabledExtensionNames =
    kRequiredDeviceExtensions.data();
    device_ = physical_device_.createDevice(device_info);

    device_.getQueue(graphics_qf_idx_, 0, &graphics_queue_);
    transfer_queue_ = graphics_queue_;
    transfer_qf_idx_ = graphics_qf_idx_;
}

uint32_t Renderer::findMemoryType(const uint32_t type_filter,
                                  const vk::MemoryPropertyFlags prop_flags) const noexcept {
    assert(type_filter != 0);

    auto mem_props = physical_device_.getMemoryProperties();

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if (type_filter & (1 << i) &&
        (mem_props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
        return i;
    }

    return UINT32_MAX;
}

void Renderer::copyBuffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) noexcept {
    assert(src != nullptr && dst != nullptr && size > 0);

    using UsageFlags = vk::CommandBufferUsageFlagBits;

    vk::CommandBufferAllocateInfo alloc_info;
    vk::CommandBuffer command_copy_buffer;
    vk::CommandBufferBeginInfo begin_info;
    vk::SubmitInfo submit_info;

    alloc_info.commandPool = command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 1;
    command_copy_buffer = device_.allocateCommandBuffers(alloc_info)[0];

    begin_info.flags = UsageFlags::eOneTimeSubmit;
    command_copy_buffer.begin(begin_info);

    command_copy_buffer.copyBuffer(src, dst, vk::BufferCopy(0, 0, size));
    command_copy_buffer.end();

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_copy_buffer;
    vk::Result result = graphics_queue_.submit(1, &submit_info, nullptr);
    assert(result == vk::Result::eSuccess);
    graphics_queue_.waitIdle();
}

BufferHandle Renderer::createBuffer(const vk::BufferCreateInfo& create_info,
                                         const vk::MemoryPropertyFlags flags) const noexcept {
    assert(create_info.size > 0);
    BufferHandle handle;
    handle.buffer = device_.createBuffer(create_info, nullptr);
    assert(handle.buffer != nullptr);

    vk::MemoryRequirements mem_req = device_.getBufferMemoryRequirements(handle.buffer);
    vk::MemoryAllocateInfo alloc_info = {};
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_req.memoryTypeBits, flags);
    handle.memory = device_.allocateMemory(alloc_info);
    assert(handle.memory != VK_NULL_HANDLE);

    device_.bindBufferMemory(handle.buffer, handle.memory, 0);

    return handle;
}

void Renderer::createCommandPool() noexcept {
    assert(device_ != nullptr);

    vk::CommandPoolCreateInfo pool_info = {};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = graphics_qf_idx_;

    command_pool_ = device_.createCommandPool(pool_info);
}

void Renderer::createCommandBuffers() {
    assert(command_buffers_.empty());

    vk::CommandBufferAllocateInfo alloc_info = {};
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = kMaxFramesInFlight;
    alloc_info.commandPool = command_pool_;

    command_buffers_ = device_.allocateCommandBuffers(alloc_info);

    assert(command_buffers_.size() > 0);
}

void Renderer::createVertexBuffer(const std::vector<Vertex>& vertices) noexcept {
    assert(vertex_buffer_.buffer == nullptr);

    using UsageFlags = vk::BufferUsageFlagBits;
    using MemFlags = vk::MemoryPropertyFlagBits;
    using MemoryPointer = void*;

    vk::DeviceSize buffer_size = sizeof(vertices[0]) * vertices.size();
    vk::BufferCreateInfo buffer_info = {};
    MemoryPointer data;

    buffer_info.size = buffer_size;
    buffer_info.usage = UsageFlags::eTransferSrc;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;
    BufferHandle staging_buffer = createBuffer(buffer_info,
        MemFlags::eHostVisible | MemFlags::eHostCoherent);

    data = device_.mapMemory(staging_buffer.memory, 0, buffer_info.size);
    memcpy(data, vertices.data(), buffer_info.size);
    device_.unmapMemory(staging_buffer.memory);

    buffer_info.usage = UsageFlags::eVertexBuffer | UsageFlags::eTransferDst;
    vertex_buffer_ = createBuffer(buffer_info, MemFlags::eDeviceLocal);

    copyBuffer(staging_buffer.buffer, vertex_buffer_.buffer, buffer_size);
    device_.destroyBuffer(staging_buffer.buffer);
    device_.freeMemory(staging_buffer.memory);
}

Renderer::Renderer(const quill::Logger *logger) noexcept :
        logger_(logger),
        window_(setupWindow()) {
    createInstance();
    choosePhysicalDevice();
    createDevice();
    swapchain_.create(window_, instance_, physical_device_, device_);
    createVertexBuffer(vertices);
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    graphics_pipeline_.create(device_, swapchain_);
}

Renderer::~Renderer() noexcept {
    device_.waitIdle();
    for (VkFence fence : draw_fences_) device_.destroyFence(fence);
    for (VkSemaphore semaphore : present_complete_semaphores_)
        device_.destroySemaphore(semaphore);
    for (VkSemaphore semaphore : render_finished_semaphores_)
        device_.destroySemaphore(semaphore);
    instance_.destroy();
}

/* Drawing */

void Renderer::transitionImageLayout(const uint32_t image_index,
                                     const vk::ImageLayout old_layout,
                                     const vk::ImageLayout new_layout,
                                     const vk::AccessFlags2 src_access_mask,
                                     const vk::AccessFlags2 dst_access_mask,
                                     const vk::PipelineStageFlags2 src_stage_mask,
                                     const vk::PipelineStageFlags2 dst_stage_mask) noexcept {
    vk::ImageMemoryBarrier2 barrier = {};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.srcStageMask = src_stage_mask;
    barrier.dstStageMask = dst_stage_mask;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_.image(image_index);
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

void Renderer::beginRendering(vk::RenderingAttachmentInfo attachment_info) {
    assert(command_buffers_[frame_index_] != nullptr);

    vk::RenderingInfo rendering_info = {};
    rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain_.extent()};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &attachment_info;
    command_buffers_[frame_index_].beginRendering(&rendering_info);
}

void Renderer::recordCommandBuffer(uint32_t image_index) {
    vk::CommandBufferBeginInfo begin_info = {};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    vk::Result result = command_buffers_[frame_index_].begin(&begin_info);
    assert(result == vk::Result::eSuccess);

    transitionImageLayout(
        image_index,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    auto attachment_info = swapchain_.renderingAttachmentInfo(image_index);

    beginRendering(attachment_info);

    graphics_pipeline_.bind(command_buffers_[frame_index_]);
    command_buffers_[frame_index_].bindVertexBuffers(0, 1, &vertex_buffer_.buffer,
                                                     &vertex_buffer_offset_);

    vk::Viewport viewport = {0.0f, 0.0f,
        static_cast<float>(swapchain_.extent().width),
        static_cast<float>(swapchain_.extent().height),
        0.0f, 1.0f
    };
    vk::Rect2D scissor = {vk::Offset2D{0, 0}, swapchain_.extent()};
    command_buffers_[frame_index_].setViewport(0, 1, &viewport);
    command_buffers_[frame_index_].setScissor(0, 1, &scissor);
    command_buffers_[frame_index_].draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    command_buffers_[frame_index_].endRendering();

    transitionImageLayout(image_index, vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::ePresentSrcKHR,
                          vk::AccessFlagBits2::eColorAttachmentWrite, {},
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eBottomOfPipe);

    command_buffers_[frame_index_].end();
}

void Renderer::createSyncObjects() {
    assert(present_complete_semaphores_.empty()
            && render_finished_semaphores_.empty()
            && draw_fences_.empty());
    vk::SemaphoreCreateInfo semaphore_info = {};

    vk::FenceCreateInfo fence_info = {};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    render_finished_semaphores_.resize(swapchain_.numImages());
    for (size_t i = 0; i < swapchain_.numImages(); i++) {
        render_finished_semaphores_[i] = device_.createSemaphore(semaphore_info);
    }

    present_complete_semaphores_.resize(kMaxFramesInFlight);
    draw_fences_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++) {
        present_complete_semaphores_[i] = device_.createSemaphore(semaphore_info);
        draw_fences_[i] = device_.createFence(fence_info);
    }
}

void Renderer::drawFrame() noexcept {
    std::vector<vk::Fence> fence = {draw_fences_[frame_index_]};
    vk::Result result = device_.waitForFences(1, fence.data(), VK_TRUE, UINT64_MAX);

    uint32_t image_index = swapchain_.nextImageIndex(window_, physical_device_, device_, present_complete_semaphores_[frame_index_]);
    if (image_index == UINT32_MAX) return;

    device_.resetFences(fence);

    command_buffers_[frame_index_].reset();
    recordCommandBuffer(image_index);

    vk::PipelineStageFlags wait_dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo = {};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &present_complete_semaphores_[frame_index_];
    submitInfo.pWaitDstStageMask = &wait_dst_stage_mask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffers_[frame_index_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &render_finished_semaphores_[image_index];

    result = graphics_queue_.submit(1, &submitInfo, draw_fences_[frame_index_]);
    assert(result == vk::Result::eSuccess);

    vk::SwapchainKHR swapchain = swapchain_;
    vk::PresentInfoKHR present_info = {};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[image_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    result = graphics_queue_.presentKHR(present_info);
    switch (result) {
    case vk::Result::eErrorOutOfDateKHR:
        swapchain_.refresh(window_, physical_device_, device_);
        return;
    case vk::Result::eSuboptimalKHR:
        swapchain_.refresh(window_, physical_device_, device_);
        return;
    case vk::Result::eSuccess:
        break;
    default:
        assert(result == vk::Result::eSuccess
               || result == vk::Result::eErrorOutOfDateKHR
               || result == vk::Result::eSuboptimalKHR);
    }

    if (window_resized_) {
        window_resized_ = false;
        swapchain_.refresh(window_, physical_device_, device_);
    }

    frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
}
}  // namespace rendering
