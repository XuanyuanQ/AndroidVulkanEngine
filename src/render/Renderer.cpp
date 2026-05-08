#include "ave/render/Renderer.h"

namespace ave::render {

bool Renderer::Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads)
{
    uploads_ = &uploads;
    return device_.Initialize(rhi::VulkanDeviceConfig{config.enable_validation});
}

void Renderer::Shutdown()
{
    device_.Shutdown();
    uploads_ = nullptr;
}

void Renderer::Render(core::FrameData const& frame, core::JobSystem& jobs)
{
    if (uploads_ != nullptr) {
        auto uploads = uploads_->Drain();
        for (auto const& upload : uploads) {
            device_.SubmitDebugWork(upload.debug_name, 1);
        }
    }

    auto recorded = recorder_.RecordSceneParallel(frame, jobs);
    device_.SubmitDebugWork("FrameData secondary command buffers", static_cast<uint32_t>(recorded.size()));
    graph_.Execute(RenderPassContext{&frame});
}

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

rhi::VulkanDevice& Renderer::Device() noexcept
{
    return device_;
}

} // namespace ave::render
