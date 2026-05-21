#include "ave/render/RenderSceneBuilder.h"
#include "ave/render/RenderWorld.h"
#include "ave/render/MaterialSystem.h"
#include "ave/render/FrameGraph.h"
#include "ave/project/SharedDataContract.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ave::render {

RenderSceneBuilder::RenderSceneBuilder()
{
}

void RenderSceneBuilder::BuildFromScene(project::SceneDocument const& scene, RenderSceneConfig const& config)
{
    render_world_.Clear();
    material_system_.Clear();
    
    // Build default frame graph
    BuildDefaultFrameGraph(config);
    
    // Convert game objects to render objects
    for (auto const& obj : scene.objects) {
        ConvertGameObject(obj);
    }
    
    // Cull and batch render objects
    render_world_.CullAndBatch();
}

void RenderSceneBuilder::ConvertGameObject(project::GameObjectData const& obj)
{
    RenderObject render_obj;
    render_obj.id = obj.id;
    render_obj.name = obj.name;
    render_obj.mesh_id = 0; // TODO: Resolve mesh ID
    render_obj.material_id = 0; // TODO: Resolve material ID
    render_obj.instance_count = 1;
    render_obj.visible = true;
    render_obj.layer_mask = 0xFFFFFFFF;
    render_obj.cast_shadows = false;
    
    // Convert transform to world matrix
    render_obj.world_matrix = glm::mat4(1.0f);
    
    // Set position from transform (if present)
    if (obj.components.transform.has_value()) {
        render_obj.world_matrix[3][0] = obj.components.transform->position.x;
        render_obj.world_matrix[3][1] = obj.components.transform->position.y;
        render_obj.world_matrix[3][2] = obj.components.transform->position.z;
    }
    
    // Set layer mask based on object type
    render_obj.layer_mask = 0xFFFFFFFF; // Default: all layers
    if (obj.components.button.has_value() || obj.components.image.has_value()) {
        render_obj.layer_mask = 0x00000001; // UI layer (legacy builder)
    }
    
    // Determine material ID
    std::string material_name;
    if (obj.components.mesh_renderer.has_value()) {
        material_name = obj.components.mesh_renderer->material;
    } else if (obj.components.triangle_renderer.has_value()) {
        material_name = obj.components.triangle_renderer->material;
    }
    
    if (!material_name.empty()) {
        auto it = material_id_map_.find(material_name);
        if (it != material_id_map_.end()) {
            render_obj.material_id = it->second;
        } else {
            // Register default material if not found
            MaterialConfig default_mat;
            default_mat.name = material_name;
            render_obj.material_id = RegisterMaterial(default_mat);
        }
    }
    
    render_obj.visible = true;
    render_obj.cast_shadows = obj.components.light.has_value() ? obj.components.light->cast_shadows : true;
    
    render_world_.AddRenderObject(render_obj);
}

uint32_t RenderSceneBuilder::RegisterMaterial(MaterialConfig const& config) {
    uint32_t id = next_material_id_++;
    
    Material material;
    material.id = id;
    material.name = config.name;
    material.shader_name = config.shader;
    material.is_pbr = config.is_pbr;
    
    material.params.base_color = config.base_color;
    material.params.metallic = config.metallic;
    material.params.roughness = config.roughness;
    
    material_system_.CreateMaterial(material);
    material_id_map_[config.name] = id;
    
    return id;
}

void RenderSceneBuilder::ConfigureRenderPass(RenderPassConfig const& config) {
    std::unique_ptr<RenderPass> pass;
    
    if (config.type == "DepthPrepass") {
        pass = std::make_unique<DepthPrepass>();
    } else if (config.type == "ShadowPass") {
        pass = std::make_unique<ShadowPass>();
    } else if (config.type == "PBRPass") {
        pass = std::make_unique<PBRPass>();
    } else if (config.type == "ComputePass") {
        pass = std::make_unique<ComputePass>();
    } else if (config.type == "UIPass") {
        pass = std::make_unique<UIPass>();
    } else if (config.type == "ToneMappingPass") {
        pass = std::make_unique<ToneMappingPass>();
    } else {
        throw std::runtime_error("Unknown render pass type: " + config.type);
    }
    
    PassDataFilter filter = CreateFilterFromConfig(config);
    frame_graph_.AddPass(std::move(pass), filter);
}

PassDataFilter RenderSceneBuilder::CreateFilterFromConfig(RenderPassConfig const& config) {
    PassDataFilter filter;
    filter.layer_mask = config.layer_mask;
    filter.material_id = config.material_id_filter;
    filter.light_group = config.light_group;
    filter.opaque_only = config.opaque_only;
    filter.transparent_only = config.transparent_only;
    return filter;
}

void RenderSceneBuilder::BuildDefaultFrameGraph(RenderSceneConfig const& config) {
    // Build default frame graph based on configuration
    if (config.enable_shadows) {
        RenderPassConfig shadow_config;
        shadow_config.type = "ShadowPass";
        shadow_config.light_group = 1; // Shadow-casting lights
        shadow_config.opaque_only = true;
        ConfigureRenderPass(shadow_config);
    }
    
    RenderPassConfig depth_config;
    depth_config.type = "DepthPrepass";
    depth_config.opaque_only = true;
    ConfigureRenderPass(depth_config);
    
    if (config.enable_pbr) {
        RenderPassConfig pbr_config;
        pbr_config.type = "PBRPass";
        pbr_config.layer_mask = 0xFFFFFFFF;
        ConfigureRenderPass(pbr_config);
    }
    
    RenderPassConfig ui_config;
    ui_config.type = "UIPass";
    ui_config.layer_mask = 0x00000001; // UI layer
    ConfigureRenderPass(ui_config);
    
    if (config.enable_post_processing) {
        RenderPassConfig tone_config;
        tone_config.type = "ToneMappingPass";
        ConfigureRenderPass(tone_config);
    }
}

} // namespace ave::render
