#ifndef SRC_RENDERING_GRAPHICS_PIPELINE_HPP_
#define SRC_RENDERING_GRAPHICS_PIPELINE_HPP_

#include "swapchain.hpp"
#include "vulkan/vulkan.hpp"
#include <array>
#include <glm/glm.hpp>

namespace rendering::internal_graphics_pipeline {
class GraphicsPipeline {
  public:
    GraphicsPipeline() noexcept {}
    GraphicsPipeline(GraphicsPipeline&) = delete;

    void create(vk::Device&,
                const internal_swapchain::Swapchain&) noexcept;
    void destroy(vk::Device&) noexcept;
    void bind(const vk::CommandBuffer&) const noexcept;

  private:
    using ShaderStageInfo = std::array<vk::PipelineShaderStageCreateInfo, 2>;

    ShaderStageInfo buildShaderStageInfo() const noexcept;
    void createShaderModule(vk::Device& device) noexcept;
    void createLayout(vk::Device& device) noexcept;

    vk::ShaderModule shader_module_ = nullptr;
    vk::PipelineLayout layout_ = nullptr;
    vk::Pipeline pipeline_ = nullptr;
};
}  // namespace rendering::internal_graphics_pipeline

#endif  // SRC_RENDERING_GRAPHICS_PIPELINE_HPP_
