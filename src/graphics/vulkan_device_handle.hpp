#ifndef SRC_GRAPHICS_VULKAN_DEVICE_HPP_
#define SRC_GRAPHICS_VULKAN_DEVICE_HPP_

#include "vulkan/vulkan.hpp"
#include <cstdint>
namespace graphics::vulkan::device {
class VulkanDeviceHandle {
 public:
    VulkanDeviceHandle() noexcept;
    ~VulkanDeviceHandle() noexcept;

    inline const vk::Instance& instance() const noexcept { return instance_; }
    inline const vk::Device& device() const noexcept { return device_; }
    inline const vk::Queue& queue() const noexcept { return graphics_queue_; }
    inline uint32_t queueIndex() const noexcept { return graphics_qf_idx_; }
    inline const vk::PhysicalDevice& physicalDevice() const noexcept {
        return physical_device_;
    }

    void submit(const vk::SubmitInfo& info,
                const vk::Fence fence = nullptr) const noexcept {
        graphics_queue_.submit(info, fence);
    }

    vk::Result present(const vk::PresentInfoKHR& info) const noexcept {
        return graphics_queue_.presentKHR(info);
    }

    void queueWaitIdle() const noexcept { graphics_queue_.waitIdle(); }

    vk::Format findDesiredFormat(
        const std::vector<vk::Format>& candidates,
        const vk::ImageTiling tiling,
        const vk::FormatFeatureFlags features) const noexcept;

    uint32_t findMemoryType(
        const uint32_t type_filter,
        const vk::MemoryPropertyFlags prop_flags) const noexcept;

 private:
    void createInstance() noexcept;

    void selectPhysicalDevice() noexcept;

    void createLogicalDevice() noexcept;

    uint32_t getQueueFamilyIndex(
        const vk::PhysicalDevice device) const noexcept;

    uint32_t graphics_qf_idx_ = UINT32_MAX;
    vk::Instance instance_ = nullptr;
    vk::PhysicalDevice physical_device_ = nullptr;
    vk::Device device_ = nullptr;
    vk::Queue graphics_queue_ = nullptr;
};
}  // namespace graphics::vulkan::device

#endif  // SRC_GRAPHICS_VULKAN_DEVICE_HPP_