#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <array>

#include "ave/project/SharedDataContract.h"
#include "VkBuffer.hpp"
#include "VkTexture.hpp"
#include "VkShader.hpp"

namespace ave::resource {

// Mesh runtime data
struct MeshRuntime {
    uint32_t id = 0;
    std::string name;
    
    // GPU resources
    std::unique_ptr<vkfw::VkBuffer> vertex_buffer;
    std::unique_ptr<vkfw::VkBuffer> index_buffer;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t vertex_stride = 0;
    
    bool is_loaded = false;
};

// Texture runtime data
struct TextureRuntime {
    uint32_t id = 0;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 1;
    
    // GPU resources
    std::unique_ptr<vkfw::VkTexture> texture;
    
    bool is_loaded = false;
};

// Shader runtime data
struct ShaderRuntime {
    uint32_t id = 0;
    std::string name;
    std::string entry_point;
    
    // GPU resources
    std::unique_ptr<vkfw::VkShader> vertex_shader;
    std::unique_ptr<vkfw::VkShader> fragment_shader;
    std::unique_ptr<vkfw::VkShader> compute_shader;
    
    bool is_loaded = false;
};

// Material runtime data
struct MaterialRuntime {
    uint32_t id = 0;
    std::string name;
    uint32_t shader_id = 0;
    
    // Texture references
    uint32_t base_color_texture = 0;
    uint32_t normal_texture = 0;
    uint32_t metallic_roughness_texture = 0;
    
    // PBR parameters
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    
    bool is_loaded = false;
};

// Resource requirements from FrameData
struct FrameResources {
    std::vector<uint32_t> meshes;
    std::vector<uint32_t> textures;
    std::vector<uint32_t> shaders;
    std::vector<uint32_t> materials;
};

// Mesh Manager
class MeshManager {
public:
    MeshManager();
    ~MeshManager() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    
    uint32_t LoadMesh(std::string const& path);
    bool ParseObjMeshText(std::string const& text, project::MeshData& out_mesh) const;
    uint32_t LoadMeshFromData(std::string const& name, project::MeshData const& mesh_data);
    uint32_t LoadMeshFromData(std::string const& name, std::vector<float> const& vertices, 
                              std::vector<uint32_t> const& indices, uint32_t vertex_stride);
    MeshRuntime const* GetMesh(uint32_t id) const;
    MeshRuntime const* GetMeshByPath(std::string const& path) const;
    void UnloadMesh(uint32_t id);
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    std::unordered_map<uint32_t, MeshRuntime> meshes_;
    std::unordered_map<std::string, uint32_t> path_to_id_;
    uint32_t next_id_ = 1;
};

// Texture Manager
class TextureManager {
public:
    TextureManager();
    ~TextureManager() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    
    uint32_t LoadTexture(std::string const& path);
    uint32_t LoadTextureFromData(std::string const& name, uint32_t width, uint32_t height, 
                                 void const* data, uint32_t mip_levels = 1);
    TextureRuntime const* GetTexture(uint32_t id) const;
    TextureRuntime const* GetTextureByPath(std::string const& path) const;
    void UnloadTexture(uint32_t id);
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    std::unordered_map<uint32_t, TextureRuntime> textures_;
    std::unordered_map<std::string, uint32_t> path_to_id_;
    uint32_t next_id_ = 1;
};

// Shader Manager
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    
    uint32_t LoadShader(std::string const& path);
    uint32_t LoadShaderFromData(std::string const& name, 
                                std::vector<uint32_t> const& vertex_spirv,
                                std::vector<uint32_t> const& fragment_spirv,
                                std::string const& entry_point = "main");
    uint32_t LoadComputeShader(std::string const& path);
    uint32_t LoadComputeShaderFromData(std::string const& name,
                                     std::vector<uint32_t> const& compute_spirv,
                                     std::string const& entry_point = "main");
    ShaderRuntime const* GetShader(uint32_t id) const;
    ShaderRuntime const* GetShaderByPath(std::string const& path) const;
    void UnloadShader(uint32_t id);
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    std::unordered_map<uint32_t, ShaderRuntime> shaders_;
    std::unordered_map<std::string, uint32_t> path_to_id_;
    uint32_t next_id_ = 1;
};

// Material Manager
class MaterialManager {
public:
    MaterialManager();
    ~MaterialManager() = default;
    
    uint32_t CreateMaterial(std::string const& name, uint32_t shader_id);
    bool SetTexture(uint32_t material_id, std::string const& slot, uint32_t texture_id);
    bool SetParameter(uint32_t material_id, std::string const& param, float value);
    MaterialRuntime const* GetMaterial(uint32_t id) const;
    MaterialRuntime const* GetMaterialByName(std::string const& name) const;
    void RemoveMaterial(uint32_t id);
    void Clear();
    
private:
    std::unordered_map<uint32_t, MaterialRuntime> materials_;
    std::unordered_map<std::string, uint32_t> name_to_id_;
    uint32_t next_id_ = 1;
};

// Unified Resource System
class ResourceSystem {
public:
    ResourceSystem();
    ~ResourceSystem() = default;
    
    void SetContext(vkfw::VkContext* ctx);
    
    // Ensure all resources in FrameResources are loaded
    void EnsureResources(FrameResources const& resources);
    
    // Access individual managers
    MeshManager& GetMeshManager() { return mesh_manager_; }
    TextureManager& GetTextureManager() { return texture_manager_; }
    ShaderManager& GetShaderManager() { return shader_manager_; }
    MaterialManager& GetMaterialManager() { return material_manager_; }
    
    MeshManager const& GetMeshManager() const { return mesh_manager_; }
    TextureManager const& GetTextureManager() const { return texture_manager_; }
    ShaderManager const& GetShaderManager() const { return shader_manager_; }
    MaterialManager const& GetMaterialManager() const { return material_manager_; }
    
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    MeshManager mesh_manager_;
    TextureManager texture_manager_;
    ShaderManager shader_manager_;
    MaterialManager material_manager_;
};

} // namespace ave::resource
