#include "VkCommandBuffer.hpp"
#include "VkContext.hpp"

namespace vkfw {

static vk::CommandBufferLevel GetCommandBufferLevel(CommandBufferLevel level) {
    switch (level) {
        case CommandBufferLevel::Primary:
            return vk::CommandBufferLevel::ePrimary;
        case CommandBufferLevel::Secondary:
            return vk::CommandBufferLevel::eSecondary;
        default:
            return vk::CommandBufferLevel::ePrimary;
    }
}

static vk::CommandBufferUsageFlags GetCommandBufferUsageFlags(CommandBufferUsage usage) {
    vk::CommandBufferUsageFlags flags = {};
    
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(CommandBufferUsage::OneTimeSubmit)) {
        flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(CommandBufferUsage::SimultaneousUse)) {
        flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(CommandBufferUsage::RenderPassContinue)) {
        flags |= vk::CommandBufferUsageFlagBits::eRenderPassContinue;
    }
    
    return flags;
}

bool VkCommandBuffer::Init(VkContext& ctx, CommandBufferInfo const& info) {
    level_ = info.level;
    usage_ = info.usage;

    vk::CommandPoolCreateInfo pool_info{};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = ctx.GraphicsQueueFamilyIndex();

    command_pool_ = std::make_unique<vk::raii::CommandPool>(ctx.Device(), pool_info);

    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.commandPool = *command_pool_;
    alloc_info.level = GetCommandBufferLevel(info.level);
    alloc_info.commandBufferCount = info.count;

    try {
        command_buffers_ = std::make_unique<vk::raii::CommandBuffers>(ctx.Device(), alloc_info);
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkCommandBuffer::Shutdown(VkContext& ctx) {
    command_buffers_.reset();
    level_ = CommandBufferLevel::Primary;
    usage_ = CommandBufferUsage::OneTimeSubmit;
}

void VkCommandBuffer::Begin(uint32_t index, vk::CommandBufferUsageFlags flags) {
    if (index >= command_buffers_->size()) {
        return;
    }

    try {
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = flags;
        
        auto& cmd = command_buffers_->at(index);
        cmd.begin(begin_info);
    } catch (vk::SystemError& e) {
        // Silence exception to prevent system crash
    }
}

void VkCommandBuffer::End(uint32_t index) {
    if (index >= command_buffers_->size()) {
        return;
    }

    try {
        auto& cmd = command_buffers_->at(index);
        cmd.end();
    } catch (vk::SystemError& e) {
        // Silence exception to prevent system crash
    }
}

void VkCommandBuffer::Reset(uint32_t index) {
    if (index >= command_buffers_->size()) {
        return;
    }

    try {
        auto& cmd = command_buffers_->at(index);
        cmd.reset();
    } catch (vk::SystemError& e) {
        // Silence exception to prevent system crash
    }
}

void VkCommandBuffer::ResetAll() {
    if (!command_buffers_) {
        return;
    }

    for (auto& cmd : *command_buffers_) {
        cmd.reset();
    }
}

bool VkCommandBuffer::BeginAll(vk::CommandBufferUsageFlags flags) {
    if (!command_buffers_) {
        return false;
    }

    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = flags;
    
    try {
        for (auto& cmd : *command_buffers_) {
            cmd.begin(begin_info);
        }
        return true;
    } catch (vk::SystemError&) {
        return false;
    }
}

void VkCommandBuffer::EndAll() {
    if (!command_buffers_) {
        return;
    }

    for (auto& cmd : *command_buffers_) {
        cmd.end();
    }
}

} // namespace vkfw
