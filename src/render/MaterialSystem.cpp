#include "ave/render/MaterialSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/project/XmlSceneLoader.h"
#include <android/log.h>
#include <filesystem>

namespace ave::render {

namespace {

bool LooksLikeProjectAssetRootPath(std::filesystem::path const& path)
{
    auto it = path.begin();
    if (it == path.end()) {
        return false;
    }

    auto const first = it->generic_string();
    return first == "textures"
        || first == "materials"
        || first == "meshes"
        || first == "shaders"
        || first == "scenes"
        || first == "assets"
        || first == "compiled_shaders";
}

} // namespace

MaterialSystem::MaterialSystem() = default;

void MaterialSystem::Initialize(resource::ResourceSystem* resource_system)
{
    resource_system_ = resource_system;
}

uint32_t MaterialSystem::LoadMaterial(std::string const& path)
{
    // Check if already loaded
    auto it = name_to_id_.find(path);
    if (it != name_to_id_.end()) {
        return it->second;
    }

    if (!text_asset_loader_) {
        return 0;
    }

    std::string text = text_asset_loader_(path);
    if (text.empty()) {
        return 0;
    }

    ave::project::XmlSceneLoader loader;
    auto const mat_doc = loader.LoadMaterialText(text);

    Material logical_mat{};
    logical_mat.name = path;
    logical_mat.shader_name = mat_doc.shader;
    logical_mat.params.base_color = mat_doc.base_color;
    if (!mat_doc.base_color_texture.empty()) {
        std::filesystem::path texture_path = mat_doc.base_color_texture;
        if (texture_path.is_relative() && !LooksLikeProjectAssetRootPath(texture_path)) {
            texture_path = std::filesystem::path(path).parent_path() / texture_path;
        }
        logical_mat.base_color_texture_path = texture_path.lexically_normal().generic_string();
    }
    logical_mat.params.metallic = mat_doc.metallic;
    logical_mat.params.roughness = mat_doc.roughness;

    return CreateMaterial(logical_mat);
}

uint32_t MaterialSystem::CreateMaterial(Material const& material)
{
    if(name_to_id_.find(material.name) != name_to_id_.end()) {
        return name_to_id_[material.name];
    }
    uint32_t id = next_id_++;   
    materials_.push_back(material);
    materials_.back().id = id;
    name_to_id_[material.name] = id;

    if (resource_system_ != nullptr) {
        SyncLogicalToGpu(materials_.back());
    }

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

    if (resource_system_ != nullptr) {
        SyncLogicalToGpu(materials_[id]);
    }

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
            if (instance_it->second.texture_overrides.base_color_texture != 0) {
                temp_material.textures.base_color_texture = instance_it->second.texture_overrides.base_color_texture;
            }
            if (instance_it->second.texture_overrides.normal_texture != 0) {
                temp_material.textures.normal_texture = instance_it->second.texture_overrides.normal_texture;
            }
            if (instance_it->second.texture_overrides.metallic_roughness_texture != 0) {
                temp_material.textures.metallic_roughness_texture = instance_it->second.texture_overrides.metallic_roughness_texture;
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

#define SHADER_PATH_PREFIX "compiled_shaders/"
#define VERTEX_SHADER_SUFFIX ".vert.spv"
#define FRAGMENT_SHADER_SUFFIX ".frag.spv"

void MaterialSystem::SyncLogicalToGpu(Material const& logical_mat)
{
    if (resource_system_ == nullptr) {
        return;
    }

    auto& gpu_mat_mgr = resource_system_->GetMaterialManager();
    auto& shader_mgr = resource_system_->GetShaderManager();

    auto const* gpu_mat = gpu_mat_mgr.GetMaterialByName(logical_mat.name);
    uint32_t gpu_mat_id = 0;
    if (gpu_mat == nullptr) {
        uint32_t shader_id = 0;
        auto const* shader = shader_mgr.GetShaderByPath(logical_mat.shader_name);
        if (shader != nullptr) {
            shader_id = shader->id;
        } else {
            std::string vertex_shader_path = SHADER_PATH_PREFIX + logical_mat.shader_name + VERTEX_SHADER_SUFFIX;
            std::string fragment_shader_path = SHADER_PATH_PREFIX + logical_mat.shader_name + FRAGMENT_SHADER_SUFFIX;
            std::vector<uint32_t> vertex_shader_data = shader_asset_loader_(vertex_shader_path);
            std::vector<uint32_t> fragment_shader_data = shader_asset_loader_(fragment_shader_path);
            auto  id = shader_mgr.LoadShaderFromData(logical_mat.shader_name, vertex_shader_data, fragment_shader_data);
            // auto const* fallback_shader = shader_mgr.GetShaderByPath("mesh_shader");
            if (id != 0) {
                shader_id = id;
            }else {
                __android_log_print(ANDROID_LOG_ERROR, "MaterialSystem", "Failed to load shader for material %s: %s", logical_mat.name.c_str(), logical_mat.shader_name.c_str());
                // Failed to load shader, cannot create material
                return;
            }
        }
        gpu_mat_id = gpu_mat_mgr.CreateMaterial(logical_mat.name, shader_id);
    } else {
        gpu_mat_id = gpu_mat->id;
    }

    // Sync base color and parameters
    gpu_mat_mgr.SetBaseColor(gpu_mat_id, logical_mat.params.base_color);
    gpu_mat_mgr.SetParameter(gpu_mat_id, "metallic", logical_mat.params.metallic);
    gpu_mat_mgr.SetParameter(gpu_mat_id, "roughness", logical_mat.params.roughness);

    if (!logical_mat.base_color_texture_path.empty()) {
        auto& texture_mgr = resource_system_->GetTextureManager();
        uint32_t texture_id = 0;
        if (auto const* texture = texture_mgr.GetTextureByPath(logical_mat.base_color_texture_path)) {
            texture_id = texture->id;
        } else {
            texture_id = texture_mgr.LoadTexture(logical_mat.base_color_texture_path);
        }

        if (texture_id != 0) {
            gpu_mat_mgr.SetTexture(gpu_mat_id, "base_color", texture_id);
        }
    }
}

} // namespace ave::render
