#include "ave/scene/SceneWorld.h"
#include "ave/project/SharedDataContract.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/MaterialSystem.h"
#include <android/input.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <algorithm>
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

#ifdef ENABLE_CAMERA_DEBUG
void SceneWorld::ConsumeMouseMovement(float& out_dx, float& out_dy) {
    if (g_mouse_dirty) {
        out_dx = g_mouse_dx;
        out_dy = g_mouse_dy;
        
        // 核心：用完必须清零，否则鼠标停下后相机还会由于老数据一直自转
        g_mouse_dx = 0.0f; 
        g_mouse_dy = 0.0f;
        g_mouse_dirty = false;
    } else {
        out_dx = 0.0f;
        out_dy = 0.0f;
    }
}

void SceneWorld::UpdateDebugLight( float delta_time){
    static float sunTime_ = 0.0f;
    static glm::vec3 lightPosition_{0.0f, 100.0f, -100.0f};

    sunTime_ += delta_time * 1.0f; // 太阳绕 Y 轴旋转的速度
    float const sunRadius = 100.0f;
    lightPosition_.x = sin(sunTime_) * sunRadius;
    lightPosition_.y = cos(sunTime_) * sunRadius;
    lightPosition_.z = -100.0f;

    // // 如果没有测试光源，就先添加一个
    if (lights_.empty()) {
        core::FrameLightData spot_light{
        .object_id       = "light_player_flashlight",               // 唯一标识符，比如玩家的手电筒
        .debug_name      = "Flashlight_Render_Node",               // 用于在渲染器或编辑器里显示的调试名称
        .type            = "spot",                                 // 声明为聚光灯类型

        .position        = lightPosition_, 
        .direction       = glm::vec3(0.0f, 0.0f, -1.0f),           // 沿着 Z 轴负方向（前方）照射
        .color           = glm::vec3(1.0f, 0.95f, 0.85f),          // 微黄的暖色白光

        .intensity       = 5.0f,                                   // 光照强度系数
        .range           = 25.0f,                                  // 最远照射距离 25 米
        .inner_angle     = 15.0f,                                  // 内锥角 15 度（核心最亮区域）
        .outer_angle     = 30.0f,                                  // 外锥角 30 度（边缘羽化衰减区域）

        .cast_shadows    = true,                                   // 该光源需要开启阴影生成（Shadow Pass）
        .light_group     = 1                                       // 分配到 1 号灯光组（例如用于区分室内/室外灯光）
    };
        lights_.push_back(spot_light);
    }else{
        // 否则就更新第一个光源的位置（假设它就是我们要测试的光源）
        lights_[0].position[0] = lightPosition_.x;
        lights_[0].position[1] = lightPosition_.y;
        lights_[0].position[2] = lightPosition_.z;
    }
}

void SceneWorld::UpdateDebugCamera(float delta_time)
{
    // ────────────────────────────────────────────────────────
    // 部分 A：处理鼠标右键旋转
    // ────────────────────────────────────────────────────────
    float dx = 0.0f, dy = 0.0f;
    ConsumeMouseMovement(dx, dy); // 拿出当前帧积攒的鼠标位移
    float temp = dx; dx = dy; dy = temp; // 暴力对调
    __android_log_print(ANDROID_LOG_DEBUG, "CAMERA_AXIS", "C++ 收到鼠标绝对值 -> dx: %f, dy: %f", dx, dy);

    if (glm::abs(dx) > 0.0f || glm::abs(dy) > 0.0f) {
        // 运用灵敏度更新欧拉角
        g_debug_camera.yaw   += dx * g_debug_camera.sensitivity;
        g_debug_camera.pitch -= dy * g_debug_camera.sensitivity; // 减法防止视角反转

        // 限制俯仰角，防止抬头低头看翻过去
        if (g_debug_camera.pitch > 89.0f)  g_debug_camera.pitch = 89.0f;
        if (g_debug_camera.pitch < -89.0f) g_debug_camera.pitch = -89.0f;

        // 根据新的角度，通过三角函数重新计算相机的朝向向量 (矩阵更新的核心)
        glm::vec3 front;
        front.x = cos(glm::radians(g_debug_camera.yaw)) * cos(glm::radians(g_debug_camera.pitch));
        front.y = sin(glm::radians(g_debug_camera.pitch));
        front.z = sin(glm::radians(g_debug_camera.yaw)) * cos(glm::radians(g_debug_camera.pitch));
        
        g_debug_camera.forward = glm::normalize(front);
        // 重新计算右向量
        g_debug_camera.right   = glm::normalize(glm::cross(g_debug_camera.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    // ────────────────────────────────────────────────────────
    // 部分 B：处理 WASD 键盘移动
    // ────────────────────────────────────────────────────────
    glm::vec3 movement(0.0f);
    if (IsKeyPressed(AKEYCODE_W)) movement += g_debug_camera.forward;
    if (IsKeyPressed(AKEYCODE_S)) movement -= g_debug_camera.forward;
    if (IsKeyPressed(AKEYCODE_A)) movement -= g_debug_camera.right;
    if (IsKeyPressed(AKEYCODE_D)) movement += g_debug_camera.right;

    // ────────────────────────────────────────────────────────
    // 部分 C：矩阵生效与同步 (只要移动或者旋转了，就更新 View 矩阵)
    // ────────────────────────────────────────────────────────
    bool has_moved = glm::length(movement) > 0.0f;
    bool has_rotated = (glm::abs(dx) > 0.0f || glm::abs(dy) > 0.0f);

    if (has_moved) {
        g_debug_camera.position += glm::normalize(movement) * g_debug_camera.speed * delta_time;
    }

    if (has_moved || has_rotated) {
        view_.world_position = g_debug_camera.position;
        // 用最新的位置和最新的 forward 重新构造 LookAt 矩阵
        view_.view = glm::lookAt(g_debug_camera.position, g_debug_camera.position + g_debug_camera.forward, glm::vec3(0, 1, 0));
        view_.view_projection = view_.projection * view_.view;
    }
}
#endif

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
                                  resource::ResourceSystem const& resources,
                                  render::MaterialSystem const& materials,
                                  float aspect_ratio)
{
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
            renderable.material_id = mesh.material;
            if (!mesh.mesh.empty()) {
                if (auto const* mesh_runtime = resources.GetMeshManager().GetMeshByPath(mesh.mesh)) {
                    renderable.mesh_handle = mesh_runtime->id;
                }
            }
            if (!mesh.material.empty()) {
                if (auto const* gpu_material = resources.GetMaterialManager().GetMaterialByName(mesh.material)) {
                    renderable.material_handle = gpu_material->id;
                } else if (auto const* material = materials.GetMaterial(mesh.material)) {
                    renderable.material_handle = material->id;
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

} // namespace ave::scene
