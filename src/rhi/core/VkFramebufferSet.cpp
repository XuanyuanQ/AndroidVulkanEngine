#include "VkFramebufferSet.hpp"

#include "VkContext.hpp"
#include "VkRenderPass.hpp"
#include "VkSwapchain.hpp"

namespace vkfw {

bool VkFramebufferSet::Init(VkContext& ctx, VkSwapchain& swapchain, VkRenderPass const& render_pass, vk::ImageView depth_view)
{
    auto extent = swapchain.Extent();
    framebuffers_.clear();
    framebuffers_.reserve(swapchain.ImageCount());

    try {
        for (uint32_t i = 0; i < swapchain.ImageCount(); ++i) {
            auto view = swapchain.ImageView(i);
            std::vector<vk::ImageView> attachments;
            attachments.push_back(view);
            if (depth_view) {
                attachments.push_back(depth_view);
            }
            vk::FramebufferCreateInfo create_info{};
            create_info.renderPass = render_pass.Handle();
            create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            create_info.pAttachments = attachments.data();
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
