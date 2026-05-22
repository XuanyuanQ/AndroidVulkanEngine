#include "ave/scene/SceneWorld.h"
#include "ave/project/SharedDataContract.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/MaterialSystem.h"
#include <android/input.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <android/log.h>

namespace ave::scene {

namespace {

glm::mat4 SetIdentity() {
    return glm::mat4(1.0f);
}

glm::mat4 SetTranslationScale(glm::vec3 const& position, glm::vec3 const& scale)
{
    glm::mat4 matrix = glm::mat4(1.0f);
    matrix[0][0] = scale.x;
    matrix[1][1] = scale.y;
    matrix[2][2] = scale.z;
    matrix[3][0] = position.x;
    matrix[3][1] = position.y;
    matrix[3][2] = position.z;
    return matrix;
}

glm::vec3 AddFloat3(glm::vec3 const& a, glm::vec3 const& b)
{
    return a + b;
}

glm::vec3 MultiplyFloat3(glm::vec3 const& a, glm::vec3 const& b)
{
    return a * b;
}

glm::mat4 SetInverseTranslation(glm::vec3 const& position)
{
    glm::mat4 matrix = glm::mat4(1.0f);
    matrix[3][0] = -position.x;
    matrix[3][1] = -position.y;
    matrix[3][2] = -position.z;
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

struct WorldTransform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

WorldTransform ResolveWorldTransform(project::SceneData const& scene,
                                     std::unordered_map<std::string, size_t> const& object_indices,
                                     std::unordered_map<std::string, WorldTransform>& cache,
                                     std::string const& object_id)
{
    auto cached = cache.find(object_id);
    if (cached != cache.end()) {
        return cached->second;
    }

    auto found = object_indices.find(object_id);
    if (found == object_indices.end()) {
        return {};
    }

    auto const& object = scene.objects[found->second];
    project::TransformData local{};
    if (object.components.transform.has_value()) {
        local = *object.components.transform;
    }

    WorldTransform world{};
    world.position = local.position;
    world.scale = local.scale;

    if (!object.hierarchy.parent.empty()) {
        auto parent = ResolveWorldTransform(scene, object_indices, cache, object.hierarchy.parent);
        world.position = AddFloat3(parent.position, local.position);
        world.scale = MultiplyFloat3(parent.scale, local.scale);
    }

    cache.emplace(object_id, world);
    return world;
}

bool ComputeVisibility(glm::vec3 const& camera_position,
                       float far_plane,
                       glm::vec3 const& world_position)
{
    float const distance = glm::length(world_position - camera_position);
    return distance <= far_plane;
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

void SceneWorld::RebuildFromScene(project::SceneData const& scene,
                                  resource::ResourceSystem const& resources,
                                  render::MaterialSystem const& materials,
                                  float aspect_ratio)
{
    view_ = {};
    renderables_.clear();
    lights_.clear();
    ui_items_.clear();

    view_.view = glm::mat4(1.0f);
    view_.projection = glm::mat4(1.0f);
    view_.view_projection = glm::mat4(1.0f);

    bool has_camera = false;
    std::unordered_map<std::string, size_t> object_indices;
    object_indices.reserve(scene.objects.size());
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        object_indices.emplace(scene.objects[i].id, i);
    }
    std::unordered_map<std::string, WorldTransform> world_cache;

    for (auto const& object : scene.objects) {
        auto const& components = object.components;

        project::TransformData transform{};
        if (components.transform.has_value()) {
            transform = *components.transform;
        }
        auto world_transform = ResolveWorldTransform(scene, object_indices, world_cache, object.id);

        if (components.camera.has_value() && !has_camera) {
            auto const& camera = *components.camera;
            view_.camera_object_id = object.id;
            view_.near_plane = camera.near_plane;
            view_.far_plane = camera.far_plane;
            view_.world_position = world_transform.position;
            view_.view = SetInverseTranslation(world_transform.position);
            view_.projection = SetPerspective(camera.fov, aspect_ratio, camera.near_plane, camera.far_plane);
            view_.view_projection = view_.projection * view_.view;
            has_camera = true;
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
            renderable.visible = ComputeVisibility(view_.world_position, view_.far_plane, world_transform.position);
            renderable.casts_shadow = false;
            renderable.receives_shadow = true;
            renderable.sort_key =
                (static_cast<uint64_t>(renderable.material_handle) << 32)
                ^ static_cast<uint64_t>(renderable.mesh_handle);

            renderable.world = SetTranslationScale(world_transform.position, world_transform.scale);
            renderables_.push_back(std::move(renderable));
        }

        if (components.light.has_value()) {
            auto const& light = *components.light;

            core::FrameLightData frame_light{};
            frame_light.object_id = object.id;
            frame_light.debug_name = object.name;
            frame_light.type = LightTypeToString(light.type);
            frame_light.position = world_transform.position;
            frame_light.color = light.color;
            frame_light.intensity = light.intensity;
            frame_light.range = light.range;
            frame_light.inner_angle = light.inner_angle;
            frame_light.outer_angle = light.outer_angle;
            frame_light.cast_shadows = light.cast_shadows;
            frame_light.direction = {0.0f, -1.0f, 0.0f};

            lights_.push_back(std::move(frame_light));
        }

        if (components.image.has_value()) {
            auto const& image = *components.image;

            core::FrameUiData ui{};
            ui.object_id = object.id;
            ui.debug_name = object.name;
            ui.texture_id = image.texture;
            ui.color = image.color;
            ui.position = {world_transform.position[0], world_transform.position[1]};
            ui.size = {100.0f, 100.0f};
            ui.depth = world_transform.position[2];
            ui.visible = true;
            ui.interactable = components.button.has_value();

            ui_items_.push_back(std::move(ui));
        }

        if (components.progress_bar.has_value()) {
            auto const& progress_bar = *components.progress_bar;

            core::FrameUiData ui{};
            ui.object_id = object.id;
            ui.debug_name = object.name.empty() ? object.id : object.name;
            ui.position = {world_transform.position[0], world_transform.position[1]};
            ui.size = {160.0f, 24.0f};
            ui.depth = world_transform.position[2];
            ui.visible = true;
            ui.interactable = false;

            float const denominator = std::max(progress_bar.max_value - progress_bar.min_value, 0.0001f);
            float const normalized = std::clamp((progress_bar.value - progress_bar.min_value) / denominator, 0.0f, 1.0f);
            ui.color = {0.20f + 0.60f * normalized, 0.75f, 0.30f, 1.0f};

            ui_items_.push_back(std::move(ui));
        }
    }
}

void SceneWorld::BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const
{
    out_frame.frame_index = frame_index;
    out_frame.view = view_;

    out_frame.renderables = renderables_;
    out_frame.lights = lights_;
    out_frame.ui_items = ui_items_;
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
