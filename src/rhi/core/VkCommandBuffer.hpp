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

enum class CommandBufferLevel {
    Primary,
    Secondary
};

enum class CommandBufferUsage {
    OneTimeSubmit,
    SimultaneousUse,
    RenderPassContinue
};

struct CommandBufferInfo {
    CommandBufferLevel level = CommandBufferLevel::Primary;
    CommandBufferUsage usage = CommandBufferUsage::OneTimeSubmit;
    uint32_t count = 1;
};

class VkCommandBuffer {
public:
    VkCommandBuffer() = default;
    ~VkCommandBuffer() = default;

    VkCommandBuffer(VkCommandBuffer&&) noexcept = default;
    VkCommandBuffer& operator=(VkCommandBuffer&&) noexcept = default;

    VkCommandBuffer(VkCommandBuffer const&) = delete;
    VkCommandBuffer& operator=(VkCommandBuffer const&) = delete;

    bool Init(VkContext& ctx, CommandBufferInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return command_buffers_ != nullptr; }
    
    vk::CommandBuffer Handle(uint32_t index) const { return command_buffers_->at(index); }
    uint32_t Count() const noexcept { return static_cast<uint32_t>(command_buffers_->size()); }

    void Begin(uint32_t index, vk::CommandBufferUsageFlags flags = {});
    void End(uint32_t index);
    void Reset(uint32_t index);
    
    // Add convenience methods for direct buffer access
    void ResetAll();
    bool BeginAll(vk::CommandBufferUsageFlags flags = {});
    void EndAll();

private:
    std::unique_ptr<vk::raii::CommandPool> command_pool_;
    std::unique_ptr<vk::raii::CommandBuffers> command_buffers_;
    CommandBufferLevel level_ = CommandBufferLevel::Primary;
    CommandBufferUsage usage_ = CommandBufferUsage::OneTimeSubmit;
};

} // namespace vkfw
