#pragma once

#include "ave/core/FrameData.h"
#include "ave/core/RenderTags.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {
class VkContext;
class VkTexture;
}

namespace ave::resource {
class ResourceSystem;
}

namespace ave::render {

class PipelineSystem;

struct PassDataFilter {
    core::RenderPassBit pass_bit = core::RenderPassBit::None;
    uint32_t layer_mask = 0xFFFFFFFFu;
    bool opaque_only = false;
    bool transparent_only = false;
    bool shadow_casters_only = false;
    uint32_t light_group = 0;
    std::optional<std::string> material_id{};
};

struct PassExecutionView {
    std::vector<core::FrameRenderableData const*> renderables{};
    std::vector<core::FrameLightData const*> lights{};
    std::vector<core::FrameUiData const*> ui_items{};
};

// Build a filtered view of FrameData for a specific pass invocation.
PassExecutionView BuildPassView(core::FrameData const& frame, PassDataFilter const& filter);

struct RenderTargetView {
    vk::Image image = {};
    vk::ImageView image_view = {};
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent{};
    vk::ImageLayout attachment_layout = vk::ImageLayout::eColorAttachmentOptimal;
    vk::RenderPass compatibility_render_pass = {};
    vk::Framebuffer compatibility_framebuffer = {};
    vk::RenderPass compatibility_load_render_pass = {};
    vk::Framebuffer compatibility_load_framebuffer = {};

    bool IsValid() const noexcept
    {
        return image != vk::Image{} &&
               image_view != vk::ImageView{} &&
               format != vk::Format::eUndefined &&
               extent.width > 0 &&
               extent.height > 0;
    }
};

struct DepthTargetView {
    mutable vkfw::VkTexture* texture = nullptr;
    mutable uint8_t* ready = nullptr;
    vk::Format format = vk::Format::eD32Sfloat;
    vk::Extent2D extent{};
    vk::ImageLayout attachment_layout = vk::ImageLayout::eDepthAttachmentOptimal;

    bool IsValid() const noexcept
    {
        return texture != nullptr && extent.width > 0 && extent.height > 0;
    }
};

struct FrameGraphResourceRegistry {
    static constexpr std::string_view ShadowMap = "shadow_map";
    static constexpr std::string_view ShadowViewProjection = "shadow_view_projection";

    void Clear() const
    {
        textures_.clear();
        matrices_.clear();
    }

    void SetTexture(std::string_view name, vkfw::VkTexture* texture) const
    {
        textures_[std::string{name}] = texture;
    }

    vkfw::VkTexture* GetTexture(std::string_view name) const
    {
        auto const it = textures_.find(std::string{name});
        return it != textures_.end() ? it->second : nullptr;
    }

    void SetMatrix(std::string_view name, glm::mat4 const& matrix) const
    {
        matrices_[std::string{name}] = matrix;
    }

    glm::mat4 GetMatrix(std::string_view name, glm::mat4 const& fallback = glm::mat4{1.0f}) const
    {
        auto const it = matrices_.find(std::string{name});
        return it != matrices_.end() ? it->second : fallback;
    }

private:
    mutable std::unordered_map<std::string, vkfw::VkTexture*> textures_{};
    mutable std::unordered_map<std::string, glm::mat4> matrices_{};
};

struct RenderPassContext {
    core::FrameData const* frame = nullptr;
    resource::ResourceSystem* resources = nullptr;
    PipelineSystem* pipelines = nullptr;

    // Optional Vulkan execution context (when running on the GPU backend).
    vkfw::VkContext* vk = nullptr;
    vk::CommandBuffer command_buffer = {};
    RenderTargetView color_target{};
    DepthTargetView depth_target{};
    uint32_t view_index = 0;
    uint32_t view_count = 1;
    uint32_t frame_resource_index = 0;
    uint32_t frame_resource_count = 1;
    FrameGraphResourceRegistry* frame_graph_resources = nullptr;


    // Optional: capture debug strings per pass for early bring-up.
    std::vector<std::string>* debug_output = nullptr;
};

inline core::FrameViewData const* CurrentFrameView(RenderPassContext const& context)
{
    if (context.frame == nullptr || context.view_index >= context.frame->views.size()) {
        return nullptr;
    }
    return &context.frame->views[context.view_index];
}

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual std::string_view Name() const = 0;

    // Reset runtime-owned Vulkan state while keeping the pass object alive.
    virtual void Reset(vkfw::VkContext* ctx = nullptr) { (void)ctx; }

    // Optional preload hook for one-time resource warmup before the frame loop.
    virtual void Preload(RenderPassContext const& context) { (void)context; }

    // Default filter for this pass. FrameGraph may override per-node.
    virtual PassDataFilter GetDataFilter() const = 0;

    // Execute pass for one invocation. View is pre-filtered from FrameData by FrameGraph.
    virtual void Execute(RenderPassContext const& context, PassExecutionView const& view) = 0;
};

} // namespace ave::render
