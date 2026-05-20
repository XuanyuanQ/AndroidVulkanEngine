#pragma once

#include "ave/core/RenderTags.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ave::core {

struct FrameViewData {
    std::string camera_object_id;
    float view[16]{};
    float projection[16]{};
    float view_projection[16]{};
    std::array<float, 3> world_position{0.0f, 0.0f, 0.0f};
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

struct FrameRenderableData {
    std::string object_id;
    std::string debug_name;

    std::string mesh_id;
    std::string material_id;
    std::string shader_id;// For bring-up, shader is specified directly on renderable. Extend to support material-specified shader and shader variants.

    float world[16]{};

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

    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> direction{0.0f, -1.0f, 0.0f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};

    float intensity = 1.0f;
    float range = 10.0f;
    float inner_angle = 20.0f;
    float outer_angle = 35.0f;

    bool cast_shadows = false;
    uint32_t light_group = 0;
};

struct FrameUiData {
    std::string object_id;
    std::string debug_name;

    std::string material_id;
    std::string texture_id;

    std::array<float, 2> position{0.0f, 0.0f};
    std::array<float, 2> size{0.0f, 0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};

    float depth = 0.0f;
    bool visible = true;
    bool interactable = false;

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
