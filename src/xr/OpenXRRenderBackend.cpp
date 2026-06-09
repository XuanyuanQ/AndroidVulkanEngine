#include "ave/xr/OpenXRRenderBackend.h"

#include <utility>

namespace ave::xr {

void OpenXRRenderBackend::SetNextFrameTargets(FrameTargets targets)
{
    next_targets_ = std::move(targets);
    has_next_targets_ = true;
}

void OpenXRRenderBackend::ClearNextFrameTargets()
{
    next_targets_ = {};
    has_next_targets_ = false;
    frame_started_ = false;
}

render::FrameGraphRenderResult OpenXRRenderBackend::BeginFrame(render::RenderFrameRequest& out_request)
{
    if (!has_next_targets_ || next_targets_.frame == nullptr || next_targets_.vk == nullptr ||
        next_targets_.command_buffer == vk::CommandBuffer{} || next_targets_.views.empty()) {
        return render::FrameGraphRenderResult::Skipped;
    }

    out_request = {};
    out_request.frame = next_targets_.frame;
    out_request.vk = next_targets_.vk;
    out_request.command_buffer = next_targets_.command_buffer;
    out_request.views = next_targets_.views;
    frame_started_ = true;
    return render::FrameGraphRenderResult::Success;
}

render::FrameGraphRenderResult OpenXRRenderBackend::EndFrame(render::FrameGraphRenderResult render_result)
{
    if (!frame_started_) {
        return render_result;
    }

    ClearNextFrameTargets();
    return render_result;
}

} // namespace ave::xr
