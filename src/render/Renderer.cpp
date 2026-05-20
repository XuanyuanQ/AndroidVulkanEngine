#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VkRasterRenderer.hpp"
#include "VkCommandBuffer.hpp"
#include "ave/resource/GpuUploadQueue.h"

#include <algorithm>

namespace ave::render {

namespace {

std::array<float, 3> TransformPreviewPosition(std::array<float, 3> const& position)
{
    return {
        position[0],
        position[2],
        -position[1],
    };
}

} // namespace

class Renderer::Impl {
public:
    rhi::VulkanRasterRenderer raster_renderer{};
    vkfw::VkCommandBuffer framegraph_command_buffers{};
};

Renderer::Renderer() = default;
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

bool Renderer::InitializeRasterModel(vkfw::VkContext& ctx,
                                     vkfw::VkSwapchain& swapchain,
                                     vkfw::VkFrameSync& sync,
                                     project::MeshData const& mesh,
                                     RasterShaderCode const& shaders)
{
    std::vector<RasterColorVertex> preview_vertices;
    if (mesh.vertices.empty()) {
        return false;
    }

    std::vector<uint32_t> draw_indices;
    if (!mesh.indices.empty()) {
        draw_indices = mesh.indices;
    } else {
        draw_indices.resize(mesh.vertices.size());
        for (uint32_t i = 0; i < draw_indices.size(); ++i) {
            draw_indices[i] = i;
        }
    }

    preview_vertices.reserve(draw_indices.size());

    std::vector<std::array<float, 3>> positions;
    positions.reserve(mesh.vertices.size());
    for (auto const& vertex : mesh.vertices) {
        positions.push_back(TransformPreviewPosition(vertex.position));
    }
    bool const has_any_uv = std::any_of(
        mesh.vertices.begin(),
        mesh.vertices.end(),
        [](project::VertexData const& vertex) {
            return vertex.texcoord0 != std::array<float, 2>{0.0f, 0.0f};
        });

    auto min_pos = positions.front();
    auto max_pos = positions.front();
    for (auto const& position : positions) {
        for (int i = 0; i < 3; ++i) {
            min_pos[i] = std::min(min_pos[i], position[i]);
            max_pos[i] = std::max(max_pos[i], position[i]);
        }
    }

    std::array<float, 3> center{
        (min_pos[0] + max_pos[0]) * 0.5f,
        (min_pos[1] + max_pos[1]) * 0.5f,
        (min_pos[2] + max_pos[2]) * 0.5f,
    };
    float const extent_x = max_pos[0] - min_pos[0];
    float const extent_y = max_pos[1] - min_pos[1];
    float const extent_z = max_pos[2] - min_pos[2];
    float const max_extent = std::max({extent_x, extent_y, extent_z, 0.0001f});
    float const scale = 1.6f / max_extent;

    for (uint32_t vertex_index : draw_indices) {
        if (vertex_index >= mesh.vertices.size()) {
            continue;
        }

        RasterColorVertex raster_vertex{};
        auto const& position = positions[vertex_index];
        raster_vertex.position = {
            (position[0] - center[0]) * scale,
            (position[1] - center[1]) * scale,
            (position[2] - center[2]) * scale,
        };

        auto const& source_vertex = mesh.vertices[vertex_index];
        if (has_any_uv) {
            auto const& uv = source_vertex.texcoord0;
            raster_vertex.color = {
                std::clamp(uv[0], 0.0f, 1.0f),
                std::clamp(uv[1], 0.0f, 1.0f),
                std::clamp(1.0f - uv[0], 0.0f, 1.0f),
                1.0f,
            };
        } else {
            raster_vertex.color = {0.85f, 0.82f, 0.78f, 1.0f};
        }

        preview_vertices.push_back(raster_vertex);
    }

    return InitializeRaster(ctx, swapchain, sync, preview_vertices, shaders);
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
    if (impl_ == nullptr) {
        return;
    }

    impl_->raster_renderer.RenderFrame(ctx, swapchain, sync, frame_index);
}

bool Renderer::InitializeFrameGraphBackend(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    SetVkContext(&ctx);

    // Allocate one primary command buffer per frame-in-flight.
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

    bool const core_dynamic_rendering = ctx.PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (core_dynamic_rendering) {
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
        vk::RenderingAttachmentInfoKHR color_attachment{};
        color_attachment.imageView = swapchain.ImageView(image_index);
        color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        color_attachment.clearValue = clear;

        vk::RenderingInfoKHR rendering_info{};
        rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        cmd.beginRenderingKHR(rendering_info);
    }

    RenderPassContext pass_ctx{};
    pass_ctx.frame = &frame;
    pass_ctx.resources = &resource_system_;
    pass_ctx.pipelines = &pipeline_system_;
    pass_ctx.vk = &ctx;
    pass_ctx.swapchain = &swapchain;
    pass_ctx.swapchain_image_index = image_index;
    pass_ctx.command_buffer = cmd;
    graph_.Execute(pass_ctx);

    if (core_dynamic_rendering) {
        cmd.endRendering();
    } else {
        cmd.endRenderingKHR();
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
