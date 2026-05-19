#pragma once

#include "ave/render/RenderTypes.h"
#include "ave/core/FrameData.h"
#include "ave/render/FrameGraph.h"
#include "ave/render/CommandRecorder.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/PipelineSystem.h"

#include <memory>
#include <span>

namespace ave::core {
class JobSystem;
}

namespace ave::resource {
class GpuUploadQueue;
}

namespace vkfw {
class VkContext;
class VkSwapchain;
class VkFrameSync;
}

namespace ave::render {

struct RendererConfig {
    bool enable_validation = true;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads);
    void Shutdown();
    void Render(core::FrameData const& frame, core::JobSystem& jobs);
    bool InitializeRaster(vkfw::VkContext& ctx,
                          vkfw::VkSwapchain& swapchain,
                          vkfw::VkFrameSync& sync,
                          std::span<RasterColorVertex const> vertices,
                          RasterShaderCode const& shaders);
    bool InitializeRasterModel(vkfw::VkContext& ctx,
                               vkfw::VkSwapchain& swapchain,
                               vkfw::VkFrameSync& sync,
                               std::span<resource::ObjMeshVertex const> vertices,
                               RasterShaderCode const& shaders);
    void ShutdownRaster();
    void RenderRasterFrame(vkfw::VkContext& ctx,
                           vkfw::VkSwapchain& swapchain,
                           vkfw::VkFrameSync& sync,
                           uint32_t& frame_index);

    FrameGraph& Graph() noexcept;
    resource::ResourceSystem& GetResourceSystem() { return resource_system_; }
    PipelineSystem& GetPipelineSystem() { return pipeline_system_; }
    void SetVkContext(vkfw::VkContext* ctx);

private:
    class Impl;
    resource::GpuUploadQueue* uploads_ = nullptr;
    vkfw::VkContext* vk_context_ = nullptr;
    FrameGraph graph_;
    CommandRecorder recorder_;
    resource::ResourceSystem resource_system_;
    PipelineSystem pipeline_system_;
    std::unique_ptr<Impl> impl_;
};

} // namespace ave::render
