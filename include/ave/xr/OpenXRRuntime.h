#pragma once

#include "ave/core/FrameData.h"
#include "ave/xr/OpenXRActionSystem.h"

#include <cstdint>

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
    void* android_application_vm = nullptr;
    void* android_application_context = nullptr;
};

// Minimal OpenXR lifecycle shell. This class intentionally has no OpenXR SDK
// dependency yet; the real implementation can replace the stub internals while
// keeping the Android/runtime integration points stable.
class OpenXRRuntime {
public:
    bool Initialize(OpenXRRuntimeConfig const& config);
    void Shutdown();

    void PollEvents();
    bool BeginFrame(core::FrameData const& frame);
    void EndFrame();

    bool CreateSession(void const* graphics_binding);
    void DestroySession();
    bool IsSessionReadyToBegin() const noexcept { return session_state_ == 2 && !session_running_; }
    bool IsSessionRunning() const noexcept { return session_running_; }
    void MarkSessionBegan() noexcept;
    void MarkSessionEnded() noexcept;

    bool IsEnabled() const noexcept { return enabled_; }
    bool IsInitialized() const noexcept { return initialized_; }
    OpenXRRuntimeState State() const noexcept { return state_; }
    void* GetInstanceHandle() const noexcept { return instance_handle_; }
    void* GetInstanceProcAddr() const noexcept { return get_instance_proc_addr_; }
    void* GetSessionHandle() const noexcept { return session_handle_; }
    void* GetLocalSpaceHandle() const noexcept { return local_space_handle_; }
    uint64_t SystemId() const noexcept { return system_id_; }
    void SyncActionsAndLog(int64_t predicted_display_time);
    XRInputState const& InputState() const noexcept { return action_system_.State(); }

private:
    bool ProbeOpenXRLoader();

    bool enabled_ = false;
    bool initialized_ = false;
    bool frame_started_ = false;
    bool logged_stub_frame_ = false;
    void* loader_handle_ = nullptr;
    void* get_instance_proc_addr_ = nullptr;
    void* instance_handle_ = nullptr;
    void* session_handle_ = nullptr;
    void* local_space_handle_ = nullptr;
    void* android_application_vm_ = nullptr;
    void* android_application_context_ = nullptr;
    uint64_t system_id_ = 0;
    int32_t session_state_ = 0;
    bool session_running_ = false;
    OpenXRActionSystem action_system_{};
    OpenXRRuntimeState state_ = OpenXRRuntimeState::Disabled;
};

} // namespace ave::xr
