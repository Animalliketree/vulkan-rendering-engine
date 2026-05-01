/* Copyright 2026 Alix Boivin */

#include <cstdint>
#include <volk.h>
#include <algorithm>
#include <cassert>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "../graphics/vulkan_renderer.hpp"

namespace {
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR cap) noexcept {
    if (cap.currentExtent.width != UINT32_MAX)
        return cap.currentExtent;
    else return {
        std::clamp<uint32_t>(kWindowWidth, cap.minImageExtent.width,
                            cap.maxImageExtent.width),
        std::clamp<uint32_t>(kWindowHeight, cap.minImageExtent.height,
                            cap.maxImageExtent.height)
    };
}

VkSurfaceFormatKHR chooseFromList(const std::vector<VkSurfaceFormatKHR> fmts)
noexcept {
    assert(fmts.size() > 0);

    const VkFormat kDesiredFmt = VK_FORMAT_B8G8R8A8_SRGB;
    for (VkSurfaceFormatKHR format : fmts) {
        if (format.format == kDesiredFmt) return format;
    }

    return fmts[0];
}

VkPresentModeKHR chooseFromList(const std::vector<VkPresentModeKHR> modes)
noexcept {
    assert(modes.size() > 0);

    const VkPresentModeKHR kDesiredMode = VK_PRESENT_MODE_MAILBOX_KHR;
    for (VkPresentModeKHR mode : modes) {
        if (mode == kDesiredMode) return mode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
}  // namespace

namespace graphics::vk_renderer {
VkImageView VulkanRenderer::createImageView(const VkImage& img,
                                            const VkFormat fmt,
                                            const VkImageAspectFlags aspect)
noexcept {
    VkImageView view;

    const VkImageViewCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {aspect, 0, 1, 0, 1}
    };
    vkCreateImageView(device_, &info, nullptr, &view);
    return view;
}

void VulkanRenderer::createImageViews() noexcept {
    for (VkImageView view : swapchain_.image_views)
        vkDestroyImageView(device_, view, nullptr);
    swapchain_.image_views.clear();

    for (VkImage img : swapchain_.images) {
        swapchain_.image_views.push_back(createImageView(img,
                swapchain_.format.format, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    assert(swapchain_.image_views.size() == swapchain_.images.size());
}

void VulkanRenderer::createSwapchain(VkSwapchainKHR old_swapchain) noexcept {
    assert(surface_ != nullptr);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);
    const uint32_t min_images = caps.maxImageCount > 0
        ? std::min(caps.minImageCount + 1, caps.maxImageCount)
        : caps.minImageCount + 1;

    uint32_t num_fmts;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_fmts,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(num_fmts);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &num_fmts,
                                         fmts.data());

    uint32_t num_pms;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                              &num_pms, nullptr);
    std::vector<VkPresentModeKHR> pms(num_pms);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_,
                                              &num_pms, pms.data());

    const VkSurfaceFormatKHR format = chooseFromList(fmts);

    const VkSwapchainCreateInfoKHR swapchain_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface_,
        .minImageCount = min_images,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = chooseSwapExtent(caps),
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = chooseFromList(pms),
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain};

    assert(vkCreateSwapchainKHR(device_, &swapchain_info, nullptr,
                         &swapchain_.swapchain) == VK_SUCCESS);

    vkDestroySwapchainKHR(device_, old_swapchain, nullptr);

    swapchain_.extent = swapchain_info.imageExtent;
    swapchain_.format = format;

    uint32_t num_imgs;
    vkGetSwapchainImagesKHR(device_, swapchain_.swapchain, &num_imgs, nullptr);
    swapchain_.images.resize(num_imgs);
    vkGetSwapchainImagesKHR(device_, swapchain_.swapchain, &num_imgs,
                            swapchain_.images.data());
}

void VulkanRenderer::recreateSwapchain() noexcept {
    vkDeviceWaitIdle(device_);
    createSwapchain(swapchain_.swapchain);
    createImageViews();
    createDepthResources();
}
}  // namespace graphics::vk_renderer
