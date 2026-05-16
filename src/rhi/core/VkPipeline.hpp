#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "VkShader.hpp"
#include "VkDescriptor.hpp"

namespace vkfw {

class VkContext;

struct PipelineVertexInput {
    uint32_t binding = 0;
    uint32_t location = 0;
    uint32_t stride = 0;
    vk::Format format = vk::Format::eR32G32B32A32Sfloat;
    uint32_t offset = 0;
};

struct PipelineVertexInputState {
    std::vector<PipelineVertexInput> vertex_inputs;
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    bool primitive_restart_enable = false;
};

struct PipelineRasterizationState {
    vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
    vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eNone;
    vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;
    float line_width = 1.0f;
};

struct PipelineMultisampleState {
    vk::SampleCountFlagBits rasterization_samples = vk::SampleCountFlagBits::e1;
    bool sample_shading_enable = false;
    float min_sample_shading = 1.0f;
    vk::SampleMask sample_mask = 0xFFFFFFFF;
    bool alpha_to_coverage_enable = false;
    bool alpha_to_one_enable = false;
};

struct PipelineDepthStencilState {
    bool depth_test_enable = true;
    bool depth_write_enable = true;
    vk::CompareOp depth_compare_op = vk::CompareOp::eLessOrEqual;
    bool depth_bounds_test_enable = false;
    bool stencil_test_enable = false;
    vk::StencilOpState front = {};
    vk::StencilOpState back = {};
    float min_depth_bounds = 0.0f;
    float max_depth_bounds = 1.0f;
};

struct PipelineColorBlendAttachment {
    bool blend_enable = false;
    vk::BlendFactor src_color_blend = vk::BlendFactor::eZero;
    vk::BlendFactor dst_color_blend = vk::BlendFactor::eZero;
    vk::BlendOp color_blend_op = vk::BlendOp::eAdd;
    vk::BlendFactor src_alpha_blend = vk::BlendFactor::eZero;
    vk::BlendFactor dst_alpha_blend = vk::BlendFactor::eZero;
    vk::BlendOp alpha_blend_op = vk::BlendOp::eAdd;
    vk::ColorComponentFlags color_write_mask = vk::ColorComponentFlagBits::eR | 
        vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
};

struct PipelineColorBlendState {
    bool logic_op_enable = false;
    vk::LogicOp logic_op = vk::LogicOp::eCopy;
    std::vector<PipelineColorBlendAttachment> attachments;
    float blend_constants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct PipelineViewportState {
    std::vector<vk::Viewport> viewports;
    std::vector<vk::Rect2D> scissors;
};

struct PipelineInfo {
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    PipelineVertexInputState vertex_input;
    
    // 关键修复：直接存句柄(Handle)，不要存 RAII 对象的指针
    vk::RenderPass render_pass = nullptr; 
    vk::PipelineLayout layout = nullptr; 
    
    PipelineViewportState viewport;
    PipelineRasterizationState rasterization;
    PipelineMultisampleState multisample;
    PipelineDepthStencilState depth_stencil;
    PipelineColorBlendState color_blend;
    uint32_t subpass = 0;
    vk::Pipeline base_pipeline_handle = nullptr;
    int32_t base_pipeline_index = -1;
};

class VkPipelineLayout {
public:
    VkPipelineLayout() = default;
    ~VkPipelineLayout() = default;

    VkPipelineLayout(VkPipelineLayout&&) noexcept = default;
    VkPipelineLayout& operator=(VkPipelineLayout&&) noexcept = default;

    VkPipelineLayout(VkPipelineLayout const&) = delete;
    VkPipelineLayout& operator=(VkPipelineLayout const&) = delete;

    bool Init(VkContext& ctx, std::vector<VkDescriptorSetLayout*> const& descriptor_layouts);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return layout_ != nullptr; }
    
    vk::PipelineLayout Handle() const noexcept { return *layout_; }

private:
    std::unique_ptr<vk::raii::PipelineLayout> layout_;
};

class VkPipeline {
public:
    VkPipeline() = default;
    ~VkPipeline() = default;

    VkPipeline(VkPipeline&&) noexcept = default;
    VkPipeline& operator=(VkPipeline&&) noexcept = default;

    VkPipeline(VkPipeline const&) = delete;
    VkPipeline& operator=(VkPipeline const&) = delete;

    bool Init(VkContext& ctx, PipelineInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return pipeline_ != nullptr; }
    
    vk::Pipeline Handle() const noexcept { return *pipeline_; }
    vk::PipelineBindPoint BindPoint() const noexcept { return bind_point_; }

private:
    std::unique_ptr<vk::raii::Pipeline> pipeline_;
    vk::PipelineBindPoint bind_point_ = vk::PipelineBindPoint::eGraphics;
};

} // namespace vkfw
