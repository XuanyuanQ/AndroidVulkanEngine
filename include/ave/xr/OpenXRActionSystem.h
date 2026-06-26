#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace ave::xr {

class OpenXRRuntime;

struct XRControllerState {
    bool active = false;
    bool pose_active = false;
    glm::vec2 thumbstick{0.0f, 0.0f};
    float trigger = 0.0f;
    float grip = 0.0f;
    glm::vec3 grip_position{0.0f, 0.0f, 0.0f};
    glm::quat grip_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    bool aim_pose_active = false;
    glm::vec3 aim_position{0.0f, 0.0f, 0.0f};
    glm::quat aim_orientation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct XRInputState {
    XRControllerState left{};
    XRControllerState right{};
};

class OpenXRActionSystem {
public:
    bool Initialize(OpenXRRuntime& runtime);
    void Shutdown(OpenXRRuntime& runtime);
    void SyncAndLog(OpenXRRuntime& runtime, int64_t predicted_display_time);

    bool IsInitialized() const noexcept { return initialized_; }
    XRInputState const& State() const noexcept { return state_; }

private:
    bool initialized_ = false;
    bool attached_ = false;
    uint64_t left_hand_path_ = 0;
    uint64_t right_hand_path_ = 0;
    void* action_set_ = nullptr;
    void* move_action_ = nullptr;
    void* trigger_action_ = nullptr;
    void* grip_action_ = nullptr;
    void* grip_pose_action_ = nullptr;
    void* aim_pose_action_ = nullptr;
    void* left_grip_space_ = nullptr;
    void* right_grip_space_ = nullptr;
    void* left_aim_space_ = nullptr;
    void* right_aim_space_ = nullptr;
    uint64_t last_log_frame_ = 0;
    XRInputState state_{};
};

} // namespace ave::xr
