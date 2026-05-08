#pragma once

#include "ave/core/FrameData.h"
#include "ave/render/CommandRecorder.h"
#include "ave/render/FrameGraph.h"
#include "ave/resource/GpuUploadQueue.h"
#include "ave/rhi/VulkanDevice.h"

namespace ave::render {

struct RendererConfig {
    bool enable_validation = true;
};

class Renderer {
public:
    bool Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads);
    void Shutdown();
    void Render(core::FrameData const& frame, core::JobSystem& jobs);

    FrameGraph& Graph() noexcept;
    rhi::VulkanDevice& Device() noexcept;

private:
    resource::GpuUploadQueue* uploads_ = nullptr;
    rhi::VulkanDevice device_;
    FrameGraph graph_;
    CommandRecorder recorder_;
};

} // namespace ave::render
