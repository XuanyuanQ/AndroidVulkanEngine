#pragma once

#include <array>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace ave::render {

struct TriangleDrawItem {
    std::string object_id;
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec4 color{1.0f, 0.2f, 0.1f, 1.0f};
};

struct RenderScene {
    std::vector<TriangleDrawItem> triangles;
};

struct RasterColorVertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct RasterShaderCode {
    std::vector<uint32_t> vertex;
    std::vector<uint32_t> fragment;
};

} // namespace ave::render
