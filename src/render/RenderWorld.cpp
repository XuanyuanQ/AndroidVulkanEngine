#include "ave/render/RenderWorld.h"
#include "ave/render/FrameGraph.h"
#include "ave/render/MaterialSystem.h"
#include "ave/render/RenderTypes.h"

#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ave::render {

namespace {

// Simple frustum culling implementation
struct Frustum {
    glm::vec4 planes[6]; // Normal + distance

    bool IntersectsAABB(glm::vec3 const& min, glm::vec3 const& max) const {
        for (int i = 0; i < 6; ++i) {
            float px = planes[i].x > 0.0f ? max.x : min.x;
            float py = planes[i].y > 0.0f ? max.y : min.y;
            float pz = planes[i].z > 0.0f ? max.z : min.z;
            float distance = planes[i].x * px + planes[i].y * py + planes[i].z * pz + planes[i].w;
            if (distance < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

// Extract frustum from view-projection matrix
Frustum ExtractFrustum(glm::mat4 const& view_proj) {
    Frustum frustum;
    
    // Left plane
    frustum.planes[0] = glm::vec4(
        view_proj[3][0] + view_proj[0][0],
        view_proj[3][1] + view_proj[0][1],
        view_proj[3][2] + view_proj[0][2],
        view_proj[3][3] + view_proj[0][3]
    );
    
    // Right plane
    frustum.planes[1] = glm::vec4(
        view_proj[3][0] - view_proj[0][0],
        view_proj[3][1] - view_proj[0][1],
        view_proj[3][2] - view_proj[0][2],
        view_proj[3][3] - view_proj[0][3]
    );
    
    // Bottom plane
    frustum.planes[2] = glm::vec4(
        view_proj[3][0] + view_proj[1][0],
        view_proj[3][1] + view_proj[1][1],
        view_proj[3][2] + view_proj[1][2],
        view_proj[3][3] + view_proj[1][3]
    );
    
    // Top plane
    frustum.planes[3] = glm::vec4(
        view_proj[3][0] - view_proj[1][0],
        view_proj[3][1] - view_proj[1][1],
        view_proj[3][2] - view_proj[1][2],
        view_proj[3][3] - view_proj[1][3]
    );
    
    // Near plane
    frustum.planes[4] = glm::vec4(
        view_proj[3][0] + view_proj[2][0],
        view_proj[3][1] + view_proj[2][1],
        view_proj[3][2] + view_proj[2][2],
        view_proj[3][3] + view_proj[2][3]
    );
    
    // Far plane
    frustum.planes[5] = glm::vec4(
        view_proj[3][0] - view_proj[2][0],
        view_proj[3][1] - view_proj[2][1],
        view_proj[3][2] - view_proj[2][2],
        view_proj[3][3] - view_proj[2][3]
    );
    
    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float length = glm::length(glm::vec3(frustum.planes[i]));
        if (length > 0.0f) {
            frustum.planes[i] /= length;
        }
    }
    
    return frustum;
}

// Multiply view and projection matrices
glm::mat4 MultiplyMatrices(glm::mat4 const& a, glm::mat4 const& b) {
    return a * b;
}

} // namespace

void RenderWorld::Clear() {
    render_objects_.clear();
    lights_.clear();
    culled_objects_.clear();
}

void RenderWorld::AddRenderObject(RenderObject const& object) {
    render_objects_.push_back(object);
}

void RenderWorld::AddLight(RenderLight const& light) {
    lights_.push_back(light);
}

void RenderWorld::SetCamera(RenderCamera const& camera) {
    camera_ = camera;
}

void RenderWorld::CullAndBatch() {
    culled_objects_.clear();
    
    if (render_objects_.empty()) {
        return;
    }
    
    // Calculate view-projection matrix
    auto view_proj = camera_.projection_matrix * camera_.view_matrix;
    
    // Extract frustum
    Frustum frustum = ExtractFrustum(view_proj);
    
    // Frustum culling
    for (auto const& object : render_objects_) {
        if (!object.visible) {
            continue;
        }
        
        // TODO: Calculate object AABB from world matrix
        // For now, use a simple distance check
        glm::vec3 position = glm::vec3(object.world_matrix[3]);
        float distance = glm::length(position);
        
        // Simple distance culling (objects beyond far plane)
        if (distance < camera_.far_plane) {
            culled_objects_.push_back(object);
        }
    }
    
    // Batching by material
    std::sort(culled_objects_.begin(), culled_objects_.end(), 
        [](RenderObject const& a, RenderObject const& b) {
            return a.material_id < b.material_id;
        });
}

} // namespace ave::render
