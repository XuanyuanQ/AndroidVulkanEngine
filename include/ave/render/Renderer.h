#pragma once

#include "ave/core/FrameData.h"
#include "ave/render/RenderBackend.h"
#include "ave/render/FrameGraph.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/PipelineSystem.h"
#include "ave/render/MaterialSystem.h"

#include <memory>

namespace ave::core {
class JobSystem;
}

namespace vkfw {
class VkContext;
class VkSwapchain;
class VkFrameSync;
}

namespace ave::render {

struct AndroidSurfaceRenderResources;

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

    // FrameGraph Vulkan backend (ForwardOpaque bring-up).
    FrameGraphRenderResult RenderFrame(RenderBackend& backend);

    bool InitializeFrameGraphBackend(vkfw::VkContext& ctx,
                                     vkfw::VkSwapchain& swapchain,
                                     vkfw::VkFrameSync& sync);
    void ResetFrameGraphRuntimeState(vkfw::VkContext& ctx);
    void ShutdownFrameGraphBackend();
    FrameGraphRenderResult RenderFrameGraphToTargets(RenderFrameRequest const& request);
    AndroidSurfaceRenderResources* GetAndroidSurfaceRenderResources();

    FrameGraph& Graph() noexcept;
    resource::ResourceSystem& GetResourceSystem() { return resource_system_; }
    resource::ResourceSystem const& GetResourceSystem() const { return resource_system_; }
    PipelineSystem& GetPipelineSystem() { return pipeline_system_; }
    PipelineSystem const& GetPipelineSystem() const { return pipeline_system_; }
    MaterialSystem& GetMaterialSystem() { return material_system_; }
    MaterialSystem const& GetMaterialSystem() const { return material_system_; }
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
