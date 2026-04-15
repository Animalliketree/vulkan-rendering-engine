/* Copyright 2026 Alix Boivin */

#include "swapchain.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include "vulkan/vulkan.hpp"


#include <cassert>
#include <cstdint>

namespace {
VkSurfaceKHR createSurface(SDL_Window* window, const vk::Instance instance) {
    assert(instance != nullptr);

    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface);

    assert(surface != nullptr);

    return surface;
}
}

namespace rendering::internal_swapchain {
vk::SurfaceFormatKHR Swapchain::chooseSurfaceFormat(const vk::PhysicalDevice& device) const noexcept {
    auto formats = device.getSurfaceFormatsKHR(surface_);

    assert(formats.size() > 0);
    vk::Format desired_format = vk::Format::eB8G8R8A8Srgb;
    for (vk::SurfaceFormatKHR format : formats) {
        if (format.format == desired_format) return format;
    }
    return formats[0];
}

vk::PresentModeKHR Swapchain::choosePresentMode(const vk::PhysicalDevice& device) const noexcept {
    auto modes = device.getSurfacePresentModesKHR(surface_);

    assert(modes.size() > 0);
    vk::PresentModeKHR desired_mode = vk::PresentModeKHR::eMailbox;
    for (uint32_t i = 0; i < modes.size(); i++) {
        if (modes[i] == desired_mode) return modes[i];
    }

    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseExtent(SDL_Window* window) const noexcept {
    assert(capabilities_ != nullptr);

    std::array<int, 2> window_dims;
    SDL_GetWindowSize(window, &window_dims[0], &window_dims[1]);

    if (capabilities_.currentExtent.width != UINT32_MAX)
        return capabilities_.currentExtent;
    else return {
        std::clamp<uint32_t>(window_dims[0], capabilities_.minImageExtent.width,
                            capabilities_.maxImageExtent.width),
        std::clamp<uint32_t>(window_dims[1], capabilities_.minImageExtent.height,
                            capabilities_.maxImageExtent.height)
    };
}

vk::SwapchainCreateInfoKHR Swapchain::buildSwapchainInfo(const vk::PhysicalDevice& device) const noexcept {
    uint32_t min_images = capabilities_.maxImageCount > 0
        ? std::min(capabilities_.minImageCount + 1, capabilities_.maxImageCount)
        : capabilities_.minImageCount + 1;

    vk::SwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface = surface_;
    swapchain_info.minImageCount = min_images;
    swapchain_info.imageFormat = format_.format;
    swapchain_info.imageColorSpace = format_.colorSpace;
    swapchain_info.imageExtent = extent_;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
    swapchain_info.preTransform = capabilities_.currentTransform;
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapchain_info.presentMode = choosePresentMode(device);
    swapchain_info.clipped = true;
    swapchain_info.oldSwapchain = nullptr;

    return swapchain_info;
}

void Swapchain::getImages(vk::Device& device) noexcept {
    images_ = device.getSwapchainImagesKHR(swapchain_);
}

void Swapchain::getImageViews(vk::Device& device) noexcept {
    vk::ImageViewCreateInfo view_info = {};
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = format_.format;
    view_info.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    view_info.components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity};

    image_views_.resize(images_.size());
    for (uint32_t i = 0; i < images_.size(); i++) {
        view_info.image = images_[i];
        image_views_[i] = device.createImageView(view_info);
    }
}

void Swapchain::clearImageViews(vk::Device& device) noexcept {
    for (vk::ImageView view : image_views_) {
        device.destroyImageView(view);
    }
    image_views_.clear();
}

void Swapchain::refresh(SDL_Window* window, vk::PhysicalDevice& phys_device, vk::Device& device) noexcept {
    device.waitIdle();
    clearImageViews(device);

    extent_ = chooseExtent(window);
    vk::SwapchainCreateInfoKHR swapchain_info = buildSwapchainInfo(phys_device);
    swapchain_info.oldSwapchain = swapchain_;

    swapchain_ = device.createSwapchainKHR(swapchain_info);
    device.destroySwapchainKHR(swapchain_info.oldSwapchain);

    getImages(device);
    getImageViews(device);
}

uint32_t Swapchain::nextImageIndex(SDL_Window* window, vk::PhysicalDevice& phys_device, vk::Device& device, const vk::Semaphore& semaphore) noexcept {
    assert(swapchain_ != nullptr);

    uint32_t image_index;
    vk::Result r = device.acquireNextImageKHR(
            swapchain_, UINT64_MAX, semaphore, nullptr, &image_index);
    switch (r) {
    case vk::Result::eErrorOutOfDateKHR:
        refresh(window, phys_device, device);
        return UINT32_MAX;
    case vk::Result::eSuboptimalKHR:
        refresh(window, phys_device, device);
        return UINT32_MAX;
    case vk::Result::eSuccess:
        break;
    default:
        assert(r == vk::Result::eSuccess
               || r == vk::Result::eErrorOutOfDateKHR
               || r == vk::Result::eSuboptimalKHR);
  }

  return image_index;
}

void Swapchain::create(SDL_Window* window, vk::Instance& instance,
                     const vk::PhysicalDevice& physical_device,
                     vk::Device& device) noexcept {
    surface_ = createSurface(window, static_cast<vk::Instance>(instance));

    vk::Result r = physical_device.getSurfaceCapabilitiesKHR(surface_, &capabilities_);
    assert(r == vk::Result::eSuccess);

    format_ = chooseSurfaceFormat(physical_device);
    extent_ = chooseExtent(window);

    vk::SwapchainCreateInfoKHR swapchain_info = buildSwapchainInfo(physical_device);

    swapchain_ = device.createSwapchainKHR(swapchain_info);

    getImages(device);
    getImageViews(device);
}

void Swapchain::destroy(vk::Device& device, vk::Instance& instance) noexcept {
    device.waitIdle();
    clearImageViews(device);
    if (swapchain_ != nullptr) {
        device.destroySwapchainKHR(swapchain_);
    }
    SDL_Vulkan_DestroySurface(instance, surface_, nullptr);
    swapchain_ = nullptr;
}

vk::RenderingAttachmentInfo Swapchain::renderingAttachmentInfo(
        const uint32_t image_index) const noexcept {
    vk::ClearValue clear_value;
    vk::RenderingAttachmentInfo attachment_info;

    clear_value.color = {0.0f, 0.0f, 0.0f, 1.0f};

    attachment_info.imageView = image_views_[image_index],
    attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
    attachment_info.loadOp = vk::AttachmentLoadOp::eClear,
    attachment_info.storeOp = vk::AttachmentStoreOp::eStore,
    attachment_info.clearValue = clear_value;

    return attachment_info;
}
}  // namespace rendering::internal_swapchain
