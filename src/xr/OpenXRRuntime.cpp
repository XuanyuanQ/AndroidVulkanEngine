#include "ave/xr/OpenXRRuntime.h"

#include "VkContext.hpp"
#include "LogUtil.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

#include <cstring>

namespace ave::xr {

namespace {

#if defined(__ANDROID__)
using XrBool32 = uint32_t;
using XrFlags64 = uint64_t;
using XrSystemId = uint64_t;
using XrVersion = uint64_t;
using XrResult = int32_t;
using XrStructureType = int32_t;
using XrFormFactor = int32_t;
using XrReferenceSpaceType = int32_t;
using XrSessionState = int32_t;
using XrSessionCreateFlags = uint64_t;
using XrInstance = struct XrInstance_T*;
using XrSession = struct XrSession_T*;
using XrSpace = struct XrSpace_T*;

constexpr XrResult XR_SUCCESS = 0;
constexpr XrResult XR_EVENT_UNAVAILABLE = 1;
constexpr XrStructureType XR_TYPE_INSTANCE_CREATE_INFO = 3;
constexpr XrStructureType XR_TYPE_SYSTEM_GET_INFO = 4;
constexpr XrStructureType XR_TYPE_SESSION_CREATE_INFO = 8;
constexpr XrStructureType XR_TYPE_EVENT_DATA_BUFFER = 16;
constexpr XrStructureType XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED = 18;
constexpr XrStructureType XR_TYPE_REFERENCE_SPACE_CREATE_INFO = 37;
constexpr XrStructureType XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR = 1000089000;
constexpr XrFormFactor XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1;
constexpr XrReferenceSpaceType XR_REFERENCE_SPACE_TYPE_LOCAL = 2;
constexpr XrSessionState XR_SESSION_STATE_UNKNOWN = 0;
constexpr XrSessionState XR_SESSION_STATE_IDLE = 1;
constexpr XrSessionState XR_SESSION_STATE_READY = 2;
constexpr XrSessionState XR_SESSION_STATE_SYNCHRONIZED = 3;
constexpr XrSessionState XR_SESSION_STATE_VISIBLE = 4;
constexpr XrSessionState XR_SESSION_STATE_FOCUSED = 5;
constexpr XrSessionState XR_SESSION_STATE_STOPPING = 6;
constexpr XrSessionState XR_SESSION_STATE_LOSS_PENDING = 7;
constexpr XrSessionState XR_SESSION_STATE_EXITING = 8;
constexpr XrVersion XR_CURRENT_API_VERSION = (1ULL << 48);
constexpr uint32_t XR_MAX_APPLICATION_NAME_SIZE = 128;
constexpr uint32_t XR_MAX_ENGINE_NAME_SIZE = 128;
constexpr char XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME[] = "XR_KHR_vulkan_enable2";

struct XrApplicationInfo {
    char applicationName[XR_MAX_APPLICATION_NAME_SIZE];
    uint32_t applicationVersion;
    char engineName[XR_MAX_ENGINE_NAME_SIZE];
    uint32_t engineVersion;
    XrVersion apiVersion;
};

struct XrInstanceCreateInfo {
    XrStructureType type;
    void const* next;
    XrFlags64 createFlags;
    XrApplicationInfo applicationInfo;
    uint32_t enabledApiLayerCount;
    char const* const* enabledApiLayerNames;
    uint32_t enabledExtensionCount;
    char const* const* enabledExtensionNames;
};

struct XrSystemGetInfo {
    XrStructureType type;
    void const* next;
    XrFormFactor formFactor;
};

struct XrSessionCreateInfo {
    XrStructureType type;
    void const* next;
    XrSessionCreateFlags createFlags;
    XrSystemId systemId;
};

struct XrQuaternionf {
    float x;
    float y;
    float z;
    float w;
};

struct XrVector3f {
    float x;
    float y;
    float z;
};

struct XrPosef {
    XrQuaternionf orientation;
    XrVector3f position;
};

struct XrReferenceSpaceCreateInfo {
    XrStructureType type;
    void const* next;
    XrReferenceSpaceType referenceSpaceType;
    XrPosef poseInReferenceSpace;
};

struct XrEventDataBuffer {
    XrStructureType type;
    void const* next;
    uint8_t varying[4000];
};

struct XrEventDataSessionStateChanged {
    XrStructureType type;
    void const* next;
    XrSession session;
    XrSessionState state;
    int64_t time;
};

struct XrLoaderInitInfoAndroidKHR {
    XrStructureType type;
    void const* next;
    void* applicationVM;
    void* applicationContext;
};

using PFN_xrVoidFunction = void (*)();
using PFN_xrGetInstanceProcAddr = XrResult (*)(XrInstance instance, char const* name, PFN_xrVoidFunction* function);
using PFN_xrInitializeLoaderKHR = XrResult (*)(XrLoaderInitInfoAndroidKHR const* loader_init_info);
using PFN_xrCreateInstance = XrResult (*)(XrInstanceCreateInfo const* create_info, XrInstance* instance);
using PFN_xrDestroyInstance = XrResult (*)(XrInstance instance);
using PFN_xrGetSystem = XrResult (*)(XrInstance instance, XrSystemGetInfo const* get_info, XrSystemId* system_id);
using PFN_xrCreateSession = XrResult (*)(XrInstance instance, XrSessionCreateInfo const* create_info, XrSession* session);
using PFN_xrDestroySession = XrResult (*)(XrSession session);
using PFN_xrCreateReferenceSpace = XrResult (*)(XrSession session, XrReferenceSpaceCreateInfo const* create_info, XrSpace* space);
using PFN_xrDestroySpace = XrResult (*)(XrSpace space);
using PFN_xrPollEvent = XrResult (*)(XrInstance instance, XrEventDataBuffer* event_data);

template <typename Fn>
Fn LoadOpenXRCommand(PFN_xrGetInstanceProcAddr get_proc_addr, XrInstance instance, char const* name)
{
    PFN_xrVoidFunction raw = nullptr;
    XrResult const result = get_proc_addr(instance, name, &raw);
    if (result != XR_SUCCESS || raw == nullptr) {
        LOGW("OpenXR command unavailable: %s result=%d", name, result);
        return nullptr;
    }
    return reinterpret_cast<Fn>(raw);
}

#endif

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

char const* ToString(XrSessionState state)
{
    switch (state) {
    case XR_SESSION_STATE_UNKNOWN:
        return "UNKNOWN";
    case XR_SESSION_STATE_IDLE:
        return "IDLE";
    case XR_SESSION_STATE_READY:
        return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED:
        return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:
        return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:
        return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:
        return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING:
        return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:
        return "EXITING";
    }
    return "UNKNOWN_VALUE";
}

} // namespace

bool OpenXRRuntime::ProbeOpenXRLoader()
{
#if defined(__ANDROID__)
    loader_handle_ = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if (loader_handle_ == nullptr) {
        LOGW("OpenXR loader not available: %s", dlerror());
        return false;
    }

    auto get_proc_addr =
        reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(loader_handle_, "xrGetInstanceProcAddr"));
    if (get_proc_addr == nullptr) {
        LOGW("OpenXR loader missing xrGetInstanceProcAddr: %s", dlerror());
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    get_instance_proc_addr_ = reinterpret_cast<void*>(get_proc_addr);

    auto xr_initialize_loader =
        LoadOpenXRCommand<PFN_xrInitializeLoaderKHR>(get_proc_addr, nullptr, "xrInitializeLoaderKHR");
    if (xr_initialize_loader == nullptr) {
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    if (android_application_vm_ == nullptr || android_application_context_ == nullptr) {
        LOGW("OpenXR loader init skipped: missing Android VM or application context");
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    XrLoaderInitInfoAndroidKHR loader_init_info{};
    loader_init_info.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loader_init_info.next = nullptr;
    loader_init_info.applicationVM = android_application_vm_;
    loader_init_info.applicationContext = android_application_context_;
    XrResult result = xr_initialize_loader(&loader_init_info);
    if (result != XR_SUCCESS) {
        LOGW("OpenXR xrInitializeLoaderKHR failed: result=%d", result);
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR xrInitializeLoaderKHR success");

    auto xr_create_instance = LoadOpenXRCommand<PFN_xrCreateInstance>(get_proc_addr, nullptr, "xrCreateInstance");
    if (xr_create_instance == nullptr) {
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    XrInstanceCreateInfo create_info{};
    create_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    create_info.next = nullptr;
    create_info.createFlags = 0;
    std::strncpy(create_info.applicationInfo.applicationName,
                 "AveTriangleGame",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    create_info.applicationInfo.applicationVersion = 1;
    std::strncpy(create_info.applicationInfo.engineName,
                 "AveEngine",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    create_info.applicationInfo.engineVersion = 1;
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    char const* enabled_extensions[] = {
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };
    create_info.enabledExtensionCount = 1;
    create_info.enabledExtensionNames = enabled_extensions;

    XrInstance instance = nullptr;
    result = xr_create_instance(&create_info, &instance);
    if (result != XR_SUCCESS || instance == nullptr) {
        LOGW("OpenXR xrCreateInstance failed: result=%d", result);
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    auto xr_destroy_instance = LoadOpenXRCommand<PFN_xrDestroyInstance>(get_proc_addr, instance, "xrDestroyInstance");
    auto xr_get_system = LoadOpenXRCommand<PFN_xrGetSystem>(get_proc_addr, instance, "xrGetSystem");
    if (xr_get_system == nullptr) {
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    XrSystemGetInfo system_info{};
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.next = nullptr;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system_id = 0;
    result = xr_get_system(instance, &system_info, &system_id);
    if (result != XR_SUCCESS || system_id == 0) {
        LOGW("OpenXR xrGetSystem failed: result=%d system_id=%llu",
             result,
             static_cast<unsigned long long>(system_id));
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    instance_handle_ = instance;
    system_id_ = system_id;
    LOGI("OpenXR probe success: instance=%p system_id=%llu",
         instance_handle_,
         static_cast<unsigned long long>(system_id_));
    return true;
#else
    LOGW("OpenXR probe unavailable on this platform");
    return false;
#endif
}

bool OpenXRRuntime::Initialize(OpenXRRuntimeConfig const& config)
{
    enabled_ = config.enabled;
    android_application_vm_ = config.android_application_vm;
    android_application_context_ = config.android_application_context;
    if (!enabled_) {
        state_ = OpenXRRuntimeState::Disabled;
        initialized_ = false;
        frame_started_ = false;
        LOGI("OpenXRRuntime disabled");
        return true;
    }

    if (!ProbeOpenXRLoader()) {
        LOGW("OpenXRRuntime requested but no usable OpenXR runtime was found; continuing without XR rendering");
        enabled_ = false;
        initialized_ = false;
        frame_started_ = false;
        state_ = OpenXRRuntimeState::Disabled;
        return true;
    }

    initialized_ = true;
    frame_started_ = false;
    logged_stub_frame_ = false;
    session_state_ = XR_SESSION_STATE_UNKNOWN;
    session_running_ = false;
    state_ = OpenXRRuntimeState::Ready;
    LOGI("OpenXRRuntime initialized: state=%s system_id=%llu",
         ToString(state_),
         static_cast<unsigned long long>(system_id_));
    return true;
}

bool OpenXRRuntime::CreateSession(void const* graphics_binding)
{
#if defined(__ANDROID__)
    if (!enabled_ || !initialized_ || instance_handle_ == nullptr || get_instance_proc_addr_ == nullptr ||
        graphics_binding == nullptr || system_id_ == 0) {
        return false;
    }
    if (session_handle_ != nullptr) {
        return true;
    }

    auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(get_instance_proc_addr_);
    auto xr_create_session =
        LoadOpenXRCommand<PFN_xrCreateSession>(get_proc_addr,
                                               static_cast<XrInstance>(instance_handle_),
                                               "xrCreateSession");
    auto xr_create_reference_space =
        LoadOpenXRCommand<PFN_xrCreateReferenceSpace>(get_proc_addr,
                                                      static_cast<XrInstance>(instance_handle_),
                                                      "xrCreateReferenceSpace");
    if (xr_create_session == nullptr || xr_create_reference_space == nullptr) {
        return false;
    }

    XrSessionCreateInfo session_info{};
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.next = graphics_binding;
    session_info.createFlags = 0;
    session_info.systemId = static_cast<XrSystemId>(system_id_);

    XrSession session = nullptr;
    XrResult result = xr_create_session(static_cast<XrInstance>(instance_handle_), &session_info, &session);
    if (result != XR_SUCCESS || session == nullptr) {
        LOGW("OpenXR xrCreateSession failed: result=%d", result);
        return false;
    }
    LOGI("OpenXR xrCreateSession success: session=%p", static_cast<void*>(session));

    XrReferenceSpaceCreateInfo local_space_info{};
    local_space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    local_space_info.next = nullptr;
    local_space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    local_space_info.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    local_space_info.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

    XrSpace local_space = nullptr;
    result = xr_create_reference_space(session, &local_space_info, &local_space);
    if (result != XR_SUCCESS || local_space == nullptr) {
        LOGW("OpenXR xrCreateReferenceSpace(LOCAL) failed: result=%d", result);
        auto xr_destroy_session =
            LoadOpenXRCommand<PFN_xrDestroySession>(get_proc_addr,
                                                    static_cast<XrInstance>(instance_handle_),
                                                    "xrDestroySession");
        if (xr_destroy_session != nullptr) {
            xr_destroy_session(session);
        }
        return false;
    }

    session_handle_ = session;
    local_space_handle_ = local_space;
    LOGI("OpenXR xrCreateReferenceSpace(LOCAL) success: space=%p", static_cast<void*>(local_space));
    return true;
#else
    (void)graphics_binding;
    return false;
#endif
}

void OpenXRRuntime::DestroySession()
{
#if defined(__ANDROID__)
    if (instance_handle_ == nullptr || get_instance_proc_addr_ == nullptr) {
        session_handle_ = nullptr;
        local_space_handle_ = nullptr;
        return;
    }

    auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(get_instance_proc_addr_);
    if (local_space_handle_ != nullptr) {
        auto xr_destroy_space =
            LoadOpenXRCommand<PFN_xrDestroySpace>(get_proc_addr,
                                                  static_cast<XrInstance>(instance_handle_),
                                                  "xrDestroySpace");
        if (xr_destroy_space != nullptr) {
            xr_destroy_space(static_cast<XrSpace>(local_space_handle_));
        }
    }
    if (session_handle_ != nullptr) {
        auto xr_destroy_session =
            LoadOpenXRCommand<PFN_xrDestroySession>(get_proc_addr,
                                                    static_cast<XrInstance>(instance_handle_),
                                                    "xrDestroySession");
        if (xr_destroy_session != nullptr) {
            xr_destroy_session(static_cast<XrSession>(session_handle_));
        }
    }
#endif
    local_space_handle_ = nullptr;
    session_handle_ = nullptr;
    session_state_ = XR_SESSION_STATE_UNKNOWN;
    session_running_ = false;
}

void OpenXRRuntime::Shutdown()
{
    if (!enabled_ && !initialized_) {
        return;
    }

    LOGI("OpenXRRuntime shutdown: initialized=%d state=%s", initialized_ ? 1 : 0, ToString(state_));
#if defined(__ANDROID__)
    if (instance_handle_ != nullptr && loader_handle_ != nullptr) {
        auto get_proc_addr =
            reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(loader_handle_, "xrGetInstanceProcAddr"));
        if (get_proc_addr != nullptr) {
            DestroySession();
            auto xr_destroy_instance =
                LoadOpenXRCommand<PFN_xrDestroyInstance>(get_proc_addr,
                                                         static_cast<XrInstance>(instance_handle_),
                                                         "xrDestroyInstance");
            if (xr_destroy_instance != nullptr) {
                xr_destroy_instance(static_cast<XrInstance>(instance_handle_));
            }
        }
    }
    if (loader_handle_ != nullptr) {
        dlclose(loader_handle_);
    }
#endif
    loader_handle_ = nullptr;
    get_instance_proc_addr_ = nullptr;
    instance_handle_ = nullptr;
    session_handle_ = nullptr;
    local_space_handle_ = nullptr;
    system_id_ = 0;
    session_state_ = 0;
    session_running_ = false;
    initialized_ = false;
    frame_started_ = false;
    state_ = enabled_ ? OpenXRRuntimeState::Idle : OpenXRRuntimeState::Disabled;
}

void OpenXRRuntime::PollEvents()
{
    if (!enabled_ || !initialized_) {
        return;
    }

#if defined(__ANDROID__)
    if (instance_handle_ == nullptr || get_instance_proc_addr_ == nullptr) {
        return;
    }

    auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(get_instance_proc_addr_);
    auto xr_poll_event =
        LoadOpenXRCommand<PFN_xrPollEvent>(get_proc_addr,
                                           static_cast<XrInstance>(instance_handle_),
                                           "xrPollEvent");
    if (xr_poll_event == nullptr) {
        return;
    }

    for (;;) {
        XrEventDataBuffer event_data{};
        event_data.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult const result = xr_poll_event(static_cast<XrInstance>(instance_handle_), &event_data);
        if (result == XR_EVENT_UNAVAILABLE) {
            break;
        }
        if (result != XR_SUCCESS) {
            LOGW("OpenXR xrPollEvent failed: result=%d", result);
            break;
        }

        if (event_data.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto const* session_event =
                reinterpret_cast<XrEventDataSessionStateChanged const*>(&event_data);
            session_state_ = session_event->state;
            LOGI("OpenXR session state changed: %s(%d) session=%p",
                 ToString(session_event->state),
                 session_event->state,
                 static_cast<void*>(session_event->session));

            switch (session_event->state) {
            case XR_SESSION_STATE_READY:
                state_ = OpenXRRuntimeState::Ready;
                break;
            case XR_SESSION_STATE_SYNCHRONIZED:
            case XR_SESSION_STATE_VISIBLE:
            case XR_SESSION_STATE_FOCUSED:
                state_ = OpenXRRuntimeState::Focused;
                break;
            case XR_SESSION_STATE_STOPPING:
            case XR_SESSION_STATE_LOSS_PENDING:
            case XR_SESSION_STATE_EXITING:
                state_ = OpenXRRuntimeState::Stopping;
                session_running_ = false;
                break;
            default:
                state_ = OpenXRRuntimeState::Ready;
                break;
            }
        }
    }
#else
    if (state_ == OpenXRRuntimeState::Ready) {
        state_ = OpenXRRuntimeState::Focused;
        LOGI("OpenXRRuntime stub session focused");
    }
#endif
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

void OpenXRRuntime::MarkSessionBegan() noexcept
{
    session_running_ = true;
    state_ = OpenXRRuntimeState::Focused;
}

void OpenXRRuntime::MarkSessionEnded() noexcept
{
    session_running_ = false;
}

} // namespace ave::xr
