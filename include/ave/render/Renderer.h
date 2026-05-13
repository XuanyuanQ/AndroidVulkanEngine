#pragma once

#include "ave/core/FrameData.h"
#include "ave/render/CommandRecorder.h"
#include "ave/render/FrameGraph.h"
#include "ave/render/RenderTypes.h"
#include "ave/resource/GpuUploadQueue.h"

#include <memory>
#include <span>

namespace ave::render {

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

    // bool Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads);
    void Shutdown();
    // void Render(core::FrameData const& frame, core::JobSystem& jobs);
    bool InitializeRaster(vkfw::VkContext& ctx,
                          vkfw::VkSwapchain& swapchain,
                          vkfw::VkFrameSync& sync,
                          std::span<RasterColorVertex const> vertices,
                          RasterShaderCode const& shaders);
    void ShutdownRaster();
    void RenderRasterFrame(vkfw::VkContext& ctx,
                           vkfw::VkSwapchain& swapchain,
                           vkfw::VkFrameSync& sync,
                           uint32_t& frame_index);

    FrameGraph& Graph() noexcept;
    // rhi::VulkanDevice& Device() noexcept;

private:
    class Impl;
    resource::GpuUploadQueue* uploads_ = nullptr;
    // rhi::VulkanDevice device_;
    FrameGraph graph_;
    CommandRecorder recorder_;
    std::unique_ptr<Impl> impl_;
};

} // namespace ave::render
