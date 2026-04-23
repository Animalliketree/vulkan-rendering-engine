/* Copyright 2026 Alix Boivin */

#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "../graphics/vulkan_renderer.hpp"

namespace {
const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

bool evaluatePhysicalDeviceProperties(vk::PhysicalDevice device) {
    assert(device != nullptr);

    vk::PhysicalDeviceProperties props = device.getProperties();

    return props.apiVersion >= VK_API_VERSION_1_4;
}

bool evaluateDeviceExtensions(vk::PhysicalDevice device) {
    assert(device != nullptr);

    std::vector<vk::ExtensionProperties> extensions =
        device.enumerateDeviceExtensionProperties();

    for (const char* extension : kRequiredDeviceExtensions) {
        bool available = false;
        for (vk::ExtensionProperties prop : extensions) {
            if (strcmp(extension, prop.extensionName) == 0) {
                available = true;
                break;
            }
        }

        if (!available) {
            return false;
        }
    }

    return true;
}

bool evaluatePhysicalDeviceFeatures(vk::PhysicalDevice device) {
    assert(device != nullptr);

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
    vk::PhysicalDeviceVulkan13Features vk_1_3_features;
    vk_1_3_features.pNext = &dynamic_state_features;

    vk::PhysicalDeviceFeatures2 features;
    features.pNext = &vk_1_3_features;
    device.getFeatures2(&features);

    return !(dynamic_state_features.extendedDynamicState == vk::False
             || vk_1_3_features.dynamicRendering == vk::False);
}
}  // namespace

namespace graphics::vk_renderer {
uint32_t VulkanRenderer::getQueueFamilyIndex(
        const vk::PhysicalDevice device) const noexcept {
    assert(device != nullptr);

    std::vector<vk::QueueFamilyProperties> props =
        device.getQueueFamilyProperties();

    bool supports_graphics = false;
    for (uint32_t i = 0; i < props.size(); i++) {
        supports_graphics = SDL_Vulkan_GetPresentationSupport(instance_,
                                                              device, i);
        if (supports_graphics) return i;
    }

    return UINT32_MAX;
}

void VulkanRenderer::selectPhysicalDevice() noexcept {
    assert(instance_ != nullptr);

    std::vector<vk::PhysicalDevice> devices =
        instance_.enumeratePhysicalDevices();
    assert(devices.size() > 0);

    bool device_found = false;
    for (vk::PhysicalDevice device : devices) {
        if (!evaluatePhysicalDeviceProperties(device) ||
            !evaluateDeviceExtensions(device) ||
            !evaluatePhysicalDeviceFeatures(device)) continue;

        uint32_t graphics_qf_idx = getQueueFamilyIndex(device);
        if (graphics_qf_idx == UINT32_MAX) continue;

        physical_device_ = device;
        graphics_qf_idx_ = graphics_qf_idx;
        device_found = true;
        break;
    }

    assert(device_found);
}

void VulkanRenderer::createLogicalDevice() noexcept {
    assert(physical_device_ != nullptr);

    constexpr float kQueuePriority = 0.5f;

    graphics_qf_idx_ = getQueueFamilyIndex(physical_device_);
    vk::DeviceQueueCreateInfo queue_info{{}, graphics_qf_idx_, 1,
                                         &kQueuePriority};

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extended_features{
                                                                     vk::True};

    vk::PhysicalDeviceVulkan13Features vk_1_3_features;
    vk_1_3_features.pNext = &extended_features;
    vk_1_3_features.dynamicRendering = VK_TRUE;
    vk_1_3_features.synchronization2 = VK_TRUE;

    vk::PhysicalDeviceFeatures2 features_2;
    features_2.pNext = &vk_1_3_features;

    vk::DeviceCreateInfo device_info{{}, 1, &queue_info, 0, {},
        static_cast<uint32_t>(kRequiredDeviceExtensions.size()),
        kRequiredDeviceExtensions.data(), {}, &features_2};

    device_ = physical_device_.createDevice(device_info);
    assert(device_ != nullptr);
}

uint32_t VulkanRenderer::findMemoryType(
        const uint32_t type_filter,
        const vk::MemoryPropertyFlags prop_flags) const noexcept {
    assert(physical_device_ != nullptr);

    vk::PhysicalDeviceMemoryProperties props =
        physical_device_.getMemoryProperties();

    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (type_filter & (1 << i) &&
            (props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
        return i;
    }

    abort();
}
}  // namespace graphics::vk_renderer
