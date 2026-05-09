#include "ave/rhi/VulkanDevice.h"

#if AVE_RHI_HAS_VULKAN_TYPES
#include <array>
#include <limits>
#include <vector>
#endif

namespace ave::rhi {

#if AVE_RHI_HAS_VULKAN_TYPES
namespace {

bool Check(VkResult result)
{
    return result == VK_SUCCESS;
}

} // namespace
#endif

bool VulkanDevice::Initialize(VulkanDeviceConfig const&)
{
    initialized_ = true;
    return true;
}

void VulkanDevice::Shutdown()
{
#if AVE_RHI_HAS_VULKAN_TYPES
    DestroyDevice();
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
#endif
    initialized_ = false;
}

bool VulkanDevice::IsInitialized() const noexcept
{
    return initialized_;
}

void VulkanDevice::SubmitDebugWork(std::string const&, uint32_t)
{
}

#if AVE_RHI_HAS_VULKAN_TYPES
bool VulkanDevice::CreateInstance(VulkanDeviceConfig const&, char const* application_name)
{
    if (instance_ != VK_NULL_HANDLE) {
        initialized_ = true;
        return true;
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = application_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "AveEngine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

#if defined(__ANDROID__)
    std::array<char const*, 2> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
#else
    std::array<char const*, 1> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
    };
#endif

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    initialized_ = Check(vkCreateInstance(&create_info, nullptr, &instance_));
    return initialized_;
}

bool VulkanDevice::CreateDeviceForSurface(VkSurfaceKHR surface)
{
    DestroyDevice();

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (auto const device : devices) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());

        for (uint32_t index = 0; index < queue_count; ++index) {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present_supported);
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_supported) {
                physical_device_ = device;
                graphics_queue_family_ = index;
                break;
            }
        }

        if (physical_device_ != VK_NULL_HANDLE) {
            break;
        }
    }

    if (physical_device_ == VK_NULL_HANDLE) {
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    char const* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = extensions;

    if (!Check(vkCreateDevice(physical_device_, &create_info, nullptr, &device_))) {
        DestroyDevice();
        return false;
    }

    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
    return true;
}

void VulkanDevice::DestroyDevice()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    physical_device_ = VK_NULL_HANDLE;
    graphics_queue_family_ = UINT32_MAX;
    graphics_queue_ = VK_NULL_HANDLE;
}

VkInstance VulkanDevice::Instance() const noexcept
{
    return instance_;
}

VkPhysicalDevice VulkanDevice::PhysicalDevice() const noexcept
{
    return physical_device_;
}

VkDevice VulkanDevice::Device() const noexcept
{
    return device_;
}

VkQueue VulkanDevice::GraphicsQueue() const noexcept
{
    return graphics_queue_;
}

uint32_t VulkanDevice::GraphicsQueueFamily() const noexcept
{
    return graphics_queue_family_;
}

uint32_t VulkanDevice::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return 0;
}
#endif

} // namespace ave::rhi
