#pragma once

#include "ave/render/RenderPass.h"

#include <unordered_map>

#include "VkBuffer.hpp"
#include "VkTexture.hpp"

namespace ave::render {

class DepthPrepass final : public RenderPass {
public:
    std::string_view Name() const override { return "DepthPrepass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;
};

class ShadowPass final : public RenderPass {
public:
    std::string_view Name() const override { return "ShadowPass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    vkfw::VkBuffer frame_ubo_{};
    uint32_t frame_set_id_ = 0;
    vkfw::VkTexture shadow_map_{}; //阴影图只有一张
    uint32_t shadow_shader_id_ = 0;
    bool shadow_map_initialized_ = false;
    glm::mat4 shadow_view_projection_{1.0f};
};

class PBRPass final : public RenderPass {
public:
    std::string_view Name() const override { return "PBRPass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    struct MaterialBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    vkfw::VkBuffer frame_ubo_{};
    uint32_t frame_set_id_ = 0;
    std::unordered_map<uint32_t, MaterialBinding> material_bindings_{};
    vkfw::VkTexture fallback_white_texture_{};
    vkfw::VkTexture fallback_normal_texture_{};
    uint32_t fallback_material_id_ = 0;
    vkfw::VkTexture depth_stencil_{};
};

class ComputePass final : public RenderPass {
public:
    std::string_view Name() const override { return "ComputePass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    vkfw::VkBuffer instances_buffers_[2]{};
    vkfw::VkBuffer visibility_buffers_[2]{};
    uint32_t descriptor_set_ids_[2] = {0, 0};
};

class UIPass final : public RenderPass {
public:
    std::string_view Name() const override { return "UIPass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    std::unordered_map<uint32_t, uint32_t> texture_descriptor_sets_{};
    vkfw::VkBuffer ui_vertex_buffers_[2]{};
    vkfw::VkBuffer ui_index_buffers_[2]{};
    vkfw::VkTexture fallback_white_texture_{};
    uint32_t ui_shader_id_ = 0;
};

class ToneMappingPass final : public RenderPass {
public:
    std::string_view Name() const override { return "ToneMappingPass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;
};

} // namespace ave::render
