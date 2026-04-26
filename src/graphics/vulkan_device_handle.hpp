#ifndef SRC_GRAPHICS_VULKAN_DEVICE_HPP_
#define SRC_GRAPHICS_VULKAN_DEVICE_HPP_

#include <volk.h>
#include <vector>
#include <cstdint>
namespace graphics::vulkan::device {
class VulkanDeviceHandle {
 public:
    VulkanDeviceHandle() noexcept;
    ~VulkanDeviceHandle() noexcept;

    VkFormat findDesiredFormat(
        const std::vector<VkFormat>& candidates,
        const VkImageTiling tiling,
        const VkFormatFeatureFlags features) const noexcept;

    uint32_t findMemoryType(
        const uint32_t type_filter,
        const VkMemoryPropertyFlags prop_flags) const noexcept;

 protected:
    void createInstance() noexcept;

    void selectPhysicalDevice() noexcept;

    void createLogicalDevice() noexcept;

    uint32_t getQueueFamilyIndex(
        const VkPhysicalDevice device) const noexcept;

    uint32_t graphics_qf_idx_ = UINT32_MAX;
    VkInstance instance_ = nullptr;
    VkPhysicalDevice physical_device_ = nullptr;
    VkDevice device_ = nullptr;
    VkQueue graphics_queue_ = nullptr;
};
}  // namespace graphics::vulkan::device

#endif  // SRC_GRAPHICS_VULKAN_DEVICE_HPP_