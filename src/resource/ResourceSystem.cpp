#include "ave/resource/ResourceSystem.h"
#include "VkBuffer.hpp"
#include "VkTexture.hpp"
#include "VkShader.hpp"
#include <glm/glm.hpp>

namespace ave::resource {

namespace {

struct ObjVertexRef {
    int position_index = 0;
    int texcoord_index = 0;

    bool operator==(ObjVertexRef const& other) const
    {
        return position_index == other.position_index
            && texcoord_index == other.texcoord_index;
    }
};

struct ObjVertexRefHash {
    size_t operator()(ObjVertexRef const& value) const
    {
        size_t seed = std::hash<int>{}(value.position_index);
        seed ^= std::hash<int>{}(value.texcoord_index) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

ObjVertexRef ParseObjVertexRef(std::string const& token)
{
    ObjVertexRef ref{};

    auto const first_slash = token.find('/');
    if (first_slash == std::string::npos) {
        ref.position_index = std::stoi(token);
        return ref;
    }

    ref.position_index = std::stoi(token.substr(0, first_slash));

    auto const second_slash = token.find('/', first_slash + 1);
    auto const texcoord_text = token.substr(first_slash + 1, second_slash == std::string::npos
                                                                 ? std::string::npos
                                                                 : second_slash - first_slash - 1);
    if (!texcoord_text.empty()) {
        ref.texcoord_index = std::stoi(texcoord_text);
    }

    return ref;
}

int ResolveObjIndex(int index, int count)
{
    if (index > 0) {
        return index - 1;
    }
    if (index < 0) {
        return count + index;
    }
    return -1;
}

glm::vec3 TransformPreviewPosition(glm::vec3 const& position)
{
    return {
        position.x,
        position.z,
        -position.y,
    };
}

void PreparePreviewMeshData(project::MeshData& mesh)
{
    if (mesh.vertices.empty()) {
        return;
    }

    std::vector<glm::vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (auto const& vertex : mesh.vertices) {
        positions.push_back(TransformPreviewPosition(vertex.position));
    }

    auto min_pos = positions.front();
    auto max_pos = positions.front();
    for (auto const& position : positions) {
        min_pos = glm::min(min_pos, position);
        max_pos = glm::max(max_pos, position);
    }

    glm::vec3 const center = (min_pos + max_pos) * 0.5f;
    float const extent_x = max_pos.x - min_pos.x;
    float const extent_y = max_pos.y - min_pos.y;
    float const extent_z = max_pos.z - min_pos.z;
    float const max_extent = std::max({extent_x, extent_y, extent_z, 0.0001f});
    float const scale = 1.6f / max_extent;
    bool const has_any_uv = std::any_of(
        mesh.vertices.begin(),
        mesh.vertices.end(),
        [](project::VertexData const& vertex) {
            return vertex.texcoord0 != glm::vec2{0.0f, 0.0f};
        });

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& vertex = mesh.vertices[i];
        auto const& position = positions[i];
        glm::vec4 color{0.85f, 0.82f, 0.78f, 1.0f};
        if (has_any_uv) {
            auto const& uv = vertex.texcoord0;
            color = {
                std::clamp(uv.x, 0.0f, 1.0f),
                std::clamp(uv.y, 0.0f, 1.0f),
                std::clamp(1.0f - uv.x, 0.0f, 1.0f),
                1.0f,
            };
        }

        vertex.position = (position - center) * scale;
        vertex.color = color;
    }

    if (mesh.indices.empty()) {
        mesh.indices.resize(mesh.vertices.size());
        for (uint32_t i = 0; i < mesh.indices.size(); ++i) {
            mesh.indices[i] = i;
        }
    }
}

} // namespace

bool MeshManager::ParseObjMeshText(std::string const& text, project::MeshData& out_mesh) const
{
    out_mesh = {};
    out_mesh.topology = "triangleList";

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::unordered_map<ObjVertexRef, uint32_t, ObjVertexRefHash> vertex_cache;

    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 2) {
            continue;
        }

        if (line.rfind("v ", 0) == 0) {
            std::stringstream line_stream(line.substr(2));
            glm::vec3 position{};
            line_stream >> position.x >> position.y >> position.z;
            positions.push_back(position);
            continue;
        }

        if (line.rfind("vt ", 0) == 0) {
            std::stringstream line_stream(line.substr(3));
            glm::vec2 texcoord{};
            line_stream >> texcoord.x >> texcoord.y;
            texcoords.push_back(texcoord);
            continue;
        }

        if (line.rfind("f ", 0) == 0) {
            std::stringstream line_stream(line.substr(2));
            std::vector<ObjVertexRef> polygon;
            std::string token;
            while (line_stream >> token) {
                polygon.push_back(ParseObjVertexRef(token));
            }

            if (polygon.size() < 3) {
                continue;
            }

            for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                std::array<ObjVertexRef, 3> const triangle{
                    polygon[0],
                    polygon[i],
                    polygon[i + 1],
                };

                for (auto const& ref : triangle) {
                    auto cache_it = vertex_cache.find(ref);
                    if (cache_it != vertex_cache.end()) {
                        out_mesh.indices.push_back(cache_it->second);
                        continue;
                    }

                    int const pos_index = ResolveObjIndex(ref.position_index, static_cast<int>(positions.size()));
                    if (pos_index < 0 || pos_index >= static_cast<int>(positions.size())) {
                        continue;
                    }

                    project::VertexData vertex{};
                    vertex.position = positions[static_cast<size_t>(pos_index)];

                    if (ref.texcoord_index != 0) {
                        int const tex_index = ResolveObjIndex(ref.texcoord_index, static_cast<int>(texcoords.size()));
                        if (tex_index >= 0 && tex_index < static_cast<int>(texcoords.size())) {
                            vertex.texcoord0 = texcoords[static_cast<size_t>(tex_index)];
                        }
                    }

                    uint32_t const new_index = static_cast<uint32_t>(out_mesh.vertices.size());
                    out_mesh.vertices.push_back(vertex);
                    out_mesh.indices.push_back(new_index);
                    vertex_cache.emplace(ref, new_index);
                }
            }
        }
    }

    if (positions.empty() || out_mesh.indices.empty() || out_mesh.vertices.empty()) {
        return false;
    }

    return true;
}

// Mesh Manager
MeshManager::MeshManager() = default;

uint32_t MeshManager::LoadMesh(std::string const& path)
{
    if (!ctx_) {
        return 0;
    }
    
    // Check if already loaded
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return it->second;
    }

    if (!text_asset_loader_) {
        return 0;
    }

    auto const text = text_asset_loader_(path);
    if (text.empty()) {
        return 0;
    }

    project::MeshData mesh_data{};
    mesh_data.id = path;
    mesh_data.source = path;

    if (path.size() >= 4 && path.substr(path.size() - 4) == ".obj") {
        if (!ParseObjMeshText(text, mesh_data)) {
            return 0;
        }
        PreparePreviewMeshData(mesh_data);
        return LoadMeshFromData(path, mesh_data);
    }

    // TODO: Support .gltf/.glb and other mesh formats.
    return 0;
}

uint32_t MeshManager::LoadMeshFromData(std::string const& name, project::MeshData const& mesh_data)
{
    if (!ctx_ || mesh_data.vertices.empty()) {
        return 0;
    }

    auto it = path_to_id_.find(name);
    if (it != path_to_id_.end()) {
        return it->second;
    }

    uint32_t id = next_id_++;

    MeshRuntime mesh;
    mesh.id = id;
    mesh.name = name;
    mesh.vertex_count = static_cast<uint32_t>(mesh_data.vertices.size());
    mesh.index_count = static_cast<uint32_t>(mesh_data.indices.size());
    mesh.vertex_stride = static_cast<uint32_t>(sizeof(project::VertexData));

    mesh.vertex_buffer = std::make_unique<vkfw::VkBuffer>();
    vkfw::BufferInfo vertex_buffer_info;
    vertex_buffer_info.size = mesh_data.vertices.size() * sizeof(project::VertexData);
    vertex_buffer_info.usage = vkfw::BufferUsage::Vertex;
    if (mesh.vertex_buffer->Init(*ctx_, vertex_buffer_info)) {
        mesh.vertex_buffer->UpdateData(*ctx_, mesh_data.vertices.data(), vertex_buffer_info.size);
    }

    if (!mesh_data.indices.empty()) {
        mesh.index_buffer = std::make_unique<vkfw::VkBuffer>();
        vkfw::BufferInfo index_buffer_info;
        index_buffer_info.size = mesh_data.indices.size() * sizeof(uint32_t);
        index_buffer_info.usage = vkfw::BufferUsage::Index;
        if (mesh.index_buffer->Init(*ctx_, index_buffer_info)) {
            mesh.index_buffer->UpdateData(*ctx_, mesh_data.indices.data(), index_buffer_info.size);
        }
    }

    mesh.is_loaded = true;
    meshes_[id] = std::move(mesh);
    path_to_id_[name] = id;

    return id;
}

uint32_t MeshManager::LoadMeshFromData(std::string const& name, std::vector<float> const& vertices, 
                                        std::vector<uint32_t> const& indices, uint32_t vertex_stride)
{
    if (!ctx_) {
        return 0;
    }
    
    uint32_t id = next_id_++;
    
    MeshRuntime mesh;
    mesh.id = id;
    mesh.name = name;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size()) / vertex_stride;
    mesh.index_count = static_cast<uint32_t>(indices.size());
    mesh.vertex_stride = vertex_stride * static_cast<uint32_t>(sizeof(float));
    
    // Create vertex buffer
    mesh.vertex_buffer = std::make_unique<vkfw::VkBuffer>();
    vkfw::BufferInfo vertex_buffer_info;
    vertex_buffer_info.size = vertices.size() * sizeof(float);
    vertex_buffer_info.usage = vkfw::BufferUsage::Vertex;
    if (mesh.vertex_buffer->Init(*ctx_, vertex_buffer_info)) {
        mesh.vertex_buffer->UpdateData(*ctx_, vertices.data(), vertices.size() * sizeof(float));
    }
    
    // Create index buffer (optional)
    if (!indices.empty()) {
        mesh.index_buffer = std::make_unique<vkfw::VkBuffer>();
        vkfw::BufferInfo index_buffer_info;
        index_buffer_info.size = indices.size() * sizeof(uint32_t);
        index_buffer_info.usage = vkfw::BufferUsage::Index;
        if (mesh.index_buffer->Init(*ctx_, index_buffer_info)) {
            mesh.index_buffer->UpdateData(*ctx_, indices.data(), indices.size() * sizeof(uint32_t));
        }
    }
    
    mesh.is_loaded = true;
    meshes_[id] = std::move(mesh);
    path_to_id_[name] = id;
    
    return id;
}

MeshRuntime const* MeshManager::GetMesh(uint32_t id) const
{
    auto it = meshes_.find(id);
    if (it != meshes_.end()) {
        return &it->second;
    }
    return nullptr;
}

MeshRuntime const* MeshManager::GetMeshByPath(std::string const& path) const
{
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return GetMesh(it->second);
    }
    return nullptr;
}

void MeshManager::UnloadMesh(uint32_t id)
{
    auto it = meshes_.find(id);
    if (it != meshes_.end()) {
        if (it->second.vertex_buffer && ctx_) {
            it->second.vertex_buffer->Shutdown(*ctx_);
        }
        if (it->second.index_buffer && ctx_) {
            it->second.index_buffer->Shutdown(*ctx_);
        }
        meshes_.erase(it);
    }
}

void MeshManager::Clear()
{
    for (auto& [id, mesh] : meshes_) {
        if (mesh.vertex_buffer && ctx_) {
            mesh.vertex_buffer->Shutdown(*ctx_);
        }
        if (mesh.index_buffer && ctx_) {
            mesh.index_buffer->Shutdown(*ctx_);
        }
    }
    meshes_.clear();
    path_to_id_.clear();
    next_id_ = 1;
}

// Texture Manager
TextureManager::TextureManager() = default;

uint32_t TextureManager::LoadTexture(std::string const& path)
{
    if (!ctx_) {
        return 0;
    }
    
    // Check if already loaded
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return it->second;
    }
    
    // TODO: Load texture from file (e.g., .png, .jpg, .ktx)
    // For now, return 0 to indicate not implemented
    return 0;
}

uint32_t TextureManager::LoadTextureFromData(std::string const& name, uint32_t width, uint32_t height,
                                             void const* data, uint32_t mip_levels)
{
    if (!ctx_) {
        return 0;
    }
    
    uint32_t id = next_id_++;
    
    TextureRuntime texture;
    texture.id = id;
    texture.name = name;
    texture.width = width;
    texture.height = height;
    texture.mip_levels = mip_levels;
    
    // Create texture
    texture.texture = std::make_unique<vkfw::VkTexture>();
    vkfw::TextureInfo texture_info;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.mip_levels = mip_levels;
    texture_info.format = vkfw::TextureFormat::R8G8B8A8_UNORM;
    texture_info.usage = vkfw::TextureUsage::Sampled;
    if (texture.texture->Init(*ctx_, texture_info)) {
        texture.texture->UpdateData(*ctx_, data, width * height * 4);
    }
    
    texture.is_loaded = true;
    textures_[id] = std::move(texture);
    path_to_id_[name] = id;
    
    return id;
}

TextureRuntime const* TextureManager::GetTexture(uint32_t id) const
{
    auto it = textures_.find(id);
    if (it != textures_.end()) {
        return &it->second;
    }
    return nullptr;
}

TextureRuntime const* TextureManager::GetTextureByPath(std::string const& path) const
{
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return GetTexture(it->second);
    }
    return nullptr;
}

void TextureManager::UnloadTexture(uint32_t id)
{
    auto it = textures_.find(id);
    if (it != textures_.end()) {
        if (it->second.texture && ctx_) {
            it->second.texture->Shutdown(*ctx_);
        }
        textures_.erase(it);
    }
}

void TextureManager::Clear()
{
    for (auto& [id, texture] : textures_) {
        if (texture.texture && ctx_) {
            texture.texture->Shutdown(*ctx_);
        }
    }
    textures_.clear();
    path_to_id_.clear();
    next_id_ = 1;
}

// Shader Manager
ShaderManager::ShaderManager() = default;

uint32_t ShaderManager::LoadShader(std::string const& path)
{
    if (!ctx_) {
        return 0;
    }
    
    // Check if already loaded
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return it->second;
    }
    
    // TODO: Load shader from file (e.g., .vert, .frag, .spv)
    // For now, return 0 to indicate not implemented
    return 0;
}

uint32_t ShaderManager::LoadShaderFromData(std::string const& name, 
                                std::vector<uint32_t> const& vertex_spirv,
                                std::vector<uint32_t> const& fragment_spirv,
                                std::string const& entry_point)
{
    if (!ctx_) {
        return 0;
    }
    if(path_to_id_.find(name) != path_to_id_.end()) {
        return path_to_id_[name];
    }   
    
    uint32_t id = next_id_++;
    
    ShaderRuntime shader;
    shader.id = id;
    shader.name = name;
    shader.entry_point = entry_point;
    
    // Create vertex shader
    if (!vertex_spirv.empty()) {
        shader.vertex_shader = std::make_unique<vkfw::VkShader>();
        vkfw::ShaderInfo vertex_shader_info;
        vertex_shader_info.stage = vkfw::ShaderStage::Vertex;
        vertex_shader_info.spirv_code = vertex_spirv;
        vertex_shader_info.entry_point = entry_point;
        shader.vertex_shader->Init(*ctx_, vertex_shader_info);
    }
    
    // Create fragment shader
    if (!fragment_spirv.empty()) {
        shader.fragment_shader = std::make_unique<vkfw::VkShader>();
        vkfw::ShaderInfo fragment_shader_info;
        fragment_shader_info.stage = vkfw::ShaderStage::Fragment;
        fragment_shader_info.spirv_code = fragment_spirv;
        fragment_shader_info.entry_point = entry_point;
        shader.fragment_shader->Init(*ctx_, fragment_shader_info);
    }
    
    shader.is_loaded = true;
    shaders_[id] = std::move(shader);
    path_to_id_[name] = id;
    
    return id;
}

uint32_t ShaderManager::LoadComputeShader(std::string const& path)
{
    if (!ctx_) {
        return 0;
    }
    
    // Check if already loaded
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return it->second;
    }
    
    // TODO: Load compute shader from file (e.g., .comp, .spv)
    // For now, return 0 to indicate not implemented
    return 0;
}

uint32_t ShaderManager::LoadComputeShaderFromData(std::string const& name,
                                                 std::vector<uint32_t> const& compute_spirv,
                                                 std::string const& entry_point)
{
    if (!ctx_) {
        return 0;
    }
    
    uint32_t id = next_id_++;
    
    ShaderRuntime shader;
    shader.id = id;
    shader.name = name;
    shader.entry_point = entry_point;
    
    // Create compute shader
    shader.compute_shader = std::make_unique<vkfw::VkShader>();
    vkfw::ShaderInfo compute_shader_info;
    compute_shader_info.stage = vkfw::ShaderStage::Compute;
    compute_shader_info.spirv_code = compute_spirv;
    compute_shader_info.entry_point = entry_point;
    shader.compute_shader->Init(*ctx_, compute_shader_info);
    
    shader.is_loaded = true;
    shaders_[id] = std::move(shader);
    
    return id;
}

ShaderRuntime const* ShaderManager::GetShader(uint32_t id) const
{
    auto it = shaders_.find(id);
    if (it != shaders_.end()) {
        return &it->second;
    }
    return nullptr;
}

ShaderRuntime const* ShaderManager::GetShaderByPath(std::string const& path) const
{
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return GetShader(it->second);
    }
    return nullptr;
}

void ShaderManager::UnloadShader(uint32_t id)
{
    auto it = shaders_.find(id);
    if (it != shaders_.end()) {
        if (it->second.vertex_shader && ctx_) {
            it->second.vertex_shader->Shutdown(*ctx_);
        }
        if (it->second.fragment_shader && ctx_) {
            it->second.fragment_shader->Shutdown(*ctx_);
        }
        if (it->second.compute_shader && ctx_) {
            it->second.compute_shader->Shutdown(*ctx_);
        }
        shaders_.erase(it);
    }
}

void ShaderManager::Clear()
{
    for (auto& [id, shader] : shaders_) {
        if (shader.vertex_shader && ctx_) {
            shader.vertex_shader->Shutdown(*ctx_);
        }
        if (shader.fragment_shader && ctx_) {
            shader.fragment_shader->Shutdown(*ctx_);
        }
        if (shader.compute_shader && ctx_) {
            shader.compute_shader->Shutdown(*ctx_);
        }
    }
    shaders_.clear();
    next_id_ = 1;
}

// Material Manager
MaterialManager::MaterialManager() = default;

uint32_t MaterialManager::CreateMaterial(std::string const& name, uint32_t shader_id)
{
    
    if(name_to_id_.find(name) != name_to_id_.end()) {
        return name_to_id_[name];
    }
    uint32_t id = next_id_++;
    
    MaterialRuntime material;
    material.id = id;
    material.name = name;
    material.shader_id = shader_id;
    
    material.is_loaded = true;
    materials_[id] = std::move(material);
    name_to_id_[name] = id;
    
    return id;
}

bool MaterialManager::SetTexture(uint32_t material_id, std::string const& slot, uint32_t texture_id)
{
    auto it = materials_.find(material_id);
    if (it == materials_.end()) {
        return false;
    }
    
    if (slot == "base_color") {
        it->second.base_color_texture = texture_id;
    } else if (slot == "normal") {
        it->second.normal_texture = texture_id;
    } else if (slot == "metallic_roughness") {
        it->second.metallic_roughness_texture = texture_id;
    }
    
    return true;
}

bool MaterialManager::SetParameter(uint32_t material_id, std::string const& param, float value)
{
    auto it = materials_.find(material_id);
    if (it == materials_.end()) {
        return false;
    }
    
    if (param == "metallic") {
        it->second.metallic = value;
    } else if (param == "roughness") {
        it->second.roughness = value;
    }
    
    return true;
}

bool MaterialManager::SetBaseColor(uint32_t material_id, glm::vec4 const& color)
{
    auto it = materials_.find(material_id);
    if (it == materials_.end()) {
        return false;
    }
    it->second.base_color = color;
    return true;
}

MaterialRuntime const* MaterialManager::GetMaterial(uint32_t id) const
{
    auto it = materials_.find(id);
    if (it != materials_.end()) {
        return &it->second;
    }
    return nullptr;
}

MaterialRuntime const* MaterialManager::GetMaterialByName(std::string const& name) const
{
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        return nullptr;
    }
    return GetMaterial(it->second);
}

void MaterialManager::RemoveMaterial(uint32_t id)
{
    materials_.erase(id);
}

void MaterialManager::Clear()
{
    materials_.clear();
    name_to_id_.clear();
    next_id_ = 1;
}

// Resource System
ResourceSystem::ResourceSystem() = default;

void ResourceSystem::SetContext(vkfw::VkContext* ctx)
{
    ctx_ = ctx;
    mesh_manager_.SetContext(ctx);
    texture_manager_.SetContext(ctx);
    shader_manager_.SetContext(ctx);
}

void ResourceSystem::EnsureResources(FrameResources const& resources)
{
    // Ensure meshes are loaded
    for (uint32_t mesh_id : resources.meshes) {
        if (!mesh_manager_.GetMesh(mesh_id)) {
            // Note: Loading from asset system requires AssetManager integration
            // For now, meshes should be loaded explicitly via LoadMesh()
            // Future: mesh_manager_.LoadMeshFromAsset(asset_manager_, mesh_id);
        }
    }
    
    // Ensure textures are loaded
    for (uint32_t texture_id : resources.textures) {
        if (!texture_manager_.GetTexture(texture_id)) {
            // Note: Loading from asset system requires AssetManager integration
            // For now, textures should be loaded explicitly via LoadTexture()
            // Future: texture_manager_.LoadTextureFromAsset(asset_manager_, texture_id);
        }
    }
    
    // Ensure shaders are loaded
    for (uint32_t shader_id : resources.shaders) {
        if (!shader_manager_.GetShader(shader_id)) {
            // Note: Loading from asset system requires AssetManager integration
            // For now, shaders should be loaded explicitly via LoadShader()
            // Future: shader_manager_.LoadShaderFromAsset(asset_manager_, shader_id);
        }
    }
    
    // Ensure materials are loaded
    for (uint32_t material_id : resources.materials) {
        if (!material_manager_.GetMaterial(material_id)) {
            // Note: Materials are created via MaterialSystem, not loaded from assets
            // This check is for validation only
        }
    }
}

void ResourceSystem::Clear()
{
    mesh_manager_.Clear();
    texture_manager_.Clear();
    shader_manager_.Clear();
    material_manager_.Clear();
}

} // namespace ave::resource
