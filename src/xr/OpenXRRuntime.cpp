#include "ave/xr/OpenXRRuntime.h"

#include "VkContext.hpp"
#include "LogUtil.h"

namespace ave::xr {

namespace {

char const* ToString(OpenXRRuntimeState state)
{
    switch (state) {
    case OpenXRRuntimeState::Disabled:
        return "Disabled";
    case OpenXRRuntimeState::Idle:
        return "Idle";
    case OpenXRRuntimeState::Ready:
        return "Ready";
    case OpenXRRuntimeState::Focused:
        return "Focused";
    case OpenXRRuntimeState::Stopping:
        return "Stopping";
    }
    return "Unknown";
}

} // namespace

bool OpenXRRuntime::Initialize(vkfw::VkContext& ctx, OpenXRRuntimeConfig const& config)
{
    enabled_ = config.enabled;
    if (!enabled_) {
        state_ = OpenXRRuntimeState::Disabled;
        initialized_ = false;
        frame_started_ = false;
        LOGI("OpenXRRuntime disabled");
        return true;
    }

    if (!ctx.IsInitialized()) {
        LOGW("OpenXRRuntime requested but VkContext is not initialized");
        return false;
    }

    initialized_ = true;
    frame_started_ = false;
    logged_stub_frame_ = false;
    state_ = OpenXRRuntimeState::Ready;
    LOGI("OpenXRRuntime initialized in stub mode: state=%s", ToString(state_));
    return true;
}

void OpenXRRuntime::Shutdown(vkfw::VkContext* ctx)
{
    (void)ctx;
    if (!enabled_ && !initialized_) {
        return;
    }

    LOGI("OpenXRRuntime shutdown: initialized=%d state=%s", initialized_ ? 1 : 0, ToString(state_));
    initialized_ = false;
    frame_started_ = false;
    state_ = enabled_ ? OpenXRRuntimeState::Idle : OpenXRRuntimeState::Disabled;
}

void OpenXRRuntime::PollEvents()
{
    if (!enabled_ || !initialized_) {
        return;
    }

    // Stub mode has no platform events yet. Keep this call site so the Android
    // render loop already matches the future OpenXR session event flow.
    if (state_ == OpenXRRuntimeState::Ready) {
        state_ = OpenXRRuntimeState::Focused;
        LOGI("OpenXRRuntime stub session focused");
    }
}

bool OpenXRRuntime::BeginFrame(core::FrameData const& frame)
{
    if (!enabled_ || !initialized_ || state_ != OpenXRRuntimeState::Focused) {
        return false;
    }

    frame_started_ = true;
    if (!logged_stub_frame_) {
        LOGI("OpenXRRuntime stub BeginFrame: views=%zu frame_index=%llu",
             frame.views.size(),
             static_cast<unsigned long long>(frame.frame_index));
        logged_stub_frame_ = true;
    }
    return true;
}

void OpenXRRuntime::EndFrame()
{
    if (!frame_started_) {
        return;
    }

    frame_started_ = false;
}

} // namespace ave::xr
