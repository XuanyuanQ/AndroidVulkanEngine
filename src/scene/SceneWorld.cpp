#include "ave/scene/SceneWorld.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ave::scene {
namespace {

void SetIdentity(float matrix[16])
{
    std::fill(matrix, matrix + 16, 0.0f);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

void SetTranslationScale(float matrix[16],
                         std::array<float, 3> const& position,
                         std::array<float, 3> const& scale)
{
    SetIdentity(matrix);
    matrix[0] = scale[0];
    matrix[5] = scale[1];
    matrix[10] = scale[2];
    matrix[12] = position[0];
    matrix[13] = position[1];
    matrix[14] = position[2];
}

std::array<float, 3> AddFloat3(std::array<float, 3> const& a, std::array<float, 3> const& b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

std::array<float, 3> MultiplyFloat3(std::array<float, 3> const& a, std::array<float, 3> const& b)
{
    return {a[0] * b[0], a[1] * b[1], a[2] * b[2]};
}

void SetInverseTranslation(float matrix[16], std::array<float, 3> const& position)
{
    SetIdentity(matrix);
    matrix[12] = -position[0];
    matrix[13] = -position[1];
    matrix[14] = -position[2];
}

void SetPerspective(float matrix[16], float fov_degrees, float aspect_ratio, float near_plane, float far_plane)
{
    std::fill(matrix, matrix + 16, 0.0f);

    float const fov_radians = fov_degrees * 3.14159265358979323846f / 180.0f;
    float const tan_half_fov = std::tan(fov_radians * 0.5f);
    if (tan_half_fov <= 0.0f || aspect_ratio <= 0.0f || far_plane <= near_plane) {
        SetIdentity(matrix);
        return;
    }

    matrix[0] = 1.0f / (aspect_ratio * tan_half_fov);
    matrix[5] = 1.0f / tan_half_fov;
    matrix[10] = -(far_plane + near_plane) / (far_plane - near_plane);
    matrix[11] = -1.0f;
    matrix[14] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
}

void MultiplyMatrices(float out[16], float const a[16], float const b[16])
{
    float result[16]{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result[row * 4 + col] =
                a[row * 4 + 0] * b[0 * 4 + col] +
                a[row * 4 + 1] * b[1 * 4 + col] +
                a[row * 4 + 2] * b[2 * 4 + col] +
                a[row * 4 + 3] * b[3 * 4 + col];
        }
    }
    std::copy(result, result + 16, out);
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
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
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

bool ComputeVisibility(std::array<float, 3> const& camera_position,
                       float far_plane,
                       std::array<float, 3> const& world_position)
{
    float const dx = world_position[0] - camera_position[0];
    float const dy = world_position[1] - camera_position[1];
    float const dz = world_position[2] - camera_position[2];
    float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return distance <= far_plane;
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

void SceneWorld::RebuildFromScene(project::SceneData const& scene)
{
    view_ = {};
    renderables_.clear();
    lights_.clear();
    ui_items_.clear();

    SetIdentity(view_.view);
    SetIdentity(view_.projection);
    SetIdentity(view_.view_projection);

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
            SetInverseTranslation(view_.view, world_transform.position);
            SetPerspective(view_.projection, camera.fov, 16.0f / 9.0f, camera.near_plane, camera.far_plane);
            MultiplyMatrices(view_.view_projection, view_.projection, view_.view);
            has_camera = true;
        }

        if (components.mesh_renderer.has_value()) {
            auto const& mesh = *components.mesh_renderer;

            core::FrameRenderableData renderable{};
            renderable.object_id = object.id;
            renderable.debug_name = object.name;
            renderable.mesh_id = mesh.mesh;
            renderable.material_id = mesh.material;
            renderable.index_count = static_cast<uint32_t>(mesh.indices.size());
            renderable.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
            renderable.visible = ComputeVisibility(view_.world_position, view_.far_plane, world_transform.position);
            renderable.casts_shadow = false;
            renderable.receives_shadow = true;
            renderable.sort_key = (HashString(renderable.material_id) << 16) ^ HashString(renderable.mesh_id);

            SetTranslationScale(renderable.world, world_transform.position, world_transform.scale);
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
