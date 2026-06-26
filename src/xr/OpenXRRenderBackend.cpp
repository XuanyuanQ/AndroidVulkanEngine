#include "ave/xr/OpenXRRenderBackend.h"

#include "ave/xr/OpenXRRuntime.h"
#include "ave/xr/OpenXRActionSystem.h"
#include "LogUtil.h"
#include "VkContext.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ave::xr {

namespace {

#if defined(__ANDROID__)
using XrFlags64 = uint64_t;
using XrSystemId = uint64_t;
using XrVersion = uint64_t;
using XrTime = int64_t;
using XrDuration = int64_t;
using XrResult = int32_t;
using XrStructureType = int32_t;
using XrBool32 = uint32_t;
using XrSession = struct XrSession_T*;
using XrSpace = struct XrSpace_T*;
using XrSwapchain = struct XrSwapchain_T*;
using XrInstance = struct XrInstance_T*;
using XrViewConfigurationType = int32_t;
using XrEnvironmentBlendMode = int32_t;
using XrSwapchainUsageFlags = XrFlags64;
using XrCompositionLayerFlags = XrFlags64;

constexpr XrResult XR_SUCCESS = 0;
constexpr XrStructureType XR_TYPE_VIEW_LOCATE_INFO = 6;
constexpr XrStructureType XR_TYPE_VIEW = 7;
constexpr XrStructureType XR_TYPE_SWAPCHAIN_CREATE_INFO = 9;
constexpr XrStructureType XR_TYPE_SESSION_BEGIN_INFO = 10;
constexpr XrStructureType XR_TYPE_VIEW_STATE = 11;
constexpr XrStructureType XR_TYPE_FRAME_END_INFO = 12;
constexpr XrStructureType XR_TYPE_FRAME_WAIT_INFO = 33;
constexpr XrStructureType XR_TYPE_COMPOSITION_LAYER_PROJECTION = 35;
constexpr XrStructureType XR_TYPE_VIEW_CONFIGURATION_VIEW = 41;
constexpr XrStructureType XR_TYPE_FRAME_STATE = 44;
constexpr XrStructureType XR_TYPE_FRAME_BEGIN_INFO = 46;
constexpr XrStructureType XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW = 48;
constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO = 55;
constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO = 56;
constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO = 57;
constexpr XrStructureType XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR = 1000025000;
constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR = 1000025001;
constexpr XrStructureType XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR = 1000025002;
constexpr XrStructureType XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR = 1000090000;
constexpr XrStructureType XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR = 1000090001;
constexpr XrStructureType XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR = 1000090003;
constexpr XrViewConfigurationType XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO = 2;
constexpr XrEnvironmentBlendMode XR_ENVIRONMENT_BLEND_MODE_OPAQUE = 1;
constexpr XrDuration XR_INFINITE_DURATION = 0x7fffffffffffffffLL;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT = 0x00000001;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT = 0x00000010;

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

struct XrSwapchainImageVulkanKHR {
    XrStructureType type;
    void* next;
    VkImage image;
};

struct XrViewConfigurationView {
    XrStructureType type;
    void* next;
    uint32_t recommendedImageRectWidth;
    uint32_t maxImageRectWidth;
    uint32_t recommendedImageRectHeight;
    uint32_t maxImageRectHeight;
    uint32_t recommendedSwapchainSampleCount;
    uint32_t maxSwapchainSampleCount;
};

struct XrSwapchainCreateInfo {
    XrStructureType type;
    void const* next;
    XrFlags64 createFlags;
    XrSwapchainUsageFlags usageFlags;
    int64_t format;
    uint32_t sampleCount;
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;
    uint32_t arraySize;
    uint32_t mipCount;
};

struct XrSessionBeginInfo {
    XrStructureType type;
    void const* next;
    XrViewConfigurationType primaryViewConfigurationType;
};

struct XrFrameWaitInfo {
    XrStructureType type;
    void const* next;
};

struct XrFrameState {
    XrStructureType type;
    void* next;
    XrTime predictedDisplayTime;
    XrDuration predictedDisplayPeriod;
    XrBool32 shouldRender;
};

struct XrFrameBeginInfo {
    XrStructureType type;
    void const* next;
};

struct XrFrameEndInfo {
    XrStructureType type;
    void const* next;
    XrTime displayTime;
    XrEnvironmentBlendMode environmentBlendMode;
    uint32_t layerCount;
    void const* const* layers;
};

struct XrSwapchainImageAcquireInfo {
    XrStructureType type;
    void const* next;
};

struct XrSwapchainImageWaitInfo {
    XrStructureType type;
    void const* next;
    XrDuration timeout;
};

struct XrSwapchainImageReleaseInfo {
    XrStructureType type;
    void const* next;
};

struct XrOffset2Di {
    int32_t x;
    int32_t y;
};

struct XrExtent2Di {
    int32_t width;
    int32_t height;
};

struct XrRect2Di {
    XrOffset2Di offset;
    XrExtent2Di extent;
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

struct XrFovf {
    float angleLeft;
    float angleRight;
    float angleUp;
    float angleDown;
};

struct XrView {
    XrStructureType type;
    void* next;
    XrPosef pose;
    XrFovf fov;
};

struct XrViewLocateInfo {
    XrStructureType type;
    void const* next;
    XrViewConfigurationType viewConfigurationType;
    XrTime displayTime;
    XrSpace space;
};

struct XrViewState {
    XrStructureType type;
    void* next;
    XrFlags64 viewStateFlags;
};

struct XrSwapchainSubImage {
    XrSwapchain swapchain;
    XrRect2Di imageRect;
    uint32_t imageArrayIndex;
};

struct XrCompositionLayerProjectionView {
    XrStructureType type;
    void const* next;
    XrPosef pose;
    XrFovf fov;
    XrSwapchainSubImage subImage;
};

struct XrCompositionLayerProjection {
    XrStructureType type;
    void const* next;
    XrCompositionLayerFlags layerFlags;
    XrSpace space;
    uint32_t viewCount;
    XrCompositionLayerProjectionView const* views;
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
using PFN_xrBeginSession = XrResult (*)(XrSession session, XrSessionBeginInfo const* begin_info);
using PFN_xrWaitFrame = XrResult (*)(XrSession session, XrFrameWaitInfo const* frame_wait_info, XrFrameState* frame_state);
using PFN_xrBeginFrame = XrResult (*)(XrSession session, XrFrameBeginInfo const* frame_begin_info);
using PFN_xrEndFrame = XrResult (*)(XrSession session, XrFrameEndInfo const* frame_end_info);
using PFN_xrEnumerateViewConfigurationViews =
    XrResult (*)(XrInstance instance,
                 XrSystemId system_id,
                 XrViewConfigurationType view_configuration_type,
                 uint32_t view_capacity_input,
                 uint32_t* view_count_output,
                 XrViewConfigurationView* views);
using PFN_xrEnumerateSwapchainFormats =
    XrResult (*)(XrSession session, uint32_t format_capacity_input, uint32_t* format_count_output, int64_t* formats);
using PFN_xrCreateSwapchain =
    XrResult (*)(XrSession session, XrSwapchainCreateInfo const* create_info, XrSwapchain* swapchain);
using PFN_xrDestroySwapchain = XrResult (*)(XrSwapchain swapchain);
using PFN_xrEnumerateSwapchainImages =
    XrResult (*)(XrSwapchain swapchain, uint32_t image_capacity_input, uint32_t* image_count_output, XrSwapchainImageVulkanKHR* images);
using PFN_xrAcquireSwapchainImage =
    XrResult (*)(XrSwapchain swapchain, XrSwapchainImageAcquireInfo const* acquire_info, uint32_t* index);
using PFN_xrWaitSwapchainImage =
    XrResult (*)(XrSwapchain swapchain, XrSwapchainImageWaitInfo const* wait_info);
using PFN_xrReleaseSwapchainImage =
    XrResult (*)(XrSwapchain swapchain, XrSwapchainImageReleaseInfo const* release_info);
using PFN_xrLocateViews =
    XrResult (*)(XrSession session,
                 XrViewLocateInfo const* view_locate_info,
                 XrViewState* view_state,
                 uint32_t view_capacity_input,
                 uint32_t* view_count_output,
                 XrView* views);

template <typename Fn>
Fn LoadOpenXRCommand(PFN_xrGetInstanceProcAddr get_proc_addr, XrInstance instance, char const* name)
{
    PFN_xrVoidFunction raw = nullptr;
    XrResult const result = get_proc_addr(instance, name, &raw);
    if (result != XR_SUCCESS || raw == nullptr) {
        LOGW("OpenXR render backend command unavailable: %s result=%d", name, result);
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

bool HasDeviceExtension(VkPhysicalDevice physical_device, char const* extension_name)
{
    uint32_t extension_count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extension_count);
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [&](VkExtensionProperties const& extension) {
        return std::strcmp(extension.extensionName, extension_name) == 0;
    });
}

int64_t ChooseSwapchainFormat(std::vector<int64_t> const& formats)
{
    int64_t const preferred[] = {
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (int64_t candidate : preferred) {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
            return candidate;
        }
    }
    return formats.empty() ? 0 : formats.front();
}

float ApplyDeadzone(float value, float deadzone)
{
    return std::abs(value) < deadzone ? 0.0f : value;
}

glm::quat YawRotation(float yaw_radians)
{
    return glm::angleAxis(yaw_radians, glm::vec3{0.0f, 1.0f, 0.0f});
}

float ExtractYawRadians(glm::quat const& orientation)
{
    glm::vec3 const forward = orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    return std::atan2(-forward.x, -forward.z);
}

glm::mat4 BuildViewMatrix(glm::vec3 const& world_position, glm::quat const& world_orientation)
{
    glm::mat4 const world =
        glm::translate(glm::mat4{1.0f}, world_position) * glm::mat4_cast(world_orientation);
    return glm::inverse(world);
}

glm::vec3 PosePosition(XrPosef const& pose)
{
    return {pose.position.x, pose.position.y, pose.position.z};
}

glm::quat PoseOrientation(XrPosef const& pose)
{
    return {pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z};
}

glm::vec3 ExtractCameraRight(glm::mat4 const& camera_world)
{
    return glm::normalize(glm::vec3{camera_world[0]});
}

glm::vec3 ExtractCameraUp(glm::mat4 const& camera_world)
{
    return glm::normalize(glm::vec3{camera_world[1]});
}

glm::vec3 ExtractCameraForward(glm::mat4 const& camera_world)
{
    return glm::normalize(-glm::vec3{camera_world[2]});
}

glm::mat4 BuildProjectionMatrix(XrFovf const& fov, float near_plane, float far_plane)
{
    float const tan_left = std::tan(fov.angleLeft);
    float const tan_right = std::tan(fov.angleRight);
    float const tan_down = std::tan(fov.angleDown);
    float const tan_up = std::tan(fov.angleUp);
    float const tan_width = tan_right - tan_left;
    float const tan_height = tan_up - tan_down;

    glm::mat4 projection{0.0f};
    projection[0][0] = 2.0f / tan_width;
    projection[1][1] = 2.0f / tan_height;
    projection[2][0] = (tan_right + tan_left) / tan_width;
    projection[2][1] = (tan_up + tan_down) / tan_height;
    projection[2][2] = far_plane / (near_plane - far_plane);
    projection[2][3] = -1.0f;
    projection[3][2] = (far_plane * near_plane) / (near_plane - far_plane);
    projection[1][1] *= -1.0f;
    return projection;
}

bool RayPlaneHit(glm::vec3 const& ray_origin,
                 glm::vec3 const& ray_direction,
                 glm::vec3 const& plane_point,
                 glm::vec3 const& plane_normal,
                 float& out_t)
{
    float const denom = glm::dot(ray_direction, plane_normal);
    if (std::abs(denom) < 0.0001f) {
        return false;
    }
    out_t = glm::dot(plane_point - ray_origin, plane_normal) / denom;
    return out_t > 0.0f;
}
#endif

} // namespace

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

bool OpenXRRenderBackend::InitializeGraphics(OpenXRRuntime& runtime)
{
#if defined(__ANDROID__)
    runtime_ = &runtime;
    if (HasGraphics()) {
        return true;
    }
    if (!runtime.IsInitialized() || runtime.GetInstanceHandle() == nullptr ||
        runtime.GetInstanceProcAddr() == nullptr || runtime.SystemId() == 0) {
        return false;
    }

    auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(runtime.GetInstanceProcAddr());
    auto instance = static_cast<XrInstance>(runtime.GetInstanceHandle());
    auto system_id = static_cast<XrSystemId>(runtime.SystemId());

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
    auto xr_enumerate_view_configuration_views =
        LoadOpenXRCommand<PFN_xrEnumerateViewConfigurationViews>(
            get_proc_addr,
            instance,
            "xrEnumerateViewConfigurationViews");
    auto xr_enumerate_swapchain_formats =
        LoadOpenXRCommand<PFN_xrEnumerateSwapchainFormats>(
            get_proc_addr,
            instance,
            "xrEnumerateSwapchainFormats");
    auto xr_create_swapchain =
        LoadOpenXRCommand<PFN_xrCreateSwapchain>(
            get_proc_addr,
            instance,
            "xrCreateSwapchain");
    auto xr_enumerate_swapchain_images =
        LoadOpenXRCommand<PFN_xrEnumerateSwapchainImages>(
            get_proc_addr,
            instance,
            "xrEnumerateSwapchainImages");
    if (xr_get_vulkan_graphics_requirements == nullptr ||
        xr_get_vulkan_graphics_device == nullptr ||
        xr_create_vulkan_instance == nullptr ||
        xr_create_vulkan_device == nullptr ||
        xr_enumerate_view_configuration_views == nullptr ||
        xr_enumerate_swapchain_formats == nullptr ||
        xr_create_swapchain == nullptr ||
        xr_enumerate_swapchain_images == nullptr) {
        return false;
    }

    XrGraphicsRequirementsVulkanKHR graphics_requirements{};
    graphics_requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    XrResult result = xr_get_vulkan_graphics_requirements(instance, system_id, &graphics_requirements);
    if (result != XR_SUCCESS) {
        LOGW("OpenXRRenderBackend xrGetVulkanGraphicsRequirements2KHR failed: result=%d", result);
        return false;
    }
    LOGI("OpenXRRenderBackend Vulkan requirements: min_api=0x%llx max_api=0x%llx",
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
    xr_vk_instance_create_info.systemId = system_id;
    xr_vk_instance_create_info.pfnGetInstanceProcAddr = ::vkGetInstanceProcAddr;
    xr_vk_instance_create_info.vulkanCreateInfo = &vk_instance_create_info;

    VkInstance xr_vk_instance = VK_NULL_HANDLE;
    VkResult vk_result = VK_SUCCESS;
    result = xr_create_vulkan_instance(instance, &xr_vk_instance_create_info, &xr_vk_instance, &vk_result);
    if (result != XR_SUCCESS || vk_result != VK_SUCCESS || xr_vk_instance == VK_NULL_HANDLE) {
        LOGW("OpenXRRenderBackend xrCreateVulkanInstanceKHR failed: result=%d vk_result=%d instance=%p",
             result,
             vk_result,
             static_cast<void*>(xr_vk_instance));
        return false;
    }

    XrVulkanGraphicsDeviceGetInfoKHR graphics_device_info{};
    graphics_device_info.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
    graphics_device_info.systemId = system_id;
    graphics_device_info.vulkanInstance = xr_vk_instance;

    VkPhysicalDevice xr_physical_device = VK_NULL_HANDLE;
    result = xr_get_vulkan_graphics_device(instance, &graphics_device_info, &xr_physical_device);
    if (result != XR_SUCCESS || xr_physical_device == VK_NULL_HANDLE) {
        LOGW("OpenXRRenderBackend xrGetVulkanGraphicsDevice2KHR failed: result=%d physical_device=%p",
             result,
             static_cast<void*>(xr_physical_device));
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    uint32_t const queue_family_index = FindGraphicsQueueFamily(xr_physical_device);
    if (queue_family_index == ~0u) {
        LOGW("OpenXRRenderBackend graphics queue family not found");
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    std::vector<char const*> device_extensions{};
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
    bool const supports_dynamic_rendering =
        HasDeviceExtension(xr_physical_device, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (!supports_dynamic_rendering) {
        LOGW("OpenXRRenderBackend XR Vulkan device does not support VK_KHR_dynamic_rendering yet; PBR XR path requires it");
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }
    device_extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;
    device_create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    device_create_info.ppEnabledExtensionNames = device_extensions.data();
    device_create_info.pNext = &dynamic_rendering_features;

    XrVulkanDeviceCreateInfoKHR xr_vk_device_create_info{};
    xr_vk_device_create_info.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    xr_vk_device_create_info.systemId = system_id;
    xr_vk_device_create_info.pfnGetInstanceProcAddr = ::vkGetInstanceProcAddr;
    xr_vk_device_create_info.vulkanPhysicalDevice = xr_physical_device;
    xr_vk_device_create_info.vulkanCreateInfo = &device_create_info;

    VkDevice xr_vk_device = VK_NULL_HANDLE;
    vk_result = VK_SUCCESS;
    result = xr_create_vulkan_device(instance, &xr_vk_device_create_info, &xr_vk_device, &vk_result);
    if (result != XR_SUCCESS || vk_result != VK_SUCCESS || xr_vk_device == VK_NULL_HANDLE) {
        LOGW("OpenXRRenderBackend xrCreateVulkanDeviceKHR failed: result=%d vk_result=%d device=%p",
             result,
             vk_result,
             static_cast<void*>(xr_vk_device));
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    XrGraphicsBindingVulkanKHR graphics_binding{};
    graphics_binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    graphics_binding.instance = xr_vk_instance;
    graphics_binding.physicalDevice = xr_physical_device;
    graphics_binding.device = xr_vk_device;
    graphics_binding.queueFamilyIndex = queue_family_index;
    graphics_binding.queueIndex = 0;
    if (!runtime.CreateSession(&graphics_binding)) {
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    auto session = static_cast<XrSession>(runtime.GetSessionHandle());
    if (session == nullptr) {
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    uint32_t view_count = 0;
    result = xr_enumerate_view_configuration_views(instance,
                                                  system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                  0,
                                                  &view_count,
                                                  nullptr);
    if (result != XR_SUCCESS || view_count == 0) {
        LOGW("OpenXRRenderBackend failed to query view configuration count: result=%d count=%u", result, view_count);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    std::vector<XrViewConfigurationView> view_configs(view_count);
    for (auto& view_config : view_configs) {
        view_config.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    result = xr_enumerate_view_configuration_views(instance,
                                                  system_id,
                                                  XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                  view_count,
                                                  &view_count,
                                                  view_configs.data());
    if (result != XR_SUCCESS || view_count < 2) {
        LOGW("OpenXRRenderBackend failed to query stereo view configuration: result=%d count=%u", result, view_count);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    uint32_t format_count = 0;
    result = xr_enumerate_swapchain_formats(session, 0, &format_count, nullptr);
    if (result != XR_SUCCESS || format_count == 0) {
        LOGW("OpenXRRenderBackend failed to query swapchain formats: result=%d count=%u", result, format_count);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }
    std::vector<int64_t> formats(format_count);
    result = xr_enumerate_swapchain_formats(session, format_count, &format_count, formats.data());
    if (result != XR_SUCCESS) {
        LOGW("OpenXRRenderBackend failed to enumerate swapchain formats: result=%d", result);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    int64_t const swapchain_format = ChooseSwapchainFormat(formats);
    uint32_t const swapchain_width = view_configs[0].recommendedImageRectWidth;
    uint32_t const swapchain_height = view_configs[0].recommendedImageRectHeight;

    XrSwapchainCreateInfo swapchain_info{};
    swapchain_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    swapchain_info.format = swapchain_format;
    swapchain_info.sampleCount = std::max(1u, view_configs[0].recommendedSwapchainSampleCount);
    swapchain_info.width = swapchain_width;
    swapchain_info.height = swapchain_height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = 2;
    swapchain_info.mipCount = 1;

    XrSwapchain swapchain = nullptr;
    result = xr_create_swapchain(session, &swapchain_info, &swapchain);
    if (result != XR_SUCCESS || swapchain == nullptr) {
        LOGW("OpenXRRenderBackend xrCreateSwapchain failed: result=%d format=%lld size=%ux%u",
             result,
             static_cast<long long>(swapchain_format),
             swapchain_width,
             swapchain_height);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    uint32_t swapchain_image_count = 0;
    result = xr_enumerate_swapchain_images(swapchain, 0, &swapchain_image_count, nullptr);
    if (result != XR_SUCCESS || swapchain_image_count == 0) {
        LOGW("OpenXRRenderBackend failed to query swapchain images: result=%d count=%u", result, swapchain_image_count);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }
    std::vector<XrSwapchainImageVulkanKHR> swapchain_images(swapchain_image_count);
    for (auto& image : swapchain_images) {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
    }
    result = xr_enumerate_swapchain_images(swapchain,
                                           swapchain_image_count,
                                           &swapchain_image_count,
                                           swapchain_images.data());
    if (result != XR_SUCCESS) {
        LOGW("OpenXRRenderBackend failed to enumerate swapchain images: result=%d", result);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    VkCommandPoolCreateInfo command_pool_info{};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = queue_family_index;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkResult vk_result2 = vkCreateCommandPool(xr_vk_device, &command_pool_info, nullptr, &command_pool);
    if (vk_result2 != VK_SUCCESS) {
        LOGW("OpenXRRenderBackend vkCreateCommandPool failed: result=%d", vk_result2);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vk_result2 = vkAllocateCommandBuffers(xr_vk_device, &command_buffer_info, &command_buffer);
    if (vk_result2 != VK_SUCCESS || command_buffer == VK_NULL_HANDLE) {
        LOGW("OpenXRRenderBackend vkAllocateCommandBuffers failed: result=%d", vk_result2);
        vkDestroyCommandPool(xr_vk_device, command_pool, nullptr);
        runtime.DestroySession();
        vkDestroyDevice(xr_vk_device, nullptr);
        vkDestroyInstance(xr_vk_instance, nullptr);
        return false;
    }

    xr_swapchain_image_views_.clear();
    xr_swapchain_eye_image_views_.clear();
    xr_swapchain_images_.clear();
    xr_swapchain_image_views_.reserve(swapchain_images.size());
    xr_swapchain_eye_image_views_.reserve(swapchain_images.size() * 2u);
    xr_swapchain_images_.reserve(swapchain_images.size());
    for (auto const& image : swapchain_images) {
        xr_swapchain_images_.push_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(image.image)));
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view_info.format = static_cast<VkFormat>(swapchain_format);
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 2;
        VkImageView image_view = VK_NULL_HANDLE;
        vk_result2 = vkCreateImageView(xr_vk_device, &view_info, nullptr, &image_view);
        if (vk_result2 != VK_SUCCESS) {
            LOGW("OpenXRRenderBackend vkCreateImageView failed: result=%d", vk_result2);
            for (void* existing_view : xr_swapchain_image_views_) {
                vkDestroyImageView(xr_vk_device, static_cast<VkImageView>(existing_view), nullptr);
            }
            xr_swapchain_image_views_.clear();
            vkDestroyCommandPool(xr_vk_device, command_pool, nullptr);
            runtime.DestroySession();
            vkDestroyDevice(xr_vk_device, nullptr);
            vkDestroyInstance(xr_vk_instance, nullptr);
            return false;
        }
        xr_swapchain_image_views_.push_back(image_view);

        for (uint32_t eye = 0; eye < 2; ++eye) {
            VkImageViewCreateInfo eye_view_info{};
            eye_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            eye_view_info.image = image.image;
            eye_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            eye_view_info.format = static_cast<VkFormat>(swapchain_format);
            eye_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            eye_view_info.subresourceRange.baseMipLevel = 0;
            eye_view_info.subresourceRange.levelCount = 1;
            eye_view_info.subresourceRange.baseArrayLayer = eye;
            eye_view_info.subresourceRange.layerCount = 1;
            VkImageView eye_image_view = VK_NULL_HANDLE;
            vk_result2 = vkCreateImageView(xr_vk_device, &eye_view_info, nullptr, &eye_image_view);
            if (vk_result2 != VK_SUCCESS) {
                LOGW("OpenXRRenderBackend vkCreateImageView eye failed: result=%d eye=%u", vk_result2, eye);
                for (void* existing_view : xr_swapchain_eye_image_views_) {
                    vkDestroyImageView(xr_vk_device, static_cast<VkImageView>(existing_view), nullptr);
                }
                for (void* existing_view : xr_swapchain_image_views_) {
                    vkDestroyImageView(xr_vk_device, static_cast<VkImageView>(existing_view), nullptr);
                }
                xr_swapchain_eye_image_views_.clear();
                xr_swapchain_image_views_.clear();
                vkDestroyCommandPool(xr_vk_device, command_pool, nullptr);
                runtime.DestroySession();
                vkDestroyDevice(xr_vk_device, nullptr);
                vkDestroyInstance(xr_vk_instance, nullptr);
                return false;
            }
            xr_swapchain_eye_image_views_.push_back(eye_image_view);
        }
    }

    vkGetDeviceQueue(xr_vk_device, queue_family_index, 0, reinterpret_cast<VkQueue*>(&xr_queue_));

    xr_vulkan_instance_ = xr_vk_instance;
    xr_vulkan_device_ = xr_vk_device;
    xr_physical_device_ = xr_physical_device;
    xr_queue_family_index_ = queue_family_index;
    xr_swapchain_ = swapchain;
    xr_swapchain_width_ = swapchain_width;
    xr_swapchain_height_ = swapchain_height;
    xr_swapchain_format_ = swapchain_format;
    xr_command_pool_ = command_pool;
    xr_command_buffer_ = command_buffer;
    xr_supports_dynamic_rendering_ = supports_dynamic_rendering;
    LOGI("OpenXRRenderBackend graphics initialized: instance=%p device=%p physical_device=%p queue_family=%u swapchain=%p images=%zu size=%ux%u format=%lld",
         xr_vulkan_instance_,
         xr_vulkan_device_,
         xr_physical_device_,
         xr_queue_family_index_,
         xr_swapchain_,
         xr_swapchain_image_views_.size(),
         xr_swapchain_width_,
         xr_swapchain_height_,
         static_cast<long long>(xr_swapchain_format_));
    return true;
#else
    (void)runtime;
    return false;
#endif
}

bool OpenXRRenderBackend::InitializeFrameResources(vkfw::VkContext& ctx)
{
#if defined(__ANDROID__)
    if (!HasSwapchain() || xr_swapchain_width_ == 0 || xr_swapchain_height_ == 0) {
        return false;
    }

    for (auto& depth_texture : depth_textures_) {
        if (depth_texture.IsInitialized()) {
            depth_texture.Shutdown(ctx);
        }
    }
    depth_textures_.clear();
    depth_texture_ready_.clear();
    depth_textures_.resize(xr_swapchain_images_.size() * 2u);
    depth_texture_ready_.assign(xr_swapchain_images_.size() * 2u, 0u);
    for (auto& depth_texture : depth_textures_) {
        if (!depth_texture.Init(ctx, vkfw::TextureInfo{
                                         .width = xr_swapchain_width_,
                                         .height = xr_swapchain_height_,
                                         .mip_levels = 1,
                                         .format = vkfw::TextureFormat::D32_SFLOAT,
                                         .usage = static_cast<vkfw::TextureUsage>(
                                             static_cast<uint32_t>(vkfw::TextureUsage::DepthStencilAttachment) |
                                             static_cast<uint32_t>(vkfw::TextureUsage::Sampled)),
                                         .mipmap = false,
                                     })) {
            for (auto& created_depth : depth_textures_) {
                if (created_depth.IsInitialized()) {
                    created_depth.Shutdown(ctx);
                }
            }
            depth_textures_.clear();
            depth_texture_ready_.clear();
            return false;
        }
    }
    LOGI("OpenXRRenderBackend frame resources initialized: depth_count=%zu size=%ux%u",
         depth_textures_.size(),
         xr_swapchain_width_,
         xr_swapchain_height_);
    return true;
#else
    (void)ctx;
    return false;
#endif
}

void OpenXRRenderBackend::ShutdownFrameResources(vkfw::VkContext& ctx)
{
    for (auto& depth_texture : depth_textures_) {
        if (depth_texture.IsInitialized()) {
            depth_texture.Shutdown(ctx);
        }
    }
    depth_textures_.clear();
    depth_texture_ready_.clear();
}

void OpenXRRenderBackend::ShutdownGraphics(OpenXRRuntime& runtime)
{
#if defined(__ANDROID__)
    if (runtime.GetInstanceHandle() != nullptr && runtime.GetInstanceProcAddr() != nullptr && xr_swapchain_ != nullptr) {
        auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(runtime.GetInstanceProcAddr());
        auto xr_destroy_swapchain =
            LoadOpenXRCommand<PFN_xrDestroySwapchain>(get_proc_addr,
                                                      static_cast<XrInstance>(runtime.GetInstanceHandle()),
                                                      "xrDestroySwapchain");
        if (xr_destroy_swapchain != nullptr) {
            xr_destroy_swapchain(static_cast<XrSwapchain>(xr_swapchain_));
        }
    }
    if (xr_vulkan_device_ != nullptr) {
        auto device = static_cast<VkDevice>(xr_vulkan_device_);
        for (void* image_view : xr_swapchain_eye_image_views_) {
            if (image_view != nullptr) {
                vkDestroyImageView(device, static_cast<VkImageView>(image_view), nullptr);
            }
        }
        for (void* image_view : xr_swapchain_image_views_) {
            if (image_view != nullptr) {
                vkDestroyImageView(device, static_cast<VkImageView>(image_view), nullptr);
            }
        }
        if (xr_command_pool_ != nullptr) {
            vkDestroyCommandPool(device, static_cast<VkCommandPool>(xr_command_pool_), nullptr);
        }
    }
    runtime.DestroySession();
#else
    (void)runtime;
#endif
    xr_vulkan_instance_ = nullptr;
    xr_vulkan_device_ = nullptr;
    xr_physical_device_ = nullptr;
    xr_queue_ = nullptr;
    xr_swapchain_ = nullptr;
    xr_swapchain_width_ = 0;
    xr_swapchain_height_ = 0;
    xr_swapchain_format_ = 0;
    xr_queue_family_index_ = ~0u;
    xr_supports_dynamic_rendering_ = false;
    xr_command_pool_ = nullptr;
    xr_command_buffer_ = nullptr;
    xr_swapchain_images_.clear();
    xr_swapchain_image_views_.clear();
    xr_swapchain_eye_image_views_.clear();
    depth_textures_.clear();
    depth_texture_ready_.clear();
    runtime_ = nullptr;
    runtime_frame_started_ = false;
    xr_session_begun_ = false;
    logged_waiting_for_session_ready_ = false;
    xr_frame_begun_ = false;
    xr_frame_should_render_ = false;
    xr_predicted_display_time_ = 0;
    xr_origin_position_ = glm::vec3{0.0f, 0.0f, 0.0f};
    xr_origin_yaw_radians_ = 0.0f;
    xr_last_input_time_ = 0;
    xr_locomotion_log_counter_ = 0;
    xr_ui_pointer_has_hit_ = false;
    xr_ui_pointer_ray_origin_ = glm::vec3{0.0f, 0.0f, 0.0f};
    xr_ui_pointer_ray_end_ = glm::vec3{0.0f, 0.0f, -1.0f};
    xr_ui_pointer_hit_position_ = glm::vec3{0.0f, 0.0f, -1.0f};
    xr_acquired_image_index_ = 0;
}

bool OpenXRRenderBackend::BeginRuntimeFrame(core::FrameData const& frame)
{
    render::RenderFrameRequest request{};
    request.frame = &frame;
    if (BeginFrame(request) != render::FrameGraphRenderResult::Success) {
        return false;
    }
    EndFrame(render::FrameGraphRenderResult::Success);
    return true;
}

bool OpenXRRenderBackend::TryGetXRUiPointerNdc(float& out_x_ndc, float& out_y_ndc) const
{
#if defined(__ANDROID__)
    const_cast<OpenXRRenderBackend*>(this)->UpdateXRUiPointerRay();
    if (runtime_ == nullptr || xr_frame_data_.views.empty() || !runtime_->InputState().right.aim_pose_active) {
        return false;
    }

    glm::vec3 const ray_origin = xr_ui_pointer_ray_origin_;
    glm::vec3 const ray_direction = glm::normalize(xr_ui_pointer_ray_end_ - xr_ui_pointer_ray_origin_);

    core::FrameViewData const& basis_view = xr_frame_data_.views.front();
    glm::mat4 const camera_world = glm::inverse(basis_view.view);
    glm::vec3 const right = ExtractCameraRight(camera_world);
    glm::vec3 const up = ExtractCameraUp(camera_world);
    glm::vec3 const forward = ExtractCameraForward(camera_world);

    glm::vec3 stereo_center{0.0f};
    for (auto const& view : xr_frame_data_.views) {
        stereo_center += view.world_position;
    }
    stereo_center /= static_cast<float>(xr_frame_data_.views.size());

    float constexpr panel_distance = 2.0f;
    float constexpr panel_half_height = 0.55f;
    float const ui_aspect_ratio =
        xr_swapchain_width_ > 0 ? static_cast<float>(xr_swapchain_height_) / static_cast<float>(xr_swapchain_width_) : 1.0f;
    float const panel_half_width = panel_half_height / std::max(ui_aspect_ratio, 0.01f);
    glm::vec3 const panel_center = stereo_center + forward * panel_distance - up * 0.05f;

    float t = 0.0f;
    if (!RayPlaneHit(ray_origin, ray_direction, panel_center, -forward, t)) {
        return false;
    }
    glm::vec3 const hit = ray_origin + ray_direction * t;
    const_cast<OpenXRRenderBackend*>(this)->xr_ui_pointer_hit_position_ = hit;
    glm::vec3 const local = hit - panel_center;
    float const panel_x = glm::dot(local, right) / panel_half_width;
    float const panel_y = glm::dot(local, up) / panel_half_height;
    if (panel_x < -1.0f || panel_x > 1.0f || panel_y < -1.0f || panel_y > 1.0f) {
        return false;
    }

    // XRWorldUIPass currently maps UI position as panel_position=(x, -y).
    out_x_ndc = panel_x;
    out_y_ndc = -panel_y;
    const_cast<OpenXRRenderBackend*>(this)->xr_ui_pointer_has_hit_ = true;
    const_cast<OpenXRRenderBackend*>(this)->xr_ui_pointer_ray_end_ = hit;
    return true;
#else
    (void)out_x_ndc;
    (void)out_y_ndc;
    return false;
#endif
}

void OpenXRRenderBackend::UpdateXRUiPointerRay()
{
#if defined(__ANDROID__)
    xr_ui_pointer_has_hit_ = false;
    if (runtime_ == nullptr || !runtime_->InputState().right.aim_pose_active) {
        return;
    }

    XRInputState const& input = runtime_->InputState();
    glm::quat const origin_rotation = YawRotation(xr_origin_yaw_radians_);
    glm::vec3 const ray_origin = xr_origin_position_ + origin_rotation * input.right.aim_position;
    glm::quat const ray_orientation = origin_rotation * input.right.aim_orientation;
    glm::vec3 const ray_direction = glm::normalize(ray_orientation * glm::vec3{0.0f, 0.0f, -1.0f});
    xr_ui_pointer_ray_origin_ = ray_origin;
    xr_ui_pointer_ray_end_ = ray_origin + ray_direction * 3.0f;
#endif
}

bool OpenXRRenderBackend::IsXRUiPointerActive() const noexcept
{
#if defined(__ANDROID__)
    return runtime_ != nullptr &&
           runtime_->InputState().right.aim_pose_active &&
           (runtime_->InputState().right.trigger > 0.35f || xr_ui_pointer_has_hit_);
#else
    return false;
#endif
}

void OpenXRRenderBackend::EndRuntimeFrame()
{
    runtime_frame_started_ = false;
}

render::FrameGraphRenderResult OpenXRRenderBackend::BeginFrame(render::RenderFrameRequest& out_request)
{
    if (runtime_ != nullptr && HasGraphics() && HasSwapchain()) {
#if defined(__ANDROID__)
        if (next_targets_.frame == nullptr || next_targets_.vk == nullptr) {
            return render::FrameGraphRenderResult::Skipped;
        }
        if (runtime_->GetSessionHandle() == nullptr || runtime_->GetLocalSpaceHandle() == nullptr) {
            return render::FrameGraphRenderResult::Skipped;
        }

        runtime_->PollEvents();

        auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(runtime_->GetInstanceProcAddr());
        auto instance = static_cast<XrInstance>(runtime_->GetInstanceHandle());
        auto session = static_cast<XrSession>(runtime_->GetSessionHandle());

        auto xr_begin_session = LoadOpenXRCommand<PFN_xrBeginSession>(get_proc_addr, instance, "xrBeginSession");
        auto xr_wait_frame = LoadOpenXRCommand<PFN_xrWaitFrame>(get_proc_addr, instance, "xrWaitFrame");
        auto xr_begin_frame = LoadOpenXRCommand<PFN_xrBeginFrame>(get_proc_addr, instance, "xrBeginFrame");
        auto xr_acquire_swapchain_image =
            LoadOpenXRCommand<PFN_xrAcquireSwapchainImage>(get_proc_addr, instance, "xrAcquireSwapchainImage");
        auto xr_wait_swapchain_image =
            LoadOpenXRCommand<PFN_xrWaitSwapchainImage>(get_proc_addr, instance, "xrWaitSwapchainImage");
        auto xr_locate_views = LoadOpenXRCommand<PFN_xrLocateViews>(get_proc_addr, instance, "xrLocateViews");
        if (xr_begin_session == nullptr || xr_wait_frame == nullptr || xr_begin_frame == nullptr ||
            xr_acquire_swapchain_image == nullptr || xr_wait_swapchain_image == nullptr) {
            return render::FrameGraphRenderResult::Skipped;
        }

        if (!xr_session_begun_) {
            if (!runtime_->IsSessionReadyToBegin()) {
                if (!logged_waiting_for_session_ready_) {
                    LOGI("OpenXRRenderBackend waiting for XR_SESSION_STATE_READY before xrBeginSession");
                    logged_waiting_for_session_ready_ = true;
                }
                return render::FrameGraphRenderResult::Skipped;
            }

            XrSessionBeginInfo begin_info{};
            begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
            begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrResult const begin_result = xr_begin_session(session, &begin_info);
            if (begin_result != XR_SUCCESS) {
                LOGW("OpenXRRenderBackend xrBeginSession failed: result=%d", begin_result);
                return render::FrameGraphRenderResult::Skipped;
            }
            xr_session_begun_ = true;
            logged_waiting_for_session_ready_ = false;
            runtime_->MarkSessionBegan();
            LOGI("OpenXRRenderBackend xrBeginSession success");
        }

        XrFrameWaitInfo wait_info{};
        wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
        XrFrameState frame_state{};
        frame_state.type = XR_TYPE_FRAME_STATE;
        XrResult result = xr_wait_frame(session, &wait_info, &frame_state);
        if (result != XR_SUCCESS) {
            LOGW("OpenXRRenderBackend xrWaitFrame failed: result=%d", result);
            return render::FrameGraphRenderResult::Skipped;
        }

        XrFrameBeginInfo begin_frame_info{};
        begin_frame_info.type = XR_TYPE_FRAME_BEGIN_INFO;
        result = xr_begin_frame(session, &begin_frame_info);
        if (result != XR_SUCCESS) {
            LOGW("OpenXRRenderBackend xrBeginFrame failed: result=%d", result);
            return render::FrameGraphRenderResult::Skipped;
        }

        xr_predicted_display_time_ = frame_state.predictedDisplayTime;
        runtime_->SyncActionsAndLog(frame_state.predictedDisplayTime);
        UpdateXRUiPointerRay();
        xr_frame_should_render_ = frame_state.shouldRender != 0;
        xr_frame_begun_ = true;
        if (!xr_frame_should_render_) {
            frame_started_ = true;
            out_request = {};
            return render::FrameGraphRenderResult::Success;
        }

        XrView views[2]{};
        views[0].type = XR_TYPE_VIEW;
        views[1].type = XR_TYPE_VIEW;
        if (xr_locate_views == nullptr) {
            xr_frame_should_render_ = false;
            frame_started_ = true;
            out_request = {};
            return render::FrameGraphRenderResult::Success;
        }
        XrViewLocateInfo locate_info{};
        locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
        locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = frame_state.predictedDisplayTime;
        locate_info.space = static_cast<XrSpace>(runtime_->GetLocalSpaceHandle());

        XrViewState view_state{};
        view_state.type = XR_TYPE_VIEW_STATE;
        uint32_t view_count = 0;
        result = xr_locate_views(session, &locate_info, &view_state, 2, &view_count, views);
        if (result != XR_SUCCESS || view_count < 2) {
            LOGW("OpenXRRenderBackend xrLocateViews failed: result=%d count=%u", result, view_count);
            xr_frame_should_render_ = false;
            frame_started_ = true;
            out_request = {};
            return render::FrameGraphRenderResult::Success;
        }

        float dt_seconds = 1.0f / 72.0f;
        if (xr_last_input_time_ != 0 && frame_state.predictedDisplayTime > xr_last_input_time_) {
            dt_seconds = static_cast<float>(
                static_cast<double>(frame_state.predictedDisplayTime - xr_last_input_time_) * 0.000000001);
            dt_seconds = std::clamp(dt_seconds, 0.0f, 0.1f);
        }
        xr_last_input_time_ = frame_state.predictedDisplayTime;

        XRInputState const& input = runtime_->InputState();
        glm::vec2 move_stick{
            ApplyDeadzone(input.left.thumbstick.x, 0.18f),
            ApplyDeadzone(input.left.thumbstick.y, 0.18f),
        };
        glm::vec2 turn_stick{
            ApplyDeadzone(input.right.thumbstick.x, 0.22f),
            ApplyDeadzone(input.right.thumbstick.y, 0.22f),
        };

        glm::quat const hmd_local_orientation = PoseOrientation(views[0].pose);
        float const world_yaw = xr_origin_yaw_radians_ + ExtractYawRadians(hmd_local_orientation);
        glm::quat const move_yaw = YawRotation(world_yaw);
        glm::vec3 const right = move_yaw * glm::vec3{1.0f, 0.0f, 0.0f};
        glm::vec3 const forward = move_yaw * glm::vec3{0.0f, 0.0f, -1.0f};
        glm::vec3 const up = glm::vec3{0.0f, 1.0f, 0.0f};
        float constexpr move_speed_mps = 2.0f;
        float constexpr vertical_speed_mps = 1.5f;
        float constexpr turn_speed_rps = 1.8f;
        xr_origin_position_ += (right * move_stick.x + forward * move_stick.y) * move_speed_mps * dt_seconds;
        xr_origin_position_ += up * turn_stick.y * vertical_speed_mps * dt_seconds;
        xr_origin_yaw_radians_ -= turn_stick.x * turn_speed_rps * dt_seconds;

        ++xr_locomotion_log_counter_;
        if ((xr_locomotion_log_counter_ % 120u) == 1u) {
            LOGI("XROrigin locomotion pos=(%.2f, %.2f, %.2f) yaw=%.2f left_stick=(%.2f, %.2f) right_stick=(%.2f, %.2f)",
                 xr_origin_position_.x,
                 xr_origin_position_.y,
                 xr_origin_position_.z,
                 xr_origin_yaw_radians_,
                 move_stick.x,
                 move_stick.y,
                 turn_stick.x,
                 turn_stick.y);
        }

        XrSwapchainImageAcquireInfo acquire_info{};
        acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        result = xr_acquire_swapchain_image(static_cast<XrSwapchain>(xr_swapchain_), &acquire_info, &xr_acquired_image_index_);
        if (result != XR_SUCCESS || xr_acquired_image_index_ >= xr_swapchain_images_.size()) {
            LOGW("OpenXRRenderBackend xrAcquireSwapchainImage failed: result=%d index=%u",
                 result,
                 xr_acquired_image_index_);
            xr_frame_should_render_ = false;
            frame_started_ = true;
            out_request = {};
            return render::FrameGraphRenderResult::Success;
        }

        XrSwapchainImageWaitInfo image_wait_info{};
        image_wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
        image_wait_info.timeout = XR_INFINITE_DURATION;
        result = xr_wait_swapchain_image(static_cast<XrSwapchain>(xr_swapchain_), &image_wait_info);
        if (result != XR_SUCCESS) {
            LOGW("OpenXRRenderBackend xrWaitSwapchainImage failed: result=%d", result);
            auto xr_release_swapchain_image =
                LoadOpenXRCommand<PFN_xrReleaseSwapchainImage>(get_proc_addr, instance, "xrReleaseSwapchainImage");
            if (xr_release_swapchain_image != nullptr) {
                XrSwapchainImageReleaseInfo release_info{};
                release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                xr_release_swapchain_image(static_cast<XrSwapchain>(xr_swapchain_), &release_info);
            }
            xr_frame_should_render_ = false;
            frame_started_ = true;
            out_request = {};
            return render::FrameGraphRenderResult::Success;
        }

        auto command_buffer = static_cast<VkCommandBuffer>(xr_command_buffer_);
        vkResetCommandBuffer(command_buffer, 0);
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkImage image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(xr_swapchain_images_[xr_acquired_image_index_]));
        VkImageSubresourceRange color_range{};
        color_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        color_range.baseMipLevel = 0;
        color_range.levelCount = 1;
        color_range.baseArrayLayer = 0;
        color_range.layerCount = 2;

        VkImageMemoryBarrier to_color{};
        to_color.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_color.srcAccessMask = 0;
        to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        to_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_color.image = image;
        to_color.subresourceRange = color_range;
        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &to_color);

        xr_frame_data_ = *next_targets_.frame;
        xr_frame_data_.views.clear();
        xr_frame_data_.views.reserve(2);
        glm::quat const origin_rotation = YawRotation(xr_origin_yaw_radians_);
        for (uint32_t eye = 0; eye < 2; ++eye) {
            core::FrameViewData frame_view{};
            frame_view.camera_object_id = eye == 0 ? "xr_left_eye" : "xr_right_eye";
            frame_view.near_plane = next_targets_.frame->views.empty() ? 0.1f : next_targets_.frame->views[0].near_plane;
            frame_view.far_plane = next_targets_.frame->views.empty() ? 1000.0f : next_targets_.frame->views[0].far_plane;
            glm::vec3 const local_position = PosePosition(views[eye].pose);
            glm::quat const local_orientation = PoseOrientation(views[eye].pose);
            glm::vec3 const world_position = xr_origin_position_ + origin_rotation * local_position;
            glm::quat const world_orientation = origin_rotation * local_orientation;
            frame_view.view = BuildViewMatrix(world_position, world_orientation);
            frame_view.projection = BuildProjectionMatrix(views[eye].fov, frame_view.near_plane, frame_view.far_plane);
            frame_view.view_projection = frame_view.projection * frame_view.view;
            frame_view.world_position = world_position;
            xr_frame_data_.views.push_back(frame_view);
        }

        out_request = {};
        out_request.frame = &xr_frame_data_;
        out_request.vk = next_targets_.vk;
        out_request.command_buffer = command_buffer;
        out_request.backend_debug = this;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            uint32_t const eye_view_index = xr_acquired_image_index_ * 2u + eye;
            if (eye_view_index >= xr_swapchain_eye_image_views_.size()) {
                continue;
            }
            render::RenderViewTarget target{};
            target.color_target.image = image;
            target.color_target.image_view = static_cast<VkImageView>(xr_swapchain_eye_image_views_[eye_view_index]);
            target.color_target.format = static_cast<vk::Format>(xr_swapchain_format_);
            target.color_target.extent = vk::Extent2D{xr_swapchain_width_, xr_swapchain_height_};
            target.view_index = eye;
            target.frame_resource_index = eye_view_index;
            target.frame_resource_count = static_cast<uint32_t>(xr_swapchain_images_.size() * 2u);
            if (eye_view_index < depth_textures_.size()) {
                target.depth_target.texture = &depth_textures_[eye_view_index];
                target.depth_target.extent = target.color_target.extent;
                target.depth_target.format = vk::Format::eD32Sfloat;
            }
            if (eye_view_index < depth_texture_ready_.size()) {
                target.depth_target.ready = &depth_texture_ready_[eye_view_index];
            }
            out_request.views.push_back(target);
        }
        frame_started_ = true;
        return render::FrameGraphRenderResult::Success;
#else
        return render::FrameGraphRenderResult::Skipped;
#endif
    }

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

    if (runtime_ != nullptr && HasGraphics() && HasSwapchain() && xr_frame_begun_) {
#if defined(__ANDROID__)
        auto get_proc_addr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(runtime_->GetInstanceProcAddr());
        auto instance = static_cast<XrInstance>(runtime_->GetInstanceHandle());
        auto session = static_cast<XrSession>(runtime_->GetSessionHandle());
        auto local_space = static_cast<XrSpace>(runtime_->GetLocalSpaceHandle());

        auto xr_end_frame = LoadOpenXRCommand<PFN_xrEndFrame>(get_proc_addr, instance, "xrEndFrame");
        auto xr_release_swapchain_image =
            LoadOpenXRCommand<PFN_xrReleaseSwapchainImage>(get_proc_addr, instance, "xrReleaseSwapchainImage");
        auto xr_locate_views = LoadOpenXRCommand<PFN_xrLocateViews>(get_proc_addr, instance, "xrLocateViews");

        if (xr_frame_should_render_) {
            auto command_buffer = static_cast<VkCommandBuffer>(xr_command_buffer_);
            vkEndCommandBuffer(command_buffer);

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            VkQueue queue = static_cast<VkQueue>(xr_queue_);
            VkResult submit_result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
            if (submit_result == VK_SUCCESS) {
                submit_result = vkQueueWaitIdle(queue);
            }
            if (submit_result != VK_SUCCESS) {
                LOGW("OpenXRRenderBackend XR graph submit failed: result=%d", submit_result);
                render_result = render::FrameGraphRenderResult::Skipped;
                xr_frame_should_render_ = false;
            }

            if (xr_release_swapchain_image != nullptr) {
                XrSwapchainImageReleaseInfo release_info{};
                release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                XrResult const release_result =
                    xr_release_swapchain_image(static_cast<XrSwapchain>(xr_swapchain_), &release_info);
                if (release_result != XR_SUCCESS) {
                    LOGW("OpenXRRenderBackend xrReleaseSwapchainImage failed: result=%d", release_result);
                    xr_frame_should_render_ = false;
                }
            }
        }

        XrFrameEndInfo end_info{};
        end_info.type = XR_TYPE_FRAME_END_INFO;
        end_info.displayTime = xr_predicted_display_time_;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

        XrCompositionLayerProjection projection_layer{};
        XrCompositionLayerProjectionView projection_views[2]{};
        void const* layers[1]{};
        XrView views[2]{};
        views[0].type = XR_TYPE_VIEW;
        views[1].type = XR_TYPE_VIEW;

        if (xr_frame_should_render_ && xr_locate_views != nullptr && local_space != nullptr) {
            XrViewLocateInfo locate_info{};
            locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
            locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate_info.displayTime = xr_predicted_display_time_;
            locate_info.space = local_space;

            XrViewState view_state{};
            view_state.type = XR_TYPE_VIEW_STATE;
            uint32_t view_count = 0;
            XrResult const locate_result = xr_locate_views(session, &locate_info, &view_state, 2, &view_count, views);
            if (locate_result == XR_SUCCESS && view_count >= 2) {
                for (uint32_t i = 0; i < 2; ++i) {
                    projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projection_views[i].pose = views[i].pose;
                    projection_views[i].fov = views[i].fov;
                    projection_views[i].subImage.swapchain = static_cast<XrSwapchain>(xr_swapchain_);
                    projection_views[i].subImage.imageRect.offset = {0, 0};
                    projection_views[i].subImage.imageRect.extent = {
                        static_cast<int32_t>(xr_swapchain_width_),
                        static_cast<int32_t>(xr_swapchain_height_),
                    };
                    projection_views[i].subImage.imageArrayIndex = i;
                }
                projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
                projection_layer.space = local_space;
                projection_layer.viewCount = 2;
                projection_layer.views = projection_views;
                layers[0] = &projection_layer;
                end_info.layerCount = 1;
                end_info.layers = layers;
            } else {
                LOGW("OpenXRRenderBackend xrLocateViews failed before xrEndFrame: result=%d count=%u",
                     locate_result,
                     view_count);
            }
        }

        if (xr_end_frame != nullptr) {
            XrResult const end_result = xr_end_frame(session, &end_info);
            if (end_result != XR_SUCCESS) {
                LOGW("OpenXRRenderBackend xrEndFrame failed: result=%d", end_result);
                render_result = render::FrameGraphRenderResult::Skipped;
            }
        }
#endif
        frame_started_ = false;
        runtime_frame_started_ = false;
        xr_frame_begun_ = false;
        xr_frame_should_render_ = false;
        return render_result;
    }

    ClearNextFrameTargets();
    return render_result;
}

} // namespace ave::xr
