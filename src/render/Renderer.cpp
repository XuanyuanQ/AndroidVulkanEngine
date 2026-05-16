#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VkRasterRenderer.hpp"
#include "VkRenderPass.hpp"
#include "ave/resource/GpuUploadQueue.h"

namespace ave::render {

class Renderer::Impl {
public:
    rhi::VulkanRasterRenderer raster_renderer{};
    vkfw::VkRenderPass raster_render_pass{};
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

    // Ensure context is wired (ResourceSystem/PipelineSystem depend on this)
    SetVkContext(&ctx);

    // Upload mesh through ResourceSystem so the demo path matches the engine architecture.
    std::vector<float> packed_vertices;
    packed_vertices.reserve(vertices.size() * 7);
    for (auto const& v : vertices) {
        packed_vertices.push_back(v.position[0]);
        packed_vertices.push_back(v.position[1]);
        packed_vertices.push_back(v.position[2]);
        packed_vertices.push_back(v.color[0]);
        packed_vertices.push_back(v.color[1]);
        packed_vertices.push_back(v.color[2]);
        packed_vertices.push_back(v.color[3]);
    }
    std::vector<uint32_t> indices; // non-indexed draw for the demo
    uint32_t const mesh_id = resource_system_.GetMeshManager().LoadMeshFromData(
        "raster_demo_mesh", packed_vertices, indices, /*vertex_stride*/ 7);
    auto const* mesh = resource_system_.GetMeshManager().GetMesh(mesh_id);
    if (!mesh || !mesh->vertex_buffer) {
        impl_.reset();
        return false;
    }

    // Upload shaders through ResourceSystem
    uint32_t const shader_id = resource_system_.GetShaderManager().LoadShaderFromData(
        "raster_demo_shader", shaders.vertex, shaders.fragment, "main");

    // Create a simple swapchain-compatible render pass (same as the legacy raster demo path)
    vkfw::RenderPassInfo render_pass_info{};
    vkfw::RenderPassSubpass subpass{};
    vkfw::RenderPassAttachment color_attachment{};
    color_attachment.binding = 0;
    color_attachment.type = vkfw::RenderPassAttachmentType::Color;
    color_attachment.format = swapchain.Format();
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.load_op = vkfw::RenderPassLoadOp::Clear;
    color_attachment.store_op = vkfw::RenderPassStoreOp::Store;
    color_attachment.initial_layout = vk::ImageLayout::eUndefined;
    color_attachment.final_layout = vk::ImageLayout::ePresentSrcKHR;
    subpass.color_attachments.push_back(color_attachment);
    render_pass_info.subpasses.push_back(subpass);
    render_pass_info.final_layout = vk::ImageLayout::ePresentSrcKHR;

    if (!impl_->raster_render_pass.Init(ctx, render_pass_info)) {
        impl_.reset();
        return false;
    }

    // Create graphics pipeline via PipelineSystem
    PipelineKey pipeline_key{};
    pipeline_key.pass_id = 0;
    pipeline_key.shader_id = shader_id;
    pipeline_key.vertex_layout_id = 1; // RasterColorVertex (position/color)
    pipeline_key.render_state_id = 1;
    pipeline_key.layout_profile = 0; // demo shader uses no descriptor sets
    pipeline_key.rt_format = static_cast<uint32_t>(swapchain.Format());
    pipeline_key.render_pass = reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(impl_->raster_render_pass.Handle()));
    pipeline_key.viewport_width = swapchain.Extent().width;
    pipeline_key.viewport_height = swapchain.Extent().height;

    uint32_t const pipeline_id = pipeline_system_.GetPipelineCache().GetOrCreatePipeline(pipeline_key);
    if (pipeline_id == 0) {
        impl_.reset();
        return false;
    }
    auto const* pipeline = pipeline_system_.GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        impl_.reset();
        return false;
    }

    // Let the raster renderer manage swapchain/renderpass/framebuffers/command buffers,
    // but use external mesh/pipeline resources.
    if (!impl_->raster_renderer.InitializeWithExternalResources(
            ctx,
            swapchain,
            sync,
            mesh->vertex_buffer.get(),
            mesh->vertex_count,
            pipeline,
            &impl_->raster_render_pass)) {
        impl_.reset();
        return false;
    }

    return true;
}

void Renderer::ShutdownRaster()
{
    if (impl_ != nullptr) {
        impl_->raster_renderer.Shutdown();
        if (vk_context_ != nullptr) {
            impl_->raster_render_pass.Shutdown(*vk_context_);
        }
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

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

} // namespace ave::render
