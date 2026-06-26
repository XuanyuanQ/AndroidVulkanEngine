#include "ave/xr/OpenXRActionSystem.h"

#include "ave/xr/OpenXRRuntime.h"
#include "LogUtil.h"

#include <openxr/openxr.h>

#include <array>
#include <cstring>

namespace ave::xr {
namespace {

template <typename Fn>
Fn LoadOpenXRCommand(OpenXRRuntime& runtime, char const* name)
{
    auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(runtime.GetInstanceProcAddr());
    auto instance = reinterpret_cast<XrInstance>(runtime.GetInstanceHandle());
    if (get_proc_addr == nullptr || instance == XR_NULL_HANDLE) {
        return nullptr;
    }

    PFN_xrVoidFunction raw = nullptr;
    XrResult const result = get_proc_addr(instance, name, &raw);
    if (result != XR_SUCCESS || raw == nullptr) {
        LOGW("OpenXR action command unavailable: %s result=%d", name, result);
        return nullptr;
    }
    return reinterpret_cast<Fn>(raw);
}

void CopyName(char* dst, uint32_t dst_size, char const* src)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

bool StringToPath(OpenXRRuntime& runtime, char const* path, XrPath& out_path)
{
    auto xr_string_to_path = LoadOpenXRCommand<PFN_xrStringToPath>(runtime, "xrStringToPath");
    if (xr_string_to_path == nullptr) {
        return false;
    }
    XrResult const result =
        xr_string_to_path(reinterpret_cast<XrInstance>(runtime.GetInstanceHandle()), path, &out_path);
    if (result != XR_SUCCESS) {
        LOGW("OpenXR xrStringToPath failed: path=%s result=%d", path, result);
        return false;
    }
    return true;
}

XrActionCreateInfo MakeActionCreateInfo(XrActionType type,
                                        char const* action_name,
                                        char const* localized_name,
                                        std::array<XrPath, 2> const& subaction_paths)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    info.countSubactionPaths = static_cast<uint32_t>(subaction_paths.size());
    info.subactionPaths = subaction_paths.data();
    CopyName(info.actionName, XR_MAX_ACTION_NAME_SIZE, action_name);
    CopyName(info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, localized_name);
    return info;
}

bool CreateAction(OpenXRRuntime& runtime,
                  XrActionSet action_set,
                  XrActionType type,
                  char const* action_name,
                  char const* localized_name,
                  std::array<XrPath, 2> const& subaction_paths,
                  XrAction& out_action)
{
    auto xr_create_action = LoadOpenXRCommand<PFN_xrCreateAction>(runtime, "xrCreateAction");
    if (xr_create_action == nullptr) {
        return false;
    }

    XrActionCreateInfo info = MakeActionCreateInfo(type, action_name, localized_name, subaction_paths);
    XrResult const result = xr_create_action(action_set, &info, &out_action);
    if (result != XR_SUCCESS || out_action == XR_NULL_HANDLE) {
        LOGW("OpenXR xrCreateAction failed: action=%s result=%d", action_name, result);
        return false;
    }
    return true;
}

XRControllerState ReadControllerState(OpenXRRuntime& runtime,
                                      XrSession session,
                                      XrSpace base_space,
                                      XrAction move_action,
                                      XrAction trigger_action,
                                      XrAction grip_action,
                                      XrAction grip_pose_action,
                                      XrSpace grip_space,
                                      XrPath hand_path,
                                      XrTime predicted_display_time)
{
    XRControllerState state{};

    auto xr_get_vector2 =
        LoadOpenXRCommand<PFN_xrGetActionStateVector2f>(runtime, "xrGetActionStateVector2f");
    auto xr_get_float = LoadOpenXRCommand<PFN_xrGetActionStateFloat>(runtime, "xrGetActionStateFloat");
    auto xr_get_pose = LoadOpenXRCommand<PFN_xrGetActionStatePose>(runtime, "xrGetActionStatePose");
    auto xr_locate_space = LoadOpenXRCommand<PFN_xrLocateSpace>(runtime, "xrLocateSpace");

    if (xr_get_vector2 != nullptr) {
        XrActionStateGetInfo get_info{XR_TYPE_ACTION_STATE_GET_INFO};
        get_info.action = move_action;
        get_info.subactionPath = hand_path;
        XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (xr_get_vector2(session, &get_info, &value) == XR_SUCCESS && value.isActive != 0) {
            state.active = true;
            state.thumbstick = {value.currentState.x, value.currentState.y};
        }
    }

    if (xr_get_float != nullptr) {
        XrActionStateGetInfo get_info{XR_TYPE_ACTION_STATE_GET_INFO};
        get_info.action = trigger_action;
        get_info.subactionPath = hand_path;
        XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
        if (xr_get_float(session, &get_info, &value) == XR_SUCCESS && value.isActive != 0) {
            state.active = true;
            state.trigger = value.currentState;
        }

        get_info.action = grip_action;
        value = {XR_TYPE_ACTION_STATE_FLOAT};
        if (xr_get_float(session, &get_info, &value) == XR_SUCCESS && value.isActive != 0) {
            state.active = true;
            state.grip = value.currentState;
        }
    }

    if (xr_get_pose != nullptr) {
        XrActionStateGetInfo get_info{XR_TYPE_ACTION_STATE_GET_INFO};
        get_info.action = grip_pose_action;
        get_info.subactionPath = hand_path;
        XrActionStatePose pose_state{XR_TYPE_ACTION_STATE_POSE};
        if (xr_get_pose(session, &get_info, &pose_state) == XR_SUCCESS && pose_state.isActive != 0) {
            state.active = true;
        }
    }

    if (xr_locate_space != nullptr && grip_space != XR_NULL_HANDLE && base_space != XR_NULL_HANDLE) {
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XrResult const result = xr_locate_space(grip_space, base_space, predicted_display_time, &location);
        bool const position_valid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        bool const orientation_valid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        if (result == XR_SUCCESS && position_valid && orientation_valid) {
            state.pose_active = true;
            state.active = true;
            state.grip_position = {
                location.pose.position.x,
                location.pose.position.y,
                location.pose.position.z,
            };
            state.grip_orientation = {
                location.pose.orientation.w,
                location.pose.orientation.x,
                location.pose.orientation.y,
                location.pose.orientation.z,
            };
        }
    }

    return state;
}

} // namespace

bool OpenXRActionSystem::Initialize(OpenXRRuntime& runtime)
{
    if (initialized_) {
        return true;
    }
    if (runtime.GetInstanceHandle() == nullptr || runtime.GetSessionHandle() == nullptr) {
        return false;
    }

    auto xr_create_action_set = LoadOpenXRCommand<PFN_xrCreateActionSet>(runtime, "xrCreateActionSet");
    auto xr_suggest_bindings =
        LoadOpenXRCommand<PFN_xrSuggestInteractionProfileBindings>(runtime, "xrSuggestInteractionProfileBindings");
    auto xr_attach_action_sets =
        LoadOpenXRCommand<PFN_xrAttachSessionActionSets>(runtime, "xrAttachSessionActionSets");
    auto xr_create_action_space = LoadOpenXRCommand<PFN_xrCreateActionSpace>(runtime, "xrCreateActionSpace");
    if (xr_create_action_set == nullptr || xr_suggest_bindings == nullptr ||
        xr_attach_action_sets == nullptr || xr_create_action_space == nullptr) {
        return false;
    }

    XrPath left_hand = XR_NULL_PATH;
    XrPath right_hand = XR_NULL_PATH;
    XrPath touch_profile = XR_NULL_PATH;
    XrPath left_thumbstick = XR_NULL_PATH;
    XrPath right_thumbstick = XR_NULL_PATH;
    XrPath left_trigger = XR_NULL_PATH;
    XrPath right_trigger = XR_NULL_PATH;
    XrPath left_squeeze = XR_NULL_PATH;
    XrPath right_squeeze = XR_NULL_PATH;
    XrPath left_grip_pose = XR_NULL_PATH;
    XrPath right_grip_pose = XR_NULL_PATH;

    if (!StringToPath(runtime, "/user/hand/left", left_hand) ||
        !StringToPath(runtime, "/user/hand/right", right_hand) ||
        !StringToPath(runtime, "/interaction_profiles/oculus/touch_controller", touch_profile) ||
        !StringToPath(runtime, "/user/hand/left/input/thumbstick", left_thumbstick) ||
        !StringToPath(runtime, "/user/hand/right/input/thumbstick", right_thumbstick) ||
        !StringToPath(runtime, "/user/hand/left/input/trigger/value", left_trigger) ||
        !StringToPath(runtime, "/user/hand/right/input/trigger/value", right_trigger) ||
        !StringToPath(runtime, "/user/hand/left/input/squeeze/value", left_squeeze) ||
        !StringToPath(runtime, "/user/hand/right/input/squeeze/value", right_squeeze) ||
        !StringToPath(runtime, "/user/hand/left/input/grip/pose", left_grip_pose) ||
        !StringToPath(runtime, "/user/hand/right/input/grip/pose", right_grip_pose)) {
        return false;
    }

    XrActionSetCreateInfo action_set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    CopyName(action_set_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "ave_gameplay");
    CopyName(action_set_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Ave Gameplay");
    action_set_info.priority = 0;

    XrActionSet action_set = XR_NULL_HANDLE;
    XrResult result =
        xr_create_action_set(reinterpret_cast<XrInstance>(runtime.GetInstanceHandle()), &action_set_info, &action_set);
    if (result != XR_SUCCESS || action_set == XR_NULL_HANDLE) {
        LOGW("OpenXR xrCreateActionSet failed: result=%d", result);
        return false;
    }

    std::array<XrPath, 2> const subaction_paths{left_hand, right_hand};
    XrAction move_action = XR_NULL_HANDLE;
    XrAction trigger_action = XR_NULL_HANDLE;
    XrAction grip_action = XR_NULL_HANDLE;
    XrAction grip_pose_action = XR_NULL_HANDLE;
    if (!CreateAction(runtime, action_set, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", subaction_paths, move_action) ||
        !CreateAction(runtime, action_set, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", subaction_paths, trigger_action) ||
        !CreateAction(runtime, action_set, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip", subaction_paths, grip_action) ||
        !CreateAction(runtime, action_set, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", subaction_paths, grip_pose_action)) {
        action_set_ = action_set;
        Shutdown(runtime);
        return false;
    }

    std::array<XrActionSuggestedBinding, 8> bindings{{
        {move_action, left_thumbstick},
        {move_action, right_thumbstick},
        {trigger_action, left_trigger},
        {trigger_action, right_trigger},
        {grip_action, left_squeeze},
        {grip_action, right_squeeze},
        {grip_pose_action, left_grip_pose},
        {grip_pose_action, right_grip_pose},
    }};

    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = touch_profile;
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    result = xr_suggest_bindings(reinterpret_cast<XrInstance>(runtime.GetInstanceHandle()), &suggested);
    if (result != XR_SUCCESS) {
        LOGW("OpenXR xrSuggestInteractionProfileBindings failed: result=%d", result);
        action_set_ = action_set;
        Shutdown(runtime);
        return false;
    }

    XrSessionActionSetsAttachInfo attach_info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach_info.countActionSets = 1;
    attach_info.actionSets = &action_set;
    result = xr_attach_action_sets(reinterpret_cast<XrSession>(runtime.GetSessionHandle()), &attach_info);
    if (result != XR_SUCCESS) {
        LOGW("OpenXR xrAttachSessionActionSets failed: result=%d", result);
        action_set_ = action_set;
        Shutdown(runtime);
        return false;
    }

    XrActionSpaceCreateInfo space_info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space_info.action = grip_pose_action;
    space_info.poseInActionSpace.orientation.w = 1.0f;

    XrSpace left_space = XR_NULL_HANDLE;
    space_info.subactionPath = left_hand;
    result = xr_create_action_space(reinterpret_cast<XrSession>(runtime.GetSessionHandle()), &space_info, &left_space);
    if (result != XR_SUCCESS || left_space == XR_NULL_HANDLE) {
        LOGW("OpenXR xrCreateActionSpace(left grip) failed: result=%d", result);
        action_set_ = action_set;
        Shutdown(runtime);
        return false;
    }

    XrSpace right_space = XR_NULL_HANDLE;
    space_info.subactionPath = right_hand;
    result = xr_create_action_space(reinterpret_cast<XrSession>(runtime.GetSessionHandle()), &space_info, &right_space);
    if (result != XR_SUCCESS || right_space == XR_NULL_HANDLE) {
        LOGW("OpenXR xrCreateActionSpace(right grip) failed: result=%d", result);
        left_grip_space_ = left_space;
        action_set_ = action_set;
        Shutdown(runtime);
        return false;
    }

    left_hand_path_ = left_hand;
    right_hand_path_ = right_hand;
    action_set_ = action_set;
    move_action_ = move_action;
    trigger_action_ = trigger_action;
    grip_action_ = grip_action;
    grip_pose_action_ = grip_pose_action;
    left_grip_space_ = left_space;
    right_grip_space_ = right_space;
    attached_ = true;
    initialized_ = true;
    state_ = {};
    last_log_frame_ = 0;
    LOGI("OpenXRActionSystem initialized: Oculus Touch bindings attached");
    return true;
}

void OpenXRActionSystem::Shutdown(OpenXRRuntime& runtime)
{
    auto xr_destroy_space = LoadOpenXRCommand<PFN_xrDestroySpace>(runtime, "xrDestroySpace");
    auto xr_destroy_action = LoadOpenXRCommand<PFN_xrDestroyAction>(runtime, "xrDestroyAction");
    auto xr_destroy_action_set = LoadOpenXRCommand<PFN_xrDestroyActionSet>(runtime, "xrDestroyActionSet");

    if (xr_destroy_space != nullptr) {
        if (left_grip_space_ != nullptr) {
            xr_destroy_space(reinterpret_cast<XrSpace>(left_grip_space_));
        }
        if (right_grip_space_ != nullptr) {
            xr_destroy_space(reinterpret_cast<XrSpace>(right_grip_space_));
        }
    }
    if (xr_destroy_action != nullptr) {
        if (move_action_ != nullptr) {
            xr_destroy_action(reinterpret_cast<XrAction>(move_action_));
        }
        if (trigger_action_ != nullptr) {
            xr_destroy_action(reinterpret_cast<XrAction>(trigger_action_));
        }
        if (grip_action_ != nullptr) {
            xr_destroy_action(reinterpret_cast<XrAction>(grip_action_));
        }
        if (grip_pose_action_ != nullptr) {
            xr_destroy_action(reinterpret_cast<XrAction>(grip_pose_action_));
        }
    }
    if (xr_destroy_action_set != nullptr && action_set_ != nullptr) {
        xr_destroy_action_set(reinterpret_cast<XrActionSet>(action_set_));
    }

    initialized_ = false;
    attached_ = false;
    left_hand_path_ = 0;
    right_hand_path_ = 0;
    action_set_ = nullptr;
    move_action_ = nullptr;
    trigger_action_ = nullptr;
    grip_action_ = nullptr;
    grip_pose_action_ = nullptr;
    left_grip_space_ = nullptr;
    right_grip_space_ = nullptr;
    last_log_frame_ = 0;
    state_ = {};
}

void OpenXRActionSystem::SyncAndLog(OpenXRRuntime& runtime, int64_t predicted_display_time)
{
    if (!initialized_ || !attached_ || !runtime.IsSessionRunning() ||
        runtime.GetSessionHandle() == nullptr || runtime.GetLocalSpaceHandle() == nullptr) {
        return;
    }

    auto xr_sync_actions = LoadOpenXRCommand<PFN_xrSyncActions>(runtime, "xrSyncActions");
    if (xr_sync_actions == nullptr) {
        return;
    }

    XrActiveActionSet active_action_set{};
    active_action_set.actionSet = reinterpret_cast<XrActionSet>(action_set_);
    active_action_set.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo sync_info{XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active_action_set;

    XrResult const sync_result =
        xr_sync_actions(reinterpret_cast<XrSession>(runtime.GetSessionHandle()), &sync_info);
    if (sync_result != XR_SUCCESS) {
        LOGW("OpenXR xrSyncActions failed: result=%d", sync_result);
        return;
    }

    auto session = reinterpret_cast<XrSession>(runtime.GetSessionHandle());
    auto base_space = reinterpret_cast<XrSpace>(runtime.GetLocalSpaceHandle());
    state_.left = ReadControllerState(runtime,
                                      session,
                                      base_space,
                                      reinterpret_cast<XrAction>(move_action_),
                                      reinterpret_cast<XrAction>(trigger_action_),
                                      reinterpret_cast<XrAction>(grip_action_),
                                      reinterpret_cast<XrAction>(grip_pose_action_),
                                      reinterpret_cast<XrSpace>(left_grip_space_),
                                      static_cast<XrPath>(left_hand_path_),
                                      static_cast<XrTime>(predicted_display_time));
    state_.right = ReadControllerState(runtime,
                                       session,
                                       base_space,
                                       reinterpret_cast<XrAction>(move_action_),
                                       reinterpret_cast<XrAction>(trigger_action_),
                                       reinterpret_cast<XrAction>(grip_action_),
                                       reinterpret_cast<XrAction>(grip_pose_action_),
                                       reinterpret_cast<XrSpace>(right_grip_space_),
                                       static_cast<XrPath>(right_hand_path_),
                                       static_cast<XrTime>(predicted_display_time));

    ++last_log_frame_;
    if ((last_log_frame_ % 60u) != 1u) {
        return;
    }

    LOGI("XR input L active=%d stick=(%.2f, %.2f) trigger=%.2f grip=%.2f pose=%d pos=(%.2f, %.2f, %.2f) | "
         "R active=%d stick=(%.2f, %.2f) trigger=%.2f grip=%.2f pose=%d pos=(%.2f, %.2f, %.2f)",
         state_.left.active ? 1 : 0,
         state_.left.thumbstick.x,
         state_.left.thumbstick.y,
         state_.left.trigger,
         state_.left.grip,
         state_.left.pose_active ? 1 : 0,
         state_.left.grip_position.x,
         state_.left.grip_position.y,
         state_.left.grip_position.z,
         state_.right.active ? 1 : 0,
         state_.right.thumbstick.x,
         state_.right.thumbstick.y,
         state_.right.trigger,
         state_.right.grip,
         state_.right.pose_active ? 1 : 0,
         state_.right.grip_position.x,
         state_.right.grip_position.y,
         state_.right.grip_position.z);
}

} // namespace ave::xr
