#pragma once

#include <cstdint>
#include <string>

#if (defined(AVE_HAS_VULKAN) && AVE_HAS_VULKAN) || defined(__ANDROID__)
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#define AVE_RHI_HAS_VULKAN_TYPES 1
#else
#define AVE_RHI_HAS_VULKAN_TYPES 0
#endif

namespace ave::rhi {

struct VulkanDeviceConfig {
    bool enable_validation = true;
};

class VulkanDevice {
public:
    bool Initialize(VulkanDeviceConfig const& config);
    void Shutdown();
    bool IsInitialized() const noexcept;
    void SubmitDebugWork(std::string const& label, uint32_t command_buffer_count);

#if AVE_RHI_HAS_VULKAN_TYPES
    bool CreateInstance(VulkanDeviceConfig const& config, char const* application_name);
    bool CreateDeviceForSurface(VkSurfaceKHR surface);
    void DestroyDevice();

    VkInstance Instance() const noexcept;
    VkPhysicalDevice PhysicalDevice() const noexcept;
    VkDevice Device() const noexcept;
    VkQueue GraphicsQueue() const noexcept;
    uint32_t GraphicsQueueFamily() const noexcept;
    uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
#endif

private:
    bool initialized_ = false;

#if AVE_RHI_HAS_VULKAN_TYPES
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = UINT32_MAX;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
#endif
};

} // namespace ave::rhi
