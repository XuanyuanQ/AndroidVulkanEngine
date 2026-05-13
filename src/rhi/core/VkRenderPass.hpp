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

enum class RenderPassLoadOp {
    Load,
    Clear,
    DontCare
};

enum class RenderPassStoreOp {
    Store,
    DontCare
};

enum class RenderPassAttachmentType {
    Color,
    Depth,
    Stencil
};

struct RenderPassAttachment {
    uint32_t binding = 0;
    RenderPassAttachmentType type = RenderPassAttachmentType::Color;
    vk::Format format = vk::Format::eUndefined;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    RenderPassLoadOp load_op = RenderPassLoadOp::Clear;
    RenderPassStoreOp store_op = RenderPassStoreOp::Store;
    vk::ImageLayout initial_layout = vk::ImageLayout::eUndefined;
    vk::ImageLayout final_layout = vk::ImageLayout::ePresentSrcKHR;
};

struct RenderPassSubpass {
    std::vector<RenderPassAttachment> color_attachments;
    RenderPassAttachment depth_attachment;
    RenderPassAttachment stencil_attachment;
    vk::PipelineBindPoint bind_point = vk::PipelineBindPoint::eGraphics;
};

struct RenderPassInfo {
    std::vector<RenderPassSubpass> subpasses;
    vk::ImageLayout final_layout = vk::ImageLayout::ePresentSrcKHR;
};

class VkRenderPass {
public:
    VkRenderPass() = default;
    ~VkRenderPass() = default;

    VkRenderPass(VkRenderPass&&) noexcept = default;
    VkRenderPass& operator=(VkRenderPass&&) noexcept = default;

    VkRenderPass(VkRenderPass const&) = delete;
    VkRenderPass& operator=(VkRenderPass const&) = delete;

    bool Init(VkContext& ctx, RenderPassInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return render_pass_ != nullptr; }
    
    vk::RenderPass Handle() const noexcept { return *render_pass_; }
    vk::raii::RenderPass const& GetRenderPass() const noexcept { return *render_pass_; }

private:
    std::unique_ptr<vk::raii::RenderPass> render_pass_;
};

} // namespace vkfw
