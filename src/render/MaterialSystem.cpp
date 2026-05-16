#include "ave/render/MaterialSystem.h"

namespace ave::render {

MaterialSystem::MaterialSystem() = default;

uint32_t MaterialSystem::CreateMaterial(Material const& material)
{
    uint32_t id = next_id_++;
    materials_.push_back(material);
    materials_.back().id = id;
    name_to_id_[material.name] = id;
    return id;
}

uint32_t MaterialSystem::CreateMaterialInstance(uint32_t base_material_id, 
                                                std::string const& instance_name,
                                                MaterialTextures const& texture_overrides)
{
    // Check if base material exists
    if (base_material_id >= materials_.size() || base_material_id == 0) {
        return 0;
    }
    
    uint32_t id = next_instance_id_++;
    MaterialInstance instance;
    instance.id = id;
    instance.base_material_id = base_material_id;
    instance.name = instance_name;
    instance.texture_overrides = texture_overrides;
    
    material_instances_[id] = instance;
    return id;
}

bool MaterialSystem::UpdateMaterial(uint32_t id, Material const& material)
{
    if (id >= materials_.size() || id == 0) {
        return false;
    }
    materials_[id] = material;
    materials_[id].id = id;
    name_to_id_[material.name] = id;
    return true;
}

bool MaterialSystem::UpdateMaterialInstance(uint32_t instance_id, MaterialTextures const& texture_overrides)
{
    auto it = material_instances_.find(instance_id);
    if (it == material_instances_.end()) {
        return false;
    }
    it->second.texture_overrides = texture_overrides;
    return true;
}

Material const* MaterialSystem::GetMaterial(uint32_t id) const
{
    // Check if it's a material instance
    auto instance_it = material_instances_.find(id);
    if (instance_it != material_instances_.end()) {
        // Return base material with instance overrides applied
        uint32_t base_id = instance_it->second.base_material_id;
        if (base_id > 0 && base_id <= materials_.size()) {
            // Create a copy of the base material with overrides applied
            static thread_local Material temp_material;
            temp_material = materials_[base_id - 1];
            
            // Apply texture overrides
            if (!instance_it->second.texture_overrides.base_color_texture.empty()) {
                temp_material.textures.base_color = instance_it->second.texture_overrides.base_color_texture;
            }
            if (!instance_it->second.texture_overrides.normal_texture.empty()) {
                temp_material.textures.normal = instance_it->second.texture_overrides.normal_texture;
            }
            if (!instance_it->second.texture_overrides.metallic_roughness_texture.empty()) {
                temp_material.textures.metallic_roughness = instance_it->second.texture_overrides.metallic_roughness_texture;
            }
            
            // Apply parameter overrides
            if (instance_it->second.param_overrides) {
                temp_material.params = *instance_it->second.param_overrides;
            }
            
            return &temp_material;
        }
        return nullptr;
    }
    
    // Check if it's a base material
    if (id > 0 && id <= materials_.size()) {
        return &materials_[id - 1];
    }
    return nullptr;
}

MaterialInstance const* MaterialSystem::GetMaterialInstance(uint32_t id) const
{
    auto it = material_instances_.find(id);
    if (it != material_instances_.end()) {
        return &it->second;
    }
    return nullptr;
}

Material const* MaterialSystem::GetMaterial(std::string const& name) const
{
    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
        return GetMaterial(it->second);
    }
    return nullptr;
}

void MaterialSystem::RemoveMaterial(uint32_t id)
{
    if (id > 0 && id <= materials_.size()) {
        name_to_id_.erase(materials_[id - 1].name);
        materials_.erase(materials_.begin() + id - 1);
    }
}

void MaterialSystem::RemoveMaterialInstance(uint32_t id)
{
    material_instances_.erase(id);
}

void MaterialSystem::Clear()
{
    materials_.clear();
    material_instances_.clear();
    name_to_id_.clear();
    next_id_ = 1;
    next_instance_id_ = 10000;
}

void MaterialSystem::SetBaseColor(uint32_t material_id, std::array<float, 4> const& color) {
    auto* material = GetMaterial(material_id);
    if (material) {
        material->params.base_color = color;
    }
}

void MaterialSystem::SetMetallic(uint32_t material_id, float metallic) {
    auto* material = GetMaterial(material_id);
    if (material) {
        material->params.metallic = metallic;
    }
}

void MaterialSystem::SetRoughness(uint32_t material_id, float roughness) {
    auto* material = GetMaterial(material_id);
    if (material) {
        material->params.roughness = roughness;
    }
}

void MaterialSystem::SetTexture(uint32_t material_id, std::string const& texture_type, uint32_t texture_id) {
    auto* material = GetMaterial(material_id);
    if (!material) {
        return;
    }

    if (texture_type == "base_color") {
        material->textures.base_color_texture = texture_id;
    } else if (texture_type == "metallic_roughness") {
        material->textures.metallic_roughness_texture = texture_id;
    } else if (texture_type == "normal") {
        material->textures.normal_texture = texture_id;
    } else if (texture_type == "occlusion") {
        material->textures.occlusion_texture = texture_id;
    } else if (texture_type == "emissive") {
        material->textures.emissive_texture = texture_id;
    }
}

void MaterialSystem::Clear() {
    materials_.clear();
    name_to_id_.clear();
    next_id_ = 1;
}

} // namespace ave::render
