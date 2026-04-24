/* Copyright 2026 Alix Boivin */

#include <algorithm>
#include <cassert>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "../graphics/vulkan_renderer.hpp"
#include "vulkan/vulkan.hpp"

namespace {
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR cap) {
    if (cap.currentExtent.width != UINT32_MAX)
        return cap.currentExtent;
    else return {
        std::clamp<uint32_t>(kWindowWidth, cap.minImageExtent.width,
                            cap.maxImageExtent.width),
        std::clamp<uint32_t>(kWindowHeight, cap.minImageExtent.height,
                            cap.maxImageExtent.height)
    };
}

vk::SurfaceFormatKHR chooseFromList(std::vector<vk::SurfaceFormatKHR> fmts) {
    assert(fmts.size() > 0);

    const vk::Format kDesiredFmt = vk::Format::eB8G8R8A8Srgb;
    for (vk::SurfaceFormatKHR format : fmts) {
        if (format.format == kDesiredFmt) return format;
    }

    return fmts[0];
}

vk::PresentModeKHR chooseFromList(std::vector<vk::PresentModeKHR> modes) {
    assert(modes.size() > 0);

    const vk::PresentModeKHR kDesiredMode = vk::PresentModeKHR::eMailbox;
    for (vk::PresentModeKHR mode : modes) {
        if (mode == kDesiredMode) return mode;
    }

    return vk::PresentModeKHR::eFifo;
}
}  // namespace

namespace graphics::vk_renderer {
vk::ImageView VulkanRenderer::createImageView(const vk::Image& img,
                                              const vk::Format fmt,
                                  const vk::ImageAspectFlags aspect) noexcept {
    vk::ImageViewCreateInfo info{{}, img, vk::ImageViewType::e2D, fmt,
                                 {vk::ComponentSwizzle::eIdentity},
                                 {aspect, 0, 1, 0, 1}};

    return device_.createImageView(info);
}

void VulkanRenderer::createImageViews() noexcept {
    assert(swapchain_.image_views.empty());

    for (vk::Image img : swapchain_.images) {
        swapchain_.image_views.push_back(createImageView(img,
                swapchain_.format.format, vk::ImageAspectFlagBits::eColor));
    }

    assert(swapchain_.image_views.size() == swapchain_.images.size());
}

void VulkanRenderer::createSwapchain(vk::SwapchainKHR old_swapchain) noexcept {
    assert(surface_ != nullptr);

    vk::SurfaceCapabilitiesKHR capabilities =
        physical_device_.getSurfaceCapabilitiesKHR(surface_);
    assert(capabilities != nullptr);

    swapchain_.extent = chooseSwapExtent(capabilities);

    swapchain_.format = chooseFromList(
        physical_device_.getSurfaceFormatsKHR(surface_));

    uint32_t min_images = capabilities.maxImageCount > 0
        ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
        : capabilities.minImageCount + 1;

    vk::SwapchainCreateInfoKHR swapchain_info{{}, surface_, min_images,
        swapchain_.format.format, swapchain_.format.colorSpace,
        swapchain_.extent, 1, vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive, 0, {}, capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        chooseFromList(physical_device_.getSurfacePresentModesKHR(surface_)),
        vk::True, old_swapchain};

    swapchain_.swapchain = device_.createSwapchainKHR(swapchain_info);
    assert(swapchain_.swapchain != nullptr);

    device_.destroySwapchainKHR(old_swapchain);

    swapchain_.images = device_.getSwapchainImagesKHR(swapchain_.swapchain);
}

void VulkanRenderer::recreateSwapchain() {
    device_.waitIdle();

    for (vk::ImageView view : swapchain_.image_views)
        device_.destroyImageView(view);
    swapchain_.image_views.clear();

    createSwapchain(swapchain_.swapchain);
    createImageViews();
    createDepthResources();
}
}  // namespace graphics::vk_renderer
