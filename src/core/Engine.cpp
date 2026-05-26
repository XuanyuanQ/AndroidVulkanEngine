#include "ave/core/Engine.h"

namespace ave::core {

bool Engine::Initialize(EngineConfig const& config)
{
    jobs_.Start(config.worker_threads);

    if (!renderer_.Initialize(render::RendererConfig{config.enable_validation})) {
        jobs_.Stop();
        return false;
    }

    assets_.Initialize(jobs_);
    return true;
}

void Engine::Shutdown()
{
    jobs_.WaitIdle();
    renderer_.Shutdown();
    jobs_.Stop();
}

void Engine::Tick(float)
{
    profiler_.BeginFrame();
    Profiler::Scope frame_scope(profiler_, "Engine::Tick");

    FrameData frame{};
    scene_.BuildFrameData(frame_index_++, frame);
    renderer_.Render(frame, jobs_);
}

scene::SceneWorld& Engine::Scene() noexcept
{
    return scene_;
}

resource::AssetManager& Engine::Assets() noexcept
{
    return assets_;
}

render::Renderer& Engine::Renderer() noexcept
{
    return renderer_;
}

JobSystem& Engine::Jobs() noexcept
{
    return jobs_;
}

Profiler& Engine::GetProfiler() noexcept
{
    return profiler_;
}

} // namespace ave::core
