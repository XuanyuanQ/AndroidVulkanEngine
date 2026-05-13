#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {

class VkContext;

enum class MemoryType {
    DeviceLocal,
    HostVisible,
    HostCoherent,
    HostCached
};

struct MemoryAllocation {
    vk::DeviceMemory memory;
    void* mapped_data = nullptr;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint32_t memory_type = 0;
};

class VkMemoryAllocator {
public:
    VkMemoryAllocator() = default;
    ~VkMemoryAllocator() = default;

    VkMemoryAllocator(VkMemoryAllocator&&) noexcept = default;
    VkMemoryAllocator& operator=(VkMemoryAllocator&&) noexcept = default;

    VkMemoryAllocator(VkMemoryAllocator const&) = delete;
    VkMemoryAllocator& operator=(VkMemoryAllocator const&) = delete;

    bool Init(VkContext& ctx);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return ctx_ != nullptr; }

    MemoryAllocation Allocate(uint32_t size, vk::MemoryPropertyFlags required_properties);
    MemoryAllocation Allocate(uint32_t size, vk::MemoryPropertyFlags required_properties, vk::MemoryPropertyFlags preferred_properties);
    void Deallocate(MemoryAllocation& allocation);

    uint32_t FindMemoryType(uint32_t type_bits, vk::MemoryPropertyFlags properties) const;

private:
    VkContext* ctx_ = nullptr;
    std::vector<MemoryAllocation> allocations_;
    vk::PhysicalDeviceMemoryProperties memory_properties_;
};

} // namespace vkfw
