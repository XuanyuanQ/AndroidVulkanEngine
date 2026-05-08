#pragma once

#include <array>
#include <string>
#include <vector>

namespace ave::project {

struct TransformData {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct TriangleRendererData {
    std::array<float, 4> color{1.0f, 0.2f, 0.1f, 1.0f};
    std::string material;
};

struct VertexData {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshRendererData {
    std::string material;
    std::vector<VertexData> vertices;
};

struct ScriptData {
    std::string java_class;
};

struct ButtonData {
    std::string target;
    std::string method;
};

struct GameObjectData {
    std::string id;
    std::string name;
    std::string parent;
    TransformData transform{};
    bool has_triangle = false;
    TriangleRendererData triangle{};
    bool has_mesh = false;
    MeshRendererData mesh{};
    bool has_script = false;
    ScriptData script{};
    bool has_button = false;
    ButtonData button{};
};

struct SceneDocument {
    std::string name;
    std::vector<GameObjectData> objects;
};

} // namespace ave::project
