#include "VkMemory.hpp"
#include "VkContext.hpp"

namespace vkfw {

bool VkMemoryAllocator::Init(VkContext& ctx) {
    ctx_ = &ctx;
    memory_properties_ = ctx.PhysicalDevice().getMemoryProperties();
    return true;
}

void VkMemoryAllocator::Shutdown(VkContext& /*ctx*/) {
    // Deallocate all allocations
    for (auto& allocation : allocations_) {
        if (allocation.memory) {
            // vk::DeviceMemory doesn't have reset(), it's just a handle
            // The RAII wrapper will handle cleanup automatically
        }
    }
    allocations_.clear();
    ctx_ = nullptr;
}

MemoryAllocation VkMemoryAllocator::Allocate(uint32_t size, vk::MemoryPropertyFlags required_properties) {
    return Allocate(size, required_properties, vk::MemoryPropertyFlags{});
}

MemoryAllocation VkMemoryAllocator::Allocate(uint32_t size, vk::MemoryPropertyFlags required_properties, vk::MemoryPropertyFlags preferred_properties) {
    // Find suitable memory type
    uint32_t memory_type = FindMemoryType(UINT32_MAX, required_properties | preferred_properties);
    if (memory_type == UINT32_MAX) {
        // Try with just required properties
        memory_type = FindMemoryType(UINT32_MAX, required_properties);
        if (memory_type == UINT32_MAX) {
            return {};
        }
    }

    // Allocate memory
    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = size;
    alloc_info.memoryTypeIndex = memory_type;

    try {
        auto memory = std::make_unique<vk::raii::DeviceMemory>(ctx_->Device(), alloc_info);
        
        MemoryAllocation allocation{};
        allocation.size = size;
        allocation.memory_type = memory_type;
        allocation.offset = 0;
        
        // Map if host visible
        if (memory_properties_.memoryTypes[memory_type].propertyFlags & 
            (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) {
            allocation.mapped_data = memory->mapMemory(0, size);
        }
        
        allocations_.push_back(allocation);
        
        // Transfer ownership to caller
        allocation.memory = *memory;
        memory.release();
        
        return allocation;
    } catch (vk::SystemError& e) {
        return {};
    }
}

void VkMemoryAllocator::Deallocate(MemoryAllocation& allocation) {
    if (!allocation.memory) {
        return;
    }

    // Find and remove allocation first
    for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
        if (it->memory == allocation.memory) {
            allocations_.erase(it);
            break;
        }
    }

    // Unmap if mapped (after removing from tracking)
    if (allocation.mapped_data) {
        // Create temporary RAII object to unmap
        vk::DeviceMemory raw_memory = allocation.memory;
        auto memory = std::make_unique<vk::raii::DeviceMemory>(
            ctx_->Device(), 
            static_cast<VkDeviceMemory>(raw_memory)
        );
        memory->unmapMemory();
    }
}

uint32_t VkMemoryAllocator::FindMemoryType(uint32_t type_bits, vk::MemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < memory_properties_.memoryTypeCount; ++i) {
        if ((type_bits & (1 << i)) && 
            (memory_properties_.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace vkfw
