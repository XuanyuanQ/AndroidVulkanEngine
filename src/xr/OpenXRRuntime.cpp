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
using XrInstance = struct XrInstance_T*;

constexpr XrResult XR_SUCCESS = 0;
constexpr XrStructureType XR_TYPE_INSTANCE_CREATE_INFO = 3;
constexpr XrStructureType XR_TYPE_SYSTEM_GET_INFO = 4;
constexpr XrStructureType XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR = 1000089000;
constexpr XrFormFactor XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1;
constexpr XrVersion XR_CURRENT_API_VERSION = (1ULL << 48);
constexpr uint32_t XR_MAX_APPLICATION_NAME_SIZE = 128;
constexpr uint32_t XR_MAX_ENGINE_NAME_SIZE = 128;

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

bool OpenXRRuntime::Initialize(vkfw::VkContext& ctx, OpenXRRuntimeConfig const& config)
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

    if (!ctx.IsInitialized()) {
        LOGW("OpenXRRuntime requested but VkContext is not initialized");
        return false;
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
    state_ = OpenXRRuntimeState::Ready;
    LOGI("OpenXRRuntime initialized: state=%s system_id=%llu",
         ToString(state_),
         static_cast<unsigned long long>(system_id_));
    return true;
}

void OpenXRRuntime::Shutdown(vkfw::VkContext* ctx)
{
    (void)ctx;
    if (!enabled_ && !initialized_) {
        return;
    }

    LOGI("OpenXRRuntime shutdown: initialized=%d state=%s", initialized_ ? 1 : 0, ToString(state_));
#if defined(__ANDROID__)
    if (instance_handle_ != nullptr && loader_handle_ != nullptr) {
        auto get_proc_addr =
            reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(loader_handle_, "xrGetInstanceProcAddr"));
        if (get_proc_addr != nullptr) {
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
    instance_handle_ = nullptr;
    system_id_ = 0;
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
