#pragma once

#include "ave/core/FrameData.h"
#include "ave/render/RenderPass.h"

#include <cstdint>
#include <vector>

namespace vkfw {
class VkContext;
}

namespace ave::render {

enum class FrameGraphRenderResult {
    Success,
    Skipped,
    SwapchainOutOfDate,
};

struct RenderViewTarget {
    RenderTargetView color_target{};
    DepthTargetView depth_target{};
    uint32_t view_index = 0;
    uint32_t frame_resource_index = 0;
    uint32_t frame_resource_count = 1;
};

struct RenderFrameRequest {
    core::FrameData const* frame = nullptr;
    vkfw::VkContext* vk = nullptr;
    vk::CommandBuffer command_buffer = {};
    std::vector<RenderViewTarget> views{};
    void const* backend_debug = nullptr;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual FrameGraphRenderResult BeginFrame(RenderFrameRequest& out_request) = 0;
    virtual FrameGraphRenderResult EndFrame(FrameGraphRenderResult render_result) = 0;
};

} // namespace ave::render
