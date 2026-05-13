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

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    Staging
};

struct BufferInfo {
    uint32_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    bool mappable = true;
};

class VkBuffer {
public:
    VkBuffer() = default;
    ~VkBuffer() = default;

    VkBuffer(VkBuffer&&) noexcept = default;
    VkBuffer& operator=(VkBuffer&&) noexcept = default;

    VkBuffer(VkBuffer const&) = delete;
    VkBuffer& operator=(VkBuffer const&) = delete;

    bool Init(VkContext& ctx, BufferInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return buffer_ != nullptr; }
    
    vk::Buffer Handle() const noexcept { return *buffer_; }
    vk::DeviceMemory Memory() const noexcept { return *memory_; }
    void* MappedData() const noexcept { return mapped_data_; }
    uint32_t Size() const noexcept { return size_; }

    void* Map(VkContext& ctx);
    void Unmap(VkContext& ctx);
    void UpdateData(VkContext& ctx, void const* data, uint32_t size);
private:
    bool IsMappable() const;

private:
    std::unique_ptr<vk::raii::Buffer> buffer_;
    std::unique_ptr<vk::raii::DeviceMemory> memory_;
    void* mapped_data_ = nullptr;
    uint32_t size_ = 0;
    BufferUsage usage_ = BufferUsage::Vertex;
};

} // namespace vkfw
