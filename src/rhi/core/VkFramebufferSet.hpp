#pragma once

#include <cstdint>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {

class VkContext;
class VkSwapchain;
class VkRenderPass;

class VkFramebufferSet {
public:
    VkFramebufferSet() = default;
    ~VkFramebufferSet() = default;

    VkFramebufferSet(VkFramebufferSet&&) noexcept = default;
    VkFramebufferSet& operator=(VkFramebufferSet&&) noexcept = default;

    VkFramebufferSet(VkFramebufferSet const&) = delete;
    VkFramebufferSet& operator=(VkFramebufferSet const&) = delete;

    bool Init(VkContext& ctx, VkSwapchain& swapchain, VkRenderPass const& render_pass, vk::ImageView depth_view = {});
    bool Init(VkContext& ctx,
              VkSwapchain& swapchain,
              VkRenderPass const& render_pass,
              std::vector<vk::ImageView> const& depth_views);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return !framebuffers_.empty(); }
    uint32_t Count() const noexcept { return static_cast<uint32_t>(framebuffers_.size()); }
    vk::Framebuffer Handle(uint32_t index) const { return *framebuffers_.at(index); }

private:
    std::vector<vk::raii::Framebuffer> framebuffers_{};
};

} // namespace vkfw
