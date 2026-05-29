#ifndef SRC_GRAPHICS_VULKAN_CONTEXT_HPP_
#define SRC_GRAPHICS_VULKAN_CONTEXT_HPP_

#include <volk.h>

#include <cstdint>

#include <vector>

namespace graphics::vulkan {
class VulkanContext {
 public:
    VulkanContext() noexcept;
    ~VulkanContext() noexcept;

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
}  // namespace graphics::vulkan

#endif  // SRC_GRAPHICS_VULKAN_CONTEXT_HPP_
