#include "ave/xr/OpenXRRuntime.h"

#include "VkContext.hpp"
#include "LogUtil.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

#include <cstring>
#include <vector>

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
using XrSessionCreateFlags = uint64_t;
using XrInstance = struct XrInstance_T*;
using XrSession = struct XrSession_T*;
using XrSpace = struct XrSpace_T*;

constexpr XrResult XR_SUCCESS = 0;
constexpr XrStructureType XR_TYPE_INSTANCE_CREATE_INFO = 3;
constexpr XrStructureType XR_TYPE_SYSTEM_GET_INFO = 4;
constexpr XrStructureType XR_TYPE_SESSION_CREATE_INFO = 8;
constexpr XrStructureType XR_TYPE_REFERENCE_SPACE_CREATE_INFO = 37;
constexpr XrStructureType XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR = 1000089000;
constexpr XrStructureType XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR = 1000025000;
constexpr XrStructureType XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR = 1000025002;
constexpr XrStructureType XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR = 1000090000;
constexpr XrStructureType XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR = 1000090001;
constexpr XrStructureType XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR = 1000090003;
constexpr XrFormFactor XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1;
constexpr XrReferenceSpaceType XR_REFERENCE_SPACE_TYPE_LOCAL = 2;
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

struct XrLoaderInitInfoAndroidKHR {
    XrStructureType type;
    void const* next;
    void* applicationVM;
    void* applicationContext;
};

struct XrGraphicsBindingVulkanKHR {
    XrStructureType type;
    void const* next;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t queueFamilyIndex;
    uint32_t queueIndex;
};

struct XrGraphicsRequirementsVulkanKHR {
    XrStructureType type;
    void* next;
    XrVersion minApiVersionSupported;
    XrVersion maxApiVersionSupported;
};

struct XrVulkanGraphicsDeviceGetInfoKHR {
    XrStructureType type;
    void const* next;
    XrSystemId systemId;
    VkInstance vulkanInstance;
};

using XrVulkanInstanceCreateFlagsKHR = XrFlags64;
using XrVulkanDeviceCreateFlagsKHR = XrFlags64;

struct XrVulkanInstanceCreateInfoKHR {
    XrStructureType type;
    void const* next;
    XrSystemId systemId;
    XrVulkanInstanceCreateFlagsKHR createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    VkInstanceCreateInfo const* vulkanCreateInfo;
    VkAllocationCallbacks const* vulkanAllocator;
};

struct XrVulkanDeviceCreateInfoKHR {
    XrStructureType type;
    void const* next;
    XrSystemId systemId;
    XrVulkanDeviceCreateFlagsKHR createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    VkPhysicalDevice vulkanPhysicalDevice;
    VkDeviceCreateInfo const* vulkanCreateInfo;
    VkAllocationCallbacks const* vulkanAllocator;
};

using PFN_xrVoidFunction = void (*)();
using PFN_xrGetInstanceProcAddr = XrResult (*)(XrInstance instance, char const* name, PFN_xrVoidFunction* function);
using PFN_xrInitializeLoaderKHR = XrResult (*)(XrLoaderInitInfoAndroidKHR const* loader_init_info);
using PFN_xrCreateInstance = XrResult (*)(XrInstanceCreateInfo const* create_info, XrInstance* instance);
using PFN_xrDestroyInstance = XrResult (*)(XrInstance instance);
using PFN_xrGetSystem = XrResult (*)(XrInstance instance, XrSystemGetInfo const* get_info, XrSystemId* system_id);
using PFN_xrGetVulkanGraphicsRequirements2KHR =
    XrResult (*)(XrInstance instance, XrSystemId system_id, XrGraphicsRequirementsVulkanKHR* graphics_requirements);
using PFN_xrGetVulkanGraphicsDevice2KHR =
    XrResult (*)(XrInstance instance,
                 XrVulkanGraphicsDeviceGetInfoKHR const* get_info,
                 VkPhysicalDevice* vk_physical_device);
using PFN_xrCreateVulkanInstanceKHR =
    XrResult (*)(XrInstance instance,
                 XrVulkanInstanceCreateInfoKHR const* create_info,
                 VkInstance* vulkan_instance,
                 VkResult* vulkan_result);
using PFN_xrCreateVulkanDeviceKHR =
    XrResult (*)(XrInstance instance,
                 XrVulkanDeviceCreateInfoKHR const* create_info,
                 VkDevice* vulkan_device,
                 VkResult* vulkan_result);
using PFN_xrCreateSession = XrResult (*)(XrInstance instance, XrSessionCreateInfo const* create_info, XrSession* session);
using PFN_xrDestroySession = XrResult (*)(XrSession session);
using PFN_xrCreateReferenceSpace = XrResult (*)(XrSession session, XrReferenceSpaceCreateInfo const* create_info, XrSpace* space);
using PFN_xrDestroySpace = XrResult (*)(XrSpace space);

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

uint32_t FindGraphicsQueueFamily(VkPhysicalDevice physical_device)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    if (queue_family_count == 0) {
        return ~0u;
    }

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return i;
        }
    }
    return ~0u;
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

bool OpenXRRuntime::ProbeOpenXRLoader(vkfw::VkContext& ctx)
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

    auto xr_create_session = LoadOpenXRCommand<PFN_xrCreateSession>(get_proc_addr, instance, "xrCreateSession");
    auto xr_destroy_session = LoadOpenXRCommand<PFN_xrDestroySession>(get_proc_addr, instance, "xrDestroySession");
    auto xr_get_vulkan_graphics_requirements =
        LoadOpenXRCommand<PFN_xrGetVulkanGraphicsRequirements2KHR>(
            get_proc_addr,
            instance,
            "xrGetVulkanGraphicsRequirements2KHR");
    auto xr_get_vulkan_graphics_device =
        LoadOpenXRCommand<PFN_xrGetVulkanGraphicsDevice2KHR>(
            get_proc_addr,
            instance,
            "xrGetVulkanGraphicsDevice2KHR");
    auto xr_create_vulkan_instance =
        LoadOpenXRCommand<PFN_xrCreateVulkanInstanceKHR>(
            get_proc_addr,
            instance,
            "xrCreateVulkanInstanceKHR");
    auto xr_create_vulkan_device =
        LoadOpenXRCommand<PFN_xrCreateVulkanDeviceKHR>(
            get_proc_addr,
            instance,
            "xrCreateVulkanDeviceKHR");
    auto xr_create_reference_space =
        LoadOpenXRCommand<PFN_xrCreateReferenceSpace>(get_proc_addr, instance, "xrCreateReferenceSpace");
    auto xr_destroy_space = LoadOpenXRCommand<PFN_xrDestroySpace>(get_proc_addr, instance, "xrDestroySpace");
    if (xr_create_session == nullptr || xr_destroy_session == nullptr ||
        xr_get_vulkan_graphics_requirements == nullptr ||
        xr_get_vulkan_graphics_device == nullptr ||
        xr_create_vulkan_instance == nullptr ||
        xr_create_vulkan_device == nullptr ||
        xr_create_reference_space == nullptr || xr_destroy_space == nullptr) {
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    XrGraphicsRequirementsVulkanKHR graphics_requirements{};
    graphics_requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    graphics_requirements.next = nullptr;
    result = xr_get_vulkan_graphics_requirements(instance, system_id, &graphics_requirements);
    if (result != XR_SUCCESS) {
        LOGW("OpenXR xrGetVulkanGraphicsRequirements2KHR failed: result=%d", result);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR Vulkan graphics requirements: min_api=0x%llx max_api=0x%llx",
         static_cast<unsigned long long>(graphics_requirements.minApiVersionSupported),
         static_cast<unsigned long long>(graphics_requirements.maxApiVersionSupported));

    VkApplicationInfo vk_app_info{};
    vk_app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    vk_app_info.pApplicationName = "AveTriangleGameXR";
    vk_app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    vk_app_info.pEngineName = "AveEngine";
    vk_app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    vk_app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo vk_instance_create_info{};
    vk_instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vk_instance_create_info.pApplicationInfo = &vk_app_info;

    XrVulkanInstanceCreateInfoKHR xr_vk_instance_create_info{};
    xr_vk_instance_create_info.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
    xr_vk_instance_create_info.next = nullptr;
    xr_vk_instance_create_info.systemId = system_id;
    xr_vk_instance_create_info.createFlags = 0;
    xr_vk_instance_create_info.pfnGetInstanceProcAddr = ::vkGetInstanceProcAddr;
    xr_vk_instance_create_info.vulkanCreateInfo = &vk_instance_create_info;
    xr_vk_instance_create_info.vulkanAllocator = nullptr;

    VkInstance xr_vk_instance = VK_NULL_HANDLE;
    VkResult vk_result = VK_SUCCESS;
    result = xr_create_vulkan_instance(instance,
                                       &xr_vk_instance_create_info,
                                       &xr_vk_instance,
                                       &vk_result);
    if (result != XR_SUCCESS || vk_result != VK_SUCCESS || xr_vk_instance == VK_NULL_HANDLE) {
        LOGW("OpenXR xrCreateVulkanInstanceKHR failed: result=%d vk_result=%d instance=%p",
             result,
             vk_result,
             static_cast<void*>(xr_vk_instance));
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR xrCreateVulkanInstanceKHR success: instance=%p", static_cast<void*>(xr_vk_instance));

    XrVulkanGraphicsDeviceGetInfoKHR graphics_device_info{};
    graphics_device_info.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
    graphics_device_info.next = nullptr;
    graphics_device_info.systemId = system_id;
    graphics_device_info.vulkanInstance = xr_vk_instance;

    VkPhysicalDevice xr_physical_device = VK_NULL_HANDLE;
    result = xr_get_vulkan_graphics_device(instance, &graphics_device_info, &xr_physical_device);
    if (result != XR_SUCCESS || xr_physical_device == VK_NULL_HANDLE) {
        LOGW("OpenXR xrGetVulkanGraphicsDevice2KHR failed: result=%d physical_device=%p",
             result,
             static_cast<void*>(xr_physical_device));
        vkDestroyInstance(xr_vk_instance, nullptr);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }

    uint32_t const xr_queue_family_index = FindGraphicsQueueFamily(xr_physical_device);
    if (xr_queue_family_index == ~0u) {
        LOGW("OpenXR Vulkan graphics queue family not found");
        vkDestroyInstance(xr_vk_instance, nullptr);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR Vulkan graphics device verified: physical_device=%p queue_family=%u",
         static_cast<void*>(xr_physical_device),
         xr_queue_family_index);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = xr_queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    XrVulkanDeviceCreateInfoKHR xr_vk_device_create_info{};
    xr_vk_device_create_info.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    xr_vk_device_create_info.next = nullptr;
    xr_vk_device_create_info.systemId = system_id;
    xr_vk_device_create_info.createFlags = 0;
    xr_vk_device_create_info.pfnGetInstanceProcAddr = ::vkGetInstanceProcAddr;
    xr_vk_device_create_info.vulkanPhysicalDevice = xr_physical_device;
    xr_vk_device_create_info.vulkanCreateInfo = &device_create_info;
    xr_vk_device_create_info.vulkanAllocator = nullptr;

    VkDevice xr_vk_device = VK_NULL_HANDLE;
    vk_result = VK_SUCCESS;
    result = xr_create_vulkan_device(instance,
                                     &xr_vk_device_create_info,
                                     &xr_vk_device,
                                     &vk_result);
    if (result != XR_SUCCESS || vk_result != VK_SUCCESS || xr_vk_device == VK_NULL_HANDLE) {
        LOGW("OpenXR xrCreateVulkanDeviceKHR failed: result=%d vk_result=%d device=%p",
             result,
             vk_result,
             static_cast<void*>(xr_vk_device));
        vkDestroyInstance(xr_vk_instance, nullptr);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR xrCreateVulkanDeviceKHR success: device=%p", static_cast<void*>(xr_vk_device));

    XrGraphicsBindingVulkanKHR graphics_binding{};
    graphics_binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    graphics_binding.next = nullptr;
    graphics_binding.instance = xr_vk_instance;
    graphics_binding.physicalDevice = xr_physical_device;
    graphics_binding.device = xr_vk_device;
    graphics_binding.queueFamilyIndex = xr_queue_family_index;
    graphics_binding.queueIndex = 0;

    XrSessionCreateInfo session_info{};
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.next = &graphics_binding;
    session_info.createFlags = 0;
    session_info.systemId = system_id;

    XrSession session = nullptr;
    result = xr_create_session(instance, &session_info, &session);
    if (result != XR_SUCCESS || session == nullptr) {
        LOGW("OpenXR xrCreateSession failed: result=%d", result);
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
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
        xr_destroy_session(session);
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        if (xr_destroy_instance != nullptr) {
            xr_destroy_instance(instance);
        }
        dlclose(loader_handle_);
        loader_handle_ = nullptr;
        return false;
    }
    LOGI("OpenXR xrCreateReferenceSpace(LOCAL) success: space=%p", static_cast<void*>(local_space));

    instance_handle_ = instance;
    session_handle_ = session;
    local_space_handle_ = local_space;
    xr_vulkan_instance_ = xr_vk_instance;
    xr_vulkan_device_ = xr_vk_device;
    system_id_ = system_id;
    LOGI("OpenXR probe success: instance=%p system_id=%llu session=%p local_space=%p",
         instance_handle_,
         static_cast<unsigned long long>(system_id_),
         session_handle_,
         local_space_handle_);
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

    if (!ProbeOpenXRLoader(ctx)) {
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
            if (xr_vulkan_device_ != nullptr) {
                vkDestroyDevice(static_cast<VkDevice>(xr_vulkan_device_), nullptr);
            }
            if (xr_vulkan_instance_ != nullptr) {
                vkDestroyInstance(static_cast<VkInstance>(xr_vulkan_instance_), nullptr);
            }
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
    session_handle_ = nullptr;
    local_space_handle_ = nullptr;
    xr_vulkan_instance_ = nullptr;
    xr_vulkan_device_ = nullptr;
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
