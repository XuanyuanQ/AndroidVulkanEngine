#include "ave/scene/SceneWorld.h"
#include "ave/project/SharedDataContract.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/MaterialSystem.h"
#include <android/input.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include "LogUtil.h"

namespace ave::scene {

namespace {

glm::mat4 SetIdentity() {
    return glm::mat4(1.0f);
}

glm::mat4 GetLocalMatrix(project::TransformData const& local)
{
    glm::mat4 matrix = glm::mat4(1.0f);
    // 1. 平移 (Translation)
    matrix = glm::translate(matrix, local.position);
    
    // 2. 旋转 (Rotation - 采用标准的 X -> Y -> Z 欧拉角顺序)
    matrix = glm::rotate(matrix, glm::radians(local.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    matrix = glm::rotate(matrix, glm::radians(local.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    matrix = glm::rotate(matrix, glm::radians(local.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    
    // 3. 缩放 (Scale)
    matrix = glm::scale(matrix, local.scale);
    
    return matrix;
}

glm::mat4 SetPerspective(float fov_degrees, float aspect_ratio, float near_plane, float far_plane)
{
    float const fov_radians = fov_degrees * 3.14159265358979323846f / 180.0f;
    glm::mat4 proj = glm::perspective(fov_radians, aspect_ratio, near_plane, far_plane);
    proj[1][1] *= -1.0f; // Vulkan NDC: Y 轴朝下，glm::perspective 是 OpenGL 约定，必须翻转
    return proj;
}

std::string LightTypeToString(project::LightType type)
{
    switch (type) {
    case project::LightType::Directional:
        return "directional";
    case project::LightType::Point:
        return "point";
    case project::LightType::Spot:
        return "spot";
    }

    return "point";
}

uint64_t HashString(std::string const& value)
{
    constexpr uint64_t kOffsetBasis = 1469598103934665603ull;
    constexpr uint64_t kPrime = 1099511628211ull;

    uint64_t hash = kOffsetBasis;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

void Deduplicate(std::vector<std::string>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::string ResolveSceneAssetPath(std::string const& asset_text)
{
    if (asset_text.empty()) {
        return {};
    }

    std::filesystem::path asset_path = asset_text;
    return asset_path.lexically_normal().generic_string();
}


bool ComputeVisibility(glm::vec3 const& camera_position,
                       float far_plane,
                       glm::vec3 const& world_position)
{
    float const distance = glm::length(world_position - camera_position);
    return distance <= far_plane;
}

bool WouldCreateHierarchyCycle(std::string const& object_id,
                               std::string const& parent_id,
                               std::unordered_map<std::string, std::string> const& parent_by_object)
{
    std::unordered_set<std::string> visited;
    std::string current = parent_id;
    while (!current.empty()) {
        if (current == object_id) {
            return true;
        }
        if (!visited.insert(current).second) {
            return true;
        }

        auto const found = parent_by_object.find(current);
        if (found == parent_by_object.end()) {
            return false;
        }
        current = found->second;
    }

    return false;
}

std::string Vec3ToString(glm::vec3 const& value)
{
    std::ostringstream stream;
    stream << "(" << value.x << ", " << value.y << ", " << value.z << ")";
    return stream.str();
}

} // namespace

uint32_t SceneWorld::AddRenderable(std::string object_id, std::string debug_name, std::string mesh_id, std::string material_id)
{
    core::FrameRenderableData renderable{};
    renderable.object_id = std::move(object_id);
    renderable.debug_name = std::move(debug_name);
    renderable.mesh_id = std::move(mesh_id);
    renderable.material_id = std::move(material_id);
    renderables_.push_back(std::move(renderable));
    return static_cast<uint32_t>(renderables_.size() - 1);
}

uint32_t SceneWorld::AddPointLight(float x, float y, float z, float intensity)
{
    core::FrameLightData light{};
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    light.intensity = intensity;
    lights_.push_back(light);
    return static_cast<uint32_t>(lights_.size() - 1);
}

int32_t SceneWorld::FindRenderableIndex(std::string const& object_id) const
{
    for (size_t i = 0; i < renderables_.size(); ++i) {
        if (renderables_[i].object_id == object_id) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t SceneWorld::FindTransformNodeIndex(std::string const& object_id) const
{
    auto const found = object_to_node_.find(object_id);
    if (found == object_to_node_.end()) {
        return -1;
    }
    return found->second;
}

void SceneWorld::MarkTransformSubtreeDirty(int32_t node_index)
{
    if (node_index < 0 || static_cast<size_t>(node_index) >= transform_nodes_.size()) {
        return;
    }

    auto& node = transform_nodes_[static_cast<size_t>(node_index)];
    if (node.dirty) {
        return;
    }

    node.dirty = true;
    dirty_node_indices_.push_back(node_index);
    for (int32_t child_index : node.children) {
        MarkTransformSubtreeDirty(child_index);
    }
}

void SceneWorld::RebuildTransformNodes(project::SceneData const& scene)
{
    transform_nodes_.clear();
    object_to_node_.clear();
    object_to_renderable_.clear();
    object_to_light_.clear();
    root_nodes_.clear();
    dirty_node_indices_.clear();

    transform_nodes_.reserve(scene.objects.size());
    object_to_node_.reserve(scene.objects.size());

    std::unordered_map<std::string, std::string> parent_by_object;
    parent_by_object.reserve(scene.objects.size());
    for (auto const& object : scene.objects) {
        parent_by_object.emplace(object.id, object.hierarchy.parent);
    }

    for (auto const& object : scene.objects) {
        TransformNode node{};
        node.object_id = object.id;
        if (object.components.transform.has_value()) {
            node.local = *object.components.transform;
        }
        node.local_matrix = GetLocalMatrix(node.local);
        transform_nodes_.push_back(std::move(node));
        object_to_node_.emplace(object.id, static_cast<int32_t>(transform_nodes_.size() - 1));
        dirty_node_indices_.push_back(static_cast<int32_t>(transform_nodes_.size() - 1));
    }

    for (auto const& object : scene.objects) {
        int32_t const node_index = FindTransformNodeIndex(object.id);
        if (node_index < 0) {
            continue;
        }

        auto& node = transform_nodes_[static_cast<size_t>(node_index)];
        if (!object.hierarchy.parent.empty()) {
            int32_t const parent_index = FindTransformNodeIndex(object.hierarchy.parent);
            if (parent_index >= 0) {
                if (WouldCreateHierarchyCycle(object.id, object.hierarchy.parent, parent_by_object)) {
                    LOGW("Hierarchy cycle detected for GameObject '%s' with parent '%s'. Treating it as a root node.",
                         object.id.c_str(),
                         object.hierarchy.parent.c_str());
                    root_nodes_.push_back(node_index);
                    continue;
                }

                node.parent_index = parent_index;
                transform_nodes_[static_cast<size_t>(parent_index)].children.push_back(node_index);
                continue;
            }
        }

        root_nodes_.push_back(node_index);
    }
}

void SceneWorld::UpdateWorldRecursive(int32_t node_index, glm::mat4 const& parent_world, bool parent_dirty)
{
    auto& node = transform_nodes_[static_cast<size_t>(node_index)];
    bool const needs_update = parent_dirty || node.dirty;
    if (needs_update) {
        node.local_matrix = GetLocalMatrix(node.local);
        node.world_matrix = parent_world * node.local_matrix;
        node.dirty = false;
    }

    for (int32_t child_index : node.children) {
        UpdateWorldRecursive(child_index, node.world_matrix, needs_update);
    }
}

void SceneWorld::UpdateAllTransforms()
{
    for (int32_t root_index : root_nodes_) {
        UpdateWorldRecursive(root_index, glm::mat4(1.0f), false);
    }
}

void SceneWorld::RefreshViewFromTransformState()
{
    if (!has_scene_camera_ || camera_node_index_ < 0 || static_cast<size_t>(camera_node_index_) >= transform_nodes_.size()) {
        view_.camera_object_id = "default_camera";
        view_.near_plane = 0.1f;
        view_.far_plane = 1000.0f;
        view_.world_position = glm::vec3(0.0f, 0.0f, 8.0f);
        view_.view = glm::translate(glm::mat4(1.0f), -view_.world_position);
        view_.projection = SetPerspective(60.0f, aspect_ratio_, 0.1f, 1000.0f);
        view_.view_projection = view_.projection * view_.view;
        return;
    }

    auto const& camera_world = transform_nodes_[static_cast<size_t>(camera_node_index_)].world_matrix;
    view_.world_position = glm::vec3(camera_world[3]);
    view_.view = glm::inverse(camera_world);
    view_.projection = SetPerspective(camera_data_.fov, aspect_ratio_, camera_data_.near_plane, camera_data_.far_plane);
    view_.view_projection = view_.projection * view_.view;
}

void SceneWorld::RefreshRenderableFromTransformState(int32_t renderable_index)
{
    if (renderable_index < 0 || static_cast<size_t>(renderable_index) >= renderables_.size()) {
        return;
    }

    auto& renderable = renderables_[static_cast<size_t>(renderable_index)];
    int32_t const node_index = FindTransformNodeIndex(renderable.object_id);
    if (node_index < 0) {
        return;
    }

    auto const& world_matrix = transform_nodes_[static_cast<size_t>(node_index)].world_matrix;
    renderable.world = world_matrix;
    renderable.visible = ComputeVisibility(view_.world_position, view_.far_plane, glm::vec3(world_matrix[3]));
}

void SceneWorld::RefreshLightFromTransformState(int32_t light_index)
{
    if (light_index < 0 || static_cast<size_t>(light_index) >= lights_.size()) {
        return;
    }

    auto& light = lights_[static_cast<size_t>(light_index)];
    int32_t const node_index = FindTransformNodeIndex(light.object_id);
    if (node_index < 0) {
        return;
    }

    auto const& world_matrix = transform_nodes_[static_cast<size_t>(node_index)].world_matrix;
    light.position = glm::vec3(world_matrix[3]);
    light.direction = glm::normalize(glm::vec3(world_matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

void SceneWorld::RefreshAllRenderablesFromTransformState()
{
    for (size_t i = 0; i < renderables_.size(); ++i) {
        RefreshRenderableFromTransformState(static_cast<int32_t>(i));
    }
}

void SceneWorld::RefreshAllLightsFromTransformState()
{
    for (size_t i = 0; i < lights_.size(); ++i) {
        RefreshLightFromTransformState(static_cast<int32_t>(i));
    }
}

bool SceneWorld::IsTransformDirty(int32_t node_index) const
{
    if (node_index < 0 || static_cast<size_t>(node_index) >= transform_nodes_.size()) {
        return false;
    }
    return transform_nodes_[static_cast<size_t>(node_index)].dirty;
}

bool SceneWorld::IsCameraNodeDirty() const
{
    if (camera_node_index_ < 0) {
        return false;
    }

    return std::find(dirty_node_indices_.begin(), dirty_node_indices_.end(), camera_node_index_) != dirty_node_indices_.end();
}

void SceneWorld::ClearDirtyTracking()
{
    dirty_node_indices_.clear();
}

void SceneWorld::RefreshAllDerivedState()
{
    RefreshViewFromTransformState();
    RefreshAllRenderablesFromTransformState();
    RefreshAllLightsFromTransformState();
    ClearDirtyTracking();
}

void SceneWorld::RefreshDirtyDerivedState()
{
    bool const camera_dirty = IsCameraNodeDirty();
    if (camera_dirty) {
        RefreshViewFromTransformState();
        RefreshAllRenderablesFromTransformState();
        RefreshAllLightsFromTransformState();
        ClearDirtyTracking();
        return;
    }

    for (int32_t node_index : dirty_node_indices_) {
        if (node_index < 0 || static_cast<size_t>(node_index) >= transform_nodes_.size()) {
            continue;
        }

        auto const& object_id = transform_nodes_[static_cast<size_t>(node_index)].object_id;

        auto const renderable_found = object_to_renderable_.find(object_id);
        if (renderable_found != object_to_renderable_.end()) {
            RefreshRenderableFromTransformState(renderable_found->second);
        }

        auto const light_found = object_to_light_.find(object_id);
        if (light_found != object_to_light_.end()) {
            RefreshLightFromTransformState(light_found->second);
        }
    }

    ClearDirtyTracking();
}

bool SceneWorld::SetObjectTransform(std::string const& object_id, project::TransformData const& transform)
{
    for (auto& object : scene_.objects) {
        if (object.id == object_id) {
            object.components.transform = transform;
            break;
        }
    }

    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index < 0) {
        return false;
    }

    auto& node = transform_nodes_[static_cast<size_t>(node_index)];
    node.local = transform;
    MarkTransformSubtreeDirty(node_index);
    UpdateAllTransforms();
    RefreshDirtyDerivedState();
    return true;
}

bool SceneWorld::SetObjectPosition(std::string const& object_id, glm::vec3 const& position)
{
    for (auto& object : scene_.objects) {
        if (object.id == object_id) {
            if (!object.components.transform.has_value()) {
                object.components.transform = project::TransformData{};
            }
            object.components.transform->position = position;
            break;
        }
    }

    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index >= 0) {
        auto& node = transform_nodes_[static_cast<size_t>(node_index)];
        node.local.position = position;
        MarkTransformSubtreeDirty(node_index);
        UpdateAllTransforms();
        RefreshDirtyDerivedState();
        return true;
    }

    int32_t const renderable_index = FindRenderableIndex(object_id);
    if (renderable_index < 0) {
        return false;
    }

    auto& world = renderables_[static_cast<size_t>(renderable_index)].world;
    world[3][0] = position.x;
    world[3][1] = position.y;
    world[3][2] = position.z;
    return true;
}

bool SceneWorld::SetObjectRotation(std::string const& object_id, glm::vec3 const& rotation)
{
    for (auto& object : scene_.objects) {
        if (object.id == object_id) {
            if (!object.components.transform.has_value()) {
                object.components.transform = project::TransformData{};
            }
            object.components.transform->rotation = rotation;
            break;
        }
    }

    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index < 0) {
        return false;
    }

    auto& node = transform_nodes_[static_cast<size_t>(node_index)];
    node.local.rotation = rotation;
    MarkTransformSubtreeDirty(node_index);
    UpdateAllTransforms();
    RefreshDirtyDerivedState();
    return true;
}

bool SceneWorld::SetObjectScale(std::string const& object_id, glm::vec3 const& scale)
{
    for (auto& object : scene_.objects) {
        if (object.id == object_id) {
            if (!object.components.transform.has_value()) {
                object.components.transform = project::TransformData{};
            }
            object.components.transform->scale = scale;
            break;
        }
    }

    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index < 0) {
        return false;
    }

    auto& node = transform_nodes_[static_cast<size_t>(node_index)];
    node.local.scale = scale;
    MarkTransformSubtreeDirty(node_index);
    UpdateAllTransforms();
    RefreshDirtyDerivedState();
    return true;
}

bool SceneWorld::SetObjectVisible(std::string const& object_id, bool visible)
{
    int32_t const idx = FindRenderableIndex(object_id);
    if (idx < 0) {
        return false;
    }
    renderables_[static_cast<size_t>(idx)].visible = visible;
    return true;
}

bool SceneWorld::SetObjectColor(std::string const& object_id, glm::vec4 const& color)
{
    int32_t const idx = FindRenderableIndex(object_id);
    if (idx < 0) {
        return false;
    }
    auto& renderable = renderables_[static_cast<size_t>(idx)];
    renderable.has_color_override = true;
    renderable.color_override = color;
    return true;
}

bool SceneWorld::GetObjectPosition(std::string const& object_id, glm::vec3& out_position) const
{
    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index >= 0) {
        out_position = transform_nodes_[static_cast<size_t>(node_index)].local.position;
        return true;
    }

    int32_t const renderable_index = FindRenderableIndex(object_id);
    if (renderable_index < 0) {
        return false;
    }

    auto const& world = renderables_[static_cast<size_t>(renderable_index)].world;
    out_position = glm::vec3{world[3][0], world[3][1], world[3][2]};
    return true;
}

bool SceneWorld::GetObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const
{
    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index < 0) {
        return false;
    }

    out_rotation = transform_nodes_[static_cast<size_t>(node_index)].local.rotation;
    return true;
}

bool SceneWorld::GetObjectScale(std::string const& object_id, glm::vec3& out_scale) const
{
    int32_t const node_index = FindTransformNodeIndex(object_id);
    if (node_index < 0) {
        return false;
    }

    out_scale = transform_nodes_[static_cast<size_t>(node_index)].local.scale;
    return true;
}

bool SceneWorld::GetObjectVisible(std::string const& object_id, bool& out_visible) const
{
    int32_t const idx = FindRenderableIndex(object_id);
    if (idx < 0) {
        return false;
    }

    out_visible = renderables_[static_cast<size_t>(idx)].visible;
    return true;
}

bool SceneWorld::GetObjectColor(std::string const& object_id, glm::vec4& out_color) const
{
    int32_t const idx = FindRenderableIndex(object_id);
    if (idx < 0) {
        return false;
    }

    auto const& renderable = renderables_[static_cast<size_t>(idx)];
    out_color = renderable.has_color_override ? renderable.color_override : glm::vec4{1.0f};
    return true;
}

void SceneWorld::RebuildFromScene(project::SceneData const& scene,
                                  resource::ResourceSystem& resources,
                                  render::MaterialSystem& materials,
                                  float aspect_ratio)
{
    scene_ = scene;
    view_ = {};
    renderables_.clear();
    lights_.clear();
    aspect_ratio_ = aspect_ratio;
    camera_node_index_ = -1;
    has_scene_camera_ = false;

    view_.view = glm::mat4(1.0f);
    view_.projection = glm::mat4(1.0f);
    view_.view_projection = glm::mat4(1.0f);

    RebuildTransformNodes(scene);
    UpdateAllTransforms();

    for (auto const& object : scene.objects) {
        auto const& components = object.components;
        int32_t const node_index = FindTransformNodeIndex(object.id);
        glm::mat4 world_matrix = glm::mat4(1.0f);
        if (node_index >= 0) {
            world_matrix = transform_nodes_[static_cast<size_t>(node_index)].world_matrix;
        }

        if (components.camera.has_value() && !has_scene_camera_) {
            auto const& camera = *components.camera;
            view_.camera_object_id = object.id;
            view_.near_plane = camera.near_plane;
            view_.far_plane = camera.far_plane;
            camera_data_ = camera;
            camera_node_index_ = node_index;
            has_scene_camera_ = true;
        }

        if (components.mesh_renderer.has_value()) {
            auto const& mesh = *components.mesh_renderer;

            core::FrameRenderableData renderable{};
            renderable.object_id = object.id;
            renderable.debug_name = object.name;
            renderable.mesh_id = mesh.mesh;
            if (!mesh.mesh.empty()) {
                if (auto const* mesh_runtime = resources.GetMeshManager().GetMeshByPath(mesh.mesh)) {
                    renderable.mesh_handle = mesh_runtime->id;
                }
            }
            if (!mesh.material.empty()) {
                renderable.material_id = mesh.material;
                if (auto const* gpu_material = resources.GetMaterialManager().GetMaterialByName(mesh.material)) {
                    renderable.material_handle = gpu_material->id;
                } else if (auto const* material = materials.GetMaterial(mesh.material)) {
                    renderable.material_handle = material->id;
                }
            } else {
                bool const has_texture_overrides =
                    !mesh.base_color_texture.empty() ||
                    !mesh.normal_texture.empty() ||
                    !mesh.metallic_roughness_texture.empty();
                bool const has_param_overrides =
                    std::abs(mesh.base_color.r - 1.0f) > 0.0001f ||
                    std::abs(mesh.base_color.g - 1.0f) > 0.0001f ||
                    std::abs(mesh.base_color.b - 1.0f) > 0.0001f ||
                    std::abs(mesh.base_color.a - 1.0f) > 0.0001f ||
                    std::abs(mesh.metallic - 0.0f) > 0.0001f ||
                    std::abs(mesh.roughness - 0.5f) > 0.0001f ||
                    std::abs(mesh.normal_scale - 1.0f) > 0.0001f;

                render::Material logical_mat{};
                logical_mat.name = (has_texture_overrides || has_param_overrides)
                    ? "__default/pbr/" + object.id
                    : "__default/pbr_material__";
                logical_mat.shader_name = "solid_triangle";
                logical_mat.params.base_color = mesh.base_color;
                logical_mat.params.metallic = mesh.metallic;
                logical_mat.params.roughness = mesh.roughness;
                logical_mat.params.normal_scale = mesh.normal_scale;
                logical_mat.base_color_texture_path = ResolveSceneAssetPath(mesh.base_color_texture);
                logical_mat.normal_texture_path = ResolveSceneAssetPath(mesh.normal_texture);
                logical_mat.metallic_roughness_texture_path =
                    ResolveSceneAssetPath(mesh.metallic_roughness_texture);

                renderable.material_id = logical_mat.name;
                materials.CreateMaterial(logical_mat);
                if (auto const* gpu_material = resources.GetMaterialManager().GetMaterialByName(logical_mat.name)) {
                    renderable.material_handle = gpu_material->id;
                }
            }
            renderable.index_count = static_cast<uint32_t>(mesh.indices.size());
            renderable.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
            // 用矩阵提取的位置进行视距裁切 (Distance culling)
            renderable.visible = ComputeVisibility(view_.world_position, view_.far_plane, glm::vec3(world_matrix[3]));
            renderable.casts_shadow = mesh.casts_shadow;
            renderable.receives_shadow = mesh.receives_shadow;
            renderable.sort_key =
                (static_cast<uint64_t>(renderable.material_handle) << 32)
                ^ static_cast<uint64_t>(renderable.mesh_handle);

            renderable.world = world_matrix;
            object_to_renderable_[object.id] = static_cast<int32_t>(renderables_.size());
            renderables_.push_back(std::move(renderable));
        }

        if (components.light.has_value()) {
            auto const& light = *components.light;

            core::FrameLightData frame_light{};
            frame_light.object_id = object.id;
            frame_light.debug_name = object.name;
            frame_light.type = LightTypeToString(light.type);
            frame_light.position = glm::vec3(world_matrix[3]);
            frame_light.color = light.color;
            frame_light.intensity = light.intensity;
            frame_light.range = light.range;
            frame_light.inner_angle = light.inner_angle;
            frame_light.outer_angle = light.outer_angle;
            frame_light.cast_shadows = light.cast_shadows;
            // 将光源在本地空间的默认朝向 (0, 0, -1) 乘以世界矩阵，推导出真实的世界照射方向
            frame_light.direction = glm::normalize(glm::vec3(world_matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            object_to_light_[object.id] = static_cast<int32_t>(lights_.size());
            lights_.push_back(std::move(frame_light));
        }

    }

    RefreshAllDerivedState();
    LOGI("%s", DumpTransformHierarchy().c_str());
}

void SceneWorld::SetAspectRatio(float aspect_ratio)
{
    aspect_ratio_ = aspect_ratio;
    RefreshAllDerivedState();
}

void SceneWorld::DumpTransformNodeRecursive(int32_t node_index, int depth, std::string& out) const
{
    if (node_index < 0 || static_cast<size_t>(node_index) >= transform_nodes_.size()) {
        return;
    }

    auto const& node = transform_nodes_[static_cast<size_t>(node_index)];
    out.append(static_cast<size_t>(depth * 2), ' ');
    out += "- ";
    out += node.object_id;
    out += " local.pos=" + Vec3ToString(node.local.position);
    out += " local.rot=" + Vec3ToString(node.local.rotation);
    out += " local.scale=" + Vec3ToString(node.local.scale);
    out += " world.pos=" + Vec3ToString(glm::vec3(node.world_matrix[3]));
    out += "\n";

    for (int32_t child_index : node.children) {
        DumpTransformNodeRecursive(child_index, depth + 1, out);
    }
}

std::string SceneWorld::DumpTransformHierarchy() const
{
    std::string dump = "Transform hierarchy:\n";
    for (int32_t root_index : root_nodes_) {
        DumpTransformNodeRecursive(root_index, 0, dump);
    }
    return dump;
}

void SceneWorld::BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const
{
    out_frame.frame_index = frame_index;
    out_frame.view = view_;
    out_frame.environment.clear_color = scene_.environment.clear_color;
    out_frame.environment.ambient_color = scene_.environment.ambient_color;

    out_frame.renderables = renderables_;
    out_frame.lights = lights_;
    out_frame.ui_items.clear();
    out_frame.resources.meshes.clear();
    out_frame.resources.materials.clear();
    out_frame.resources.textures.clear();
    out_frame.resources.shaders.clear();

    out_frame.resources.meshes.reserve(out_frame.renderables.size());
    out_frame.resources.materials.reserve(out_frame.renderables.size());
    for (auto const& renderable : out_frame.renderables) {
        if (!renderable.mesh_id.empty()) {
            out_frame.resources.meshes.push_back(renderable.mesh_id);
        }
        if (!renderable.material_id.empty()) {
            out_frame.resources.materials.push_back(renderable.material_id);
        }
    }

    out_frame.resources.textures.reserve(out_frame.ui_items.size());
    for (auto const& ui : out_frame.ui_items) {
        if (!ui.texture_id.empty()) {
            out_frame.resources.textures.push_back(ui.texture_id);
        }
        if (!ui.material_id.empty()) {
            out_frame.resources.materials.push_back(ui.material_id);
        }
    }

    Deduplicate(out_frame.resources.meshes);
    Deduplicate(out_frame.resources.materials);
    Deduplicate(out_frame.resources.textures);
    Deduplicate(out_frame.resources.shaders);
}

std::string SceneWorld::InstantiatePrefab(project::PrefabDocument const& prefab,
                                          std::string const& parent_id,
                                          resource::ResourceSystem& resources,
                                          render::MaterialSystem& materials)
{
    std::string const suffix = "_inst_" + std::to_string(++prefab_instance_counter_);
    std::unordered_map<std::string, std::string> id_map;
    std::string root_id = "";

    // Step 1: Map all original IDs to new unique IDs with suffix
    for (auto const& obj : prefab.objects) {
        std::string new_id = obj.id + suffix;
        id_map[obj.id] = new_id;

        // Check if this object is a root of the prefab (has no parent, or parent is not within the prefab)
        bool parent_in_prefab = false;
        if (!obj.hierarchy.parent.empty()) {
            for (auto const& p_obj : prefab.objects) {
                if (p_obj.id == obj.hierarchy.parent) {
                    parent_in_prefab = true;
                    break;
                }
            }
        }
        if (!parent_in_prefab) {
            if (root_id.empty()) {
                root_id = new_id;
            }
        }
    }

    // Step 2: Clone and rewrite game objects
    for (auto const& obj : prefab.objects) {
        project::GameObjectData new_obj = obj;
        new_obj.id = id_map[obj.id];
        new_obj.name = obj.name + suffix;

        // Parent ID rewrite
        if (!obj.hierarchy.parent.empty()) {
            auto it = id_map.find(obj.hierarchy.parent);
            if (it != id_map.end()) {
                new_obj.hierarchy.parent = it->second;
            } else {
                new_obj.hierarchy.parent = parent_id;
            }
        } else {
            new_obj.hierarchy.parent = parent_id;
        }

        // Button target rewrite
        if (new_obj.components.button.has_value()) {
            auto& btn = *new_obj.components.button;
            auto it = id_map.find(btn.target);
            if (it != id_map.end()) {
                btn.target = it->second;
            }
        }

        // Slider target rewrite
        if (new_obj.components.slider.has_value()) {
            auto& slider = *new_obj.components.slider;
            auto it = id_map.find(slider.target);
            if (it != id_map.end()) {
                slider.target = it->second;
            }
        }

        // Script target rewrite
        if (new_obj.components.script.has_value()) {
            auto& script = *new_obj.components.script;
            auto it = id_map.find(script.target_object);
            if (it != id_map.end()) {
                script.target_object = it->second;
            }
        }

        new_obj.hierarchy.children.clear();
        scene_.objects.push_back(std::move(new_obj));
    }

    // Step 3: Rebuild the scene graph to reflect new objects in physics/renderables
    RebuildFromScene(scene_, resources, materials, aspect_ratio_);

    return root_id;
}

} // namespace ave::scene
