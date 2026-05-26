#pragma once

#include "ave/core/FrameData.h"
#include "ave/core/JobSystem.h"
#include "ave/core/Profiler.h"
#include "ave/render/Renderer.h"
#include "ave/resource/AssetManager.h"
#include "ave/scene/SceneWorld.h"

#include <cstdint>

namespace ave::core {

struct EngineConfig {
    uint32_t worker_threads = 0;
    bool enable_validation = true;
};

class Engine {
public:
    bool Initialize(EngineConfig const& config);
    void Shutdown();
    void Tick(float delta_seconds);

    scene::SceneWorld& Scene() noexcept;
    resource::AssetManager& Assets() noexcept;
    render::Renderer& Renderer() noexcept;
    JobSystem& Jobs() noexcept;
    Profiler& GetProfiler() noexcept;

private:
    uint64_t frame_index_ = 0;
    JobSystem jobs_;
    Profiler profiler_;
    scene::SceneWorld scene_;
    resource::AssetManager assets_;
    render::Renderer renderer_;
};

} // namespace ave::core
