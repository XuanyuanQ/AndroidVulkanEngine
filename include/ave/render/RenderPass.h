#pragma once

#include "ave/core/FrameData.h"
#include "ave/core/RenderTags.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {
class VkContext;
class VkSwapchain;
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

struct RenderPassContext {
    core::FrameData const* frame = nullptr;
    resource::ResourceSystem* resources = nullptr;
    PipelineSystem* pipelines = nullptr;

    // Optional Vulkan execution context (when running on the GPU backend).
    vkfw::VkContext* vk = nullptr;
    vkfw::VkSwapchain* swapchain = nullptr;
    uint32_t swapchain_image_index = 0;
    vk::CommandBuffer command_buffer = {};
    vk::RenderPass compatibility_render_pass = {};
    vk::Framebuffer compatibility_framebuffer = {};
    vk::RenderPass compatibility_load_render_pass = {};
    vk::Framebuffer compatibility_load_framebuffer = {};

    mutable vkfw::VkTexture* current_shadow_map = nullptr;
    mutable vkfw::VkTexture* current_depth_texture = nullptr;
    mutable glm::mat4 shadow_view_projection{1.0f};//测试用

    // Optional: capture debug strings per pass for early bring-up.
    std::vector<std::string>* debug_output = nullptr;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual std::string_view Name() const = 0;

    // Default filter for this pass. FrameGraph may override per-node.
    virtual PassDataFilter GetDataFilter() const = 0;

    // Execute pass for one invocation. View is pre-filtered from FrameData by FrameGraph.
    virtual void Execute(RenderPassContext const& context, PassExecutionView const& view) = 0;
};

} // namespace ave::render
