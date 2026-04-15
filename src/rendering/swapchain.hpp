/* Copyright 2026 Alix Boivin */

#ifndef SRC_VULKAN_SWAPCHAIN_HPP_
#define SRC_VULKAN_SWAPCHAIN_HPP_

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "vulkan/vulkan.hpp"
#include <SDL3/SDL_video.h>

namespace rendering::internal_swapchain {
class Swapchain {
  public:
    Swapchain() noexcept {};

    operator vk::SwapchainKHR() const noexcept { return swapchain_; }

    void create(SDL_Window*,
        vk::Instance&,
        const vk::PhysicalDevice&,
        vk::Device&) noexcept;
    void destroy(vk::Device& device, vk::Instance& instance) noexcept;

    const vk::SurfaceFormatKHR& format() const noexcept { return format_; }
    const vk::Extent2D& extent() const noexcept { return extent_; }
    const vk::Image& image(const uint32_t image_index) const noexcept { return images_[image_index]; }
    uint32_t numImages() const noexcept { return static_cast<uint32_t>(images_.size()); }

    void refresh(SDL_Window* window, vk::PhysicalDevice& phys_device, vk::Device& device) noexcept;

    uint32_t nextImageIndex(SDL_Window* window, vk::PhysicalDevice& phys_device, vk::Device& device, const vk::Semaphore& semaphore) noexcept;

    vk::RenderingAttachmentInfo renderingAttachmentInfo(const uint32_t image_index) const noexcept;

  private:
    VkSurfaceKHR surface_;

    vk::SurfaceFormatKHR chooseSurfaceFormat(const vk::PhysicalDevice& device) const noexcept;
    vk::PresentModeKHR choosePresentMode(const vk::PhysicalDevice& device) const noexcept;
    vk::Extent2D chooseExtent(SDL_Window* window) const noexcept;
    vk::SwapchainCreateInfoKHR buildSwapchainInfo(const vk::PhysicalDevice& device) const noexcept;

    void getImages(vk::Device& device) noexcept;
    void getImageViews(vk::Device& device) noexcept;
    void clearImageViews(vk::Device& device) noexcept;

    vk::SurfaceCapabilitiesKHR capabilities_;
    vk::SurfaceFormatKHR format_;
    vk::Extent2D extent_;
    std::vector<vk::Image> images_;
    std::vector<vk::ImageView> image_views_;
    vk::SwapchainKHR swapchain_ = nullptr;
};
}  // namespace rendering::internal_swapchain

#endif  // SRC_VULKAN_SWAPCHAIN_HPP_
