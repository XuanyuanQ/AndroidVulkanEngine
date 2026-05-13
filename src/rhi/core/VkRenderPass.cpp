#include "VkRenderPass.hpp"
#include "VkContext.hpp"

namespace vkfw {

static vk::AttachmentLoadOp GetAttachmentLoadOp(RenderPassLoadOp op) {
    switch (op) {
        case RenderPassLoadOp::Load:
            return vk::AttachmentLoadOp::eLoad;
        case RenderPassLoadOp::Clear:
            return vk::AttachmentLoadOp::eClear;
        case RenderPassLoadOp::DontCare:
            return vk::AttachmentLoadOp::eDontCare;
        default:
            return vk::AttachmentLoadOp::eLoad;
    }
}

static vk::AttachmentStoreOp GetAttachmentStoreOp(RenderPassStoreOp op) {
    switch (op) {
        case RenderPassStoreOp::Store:
            return vk::AttachmentStoreOp::eStore;
        case RenderPassStoreOp::DontCare:
            return vk::AttachmentStoreOp::eDontCare;
        default:
            return vk::AttachmentStoreOp::eStore;
    }
}

static vk::ImageLayout GetImageLayout(RenderPassAttachmentType type, vk::Format /*format*/) {
    switch (type) {
        case RenderPassAttachmentType::Color:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case RenderPassAttachmentType::Depth:
        case RenderPassAttachmentType::Stencil:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        default:
            return vk::ImageLayout::eColorAttachmentOptimal;
    }
}

static vk::AttachmentReference GetAttachmentReference(RenderPassAttachment const& attachment, vk::ImageLayout layout) {
    vk::AttachmentReference ref{};
    ref.attachment = attachment.binding;
    ref.layout = layout;
    return ref;
}

bool VkRenderPass::Init(VkContext& ctx, RenderPassInfo const& info) {
    std::vector<vk::AttachmentDescription> attachments;
    std::vector<vk::SubpassDescription> subpasses;

    // 维持生命周期的外部容器
    std::vector<std::vector<vk::AttachmentReference>> all_color_refs;
    std::vector<vk::AttachmentReference> all_depth_refs;

    all_color_refs.reserve(info.subpasses.size());
    all_depth_refs.reserve(info.subpasses.size());

    for (auto const& subpass_info : info.subpasses) {
        std::vector<vk::AttachmentReference> current_color_refs;

        // 1. 处理颜色附件
        for (auto const& color_att : subpass_info.color_attachments) {
            vk::AttachmentDescription desc{};
            desc.format         = color_att.format;
            desc.samples        = color_att.samples;
            desc.loadOp         = GetAttachmentLoadOp(color_att.load_op);
            desc.storeOp        = GetAttachmentStoreOp(color_att.store_op);
            desc.initialLayout  = color_att.initial_layout; 
            desc.finalLayout    = color_att.final_layout;
            desc.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
            desc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

            attachments.push_back(desc);

            // 关键：渲染中必须使用 eColorAttachmentOptimal
            current_color_refs.push_back({
                static_cast<uint32_t>(attachments.size() - 1),
                vk::ImageLayout::eColorAttachmentOptimal
            });
        }

        all_color_refs.push_back(std::move(current_color_refs));

        vk::SubpassDescription subpass_desc{};
        subpass_desc.pipelineBindPoint = subpass_info.bind_point;
        subpass_desc.colorAttachmentCount = static_cast<uint32_t>(all_color_refs.back().size());
        subpass_desc.pColorAttachments = all_color_refs.back().data();

        // 2. 处理深度附件（现在 format 默认为 Undefined，只有设置了才会进这里）
        if (subpass_info.depth_attachment.format != vk::Format::eUndefined) {
            vk::AttachmentDescription depth_desc{};
            depth_desc.format         = subpass_info.depth_attachment.format;
            depth_desc.samples        = subpass_info.depth_attachment.samples;
            depth_desc.loadOp         = GetAttachmentLoadOp(subpass_info.depth_attachment.load_op);
            depth_desc.storeOp        = GetAttachmentStoreOp(subpass_info.depth_attachment.store_op);
            depth_desc.initialLayout  = subpass_info.depth_attachment.initial_layout;
            depth_desc.finalLayout    = subpass_info.depth_attachment.final_layout;

            attachments.push_back(depth_desc);

            all_depth_refs.push_back({
                static_cast<uint32_t>(attachments.size() - 1),
                vk::ImageLayout::eDepthStencilAttachmentOptimal
            });
            subpass_desc.pDepthStencilAttachment = &all_depth_refs.back();
        }

        subpasses.push_back(subpass_desc);
    }

    // 3. 交换链同步依赖
    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo create_info{};
    create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    create_info.pAttachments    = attachments.data();
    create_info.subpassCount    = static_cast<uint32_t>(subpasses.size());
    create_info.pSubpasses      = subpasses.data();
    create_info.dependencyCount = 1;
    create_info.pDependencies   = &dependency;

    try {
        render_pass_ = std::make_unique<vk::raii::RenderPass>(ctx.Device(), create_info);
        return true;
    } catch (...) {
        return false;
    }
}

void VkRenderPass::Shutdown(VkContext& /*ctx*/) {
    render_pass_.reset();
}

} // namespace vkfw
