#include "ave/resource/ResourceSystem.h"
#include "VkBuffer.hpp"
#include "VkTexture.hpp"
#include "VkShader.hpp"
#include <glm/glm.hpp>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <array>
#include <optional>
#include <sstream>
// #define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "LogUtil.h"
#include <numbers>


namespace ave::resource {

namespace {

struct ObjVertexRef {
    int position_index = 0;
    int texcoord_index = 0;
    int normal_index = 0;

    bool operator==(ObjVertexRef const& other) const
    {
        return position_index == other.position_index
            && texcoord_index == other.texcoord_index
            && normal_index == other.normal_index;
    }
};

struct ObjVertexRefHash {
    size_t operator()(ObjVertexRef const& value) const
    {
        size_t seed = std::hash<int>{}(value.position_index);
        seed ^= std::hash<int>{}(value.texcoord_index) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(value.normal_index) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
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

    if (second_slash != std::string::npos) {
        auto const normal_text = token.substr(second_slash + 1);
        if (!normal_text.empty()) {
            ref.normal_index = std::stoi(normal_text);
        }
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

glm::vec3 TransformPreviewDirection(glm::vec3 const& direction)
{
    return {
        direction.x,
        direction.z,
        -direction.y,
    };
}

bool HasMeaningfulNormals(project::MeshData const& mesh)
{
    for (auto const& vertex : mesh.vertices) {
        if (glm::dot(vertex.normal, vertex.normal) > 0.0001f &&
            vertex.normal != glm::vec3{0.0f, 0.0f, 1.0f}) {
            return true;
        }
    }
    return false;
}

glm::vec3 MakeFallbackTangent(glm::vec3 const& normal)
{
    glm::vec3 const helper = std::abs(normal.y) < 0.999f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    glm::vec3 tangent = glm::cross(helper, normal);
    float const length_sq = glm::dot(tangent, tangent);
    if (length_sq <= 0.000001f) {
        tangent = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        tangent = glm::normalize(tangent);
    }
    return tangent;
}

void BuildMeshNormalsAndTangents(project::MeshData& mesh)
{
    if (mesh.vertices.empty()) {
        return;
    }

    if (mesh.indices.empty()) {
        mesh.indices.resize(mesh.vertices.size());
        for (uint32_t i = 0; i < mesh.indices.size(); ++i) {
            mesh.indices[i] = i;
        }
    }

    std::vector<glm::vec3> normal_acc(mesh.vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> tangent_acc(mesh.vertices.size(), glm::vec3{0.0f});
    std::vector<glm::vec3> bitangent_acc(mesh.vertices.size(), glm::vec3{0.0f});
    bool const has_uvs = std::any_of(
        mesh.vertices.begin(),
        mesh.vertices.end(),
        [](project::VertexData const& vertex) {
            return vertex.texcoord0 != glm::vec2{0.0f, 0.0f};
        });

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t const i0 = mesh.indices[i + 0];
        uint32_t const i1 = mesh.indices[i + 1];
        uint32_t const i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        auto const& v0 = mesh.vertices[i0];
        auto const& v1 = mesh.vertices[i1];
        auto const& v2 = mesh.vertices[i2];

        glm::vec3 const p0 = v0.position;
        glm::vec3 const p1 = v1.position;
        glm::vec3 const p2 = v2.position;
        glm::vec3 const e1 = p1 - p0;
        glm::vec3 const e2 = p2 - p0;
        glm::vec3 const face_normal = glm::cross(e1, e2);
        float const normal_len_sq = glm::dot(face_normal, face_normal);
        if (normal_len_sq > 0.000001f) {
            normal_acc[i0] += face_normal;
            normal_acc[i1] += face_normal;
            normal_acc[i2] += face_normal;
        }

        if (!has_uvs) {
            continue;
        }

        glm::vec2 const uv0 = v0.texcoord0;
        glm::vec2 const uv1 = v1.texcoord0;
        glm::vec2 const uv2 = v2.texcoord0;
        glm::vec2 const duv1 = uv1 - uv0;
        glm::vec2 const duv2 = uv2 - uv0;
        float const denom = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::abs(denom) <= 0.000001f) {
            continue;
        }

        float const r = 1.0f / denom;
        glm::vec3 const tangent = (e1 * duv2.y - e2 * duv1.y) * r;
        glm::vec3 const bitangent = (e2 * duv1.x - e1 * duv2.x) * r;
        tangent_acc[i0] += tangent;
        tangent_acc[i1] += tangent;
        tangent_acc[i2] += tangent;
        bitangent_acc[i0] += bitangent;
        bitangent_acc[i1] += bitangent;
        bitangent_acc[i2] += bitangent;
    }

    bool const preserve_normals = HasMeaningfulNormals(mesh);
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& vertex = mesh.vertices[i];

        glm::vec3 normal = vertex.normal;
        if (!preserve_normals) {
            normal = normal_acc[i];
            if (glm::dot(normal, normal) <= 0.000001f) {
                normal = glm::vec3{0.0f, 1.0f, 0.0f};
            }
        }
        if (glm::dot(normal, normal) <= 0.000001f) {
            normal = glm::vec3{0.0f, 1.0f, 0.0f};
        } else {
            normal = glm::normalize(normal);
        }

        glm::vec3 tangent = tangent_acc[i];
        if (glm::dot(tangent, tangent) <= 0.000001f) {
            tangent = MakeFallbackTangent(normal);
        } else {
            tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
            if (glm::dot(tangent, tangent) <= 0.000001f) {
                tangent = MakeFallbackTangent(normal);
            }
        }

        glm::vec3 const bitangent = bitangent_acc[i];
        float handedness = 1.0f;
        if (glm::dot(bitangent, bitangent) > 0.000001f) {
            handedness = glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        }

        vertex.normal = normal;
        vertex.tangent = glm::vec4{tangent, handedness};
    }
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
        if (vertex.normal != glm::vec3{0.0f, 0.0f, 1.0f}) {
            vertex.normal = glm::normalize(TransformPreviewDirection(vertex.normal));
        }
        if (vertex.tangent != glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}) {
            glm::vec3 const preview_tangent = TransformPreviewDirection(glm::vec3{vertex.tangent});
            if (glm::dot(preview_tangent, preview_tangent) > 0.000001f) {
                vertex.tangent = glm::vec4{glm::normalize(preview_tangent), vertex.tangent.w};
            }
        }
        vertex.color = color;
    }

    if (mesh.indices.empty()) {
        mesh.indices.resize(mesh.vertices.size());
        for (uint32_t i = 0; i < mesh.indices.size(); ++i) {
            mesh.indices[i] = i;
        }
    }
}

project::VertexData MakeVertex(glm::vec3 const& position,
                               glm::vec3 const& normal,
                               glm::vec2 const& uv,
                               glm::vec4 const& tangent = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f})
{
    project::VertexData vertex{};
    vertex.position = position;
    vertex.normal = normal;
    vertex.tangent = tangent;
    vertex.texcoord0 = uv;
    vertex.color = glm::vec4{1.0f};
    return vertex;
}

project::MeshData BuildPlaneMeshData(std::string const& name)
{
    project::MeshData mesh{};
    mesh.id = name;
    mesh.source = name;
    mesh.topology = "triangleList";
    mesh.vertices = {
        MakeVertex({-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}),
        MakeVertex({ 0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}),
        MakeVertex({ 0.5f, 0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}),
        MakeVertex({-0.5f, 0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}),
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

project::MeshData BuildCubeMeshData(std::string const& name)
{
    project::MeshData mesh{};
    mesh.id = name;
    mesh.source = name;
    mesh.topology = "triangleList";

    auto append_face = [&mesh](glm::vec3 const& normal,
                               glm::vec4 const& tangent,
                               std::array<glm::vec3, 4> const& positions) {
        uint32_t const base_index = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(MakeVertex(positions[0], normal, {0.0f, 1.0f}, tangent));
        mesh.vertices.push_back(MakeVertex(positions[1], normal, {1.0f, 1.0f}, tangent));
        mesh.vertices.push_back(MakeVertex(positions[2], normal, {1.0f, 0.0f}, tangent));
        mesh.vertices.push_back(MakeVertex(positions[3], normal, {0.0f, 0.0f}, tangent));
        mesh.indices.insert(mesh.indices.end(), {
            base_index + 0, base_index + 1, base_index + 2,
            base_index + 0, base_index + 2, base_index + 3,
        });
    };

    append_face({ 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                {{{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}});
    append_face({ 0.0f,  0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f},
                {{{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}});
    append_face({ 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f, -1.0f, 1.0f},
                {{{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}});
    append_face({-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f, 1.0f, 1.0f},
                {{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}});
    append_face({ 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                {{{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}});
    append_face({ 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                {{{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}});

    return mesh;
}

project::MeshData BuildSphereMeshData(std::string const& name, uint32_t rings = 16, uint32_t segments = 32)
{
    project::MeshData mesh{};
    mesh.id = name;
    mesh.source = name;
    mesh.topology = "triangleList";

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float const v = static_cast<float>(ring) / static_cast<float>(rings);
        float const phi = v * std::numbers::pi_v<float>;
        float const y = std::cos(phi) * 0.5f;
        float const radius = std::sin(phi) * 0.5f;

        for (uint32_t segment = 0; segment <= segments; ++segment) {
            float const u = static_cast<float>(segment) / static_cast<float>(segments);
            float const theta = u * std::numbers::pi_v<float> * 2.0f;

            glm::vec3 const position{
                radius * std::cos(theta),
                y,
                radius * std::sin(theta),
            };
            glm::vec3 const normal = glm::normalize(position);
            glm::vec4 const tangent{-std::sin(theta), 0.0f, std::cos(theta), 1.0f};
            mesh.vertices.push_back(MakeVertex(position, normal, {u, 1.0f - v}, tangent));
        }
    }

    uint32_t const stride = segments + 1;
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            uint32_t const a = ring * stride + segment;
            uint32_t const b = a + stride;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }

    return mesh;
}

project::MeshData BuildCylinderMeshData(std::string const& name, uint32_t segments = 24)
{
    project::MeshData mesh{};
    mesh.id = name;
    mesh.source = name;
    mesh.topology = "triangleList";

    float constexpr half_height = 0.5f;
    float constexpr radius = 0.5f;

    for (uint32_t segment = 0; segment <= segments; ++segment) {
        float const u = static_cast<float>(segment) / static_cast<float>(segments);
        float const angle = u * std::numbers::pi_v<float> * 2.0f;
        float const x = std::cos(angle) * radius;
        float const z = std::sin(angle) * radius;
        glm::vec3 const normal = glm::normalize(glm::vec3{x, 0.0f, z});
        glm::vec4 const tangent{-std::sin(angle), 0.0f, std::cos(angle), 1.0f};
        mesh.vertices.push_back(MakeVertex({x, -half_height, z}, normal, {u, 1.0f}, tangent));
        mesh.vertices.push_back(MakeVertex({x,  half_height, z}, normal, {u, 0.0f}, tangent));
    }

    for (uint32_t segment = 0; segment < segments; ++segment) {
        uint32_t const base = segment * 2;
        mesh.indices.insert(mesh.indices.end(), {
            base + 0, base + 1, base + 2,
            base + 2, base + 1, base + 3,
        });
    }

    uint32_t const top_center_index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(MakeVertex({0.0f, half_height, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}));
    uint32_t const bottom_center_index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(MakeVertex({0.0f, -half_height, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}));

    uint32_t const top_ring_start = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t segment = 0; segment <= segments; ++segment) {
        float const u = static_cast<float>(segment) / static_cast<float>(segments);
        float const angle = u * std::numbers::pi_v<float> * 2.0f;
        float const x = std::cos(angle) * radius;
        float const z = std::sin(angle) * radius;
        mesh.vertices.push_back(MakeVertex({x, half_height, z},
                                           {0.0f, 1.0f, 0.0f},
                                           {x / radius * 0.5f + 0.5f, z / radius * 0.5f + 0.5f}));
    }

    uint32_t const bottom_ring_start = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t segment = 0; segment <= segments; ++segment) {
        float const u = static_cast<float>(segment) / static_cast<float>(segments);
        float const angle = u * std::numbers::pi_v<float> * 2.0f;
        float const x = std::cos(angle) * radius;
        float const z = std::sin(angle) * radius;
        mesh.vertices.push_back(MakeVertex({x, -half_height, z},
                                           {0.0f, -1.0f, 0.0f},
                                           {x / radius * 0.5f + 0.5f, z / radius * 0.5f + 0.5f}));
    }

    for (uint32_t segment = 0; segment < segments; ++segment) {
        mesh.indices.insert(mesh.indices.end(), {
            top_center_index, top_ring_start + segment, top_ring_start + segment + 1,
            bottom_center_index, bottom_ring_start + segment + 1, bottom_ring_start + segment,
        });
    }

    return mesh;
}

std::optional<project::MeshData> BuildPrimitiveMeshData(std::string const& name)
{
    switch (project::PrimitiveTypeFromString(name)) {
    case project::PrimitiveType::Plane:
        return BuildPlaneMeshData(name);
    case project::PrimitiveType::Cube:
        return BuildCubeMeshData(name);
    case project::PrimitiveType::Sphere:
        return BuildSphereMeshData(name);
    case project::PrimitiveType::Cylinder:
        return BuildCylinderMeshData(name);
    case project::PrimitiveType::None:
    default:
        return std::nullopt;
    }
}

} // namespace

bool MeshManager::ParseObjMeshText(std::string const& text, project::MeshData& out_mesh) const
{
    out_mesh = {};
    out_mesh.topology = "triangleList";

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
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

        if (line.rfind("vn ", 0) == 0) {
            std::stringstream line_stream(line.substr(3));
            glm::vec3 normal{};
            line_stream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
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
                            // OBJ UV 原点在左下角，Vulkan 纹理原点在左上角，需翻转 V 轴
                            auto uv = texcoords[static_cast<size_t>(tex_index)];
                            uv.y = 1.0f - uv.y;
                            vertex.texcoord0 = uv;
                        }
                    }

                    if (ref.normal_index != 0) {
                        int const normal_index = ResolveObjIndex(ref.normal_index, static_cast<int>(normals.size()));
                        if (normal_index >= 0 && normal_index < static_cast<int>(normals.size())) {
                            vertex.normal = normals[static_cast<size_t>(normal_index)];
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

    if (auto primitive_mesh = BuildPrimitiveMeshData(path)) {
        return LoadMeshFromData(path, *primitive_mesh);
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

    project::MeshData processed_mesh = mesh_data;
    BuildMeshNormalsAndTangents(processed_mesh);

    MeshRuntime mesh;
    mesh.id = id;
    mesh.name = name;
    mesh.vertex_count = static_cast<uint32_t>(processed_mesh.vertices.size());
    mesh.index_count = static_cast<uint32_t>(processed_mesh.indices.size());
    mesh.vertex_stride = static_cast<uint32_t>(sizeof(project::VertexData));

    mesh.vertex_buffer = std::make_unique<vkfw::VkBuffer>();
    vkfw::BufferInfo vertex_buffer_info;
    vertex_buffer_info.size = processed_mesh.vertices.size() * sizeof(project::VertexData);
    vertex_buffer_info.usage = vkfw::BufferUsage::Vertex;
    if (mesh.vertex_buffer->Init(*ctx_, vertex_buffer_info)) {
        mesh.vertex_buffer->UpdateData(*ctx_, processed_mesh.vertices.data(), vertex_buffer_info.size);
    }

    if (!processed_mesh.indices.empty()) {
        mesh.index_buffer = std::make_unique<vkfw::VkBuffer>();
        vkfw::BufferInfo index_buffer_info;
        index_buffer_info.size = processed_mesh.indices.size() * sizeof(uint32_t);
        index_buffer_info.usage = vkfw::BufferUsage::Index;
        if (mesh.index_buffer->Init(*ctx_, index_buffer_info)) {
            mesh.index_buffer->UpdateData(*ctx_, processed_mesh.indices.data(), index_buffer_info.size);
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

bool TextureManager::LoadImagePixels(std::string const& path,
                                     std::vector<std::uint8_t>& out_pixels,
                                     uint32_t& out_width,
                                     uint32_t& out_height) const
{
    out_pixels.clear();
    out_width = 0;
    out_height = 0;

    if (!binary_asset_loader_) {
        return false;
    }

    auto image_bytes = binary_asset_loader_(path);
    if (image_bytes.empty()) {
        return false;
    }

    int tex_width = 0;
    int tex_height = 0;
    int tex_channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(image_bytes.data(),
                                            static_cast<int>(image_bytes.size()),
                                            &tex_width,
                                            &tex_height,
                                            &tex_channels,
                                            STBI_rgb_alpha);
    if (!pixels) {
        LOGE("Failed to decode image pixels: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    out_width = static_cast<uint32_t>(tex_width);
    out_height = static_cast<uint32_t>(tex_height);
    out_pixels.assign(pixels, pixels + (static_cast<size_t>(tex_width) * static_cast<size_t>(tex_height) * 4u));
    stbi_image_free(pixels);
    return true;
}

uint32_t TextureManager::LoadTexture(std::string const& path)
{
    if (!ctx_) {
        return 0;
    }

    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) {
        return it->second;
    }


    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc* pixels = nullptr;

    if (binary_asset_loader_) {
        auto image_bytes = binary_asset_loader_(path);

        if (!image_bytes.empty()) {
            pixels = stbi_load_from_memory(image_bytes.data(),
                                           static_cast<int>(image_bytes.size()),
                                           &texWidth,
                                           &texHeight,
                                           &texChannels,
                                           STBI_rgb_alpha);
            if (!pixels) {
                LOGE("Failed to decode texture asset: %s (%s)",
                                    path.c_str(),
                                    stbi_failure_reason());
            }
        } else {
            LOGW("Texture asset loader returned empty data: %s", path.c_str());
        }
    }


    if (!pixels) {
        LOGE(
                            "Failed to load texture: %s (%s)",
                            path.c_str(),
                            stbi_failure_reason());
        return 0;
    }

    uint32_t id = LoadTextureFromData(path,
                                      static_cast<uint32_t>(texWidth),
                                      static_cast<uint32_t>(texHeight),
                                      pixels,
                                      1);
    stbi_image_free(pixels);
    if (id != 0) {
        path_to_id_[path] = id;
    }
    return id;
}

uint32_t TextureManager::LoadTextureFromData(std::string const& name, uint32_t width, uint32_t height,
                                             void const* data, uint32_t mip_levels)
{
    if (!ctx_) {
        return 0;
    }

    if(path_to_id_.find(name) != path_to_id_.end()) {
        return path_to_id_[name];
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
    
    std::string const normalized_path = std::filesystem::path(path).lexically_normal().generic_string();
    auto it = path_to_id_.find(normalized_path);
    if (it != path_to_id_.end()) {
        return it->second;
    }

    if (!shader_asset_loader_) {
        LOGE("ShaderAssetLoader is not registered, cannot load shader: %s", normalized_path.c_str());
        return 0;
    }

    std::vector<uint32_t> vertex_spirv = shader_asset_loader_(normalized_path + ".vert.spv");
    if (vertex_spirv.empty()) {
        LOGE("Failed to load vertex shader: %s.vert.spv", normalized_path.c_str());
        return 0;
    }

    std::vector<uint32_t> fragment_spirv = shader_asset_loader_(normalized_path + ".frag.spv");
    uint32_t const id = LoadShaderFromData(normalized_path, vertex_spirv, fragment_spirv, "main");
    if (id != 0) {
        path_to_id_[normalized_path] = id;
    }
    return id;
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
    
    if (!shader_asset_loader_) {
        LOGE("ShaderAssetLoader is not registered, cannot load compute shader: %s", path.c_str());
        return 0;
    }

    std::vector<uint32_t> compute_spirv = shader_asset_loader_(path);
    if (compute_spirv.empty()) {
        LOGE("Failed to load compute shader: %s", path.c_str());
        return 0;
    }

    uint32_t id = LoadComputeShaderFromData(path, compute_spirv, "main");
    if (id != 0) {
        path_to_id_[path] = id;
    }
    return id;
}

uint32_t ShaderManager::LoadComputeShaderFromData(std::string const& name,
                                                 std::vector<uint32_t> const& compute_spirv,
                                                 std::string const& entry_point)
{
    if (!ctx_) {
        return 0;
    }
    if (path_to_id_.find(name) != path_to_id_.end()) {
        return path_to_id_[name];
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
    path_to_id_[name] = id;
    
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
    } else if (param == "normal_scale") {
        it->second.normal_scale = value;
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
