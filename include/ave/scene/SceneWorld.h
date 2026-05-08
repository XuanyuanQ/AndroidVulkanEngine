#pragma once

#include "ave/core/FrameData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ave::scene {

class SceneWorld {
public:
    uint32_t AddRenderable(std::string debug_name, uint32_t mesh_id, uint32_t material_id);
    uint32_t AddPointLight(float x, float y, float z, float intensity);
    void BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const;

private:
    std::vector<core::RenderableData> renderables_;
    std::vector<core::LightData> lights_;
};

} // namespace ave::scene
