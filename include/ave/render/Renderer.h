#pragma once

#include "ave/render/RenderTypes.h"
#include "ave/core/FrameData.h"
#include "ave/render/FrameGraph.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/PipelineSystem.h"
#include "ave/render/MaterialSystem.h"

#include <memory>
#include <span>

namespace ave::core {
class JobSystem;
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

    bool Initialize(RendererConfig const& config);
    void Shutdown();
    void Render(core::FrameData const& frame, core::JobSystem& jobs);
    bool InitializeRaster(vkfw::VkContext& ctx,
                          vkfw::VkSwapchain& swapchain,
                          vkfw::VkFrameSync& sync,
                          std::span<RasterColorVertex const> vertices,
                          RasterShaderCode const& shaders);
    bool InitializeRasterMeshResource(vkfw::VkContext& ctx,
                                      vkfw::VkSwapchain& swapchain,
                                      vkfw::VkFrameSync& sync,
                                      uint32_t mesh_id,
                                      RasterShaderCode const& shaders);
    void ShutdownRaster();

    // FrameGraph Vulkan backend (ForwardOpaque bring-up).

    bool InitializeFrameGraphBackend(vkfw::VkContext& ctx,
                                     vkfw::VkSwapchain& swapchain,
                                     vkfw::VkFrameSync& sync);
    void ShutdownFrameGraphBackend();
    void RenderFrameGraphFrame(core::FrameData const& frame,
                               vkfw::VkContext& ctx,
                               vkfw::VkSwapchain& swapchain,
                               vkfw::VkFrameSync& sync,
                               uint32_t& frame_index);

    FrameGraph& Graph() noexcept;
    resource::ResourceSystem& GetResourceSystem() { return resource_system_; }
    PipelineSystem& GetPipelineSystem() { return pipeline_system_; }
    MaterialSystem& GetMaterialSystem() { return material_system_; }
    void SetVkContext(vkfw::VkContext* ctx);

private:
    class Impl;
    vkfw::VkContext* vk_context_ = nullptr;
    FrameGraph graph_;
    resource::ResourceSystem resource_system_;
    PipelineSystem pipeline_system_;
    MaterialSystem material_system_;
    std::unique_ptr<Impl> impl_;
};

} // namespace ave::render
