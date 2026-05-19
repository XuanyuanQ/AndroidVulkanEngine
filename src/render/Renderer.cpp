#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "VkRasterRenderer.hpp"
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
                                     std::span<resource::ObjMeshVertex const> vertices,
                                     RasterShaderCode const& shaders)
{
    std::vector<RasterColorVertex> preview_vertices;
    if (vertices.empty()) {
        return false;
    }

    preview_vertices.reserve(vertices.size());

    std::vector<std::array<float, 3>> positions;
    positions.reserve(vertices.size());
    for (auto const& vertex : vertices) {
        positions.push_back(TransformPreviewPosition(vertex.position));
    }

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

    for (size_t i = 0; i < vertices.size(); ++i) {
        RasterColorVertex raster_vertex{};
        auto const& position = positions[i];
        raster_vertex.position = {
            (position[0] - center[0]) * scale,
            (position[1] - center[1]) * scale,
            (position[2] - center[2]) * scale,
        };

        if (vertices[i].has_texcoord) {
            auto const& uv = vertices[i].texcoord;
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

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

} // namespace ave::render
