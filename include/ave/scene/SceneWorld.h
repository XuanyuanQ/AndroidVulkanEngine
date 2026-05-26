#pragma once

#include "ave/core/FrameData.h"
#include "ave/project/SceneDocument.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace ave::resource {
class ResourceSystem;
}

namespace ave::render {
class MaterialSystem;
}

#define ENABLE_CAMERA_DEBUG
// Debug camera support (enabled only in test builds)
#ifdef ENABLE_CAMERA_DEBUG
#include <android/input.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#endif

namespace ave::scene {

class SceneWorld {
public:
    uint32_t AddRenderable(std::string object_id, std::string debug_name, std::string mesh_id, std::string material_id);
    uint32_t AddPointLight(float x, float y, float z, float intensity);
    void BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const;
    void RebuildFromScene(project::SceneData const& scene,
                          resource::ResourceSystem const& resources,
                          render::MaterialSystem const& materials,
                          float aspect_ratio = 16.0f / 9.0f);
    bool SetObjectTransform(std::string const& object_id, project::TransformData const& transform);
    bool SetObjectPosition(std::string const& object_id, glm::vec3 const& position);
    bool SetObjectRotation(std::string const& object_id, glm::vec3 const& rotation);
    bool SetObjectScale(std::string const& object_id, glm::vec3 const& scale);
    bool SetObjectVisible(std::string const& object_id, bool visible);
    bool SetObjectColor(std::string const& object_id, glm::vec4 const& color);

#ifdef ENABLE_CAMERA_DEBUG
    // Simple debug camera for keyboard testing
struct DebugCamera {
    glm::vec3 position{0.0f, 0.0f, 8.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float yaw{-90.0f};         // 偏航角：默认 -90 度使其朝向 -Z
    float pitch{0.0f};         // 俯仰角：默认 0
    float speed{5.0f};         // 移动速度
    float sensitivity{0.1f};   // 鼠标旋转灵敏度
};

// 2. 实例化全局相机（你的代码里在用的 g_debug_camera 就在这里诞生）
DebugCamera g_debug_camera;

// 3. 实例化全局按键状态表（这就是两边通信的桥梁）
// Key 是按键编号（如 29 代表 W），Value 是是否按下（true/false）
std::unordered_map<int32_t, bool> g_key_states;

float g_mouse_dx = 0.0f;
float g_mouse_dy = 0.0f;
bool g_mouse_dirty = false;

// 4. 提供一个给外面查询状态的简单函数
bool IsKeyPressed(int32_t key_code) {
    return g_key_states[key_code]; // 如果没存过，默认返回 false
}

void UpdateDebugLight( float delta_time);
void UpdateDebugCamera( float delta_time);
void ConsumeMouseMovement(float& out_dx, float& out_dy);
#endif

private:
    struct TransformNode {
        std::string object_id;
        int32_t parent_index = -1;
        std::vector<int32_t> children;

        project::TransformData local{};
        glm::mat4 local_matrix{1.0f};
        glm::mat4 world_matrix{1.0f};
        bool dirty = true;
    };

    int32_t FindRenderableIndex(std::string const& object_id) const;
    int32_t FindTransformNodeIndex(std::string const& object_id) const;
    void MarkTransformSubtreeDirty(int32_t node_index);
    void RebuildTransformNodes(project::SceneData const& scene);
    void UpdateAllTransforms();
    void UpdateWorldRecursive(int32_t node_index, glm::mat4 const& parent_world, bool parent_dirty);
    void RefreshViewFromTransformState();
    void RefreshRenderableFromTransformState(int32_t renderable_index);
    void RefreshLightFromTransformState(int32_t light_index);
    void RefreshAllRenderablesFromTransformState();
    void RefreshAllLightsFromTransformState();
    void RefreshAllDerivedState();
    void RefreshDirtyDerivedState();
    bool IsTransformDirty(int32_t node_index) const;
    bool IsCameraNodeDirty() const;
    void ClearDirtyTracking();

    core::FrameViewData view_{};
    std::vector<core::FrameRenderableData> renderables_;
    std::vector<core::FrameLightData> lights_;
    std::vector<TransformNode> transform_nodes_;
    std::unordered_map<std::string, int32_t> object_to_node_;
    std::unordered_map<std::string, int32_t> object_to_renderable_;
    std::unordered_map<std::string, int32_t> object_to_light_;
    std::vector<int32_t> root_nodes_;
    std::vector<int32_t> dirty_node_indices_;

    int32_t camera_node_index_ = -1;
    project::CameraData camera_data_{};
    bool has_scene_camera_ = false;
    float aspect_ratio_ = 16.0f / 9.0f;
};

} // namespace ave::scene
