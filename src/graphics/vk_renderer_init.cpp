#include "vulkan_renderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

namespace {
#ifdef NDEBUG
    const std::vector<char const*> kValidationLayers = {};
#else
    const std::vector<char const*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};
#endif

constexpr char kAppTitle[] = "Game";
constexpr char kEngineTitle[] = "Hephaestus";

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
    std::vector<vk::LayerProperties> layer_props = vk::enumerateInstanceLayerProperties();

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
}  // namespace

namespace graphics::vk_renderer {
void VulkanRenderer::createInstance() noexcept {
    bool layers_supported = validationLayersSupported();

    // Handle extensions
    auto extensions = getInstanceExtensions();

    vk::ApplicationInfo app_info = buildAppInfo();

    vk::InstanceCreateInfo instance_info = {};
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    if (layers_supported) {
        instance_info.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        instance_info.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        instance_info.enabledLayerCount = 0;
        instance_info.ppEnabledLayerNames = nullptr;
    }

    instance_ = vk::createInstance(instance_info);
    assert(instance_ != nullptr);
}
}  // namespace graphics::vk_renderer