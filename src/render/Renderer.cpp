#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VkRasterRenderer.hpp"
#include "VkCommandBuffer.hpp"
#include "VkFramebufferSet.hpp"
#include "VkRenderPass.hpp"
#include "ave/resource/GpuUploadQueue.h"
#include "ave/render/RenderPasses.h"
#include "ave/render/RenderPass.h"
#include <android/log.h>

namespace ave::render {

class Renderer::Impl {
public:
    rhi::VulkanRasterRenderer raster_renderer{};
    vkfw::VkCommandBuffer framegraph_command_buffers{};
    vkfw::VkRenderPass framegraph_render_pass{};
    vkfw::VkFramebufferSet framegraph_framebuffers{};
};

Renderer::Renderer()
{
    material_system_.Initialize(&resource_system_);
}

Renderer::~Renderer() = default;

bool Renderer::Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads)
{
    (void)config;
    uploads_ = &uploads;
    return true;
}

void Renderer::Shutdown()
{
    ShutdownRaster();
    uploads_ = nullptr;
}

void Renderer::Render(core::FrameData const& frame, core::JobSystem& jobs)
{
    (void)jobs;
    if (uploads_ != nullptr) {
        auto uploads = uploads_->Drain();
        // TODO: Process uploads through RHI
    }

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
     InitializeFrameGraphBackend(ctx, sync);
     graph_.AddPass(std::make_unique<PBRPass>());
     graph_.AddPass(std::make_unique<ShadowPass>());
     graph_.AddPass(std::make_unique<DepthPrepass>());
     graph_.AddPass(std::make_unique<UIPass>());
    //  graph_.AddPass(std::make_unique<ComputePass>());
    //  resource_system_.GetShaderManager().LoadShaderFromData("mesh_shader", shaders.vertex, shaders.fragment);
     return true;

}

void Renderer::ShutdownRaster()
{
    if (impl_ != nullptr) {
        impl_->raster_renderer.Shutdown();
    }
    impl_.reset();
}

void Renderer::RenderRasterFrame(vkfw::VkContext& ctx,
                                 vkfw::VkSwapchain& swapchain,
                                 vkfw::VkFrameSync& sync,
                                 uint32_t& frame_index)
{
//        core::FrameData empty_frame{};
//        empty_frame.frame_index = frame_index;
//        empty_frame.renderables.push_back(core::FrameRenderableData{
//            .mesh_id = "meshes/viking_room.obj",
//            .material_id = "materials/triangle.material.xml",
//            .shader_id = "solid_triangle",
//            .world = {
//                1.0f, 0.0f, 0.0f, 0.0f,
//                0.0f, 1.0f, 0.0f, 0.0f,
//                0.0f, 0.0f, 1.0f, 0.0f,
//                0.0f, 0.0f, 0.0f, 1.0f,
//            },
//        });
//        RenderFrameGraphFrame(empty_frame, ctx, swapchain, sync, frame_index);
    
}

bool Renderer::InitializeFrameGraphBackend(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    SetVkContext(&ctx);

    return impl_->framegraph_command_buffers.Init(ctx, vkfw::CommandBufferInfo{
                                                          .level = vkfw::CommandBufferLevel::Primary,
                                                          .usage = vkfw::CommandBufferUsage::OneTimeSubmit,
                                                          .count = sync.FramesInFlight(),
                                                      });
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
        vkfw::RenderPassAttachment color_attachment{};
        color_attachment.binding = 0;
        color_attachment.type = vkfw::RenderPassAttachmentType::Color;
        color_attachment.format = swapchain.Format();
        color_attachment.samples = vk::SampleCountFlagBits::e1;
        color_attachment.load_op = vkfw::RenderPassLoadOp::Clear;
        color_attachment.store_op = vkfw::RenderPassStoreOp::Store;
        color_attachment.initial_layout = vk::ImageLayout::eColorAttachmentOptimal;
        color_attachment.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

        vkfw::RenderPassSubpass subpass{};
        subpass.color_attachments.push_back(color_attachment);

        vkfw::RenderPassInfo render_pass_info{};
        render_pass_info.subpasses.push_back(subpass);
        render_pass_info.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

        if (!impl_->framegraph_render_pass.Init(ctx, render_pass_info)) {
            return false;
        }
        if (!impl_->framegraph_framebuffers.Init(ctx, swapchain, impl_->framegraph_render_pass)) {
            impl_->framegraph_render_pass.Shutdown(ctx);
            return false;
        }
    }

    return impl_->framegraph_command_buffers.Init(ctx, vkfw::CommandBufferInfo{
                                                          .level = vkfw::CommandBufferLevel::Primary,
                                                          .usage = vkfw::CommandBufferUsage::OneTimeSubmit,
                                                          .count = sync.FramesInFlight(),
                                                      });
}

void Renderer::ShutdownFrameGraphBackend()
{
    if (impl_ != nullptr && vk_context_ != nullptr) {
        impl_->framegraph_command_buffers.Shutdown(*vk_context_);
        impl_->framegraph_framebuffers.Shutdown(*vk_context_);
        impl_->framegraph_render_pass.Shutdown(*vk_context_);
    }
}

void Renderer::RenderFrameGraphFrame(core::FrameData const& frame,
                                     vkfw::VkContext& ctx,
                                     vkfw::VkSwapchain& swapchain,
                                     vkfw::VkFrameSync& sync,
                                     uint32_t& frame_index)
{
    if (impl_ == nullptr || !impl_->framegraph_command_buffers.IsInitialized()) {
        return;
    }

    // Ensure context is wired (PipelineSystem/ResourceSystem need this for Vk handles).
    SetVkContext(&ctx);

    sync.WaitForFrame(ctx, frame_index);
    __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "frame_index: %u", frame_index);
    auto [acq_result, image_index] = swapchain.AcquireNextImage(UINT64_MAX, sync.ImageAvailable(frame_index), vk::Fence{});
    if (acq_result == vk::Result::eErrorOutOfDateKHR) {
        return;
    }
    if (acq_result != vk::Result::eSuccess && acq_result != vk::Result::eSuboptimalKHR) {
        return;
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

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.03f;
    clear.color.float32[1] = 0.04f;
    clear.color.float32[2] = 0.06f;
    clear.color.float32[3] = 1.0f;

    if (ctx.SupportsDynamicRendering()) {
        vk::RenderingAttachmentInfo color_attachment{};
        color_attachment.imageView = swapchain.ImageView(image_index);
        color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        color_attachment.clearValue = clear;

        vk::RenderingInfo rendering_info{};
        rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        cmd.beginRendering(rendering_info);
    } else {
        vk::RenderPassBeginInfo render_pass_begin{};
        render_pass_begin.renderPass = impl_->framegraph_render_pass.Handle();
        render_pass_begin.framebuffer = impl_->framegraph_framebuffers.Handle(image_index);
        render_pass_begin.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
        render_pass_begin.clearValueCount = 1;
        render_pass_begin.pClearValues = &clear;
        cmd.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    }

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
    }
    graph_.Execute(pass_ctx);

    if (ctx.SupportsDynamicRendering()) {
        cmd.endRendering();
    } else {
        cmd.endRenderPass();
    }

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
    ctx.GraphicsQueue().presentKHR(present);

    frame_index = (frame_index + 1) % sync.FramesInFlight();
}

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

} // namespace ave::render
