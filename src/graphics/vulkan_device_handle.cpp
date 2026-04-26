#include "vulkan_device_handle.hpp"

#include <quill/Logger.h>
#include <quill/SimpleSetup.h>
#include <quill/LogFunctions.h>
#include <volk.h>
#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vulkan/vulkan_core.h>


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
  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
  VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
  VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
};

VkApplicationInfo buildAppInfo() {
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = kAppTitle,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = kEngineTitle,
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4};
    return app_info;
}

bool validationLayersSupported() {
    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Checking validation layer support...");
    uint32_t num_props;
    vkEnumerateInstanceLayerProperties(&num_props, nullptr);
    std::vector<VkLayerProperties> layer_props(num_props);
    vkEnumerateInstanceLayerProperties(&num_props, layer_props.data());

    for (const char* target : kValidationLayers) {
        quill::info(log, "Checking layer: {}", target);
        bool layer_available = false;
        for (VkLayerProperties layer : layer_props) {
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

bool evaluatePhysicalDeviceProperties(VkPhysicalDevice device) {
    assert(device != nullptr);

    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Evaluating physical device properties...");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    return props.apiVersion >= VK_API_VERSION_1_4;
}

bool evaluateDeviceExtensions(VkPhysicalDevice device) {
    assert(device != nullptr);

    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Evaluating physical device extensions...");

    uint32_t num_ext;
    assert(vkEnumerateDeviceExtensionProperties(device, nullptr, &num_ext, nullptr)
        == VK_SUCCESS);
    std::vector<VkExtensionProperties> extensions(num_ext);
    assert(vkEnumerateDeviceExtensionProperties(device, nullptr, &num_ext, extensions.data())
        == VK_SUCCESS);

    for (const char* extension : kRequiredDeviceExtensions) {
        bool available = false;
        for (VkExtensionProperties prop : extensions) {
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

bool evaluatePhysicalDeviceFeatures(VkPhysicalDevice device) {
    assert(device != nullptr);

    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Evaluating physical device features...");

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_feats{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = nullptr,
        .rayTracingPipeline = VK_TRUE,
        .rayTracingPipelineTraceRaysIndirect = VK_TRUE,
        .rayTraversalPrimitiveCulling = VK_FALSE};

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &rt_feats,
        .extendedDynamicState = VK_TRUE};

    VkPhysicalDeviceVulkan13Features vk_1_3_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &dynamic_state_features,
        .synchronization2 = VK_TRUE};

    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk_1_3_features,
        .features = {}};
    vkGetPhysicalDeviceFeatures2(device, &features);

    return !(dynamic_state_features.extendedDynamicState == VK_FALSE
             || vk_1_3_features.dynamicRendering == VK_FALSE
             || rt_feats.rayTracingPipeline == VK_FALSE
             || rt_feats.rayTracingPipelineTraceRaysIndirect == VK_FALSE);
}
}  // namespace

namespace graphics::vulkan::device {
VulkanDeviceHandle::VulkanDeviceHandle() noexcept {
    assert(volkInitialize() == VK_SUCCESS);
    quill::Logger* log = quill::simple_logger();

    quill::info(log, "Creating instance...");
    createInstance();
    volkLoadInstance(instance_);
    quill::info(log, "Selecting physical device...");
    selectPhysicalDevice();
    quill::info(log, "Creating logical device...");
    createLogicalDevice();
    quill::info(log, "Getting graphics queue...");
    vkGetDeviceQueue(device_, graphics_qf_idx_, 0, &graphics_queue_);
    quill::info(log, "Finished initializing device!");
}

VulkanDeviceHandle::~VulkanDeviceHandle() noexcept {
    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

void VulkanDeviceHandle::createInstance() noexcept {
    quill::Logger* log = quill::simple_logger();
    bool layers_supported = validationLayersSupported();

    // Handle extensions
    quill::info(log, "Getting instance extensions...");
    auto extensions = getInstanceExtensions();

    VkApplicationInfo app_info = buildAppInfo();

    VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()};

    if (layers_supported) {
        instance_info.enabledLayerCount =
            static_cast<uint32_t>(kValidationLayers.size());
        instance_info.ppEnabledLayerNames = kValidationLayers.data();
    }

    quill::info(log, "Creating instance...");
    assert(vkCreateInstance(&instance_info, nullptr, &instance_)
        == VK_SUCCESS);
}

void VulkanDeviceHandle::selectPhysicalDevice() noexcept {
    assert(instance_ != nullptr);

    quill::Logger* log = quill::simple_logger();
    quill::info(log, "Getting phys. device count...");
    uint32_t num_devices = 0;
    assert(vkEnumeratePhysicalDevices(instance_, &num_devices, nullptr)
        == VK_SUCCESS);
    std::vector<VkPhysicalDevice> devices(num_devices);
    quill::info(log, "Getting physical devices...");
    assert(vkEnumeratePhysicalDevices(instance_, &num_devices, devices.data())
        == VK_SUCCESS);

    quill::info(log, "Evaluating physical devices...");
    bool device_found = false;
    for (VkPhysicalDevice device : devices) {
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
    VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = graphics_qf_idx_,
        .queueCount = 1,
        .pQueuePriorities = &kQueuePriority};

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_feats{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = nullptr,
        .rayTracingPipeline = VK_TRUE,
        .rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE,
        .rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE,
        .rayTracingPipelineTraceRaysIndirect = VK_FALSE,
        .rayTraversalPrimitiveCulling = VK_FALSE};

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &rt_feats,
        .extendedDynamicState = VK_TRUE};

    VkPhysicalDeviceVulkan13Features vk_1_3_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extended_features,
        .robustImageAccess = VK_FALSE,
        .inlineUniformBlock = VK_FALSE,
        .descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE,
        .pipelineCreationCacheControl = VK_FALSE,
        .privateData = VK_FALSE,
        .shaderDemoteToHelperInvocation = VK_FALSE,
        .shaderTerminateInvocation = VK_FALSE,
        .subgroupSizeControl = VK_FALSE,
        .computeFullSubgroups = VK_FALSE,
        .synchronization2 = VK_TRUE,
        .textureCompressionASTC_HDR = VK_FALSE,
        .shaderZeroInitializeWorkgroupMemory = VK_FALSE,
        .dynamicRendering = VK_TRUE,
        .shaderIntegerDotProduct = VK_FALSE,
        .maintenance4 = VK_FALSE};

    VkPhysicalDeviceFeatures2 features_2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk_1_3_features};

    VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features_2,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(kRequiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = kRequiredDeviceExtensions.data(),
        .pEnabledFeatures = nullptr};

    vkCreateDevice(physical_device_, &device_info, nullptr, &device_);
    assert(device_ != nullptr);
}

uint32_t VulkanDeviceHandle::getQueueFamilyIndex(
        const VkPhysicalDevice device) const noexcept {
    assert(device != nullptr);

    uint32_t num_props;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_props, nullptr);
    std::vector<VkQueueFamilyProperties> props(num_props);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_props, props.data());

    bool supports_graphics = false;
    for (uint32_t i = 0; i < props.size(); i++) {
        supports_graphics = SDL_Vulkan_GetPresentationSupport(instance_,
                                                              device, i);
        if (supports_graphics) return i;
    }

    return UINT32_MAX;
}

VkFormat VulkanDeviceHandle::findDesiredFormat(
        const std::vector<VkFormat>& candidates,
        const VkImageTiling tiling,
        const VkFormatFeatureFlags features) const noexcept {
    for (const VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR
                && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL
                && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    std::abort();  // Failed to find a valid format from the list
}

uint32_t VulkanDeviceHandle::findMemoryType(
        const uint32_t type_filter,
        const VkMemoryPropertyFlags prop_flags) const noexcept {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);

    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (type_filter & (1 << i) &&
            (props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags)
        return i;
    }

    std::abort();
}
}  // namespace graphics::vulkan::device
