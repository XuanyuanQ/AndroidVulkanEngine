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


namespace ave::scene {

class SceneWorld {
public:
    uint32_t AddRenderable(std::string object_id, std::string debug_name, std::string mesh_id, std::string material_id);
    uint32_t AddPointLight(float x, float y, float z, float intensity);
    void BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const;
    void RebuildFromScene(project::SceneData const& scene,
                          resource::ResourceSystem& resources,
                          render::MaterialSystem& materials,
                          float aspect_ratio = 16.0f / 9.0f);
    void SetAspectRatio(float aspect_ratio);
    bool SetObjectTransform(std::string const& object_id, project::TransformData const& transform);
    bool SetObjectPosition(std::string const& object_id, glm::vec3 const& position);
    bool SetObjectRotation(std::string const& object_id, glm::vec3 const& rotation);
    bool SetObjectScale(std::string const& object_id, glm::vec3 const& scale);
    bool SetObjectVisible(std::string const& object_id, bool visible);
    bool SetObjectColor(std::string const& object_id, glm::vec4 const& color);
    bool GetObjectPosition(std::string const& object_id, glm::vec3& out_position) const;
    bool GetObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const;
    bool GetObjectScale(std::string const& object_id, glm::vec3& out_scale) const;
    bool GetObjectVisible(std::string const& object_id, bool& out_visible) const;
    bool GetObjectColor(std::string const& object_id, glm::vec4& out_color) const;
    std::string DumpTransformHierarchy() const;

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
    void DumpTransformNodeRecursive(int32_t node_index, int depth, std::string& out) const;

    core::FrameViewData view_{};
    project::SceneData scene_{};
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
