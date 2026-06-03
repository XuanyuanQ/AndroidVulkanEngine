#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VkRasterRenderer.hpp"
#include "VkCommandBuffer.hpp"
#include "VkFramebufferSet.hpp"
#include "VkRenderPass.hpp"
#include "ave/render/RenderPasses.h"
#include "ave/render/RenderPass.h"
#include "LogUtil.h"

namespace ave::render {

class Renderer::Impl {
public:
    rhi::VulkanRasterRenderer raster_renderer{};
    vkfw::VkCommandBuffer framegraph_command_buffers{};
    vkfw::VkRenderPass framegraph_render_pass{};
    vkfw::VkFramebufferSet framegraph_framebuffers{};
    vkfw::VkRenderPass framegraph_load_render_pass{};
    vkfw::VkFramebufferSet framegraph_load_framebuffers{};
    vkfw::VkTexture depth_texture{};
};

Renderer::Renderer()
{
    material_system_.Initialize(&resource_system_);
}

Renderer::~Renderer() = default;

bool Renderer::Initialize(RendererConfig const& config)
{
    (void)config;
    return true;
}

void Renderer::Shutdown()
{
    ShutdownRaster();

}

void Renderer::Render(core::FrameData const& frame, core::JobSystem& jobs)
{
    (void)jobs;
    // TODO: Convert FrameData.resources (string ids) into ResourceSystem's numeric ids
    // and call resource_system_.EnsureResources(...) once the id mapping is wired.

    // Execute frame graph with frame data
    RenderPassContext context{};
    context.frame = &frame;
    context.resources = &resource_system_;
    context.pipelines = &pipeline_system_;
    std::vector<std::string> debug_lines;
    context.debug_output = &debug_lines;
    graph_.Execute(context);
}

void Renderer::SetVkContext(vkfw::VkContext* ctx)
{
    vk_context_ = ctx;
    resource_system_.SetContext(ctx);
    pipeline_system_.SetContext(ctx);
    pipeline_system_.SetResourceSystem(&resource_system_);
}

bool Renderer::InitializeRaster(vkfw::VkContext& ctx,
                                vkfw::VkSwapchain& swapchain,
                                vkfw::VkFrameSync& sync,
                                std::span<RasterColorVertex const> vertices,
                                RasterShaderCode const& shaders)
{
    ShutdownRaster();
    impl_ = std::make_unique<Impl>();

    std::vector<rhi::RasterColorVertex> raster_vertices;
    raster_vertices.reserve(vertices.size());
    for (auto const& vertex : vertices) {
        raster_vertices.push_back(rhi::RasterColorVertex{
            .position = vertex.position,
            .color = vertex.color,
        });
    }

    if (!impl_->raster_renderer.Initialize(
            ctx,
            swapchain,
            sync,
            raster_vertices,
            {
                shaders.vertex,
                shaders.fragment,
            })) {
        impl_.reset();
        return false;
    }

    return true;
}

bool Renderer::InitializeRasterMeshResource(vkfw::VkContext& ctx,
                                            vkfw::VkSwapchain& swapchain,
                                            vkfw::VkFrameSync& sync,
                                            uint32_t mesh_id,
                                            RasterShaderCode const& shaders)
{
    (void)shaders; // Unused for now but kept for signature compatibility
    (void)ctx;
    (void)swapchain;
    (void)sync;
    (void)mesh_id;
    return true;
}

void Renderer::ShutdownRaster()
{
    if (impl_ != nullptr) {
        impl_->raster_renderer.Shutdown();
    }
    impl_.reset();
}



bool Renderer::InitializeFrameGraphBackend(vkfw::VkContext& ctx,
                                           vkfw::VkSwapchain& swapchain,
                                           vkfw::VkFrameSync& sync)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    SetVkContext(&ctx);

    if (!ctx.SupportsDynamicRendering()) {
        vk::Extent2D const extent = swapchain.Extent();
        if (impl_->depth_texture.IsInitialized()) {
            impl_->depth_texture.Shutdown(ctx);
        }
        if (!impl_->depth_texture.Init(ctx, vkfw::TextureInfo{
                                                .width = extent.width,
                                                .height = extent.height,
                                                .mip_levels = 1,
                                                .format = vkfw::TextureFormat::D32_SFLOAT,
                                                .usage = vkfw::TextureUsage::DepthStencilAttachment,
                                                .mipmap = false,
                                            })) {
            return false;
        }

        vkfw::RenderPassAttachment color_attachment{};
        color_attachment.binding = 0;
        color_attachment.type = vkfw::RenderPassAttachmentType::Color;
        color_attachment.format = swapchain.Format();
        color_attachment.samples = vk::SampleCountFlagBits::e1;
        color_attachment.load_op = vkfw::RenderPassLoadOp::Clear;
        color_attachment.store_op = vkfw::RenderPassStoreOp::Store;
        color_attachment.initial_layout = vk::ImageLayout::eColorAttachmentOptimal;
        color_attachment.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

        vkfw::RenderPassAttachment depth_attachment{};
        depth_attachment.binding = 1;
        depth_attachment.type = vkfw::RenderPassAttachmentType::Depth;
        depth_attachment.format = vk::Format::eD32Sfloat;
        depth_attachment.samples = vk::SampleCountFlagBits::e1;
        depth_attachment.load_op = vkfw::RenderPassLoadOp::Clear;
        depth_attachment.store_op = vkfw::RenderPassStoreOp::Store;
        depth_attachment.initial_layout = vk::ImageLayout::eUndefined;
        depth_attachment.final_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vkfw::RenderPassSubpass subpass{};
        subpass.color_attachments.push_back(color_attachment);
        subpass.depth_attachment = depth_attachment;

        vkfw::RenderPassInfo render_pass_info{};
        render_pass_info.subpasses.push_back(subpass);
        render_pass_info.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

        if (!impl_->framegraph_render_pass.Init(ctx, render_pass_info)) {
            impl_->depth_texture.Shutdown(ctx);
            return false;
        }
        if (!impl_->framegraph_framebuffers.Init(ctx, swapchain, impl_->framegraph_render_pass, impl_->depth_texture.View())) {
            impl_->framegraph_render_pass.Shutdown(ctx);
            impl_->depth_texture.Shutdown(ctx);
            return false;
        }

        color_attachment.load_op = vkfw::RenderPassLoadOp::Load;
        depth_attachment.load_op = vkfw::RenderPassLoadOp::Load;
        depth_attachment.initial_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vkfw::RenderPassSubpass load_subpass{};
        load_subpass.color_attachments.push_back(color_attachment);
        load_subpass.depth_attachment = depth_attachment;

        vkfw::RenderPassInfo load_render_pass_info{};
        load_render_pass_info.subpasses.push_back(load_subpass);
        load_render_pass_info.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

        if (!impl_->framegraph_load_render_pass.Init(ctx, load_render_pass_info)) {
            impl_->framegraph_framebuffers.Shutdown(ctx);
            impl_->framegraph_render_pass.Shutdown(ctx);
            impl_->depth_texture.Shutdown(ctx);
            return false;
        }
        if (!impl_->framegraph_load_framebuffers.Init(ctx, swapchain, impl_->framegraph_load_render_pass, impl_->depth_texture.View())) {
            impl_->framegraph_load_render_pass.Shutdown(ctx);
            impl_->framegraph_framebuffers.Shutdown(ctx);
            impl_->framegraph_render_pass.Shutdown(ctx);
            impl_->depth_texture.Shutdown(ctx);
            return false;
        }
    }

    return impl_->framegraph_command_buffers.Init(ctx, vkfw::CommandBufferInfo{
                                                          .level = vkfw::CommandBufferLevel::Primary,
                                                          .usage = vkfw::CommandBufferUsage::OneTimeSubmit,
                                                          .count = sync.FramesInFlight(),
                                                      });
}

void Renderer::ResetFrameGraphRuntimeState(vkfw::VkContext& ctx)
{
    graph_.ResetRuntimeState(&ctx);
    pipeline_system_.Clear();
}

void Renderer::ShutdownFrameGraphBackend()
{
    if (impl_ != nullptr && vk_context_ != nullptr) {
        impl_->framegraph_command_buffers.Shutdown(*vk_context_);
        impl_->framegraph_load_framebuffers.Shutdown(*vk_context_);
        impl_->framegraph_load_render_pass.Shutdown(*vk_context_);
        impl_->framegraph_framebuffers.Shutdown(*vk_context_);
        impl_->framegraph_render_pass.Shutdown(*vk_context_);
        if (impl_->depth_texture.IsInitialized()) {
            impl_->depth_texture.Shutdown(*vk_context_);
        }
    }
}

FrameGraphRenderResult Renderer::RenderFrameGraphFrame(core::FrameData const& frame,
                                                       vkfw::VkContext& ctx,
                                                       vkfw::VkSwapchain& swapchain,
                                                       vkfw::VkFrameSync& sync,
                                                       uint32_t& frame_index)
{
    if (impl_ == nullptr || !impl_->framegraph_command_buffers.IsInitialized()) {
        return FrameGraphRenderResult::Skipped;
    }

    // Ensure context is wired (PipelineSystem/ResourceSystem need this for Vk handles).
    SetVkContext(&ctx);

    sync.WaitForFrame(ctx, frame_index);
    auto [acq_result, image_index] = swapchain.AcquireNextImage(UINT64_MAX, sync.ImageAvailable(frame_index), vk::Fence{});
    if (acq_result == vk::Result::eErrorOutOfDateKHR) {
        LOGW("RenderFrameGraphFrame acquire out-of-date");
        return FrameGraphRenderResult::SwapchainOutOfDate;
    }
    if (acq_result != vk::Result::eSuccess && acq_result != vk::Result::eSuboptimalKHR) {
        LOGW("RenderFrameGraphFrame acquire skipped result=%d", static_cast<int>(acq_result));
        return FrameGraphRenderResult::Skipped;
    }

    sync.ResetFence(ctx, frame_index);
    impl_->framegraph_command_buffers.Reset(frame_index);
    vk::CommandBuffer cmd = impl_->framegraph_command_buffers.Handle(frame_index);

    cmd.begin(vk::CommandBufferBeginInfo{});

    vk::Image const swap_img = swapchain.Image(image_index);
    vk::ImageSubresourceRange const range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    vk::ImageLayout const old_layout = swapchain.IsFirstUse(image_index) ? vk::ImageLayout::eUndefined
                                                                         : vk::ImageLayout::ePresentSrcKHR;

    vk::ImageMemoryBarrier to_color{};
    to_color.oldLayout = old_layout;
    to_color.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image = swap_img;
    to_color.subresourceRange = range;
    to_color.srcAccessMask = {};
    to_color.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, {}, {}, to_color);

    RenderPassContext pass_ctx{};
    pass_ctx.frame = &frame;
    pass_ctx.resources = &resource_system_;
    pass_ctx.pipelines = &pipeline_system_;
    pass_ctx.vk = &ctx;
    pass_ctx.swapchain = &swapchain;
    pass_ctx.swapchain_image_index = image_index;
    pass_ctx.command_buffer = cmd;
    if (!ctx.SupportsDynamicRendering()) {
        pass_ctx.compatibility_render_pass = impl_->framegraph_render_pass.Handle();
        pass_ctx.compatibility_framebuffer = impl_->framegraph_framebuffers.Handle(image_index);
        pass_ctx.compatibility_load_render_pass = impl_->framegraph_load_render_pass.Handle();
        pass_ctx.compatibility_load_framebuffer = impl_->framegraph_load_framebuffers.Handle(image_index);
        pass_ctx.current_depth_texture = &impl_->depth_texture;
    }
    graph_.Execute(pass_ctx);

    vk::ImageMemoryBarrier to_present{};
    to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = swap_img;
    to_present.subresourceRange = range;
    to_present.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    to_present.dstAccessMask = {};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        {}, {}, {}, to_present);

    cmd.end();
    swapchain.MarkUsed(image_index);

    vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    auto image_avail = sync.ImageAvailable(frame_index);
    submit.pWaitSemaphores = &image_avail;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    auto render_finished = sync.RenderFinished(frame_index);
    submit.pSignalSemaphores = &render_finished;
    ctx.GraphicsQueue().submit(submit, sync.InFlightFence(frame_index));

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    auto handle = swapchain.Handle();
    present.pSwapchains = &handle;
    present.pImageIndices = &image_index;
    vk::Result const present_result = ctx.GraphicsQueue().presentKHR(present);

    frame_index = (frame_index + 1) % sync.FramesInFlight();
    if (acq_result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eErrorOutOfDateKHR) {
        return FrameGraphRenderResult::SwapchainOutOfDate;
    }
    return FrameGraphRenderResult::Success;
}

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

} // namespace ave::render
