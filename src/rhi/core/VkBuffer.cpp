#include "VkBuffer.hpp"
#include "VkContext.hpp"
#include <cstring>
#include "LogUtil.h"

namespace vkfw {

static vk::BufferUsageFlags GetBufferUsageFlags(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Vertex:
            return vk::BufferUsageFlagBits::eVertexBuffer;
        case BufferUsage::Index:
            return vk::BufferUsageFlagBits::eIndexBuffer;
        case BufferUsage::Uniform:
            return vk::BufferUsageFlagBits::eUniformBuffer;
        case BufferUsage::Storage:
            return vk::BufferUsageFlagBits::eStorageBuffer;
        case BufferUsage::Staging:
            return vk::BufferUsageFlagBits::eTransferSrc;
        default:
            return vk::BufferUsageFlagBits::eVertexBuffer;
    }
}

static vk::MemoryPropertyFlags GetMemoryPropertyFlags(bool mappable) {
    vk::MemoryPropertyFlags flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    if (mappable) {
        flags |= vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    }
    return flags;
}

static uint32_t FindMemoryType(VkContext& ctx, uint32_t type_filter, vk::MemoryPropertyFlags properties) {
    auto memory_properties = ctx.PhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && 
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VkBuffer::Init(VkContext& ctx, BufferInfo const& info) {
    size_ = info.size;
    usage_ = info.usage;

    // Create buffer
    vk::BufferCreateInfo buffer_info{};
    buffer_info.size = info.size;
    buffer_info.usage = GetBufferUsageFlags(info.usage);
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    try {
        buffer_ = std::make_unique<vk::raii::Buffer>(ctx.Device(), buffer_info);
    } catch (vk::SystemError& e) {
        LOGE("VkBuffer: createBuffer failed: %s", e.what());
        return false;
    }

    // Allocate memory
    auto memory_requirements = buffer_->getMemoryRequirements();
    uint32_t memory_type = FindMemoryType(ctx, memory_requirements.memoryTypeBits, 
        GetMemoryPropertyFlags(info.mappable));

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    try {
        memory_ = std::make_unique<vk::raii::DeviceMemory>(ctx.Device(), alloc_info);
    } catch (vk::SystemError& e) {
        LOGE("VkBuffer: allocateMemory failed: %s", e.what());
        return false;
    }

    // Bind memory
    buffer_->bindMemory(*memory_, 0);

    // Map if mappable
    if (info.mappable) {
        Map(ctx);
    }

    return true;
}

void VkBuffer::Shutdown(VkContext& ctx) {
    if (mapped_data_ != nullptr) {
        Unmap(ctx);
    }
    memory_.reset();
    buffer_.reset();
    size_ = 0;
}

void* VkBuffer::Map(VkContext& ctx) {
    if (mapped_data_ == nullptr) {
        mapped_data_ = memory_->mapMemory(0, size_);
    }
    return mapped_data_;
}

void VkBuffer::Unmap(VkContext& ctx) {
    if (mapped_data_ != nullptr) {
        memory_->unmapMemory();
        mapped_data_ = nullptr;
    }
}

void VkBuffer::UpdateData(VkContext& ctx, void const* data, uint32_t size) {
    if (size > size_) {
        return; // Should not happen, but guard against overflow
    }

    void* mapped = Map(ctx);
    std::memcpy(mapped, data, size);
    
    if (!IsMappable()) {
        Unmap(ctx);
    }
}

bool VkBuffer::IsMappable() const {
    switch (usage_) {
        case BufferUsage::Staging:
        case BufferUsage::Uniform:
            return true;
        case BufferUsage::Vertex:
        case BufferUsage::Index:
            return mapped_data_ != nullptr; // Return true if buffer is mapped
        default:
            return false;
    }
}

} // namespace vkfw
