#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>

namespace ave::render {

// Scene configuration for rendering
struct RenderSceneConfig {
    bool enable_shadows = true;
    bool enable_pbr = true;
    bool enable_post_processing = true;
    uint32_t shadow_map_size = 1024;
    uint32_t max_lights = 8;
};

// Material configuration from XML
struct MaterialConfig {
    std::string name;
    std::string shader;
    glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool is_pbr = true;
};

// Render pass configuration from XML
struct RenderPassConfig {
    std::string type; // "DepthPrepass", "ShadowPass", "PBRPass", "ComputePass", "UIPass", "ToneMappingPass"
    uint32_t layer_mask = 0xFFFFFFFF;
    std::optional<std::string> material_id_filter{};
    uint32_t light_group = 0;
    bool opaque_only = false;
    bool transparent_only = false;
};

// Builder to convert XML scene data to render system structures
class RenderSceneBuilder {
public:
    RenderSceneBuilder();
    ~RenderSceneBuilder() = default;

    // Build render world from scene document
    void BuildFromScene(project::SceneDocument const& scene, RenderSceneConfig const& config);

    // Get built components
    RenderWorld& GetRenderWorld() { return render_world_; }
    MaterialSystem& GetMaterialSystem() { return material_system_; }
    FrameGraph& GetFrameGraph() { return frame_graph_; }

    // Register material from config
    uint32_t RegisterMaterial(MaterialConfig const& config);

    // Configure render pass from XML
    void ConfigureRenderPass(RenderPassConfig const& config);

private:
    void ConvertGameObject(project::GameObjectData const& obj);
    void BuildDefaultFrameGraph(RenderSceneConfig const& config);
    PassDataFilter CreateFilterFromConfig(RenderPassConfig const& config);

    RenderWorld render_world_;
    MaterialSystem material_system_;
    FrameGraph frame_graph_;

    std::unordered_map<std::string, uint32_t> material_id_map_;
    uint32_t next_material_id_ = 1;
};

} // namespace ave::render
