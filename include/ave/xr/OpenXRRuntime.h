#pragma once

#include "ave/core/FrameData.h"

#include <cstdint>

namespace vkfw {
class VkContext;
}

namespace ave::xr {

enum class OpenXRRuntimeState : uint8_t {
    Disabled,
    Idle,
    Ready,
    Focused,
    Stopping,
};

struct OpenXRRuntimeConfig {
    bool enabled = false;
};

// Minimal OpenXR lifecycle shell. This class intentionally has no OpenXR SDK
// dependency yet; the real implementation can replace the stub internals while
// keeping the Android/runtime integration points stable.
class OpenXRRuntime {
public:
    bool Initialize(vkfw::VkContext& ctx, OpenXRRuntimeConfig const& config);
    void Shutdown(vkfw::VkContext* ctx);

    void PollEvents();
    bool BeginFrame(core::FrameData const& frame);
    void EndFrame();

    bool IsEnabled() const noexcept { return enabled_; }
    bool IsInitialized() const noexcept { return initialized_; }
    OpenXRRuntimeState State() const noexcept { return state_; }

private:
    bool ProbeOpenXRLoader();

    bool enabled_ = false;
    bool initialized_ = false;
    bool frame_started_ = false;
    bool logged_stub_frame_ = false;
    void* loader_handle_ = nullptr;
    void* instance_handle_ = nullptr;
    uint64_t system_id_ = 0;
    OpenXRRuntimeState state_ = OpenXRRuntimeState::Disabled;
};

} // namespace ave::xr
