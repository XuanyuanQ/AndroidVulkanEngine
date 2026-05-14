#include "VkFramebufferSet.hpp"

#include "VkContext.hpp"
#include "VkRenderPass.hpp"
#include "VkSwapchain.hpp"

namespace vkfw {

bool VkFramebufferSet::Init(VkContext& ctx, VkSwapchain& swapchain, VkRenderPass const& render_pass)
{
    auto extent = swapchain.Extent();
    framebuffers_.clear();
    framebuffers_.reserve(swapchain.ImageCount());

    try {
        for (uint32_t i = 0; i < swapchain.ImageCount(); ++i) {
            auto view = swapchain.ImageView(i);
            vk::FramebufferCreateInfo create_info{};
            create_info.renderPass = render_pass.Handle();
            create_info.attachmentCount = 1;
            create_info.pAttachments = &view;
            create_info.width = extent.width;
            create_info.height = extent.height;
            create_info.layers = 1;
            framebuffers_.emplace_back(ctx.Device(), create_info);
        }
        return true;
    } catch (...) {
        framebuffers_.clear();
        return false;
    }
}

void VkFramebufferSet::Shutdown(VkContext& ctx)
{
    (void)ctx;
    framebuffers_.clear();
}

} // namespace vkfw
