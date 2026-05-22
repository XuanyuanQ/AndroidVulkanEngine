#pragma once

#include "ave/render/RenderPass.h"

#include <unordered_map>

#include "VkBuffer.hpp"

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
};

class ToneMappingPass final : public RenderPass {
public:
    std::string_view Name() const override { return "ToneMappingPass"; }
    PassDataFilter GetDataFilter() const override;
    void Execute(RenderPassContext const& context, PassExecutionView const& view) override;
};

} // namespace ave::render
