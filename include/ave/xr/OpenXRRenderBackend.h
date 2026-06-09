#pragma once

#include "ave/render/RenderBackend.h"

#include <vector>

namespace ave::xr {

// OpenXR-facing render backend shell. The real OpenXR integration will fill
// FrameTargets from XR swapchain images after xrAcquireSwapchainImage.
class OpenXRRenderBackend final : public render::RenderBackend {
public:
    struct FrameTargets {
        core::FrameData const* frame = nullptr;
        vkfw::VkContext* vk = nullptr;
        vk::CommandBuffer command_buffer = {};
        std::vector<render::RenderViewTarget> views{};
    };

    void SetNextFrameTargets(FrameTargets targets);
    void ClearNextFrameTargets();

    render::FrameGraphRenderResult BeginFrame(render::RenderFrameRequest& out_request) override;
    render::FrameGraphRenderResult EndFrame(render::FrameGraphRenderResult render_result) override;

private:
    FrameTargets next_targets_{};
    bool has_next_targets_ = false;
    bool frame_started_ = false;
};

} // namespace ave::xr
