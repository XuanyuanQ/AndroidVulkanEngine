#pragma once

#include "ave/core/RenderTags.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace ave::core {

struct FrameViewData {
    std::string camera_object_id;
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 view_projection{1.0f};
    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

struct FrameRenderableData {
    std::string object_id;
    std::string debug_name;

    std::string mesh_id;
    std::string material_id;
    uint32_t mesh_handle = 0;
    uint32_t material_handle = 0;

    glm::mat4 world{1.0f};

    // Routing/state (see docs/frame_data_contract_zh.md).
    uint32_t layer_mask = ToMask(RenderLayer::World);
    uint32_t pass_mask = DefaultWorldPassMask();
    uint32_t render_queue = kQueueOpaque;

    uint32_t index_count = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t first_vertex = 0;

    bool visible = true;
    bool casts_shadow = false;
    bool receives_shadow = true;

    uint64_t sort_key = 0;
};

struct FrameLightData {
    std::string object_id;
    std::string debug_name;
    std::string type{"point"};

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};

    float intensity = 1.0f;
    float range = 10.0f;
    float inner_angle = 20.0f;
    float outer_angle = 35.0f;

    bool cast_shadows = false;
    uint32_t light_group = 0;
};

struct FrameUiData {
    enum class Kind : uint8_t {
        Image,
        ButtonBackground,
        ProgressBarBackground,
        ProgressBarFill,
    };

    std::string object_id;
    std::string debug_name;

    std::string material_id;
    std::string texture_id;

    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{0.0f, 0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    float depth = 0.0f;
    bool visible = true;
    bool interactable = false;
    Kind kind = Kind::Image;

    uint32_t layer_mask = ToMask(RenderLayer::UI);
    uint32_t pass_mask = DefaultUiPassMask();
    uint32_t render_queue = kQueueOverlay;
};

struct FrameResourceTable {
    std::vector<std::string> meshes;
    std::vector<std::string> textures;
    std::vector<std::string> shaders;
    std::vector<std::string> materials;
};

struct FrameData {
    uint64_t frame_index = 0;
    FrameViewData view{};
    std::vector<FrameRenderableData> renderables;
    std::vector<FrameLightData> lights;
    std::vector<FrameUiData> ui_items;
    FrameResourceTable resources{};
};

} // namespace ave::core
