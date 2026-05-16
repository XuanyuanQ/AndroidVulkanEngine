#include "ave/render/RenderWorld.h"

#include <cmath>
#include <algorithm>

namespace ave::render {

namespace {

// Simple frustum culling implementation
struct Frustum {
    float planes[6][4]; // Normal + distance

    bool IntersectsAABB(std::array<float, 3> const& min, std::array<float, 3> const& max) const {
        for (int i = 0; i < 6; ++i) {
            float px = planes[i][0] > 0.0f ? max[0] : min[0];
            float py = planes[i][1] > 0.0f ? max[1] : min[1];
            float pz = planes[i][2] > 0.0f ? max[2] : min[2];
            
            float distance = planes[i][0] * px + planes[i][1] * py + planes[i][2] * pz + planes[i][3];
            if (distance < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

// Extract frustum from view-projection matrix
Frustum ExtractFrustum(std::array<float, 16> const& view_proj) {
    Frustum frustum;
    
    // Left plane
    frustum.planes[0][0] = view_proj[3] + view_proj[0];
    frustum.planes[0][1] = view_proj[7] + view_proj[4];
    frustum.planes[0][2] = view_proj[11] + view_proj[8];
    frustum.planes[0][3] = view_proj[15] + view_proj[12];
    
    // Right plane
    frustum.planes[1][0] = view_proj[3] - view_proj[0];
    frustum.planes[1][1] = view_proj[7] - view_proj[4];
    frustum.planes[1][2] = view_proj[11] - view_proj[8];
    frustum.planes[1][3] = view_proj[15] - view_proj[12];
    
    // Top plane
    frustum.planes[2][0] = view_proj[3] + view_proj[1];
    frustum.planes[2][1] = view_proj[7] + view_proj[5];
    frustum.planes[2][2] = view_proj[11] + view_proj[9];
    frustum.planes[2][3] = view_proj[15] + view_proj[13];
    
    // Bottom plane
    frustum.planes[3][0] = view_proj[3] - view_proj[1];
    frustum.planes[3][1] = view_proj[7] - view_proj[5];
    frustum.planes[3][2] = view_proj[11] - view_proj[9];
    frustum.planes[3][3] = view_proj[15] - view_proj[13];
    
    // Near plane
    frustum.planes[4][0] = view_proj[3] + view_proj[2];
    frustum.planes[4][1] = view_proj[7] + view_proj[6];
    frustum.planes[4][2] = view_proj[11] + view_proj[10];
    frustum.planes[4][3] = view_proj[15] + view_proj[14];
    
    // Far plane
    frustum.planes[5][0] = view_proj[3] - view_proj[2];
    frustum.planes[5][1] = view_proj[7] - view_proj[6];
    frustum.planes[5][2] = view_proj[11] - view_proj[10];
    frustum.planes[5][3] = view_proj[15] - view_proj[14];
    
    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float length = std::sqrt(frustum.planes[i][0] * frustum.planes[i][0] +
                                 frustum.planes[i][1] * frustum.planes[i][1] +
                                 frustum.planes[i][2] * frustum.planes[i][2]);
        if (length > 0.0f) {
            frustum.planes[i][0] /= length;
            frustum.planes[i][1] /= length;
            frustum.planes[i][2] /= length;
            frustum.planes[i][3] /= length;
        }
    }
    
    return frustum;
}

// Multiply view and projection matrices
std::array<float, 16> MultiplyMatrices(std::array<float, 16> const& a, std::array<float, 16> const& b) {
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result[row * 4 + col] = 
                a[row * 4 + 0] * b[0 * 4 + col] +
                a[row * 4 + 1] * b[1 * 4 + col] +
                a[row * 4 + 2] * b[2 * 4 + col] +
                a[row * 4 + 3] * b[3 * 4 + col];
        }
    }
    return result;
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
    auto view_proj = MultiplyMatrices(camera_.projection_matrix, camera_.view_matrix);
    
    // Extract frustum
    Frustum frustum = ExtractFrustum(view_proj);
    
    // Frustum culling
    for (auto const& object : render_objects_) {
        if (!object.visible) {
            continue;
        }
        
        // TODO: Calculate object AABB from world matrix
        // For now, use a simple distance check
        float distance = std::sqrt(
            object.world_matrix[12] * object.world_matrix[12] +
            object.world_matrix[13] * object.world_matrix[13] +
            object.world_matrix[14] * object.world_matrix[14]
        );
        
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
