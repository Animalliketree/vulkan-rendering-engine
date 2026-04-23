#include "vulkan_device_handle.hpp"

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

namespace {
#ifdef NDEBUG
const std::vector<char const*> kValidationLayers = {};
#else
const std::vector<char const*> kValidationLayers = {
"VK_LAYER_KHRONOS_validation"};
#endif

constexpr char kAppTitle[] = "Game";
constexpr char kEngineTitle[] = "Hephaestus";

const std::vector<const char*> kRequiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
  VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};

vk::ApplicationInfo buildAppInfo() {
    vk::ApplicationInfo app_info = {};
    app_info.pApplicationName = kAppTitle;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = kEngineTitle;
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_4;
    return app_info;
}

bool validationLayersSupported() {
    std::vector<vk::LayerProperties> layer_props =
        vk::enumerateInstanceLayerProperties();

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

std::vector<const char*> getInstanceExtensions() {
    uint32_t num_instance_extensions;
    const char* const* instance_extensions = SDL_Vulkan_GetInstanceExtensions(
        &num_instance_extensions);
    assert(instance_extensions != nullptr);

    uint32_t num_extensions;
    const char** extensions;
    num_extensions = num_instance_extensions;
    extensions = (const char**)(SDL_malloc(
        num_extensions * sizeof(const char*)));
    SDL_memcpy(&extensions[0], instance_extensions,
        num_instance_extensions * sizeof(const char*));

    std::vector<const char*> ext_vec;
    for (uint32_t i = 0 ; i < num_extensions; i++) {
        ext_vec.push_back(extensions[i]);
    }

    #ifndef NDEBUG
    ext_vec.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    #endif

    SDL_free(extensions);
    return ext_vec;
}

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

namespace graphics::vulkan::device {
VulkanDeviceHandle::VulkanDeviceHandle() noexcept {
    createInstance();
    selectPhysicalDevice();
    createLogicalDevice();
    graphics_queue_ = device_.getQueue(graphics_qf_idx_, 0);
}

VulkanDeviceHandle::~VulkanDeviceHandle() noexcept {
    device_.destroy();
    instance_.destroy();
}

void VulkanDeviceHandle::createInstance() noexcept {
    bool layers_supported = validationLayersSupported();

    // Handle extensions
    auto extensions = getInstanceExtensions();

    vk::ApplicationInfo app_info = buildAppInfo();

    vk::InstanceCreateInfo instance_info;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    if (layers_supported) {
        instance_info.enabledLayerCount =
            static_cast<uint32_t>(kValidationLayers.size());
        instance_info.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        instance_info.enabledLayerCount = 0;
        instance_info.ppEnabledLayerNames = nullptr;
    }

    instance_ = vk::createInstance(instance_info);
    assert(instance_ != nullptr);
}

void VulkanDeviceHandle::selectPhysicalDevice() noexcept {
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

void VulkanDeviceHandle::createLogicalDevice() noexcept {
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

uint32_t VulkanDeviceHandle::getQueueFamilyIndex(
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

vk::Format VulkanDeviceHandle::findDesiredFormat(
        const std::vector<vk::Format>& candidates,
        const vk::ImageTiling tiling,
        const vk::FormatFeatureFlags features) const noexcept {
    for (const vk::Format format : candidates) {
        vk::FormatProperties props = physical_device_.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear
                && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal
                && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    abort();  // Failed to find a valid format from the list
}

uint32_t VulkanDeviceHandle::findMemoryType(
        const uint32_t type_filter,
        const vk::MemoryPropertyFlags prop_flags) const noexcept {
    vk::PhysicalDeviceMemoryProperties props =
        physical_device_.getMemoryProperties();

    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (type_filter & (1 << i) &&
            (props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
        return i;
    }

    abort();
}
}  // namespace graphics::vulkan::device
