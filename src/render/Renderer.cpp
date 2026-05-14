#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VulkanRasterRenderer.hpp"

namespace ave::render {

class Renderer::Impl {
public:
    rhi::VulkanRasterRenderer raster_renderer{};
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
    if (uploads_ != nullptr) {
        (void)uploads_->Drain();
    }

    (void)recorder_.RecordSceneParallel(frame, jobs);
    graph_.Execute(RenderPassContext{&frame});
}

bool Renderer::InitializeRaster(vkfw::VkContext& ctx,
                                vkfw::VkSwapchain& swapchain,
                                vkfw::VkFrameSync& sync,
                                std::span<RasterColorVertex const> vertices,
                                RasterShaderCode const& shaders)
{
    ShutdownRaster();
    impl_ = std::make_unique<Impl>();

    std::vector<rhi::RasterColorVertex> rhi_vertices;
    rhi_vertices.reserve(vertices.size());
    for (auto const& vertex : vertices) {
        rhi::RasterColorVertex converted{};
        converted.position = vertex.position;
        converted.color = vertex.color;
        rhi_vertices.push_back(converted);
    }

    rhi::RasterShaderCode rhi_shaders{};
    rhi_shaders.vertex = shaders.vertex;
    rhi_shaders.fragment = shaders.fragment;

    if (!impl_->raster_renderer.Initialize(ctx, swapchain, sync, rhi_vertices, rhi_shaders)) {
        impl_.reset();
        return false;
    }

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
