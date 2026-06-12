#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "ave/render/AndroidSurfaceRenderBackend.h"
#include "ave/render/RenderPasses.h"
#include "ave/render/RenderPass.h"
#include "ave/render/RenderPassCommon.h"
#include "LogUtil.h"

namespace ave::render {

class Renderer::Impl {
public:
    AndroidSurfaceRenderResources surface_resources{};
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
    if (vk_context_ != nullptr) {
        try {
            graph_.ResetRuntimeState(vk_context_);
        } catch (...) {
        }
    }

    try {
        ShutdownFrameGraphBackend();
    } catch (...) {
    }

    try {
        pipeline_system_.Clear();
    } catch (...) {
    }

    detail::ResetCommonSampler();
    detail::ResetUiSampler();
    detail::ResetShadowSampler();
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

bool Renderer::InitializeFrameGraphBackend(vkfw::VkContext& ctx,
                                           vkfw::VkSwapchain& swapchain,
                                           vkfw::VkFrameSync& sync)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    SetVkContext(&ctx);
    auto& surface = impl_->surface_resources;
    surface.image_in_flight_fences.assign(swapchain.ImageCount(), vk::Fence{});

    vk::Extent2D const extent = swapchain.Extent();
    for (auto& depth_texture : surface.depth_textures) {
        if (depth_texture.IsInitialized()) {
            depth_texture.Shutdown(ctx);
        }
    }
    surface.depth_textures.clear();
    surface.depth_textures.resize(swapchain.ImageCount());
    surface.depth_texture_ready.assign(swapchain.ImageCount(), 0u);
    std::vector<vk::ImageView> depth_views;
    depth_views.reserve(swapchain.ImageCount());
    for (auto& depth_texture : surface.depth_textures) {
        if (!depth_texture.Init(ctx, vkfw::TextureInfo{
                                         .width = extent.width,
                                         .height = extent.height,
                                         .mip_levels = 1,
                                         .format = vkfw::TextureFormat::D32_SFLOAT,
                                         .usage = vkfw::TextureUsage::DepthStencilAttachment,
                                         .mipmap = false,
                                     })) {
            for (auto& created_depth : surface.depth_textures) {
                if (created_depth.IsInitialized()) {
                    created_depth.Shutdown(ctx);
                }
            }
            surface.depth_textures.clear();
            surface.depth_texture_ready.clear();
            return false;
        }
        depth_views.push_back(depth_texture.View());
    }

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

        if (!surface.framegraph_render_pass.Init(ctx, render_pass_info)) {
            for (auto& depth_texture : surface.depth_textures) {
                if (depth_texture.IsInitialized()) {
                    depth_texture.Shutdown(ctx);
                }
            }
            surface.depth_textures.clear();
            surface.depth_texture_ready.clear();
            return false;
        }
        if (!surface.framegraph_framebuffers.Init(ctx, swapchain, surface.framegraph_render_pass, depth_views)) {
            surface.framegraph_render_pass.Shutdown(ctx);
            for (auto& depth_texture : surface.depth_textures) {
                if (depth_texture.IsInitialized()) {
                    depth_texture.Shutdown(ctx);
                }
            }
            surface.depth_textures.clear();
            surface.depth_texture_ready.clear();
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

        if (!surface.framegraph_load_render_pass.Init(ctx, load_render_pass_info)) {
            surface.framegraph_framebuffers.Shutdown(ctx);
            surface.framegraph_render_pass.Shutdown(ctx);
            for (auto& depth_texture : surface.depth_textures) {
                if (depth_texture.IsInitialized()) {
                    depth_texture.Shutdown(ctx);
                }
            }
            surface.depth_textures.clear();
            surface.depth_texture_ready.clear();
            return false;
        }
        if (!surface.framegraph_load_framebuffers.Init(ctx, swapchain, surface.framegraph_load_render_pass, depth_views)) {
            surface.framegraph_load_render_pass.Shutdown(ctx);
            surface.framegraph_framebuffers.Shutdown(ctx);
            surface.framegraph_render_pass.Shutdown(ctx);
            for (auto& depth_texture : surface.depth_textures) {
                if (depth_texture.IsInitialized()) {
                    depth_texture.Shutdown(ctx);
                }
            }
            surface.depth_textures.clear();
            surface.depth_texture_ready.clear();
            return false;
        }
    }

    return surface.framegraph_command_buffers.Init(ctx, vkfw::CommandBufferInfo{
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
        auto& surface = impl_->surface_resources;
        surface.framegraph_command_buffers.Shutdown(*vk_context_);
        surface.framegraph_load_framebuffers.Shutdown(*vk_context_);
        surface.framegraph_load_render_pass.Shutdown(*vk_context_);
        surface.framegraph_framebuffers.Shutdown(*vk_context_);
        surface.framegraph_render_pass.Shutdown(*vk_context_);
        for (auto& depth_texture : surface.depth_textures) {
            if (depth_texture.IsInitialized()) {
                depth_texture.Shutdown(*vk_context_);
            }
        }
        surface.depth_textures.clear();
        surface.depth_texture_ready.clear();
        surface.image_in_flight_fences.clear();
    }
}

FrameGraphRenderResult Renderer::RenderFrameGraphFrame(core::FrameData const& frame,
                                                       vkfw::VkContext& ctx,
                                                       vkfw::VkSwapchain& swapchain,
                                                       vkfw::VkFrameSync& sync,
                                                       uint32_t& frame_index)
{
    if (impl_ == nullptr || !impl_->surface_resources.framegraph_command_buffers.IsInitialized()) {
        return FrameGraphRenderResult::Skipped;
    }

    // Ensure context is wired (PipelineSystem/ResourceSystem need this for Vk handles).
    SetVkContext(&ctx);

    AndroidSurfaceRenderBackend backend{impl_->surface_resources, frame, ctx, swapchain, sync, frame_index};
    RenderFrameRequest request{};
    FrameGraphRenderResult const begin_result = backend.BeginFrame(request);
    if (begin_result != FrameGraphRenderResult::Success) {
        return begin_result;
    }

    FrameGraphRenderResult render_result = RenderFrameGraphToTargets(request);
    if (render_result != FrameGraphRenderResult::Success) {
        LOGW("RenderFrameGraphFrame graph execution skipped");
    }
    return backend.EndFrame(render_result);
}

FrameGraphRenderResult Renderer::RenderFrameGraphToTargets(RenderFrameRequest const& request)
{
    if (request.frame == nullptr || request.vk == nullptr ||
        request.command_buffer == vk::CommandBuffer{} || request.views.empty()) {
        return FrameGraphRenderResult::Skipped;
    }

    SetVkContext(request.vk);

    uint32_t const view_count = static_cast<uint32_t>(request.views.size());
    for (uint32_t view_index = 0; view_index < view_count; ++view_index) {
        auto const& target = request.views[view_index];
        if (!target.color_target.IsValid()) {
            return FrameGraphRenderResult::Skipped;
        }

        RenderPassContext pass_ctx{};
        pass_ctx.frame = request.frame;
        pass_ctx.resources = &resource_system_;
        pass_ctx.pipelines = &pipeline_system_;
        pass_ctx.vk = request.vk;
        pass_ctx.command_buffer = request.command_buffer;
        pass_ctx.color_target = target.color_target;
        pass_ctx.depth_target = target.depth_target;
        pass_ctx.view_index = target.view_index;
        pass_ctx.view_count = view_count;
        if (CurrentFrameView(pass_ctx) == nullptr) {
            return FrameGraphRenderResult::Skipped;
        }
        pass_ctx.frame_resource_index = target.frame_resource_index;
        pass_ctx.frame_resource_count = target.frame_resource_count != 0 ? target.frame_resource_count : 1u;
        graph_.Execute(pass_ctx);
    }

    return FrameGraphRenderResult::Success;
}

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

} // namespace ave::render
