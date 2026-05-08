#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ave::core {

struct CameraData {
    float view[16]{};
    float projection[16]{};
};

struct RenderableData {
    std::string debug_name;
    uint32_t mesh_id = 0;
    uint32_t material_id = 0;
    float world[16]{};
};

struct LightData {
    float position[3]{};
    float intensity = 1.0f;
    float color[3]{1.0f, 1.0f, 1.0f};
    float radius = 10.0f;
};

struct FrameData {
    uint64_t frame_index = 0;
    CameraData camera{};
    std::vector<RenderableData> renderables;
    std::vector<LightData> lights;
};

} // namespace ave::core
