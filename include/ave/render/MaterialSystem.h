#pragma once

#include "ave/render/RenderTypes.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <optional>

namespace ave::render {

// PBR material properties
struct PBRMaterialParams {
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float emissive_factor[3] = {0.0f, 0.0f, 0.0f};
    float alpha_cutoff = 0.5f;
    bool alpha_mask = false;
    bool double_sided = false;
};

// Texture references
struct MaterialTextures {
    uint32_t base_color_texture = 0;
    uint32_t metallic_roughness_texture = 0;
    uint32_t normal_texture = 0;
    uint32_t occlusion_texture = 0;
    uint32_t emissive_texture = 0;
};

// Base material definition (template)
struct Material {
    uint32_t id = 0;
    std::string name;
    std::string shader_name;
    PBRMaterialParams params;
    MaterialTextures textures;
    bool is_pbr = true;
};

// Material instance (can override textures from base material)
struct MaterialInstance {
    uint32_t id = 0;
    uint32_t base_material_id = 0; // Reference to base material
    std::string name;
    
    // Override textures (0 = use base material texture)
    MaterialTextures texture_overrides;
    
    // Override parameters (optional)
    std::optional<PBRMaterialParams> param_overrides;
};

// Material system manages all materials and instances in the scene
class MaterialSystem {
public:
    MaterialSystem();
    ~MaterialSystem() = default;

    // Create base material from definition
    uint32_t CreateMaterial(Material const& material);
    
    // Create material instance from base material
    uint32_t CreateMaterialInstance(uint32_t base_material_id, 
                                    std::string const& instance_name,
                                    MaterialTextures const& texture_overrides = {});
    
    // Update material parameters
    bool UpdateMaterial(uint32_t id, Material const& material);
    
    // Update material instance
    bool UpdateMaterialInstance(uint32_t instance_id, MaterialTextures const& texture_overrides);
    
    // Get material by ID (returns base material or instance)
    Material const* GetMaterial(uint32_t id) const;
    
    // Get material instance by ID
    MaterialInstance const* GetMaterialInstance(uint32_t id) const;
    
    // Get base material by name
    Material const* GetMaterial(std::string const& name) const;
    
    // Remove material
    void RemoveMaterial(uint32_t id);
    
    // Remove material instance
    void RemoveMaterialInstance(uint32_t id);
    
    // Clear all materials and instances
    void Clear();

private:
    std::vector<Material> materials_;
    std::unordered_map<std::string, uint32_t> name_to_id_;
    uint32_t next_id_ = 1;
};

} // namespace ave::render
