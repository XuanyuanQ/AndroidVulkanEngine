#pragma once

#include "ave/render/RenderPass.h"

#include <unordered_map>
#include <array>
#include <vector>

#include "VkBuffer.hpp"
#include "VkTexture.hpp"

namespace ave::render {

class DepthPrepass final : public RenderPass {
public:
    std::string_view Name() const override { return "DepthPrepass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    void Preload(RenderPassContext const& context) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    struct FrameBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    struct MaterialBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    std::vector<FrameBinding> frame_bindings_{};
    std::unordered_map<uint32_t, std::vector<MaterialBinding>> material_bindings_{};
    std::vector<vkfw::VkTexture> depth_textures_{};
    vkfw::VkTexture fallback_white_texture_{};
    vkfw::VkTexture fallback_normal_texture_{};
    uint32_t depth_shader_id_ = 0;
    std::vector<uint8_t> depth_texture_ready_{};
};

class ShadowPass final : public RenderPass {
public:
    std::string_view Name() const override { return "ShadowPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    void Preload(RenderPassContext const& context) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    struct MaterialBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    struct FrameBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    std::vector<FrameBinding> frame_bindings_{};
    std::unordered_map<uint32_t, std::vector<MaterialBinding>> material_bindings_{};
    vkfw::VkTexture fallback_white_texture_{};
    std::vector<vkfw::VkTexture> shadow_maps_{};
    std::vector<uint8_t> shadow_map_initialized_{};
    uint32_t shadow_shader_id_ = 0;
    glm::mat4 shadow_view_projection_{1.0f};
};

class SkyboxPass final : public RenderPass {
public:
    std::string_view Name() const override { return "SkyboxPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    void Preload(RenderPassContext const& context) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    struct FrameBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    std::vector<FrameBinding> frame_bindings_{};
    uint32_t skybox_shader_id_ = 0;
    uint32_t skybox_mesh_id_ = 0;
};

class PBRPass final : public RenderPass {
public:
    std::string_view Name() const override { return "PBRPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    void Preload(RenderPassContext const& context) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    void EnsureEnvironmentMaps(vkfw::VkContext& ctx,
                               resource::ResourceSystem* resources,
                               glm::vec4 const& clear_color,
                               glm::vec3 const& ambient_color);

    struct MaterialBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    struct FrameBinding {
        vkfw::VkBuffer ubo{};
        uint32_t descriptor_set_id = 0;
    };

    std::vector<FrameBinding> frame_bindings_{};
    std::unordered_map<uint32_t, std::vector<MaterialBinding>> material_bindings_{};
    vkfw::VkTexture fallback_white_texture_{};
    vkfw::VkTexture fallback_normal_texture_{};
    uint32_t fallback_material_id_ = 0;
    std::vector<vkfw::VkTexture> depth_stencils_{};
};

class ComputePass final : public RenderPass {
public:
    std::string_view Name() const override { return "ComputePass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    std::vector<vkfw::VkBuffer> instances_buffers_{};
    std::vector<vkfw::VkBuffer> visibility_buffers_{};
    std::vector<uint32_t> descriptor_set_ids_{};
};

class UIPass final : public RenderPass {
public:
    std::string_view Name() const override { return "UIPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    std::unordered_map<uint32_t, uint32_t> texture_descriptor_sets_{};
    std::vector<vkfw::VkBuffer> ui_vertex_buffers_{};
    std::vector<vkfw::VkBuffer> ui_index_buffers_{};
    vkfw::VkTexture fallback_white_texture_{};
    uint32_t ui_shader_id_ = 0;
};

class XRWorldUIPass final : public RenderPass {
public:
    std::string_view Name() const override { return "XRWorldUIPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;

private:
    std::unordered_map<uint32_t, uint32_t> texture_descriptor_sets_{};
    std::vector<vkfw::VkBuffer> ui_vertex_buffers_{};
    std::vector<vkfw::VkBuffer> ui_index_buffers_{};
    vkfw::VkTexture fallback_white_texture_{};
    uint32_t ui_shader_id_ = 0;
};

class ToneMappingPass final : public RenderPass {
public:
    std::string_view Name() const override { return "ToneMappingPass"; }
    void Reset(vkfw::VkContext* ctx = nullptr) override;
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;
};

} // namespace ave::render
