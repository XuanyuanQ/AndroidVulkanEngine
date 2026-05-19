#include "ave/render/RenderSceneBuilder.h"

#include <stdexcept>

namespace ave::render {

RenderSceneBuilder::RenderSceneBuilder() {
}

void RenderSceneBuilder::BuildFromScene(project::SceneDocument const& scene, RenderSceneConfig const& config) {
    // Clear existing data
    render_world_.Clear();
    material_id_map_.clear();
    next_material_id_ = 1;

    // Convert all game objects to render objects
    for (auto const& obj : scene.objects) {
        ConvertGameObject(obj);
    }

    // Build default frame graph if no custom configuration
    BuildDefaultFrameGraph(config);
}

void RenderSceneBuilder::ConvertGameObject(project::GameObjectData const& obj) {
    RenderObject render_obj;
    render_obj.id = obj.id;
    render_obj.name = obj.name;
    
    // Convert transform to world matrix
    // For now, use identity matrix - TODO: implement proper transform conversion
    for (int i = 0; i < 16; ++i) {
        render_obj.world_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    
    // Set position from transform (if present)
    if (obj.components.transform.has_value()) {
        render_obj.world_matrix[12] = obj.components.transform->position[0];
        render_obj.world_matrix[13] = obj.components.transform->position[1];
        render_obj.world_matrix[14] = obj.components.transform->position[2];
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
