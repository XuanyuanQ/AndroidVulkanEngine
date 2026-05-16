#pragma once

#include "ave/render/RenderTypes.h"

#include <memory>
#include <vector>
#include <cstdint>

namespace ave::render {

// Forward declarations
namespace scene {
class GameObject;
class Camera;
class Light;
}

// Render object representing a drawable entity
struct RenderObject {
    uint32_t mesh_id = 0;
    uint32_t material_id = 0;
    uint32_t instance_count = 1;
    std::array<float, 16> world_matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // Identity matrix
    bool visible = true;
    uint32_t layer_mask = 0xFFFFFFFF;
};

// Light data for rendering
struct RenderLight {
    enum class Type {
        Directional,
        Point,
        Spot
    };

    Type type = Type::Directional;
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> direction{0.0f, -1.0f, 0.0f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float spot_angle = 45.0f;
    bool cast_shadows = false;
};

// Camera data for rendering
struct RenderCamera {
    std::array<float, 16> view_matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> projection_matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float fov = 60.0f;
    uint32_t viewport_width = 1920;
    uint32_t viewport_height = 1080;
};

// Per-frame render snapshot
class RenderWorld {
public:
    RenderWorld() = default;
    ~RenderWorld() = default;

    void Clear();

    // Add render objects
    void AddRenderObject(RenderObject const& object);
    void AddLight(RenderLight const& light);
    void SetCamera(RenderCamera const& camera);

    // Accessors
    std::vector<RenderObject> const& GetRenderObjects() const { return render_objects_; }
    std::vector<RenderLight> const& GetLights() const { return lights_; }
    RenderCamera const& GetCamera() const { return camera_; }
    RenderCamera& GetCamera() { return camera_; }

    // Culling and batching
    void CullAndBatch();

    // Get culled and batched objects
    std::vector<RenderObject> const& GetCulledObjects() const { return culled_objects_; }

private:
    std::vector<RenderObject> render_objects_;
    std::vector<RenderLight> lights_;
    RenderCamera camera_;
    std::vector<RenderObject> culled_objects_;
};

} // namespace ave::render
