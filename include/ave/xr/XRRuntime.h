#pragma once

#include "ave/core/FrameData.h"
#include "ave/render/Renderer.h"

#include <cstdint>
#include <vector>

namespace vkfw {
class VkContext;
}

namespace ave::xr {

enum class XRSessionState : uint8_t {
    Idle,
    Ready,
    Focused,
    Stopping,
};

struct XRFrameTiming {
    uint64_t predicted_display_time_ns = 0;
    float delta_time = 0.0f;
};

struct XRFrameTargets {
    XRFrameTiming timing{};
    std::vector<render::RenderViewTarget> views{};

    bool IsRenderable() const noexcept
    {
        return !views.empty();
    }
};

// SDK-facing boundary for OpenXR or vendor-specific XR integrations.
//
// The implementation owns XR instance/session/swapchains and translates acquired
// eye images into Renderer::RenderViewTarget. The renderer stays unaware of
// XrSession, ANativeWindow, or the final xrEndFrame composition submit.
class XRRuntimeBackend {
public:
    virtual ~XRRuntimeBackend() = default;

    virtual bool Initialize(vkfw::VkContext& ctx) = 0;
    virtual void Shutdown(vkfw::VkContext& ctx) = 0;

    virtual XRSessionState SessionState() const noexcept = 0;
    virtual bool BeginFrame(core::FrameData const& frame, XRFrameTargets& out_targets) = 0;
    virtual void EndFrame(XRFrameTargets const& rendered_targets) = 0;
};

} // namespace ave::xr
