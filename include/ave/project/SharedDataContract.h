#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <cstring>
#include <variant>
#include <vector>

// GLM for math types
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ave::project {

struct TransformData {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct VertexData {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec2 texcoord0{0.0f, 0.0f};
    glm::vec2 texcoord1{0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshData {
    std::string id;
    std::string source;
    std::string topology{"triangleList"};
    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
};

struct TextureData {
    std::string id;
    std::string source;
    std::string color_space{"srgb"};
    std::string usage{"sampled"};
    bool generate_mipmaps = true;
    std::string filter{"linear"};
    std::string wrap_u{"repeat"};
    std::string wrap_v{"repeat"};
};

using MaterialValueData = std::variant<
    bool,
    int32_t,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string>;

struct MaterialParameterData {
    std::string name;
    std::string type;
    MaterialValueData value{0.0f};
};

struct TextureSlotData {
    std::string name;
    std::string texture;
};

struct RenderStateData {
    std::string cull{"back"};
    bool depth_test = true;
    bool depth_write = true;
    std::string blend{"opaque"};
};

struct MaterialData {
    std::string id;
    std::string source;
    std::string shader;
    std::vector<MaterialParameterData> parameters;
    std::vector<TextureSlotData> textures;
    RenderStateData render_state{};
};

struct ScriptBindingData {
    std::string java_class;
    std::string method;
    std::string target_object;
};

struct AssetReferenceData {
    std::string mesh;
    std::string texture;
    std::string shader;
    std::string material;
};

struct RenderFeatureData {
    bool enable_pbr = false;
    bool enable_compute = false;
    bool enable_postfx = false;
};

struct CameraData {
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    std::string clear_flags{"solidColor"};
};

enum class LightType {
    Directional,
    Point,
    Spot,
};

struct LightData {
    LightType type = LightType::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float inner_angle = 20.0f;
    float outer_angle = 35.0f;
    bool cast_shadows = false;
};

struct ImageComponentData {
    std::string texture;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ButtonComponentData {
    std::string target;
    std::string method;
};

struct ProgressBarComponentData {
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
};

struct TriangleRendererData {
    glm::vec4 color{1.0f, 0.2f, 0.1f, 1.0f};
    std::string material;
};

struct MeshRendererData {
    std::string mesh;
    std::string material;
    std::string topology{"triangleList"};
    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
};

struct HierarchyData {
    std::string parent;
    std::vector<std::string> children;
};

struct ComponentData {
    std::optional<TransformData> transform;
    std::optional<TriangleRendererData> triangle_renderer;
    std::optional<MeshRendererData> mesh_renderer;
    std::optional<CameraData> camera;
    std::optional<LightData> light;
    std::optional<ScriptBindingData> script;
    std::optional<ImageComponentData> image;
    std::optional<ButtonComponentData> button;
    std::optional<ProgressBarComponentData> progress_bar;
};

struct GameObjectData {
    std::string id;
    std::string name;
    HierarchyData hierarchy{};
    ComponentData components{};
};

struct EnvironmentData {
    glm::vec4 clear_color{0.03f, 0.04f, 0.06f, 1.0f};
    glm::vec3 ambient_color{0.08f, 0.08f, 0.10f};
};

struct SceneData {
    std::string version{"1"};
    std::string name;
    EnvironmentData environment{};
    std::vector<GameObjectData> objects;
};

struct ProjectData {
    std::string version{"1"};
    std::string name;
    std::string package_name;
    std::string entry_scene;
    std::string orientation{"landscape"};
    std::string render_pipeline{"forward"};
    std::string color_space{"srgb"};
    std::string default_shader_root{"shaders/"};
};

struct SharedDataContract {
    ProjectData project{};
    SceneData scene{};
    std::vector<MaterialData> materials;
    std::vector<ScriptBindingData> scripts;
    std::vector<AssetReferenceData> asset_references;
    RenderFeatureData render_features{};
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
};

} // namespace ave::project