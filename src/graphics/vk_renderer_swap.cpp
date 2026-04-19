#include "vulkan_renderer.hpp"

#include <vulkan/vulkan.hpp>

#include <algorithm>

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
}  // namespace

namespace graphics::vk_renderer {
vk::SurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat() {
    assert(physical_device_ != nullptr);

    std::vector<vk::SurfaceFormatKHR> formats = physical_device_.getSurfaceFormatsKHR(surface_);
    assert(formats.size() > 0);

    vk::Format desired_format = vk::Format::eB8G8R8A8Srgb;
    for (vk::SurfaceFormatKHR format : formats) {
        if (format.format == desired_format) return format;
    }

    return formats[0];
}

vk::PresentModeKHR VulkanRenderer::chooseSwapPresentMode() {
    assert(physical_device_ != nullptr);

    std::vector<vk::PresentModeKHR> modes = physical_device_.getSurfacePresentModesKHR(surface_);
    assert(modes.size() > 0);

    vk::PresentModeKHR desired_mode = vk::PresentModeKHR::eMailbox;
    for (vk::PresentModeKHR mode : modes) {
        if (mode == desired_mode) return mode;
    }

    return vk::PresentModeKHR::eFifo;
}

void VulkanRenderer::createImageViews() noexcept {
    assert(device_ != nullptr && swapchain_.image_views.empty());

    vk::ImageViewCreateInfo view_info = {};
    view_info.pNext = nullptr;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = swapchain_.format.format;
    view_info.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    view_info.components = {
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity
    };

    swapchain_.image_views.resize(swapchain_.images.size());
    for (uint32_t i = 0; i < swapchain_.images.size(); i++) {
        view_info.image = swapchain_.images[i];

        swapchain_.image_views[i] = device_.createImageView(view_info);
        assert(swapchain_.image_views[i] != nullptr);
    }
}

void VulkanRenderer::createSwapchain(vk::SwapchainKHR old_swapchain) noexcept {
    assert(physical_device_ != nullptr && surface_ != nullptr);

    vk::SurfaceCapabilitiesKHR capabilities = physical_device_.getSurfaceCapabilitiesKHR(surface_);
    assert(capabilities != nullptr);

    swapchain_.extent = chooseSwapExtent(capabilities);

    swapchain_.format = chooseSwapSurfaceFormat();

    uint32_t min_images = capabilities.maxImageCount > 0
        ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount)
        : capabilities.minImageCount + 1;

    vk::SwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface = surface_;
    swapchain_info.minImageCount = min_images;
    swapchain_info.imageFormat = swapchain_.format.format;
    swapchain_info.imageColorSpace = swapchain_.format.colorSpace;
    swapchain_info.imageExtent = swapchain_.extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapchain_info.presentMode = chooseSwapPresentMode();
    swapchain_info.clipped = true;
    swapchain_info.oldSwapchain = old_swapchain;

    swapchain_.swapchain = device_.createSwapchainKHR(swapchain_info);
    assert(swapchain_.swapchain != nullptr);

    device_.destroySwapchainKHR(old_swapchain);

    swapchain_.images = device_.getSwapchainImagesKHR(swapchain_.swapchain);
}

void VulkanRenderer::recreateSwapchain() {
    assert(device_ != nullptr);

    device_.waitIdle();

    for (vk::ImageView view : swapchain_.image_views)
        device_.destroyImageView(view);
    swapchain_.image_views.clear();

    createSwapchain(swapchain_.swapchain);
    createImageViews();
}
}  // namespace graphics::vk_renderer