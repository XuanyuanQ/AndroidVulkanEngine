#pragma once

#include "ave/core/FrameData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ave::scene {

class SceneWorld {
public:
    uint32_t AddRenderable(std::string object_id, std::string debug_name, std::string mesh_id, std::string material_id);
    uint32_t AddPointLight(float x, float y, float z, float intensity);
    void BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const;

private:
    std::vector<core::FrameRenderableData> renderables_;
    std::vector<core::FrameLightData> lights_;
};

} // namespace ave::scene
