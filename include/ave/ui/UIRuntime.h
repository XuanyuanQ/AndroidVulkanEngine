#pragma once

#include "ave/core/FrameData.h"
#include "ave/project/SharedDataContract.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ave::ui {

struct UiImageNode {
    std::string object_id;
    std::string debug_name;
    std::string texture_id;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{100.0f, 100.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float depth = 0.0f;
    bool visible = true;
};

struct UiButtonNode {
    std::string object_id;
    std::string debug_name;
    std::string texture_id;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{100.0f, 100.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float depth = 0.0f;
    bool visible = true;
    bool interactable = true;
    std::string target;
    std::string method;
};

struct UiProgressBarNode {
    std::string object_id;
    std::string debug_name;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{160.0f, 24.0f};
    glm::vec4 background_color{0.18f, 0.18f, 0.20f, 0.9f};
    glm::vec4 fill_color{0.45f, 0.82f, 0.32f, 1.0f};
    float depth = 0.0f;
    bool visible = true;
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
};

using UiRuntimeNode = std::variant<UiImageNode, UiButtonNode, UiProgressBarNode>;

class UIRuntime {
public:
    struct ButtonAction {
        std::string target;
        std::string method;
    };

    void Clear();
    void SetViewportSize(uint32_t width, uint32_t height);
    void RebuildFromScene(project::SceneData const& scene);
    void Update(float delta_time);
    void BuildFrameUi(std::vector<core::FrameUiData>& out_items) const;
    std::optional<ButtonAction> HandlePointerUp(float x_px, float y_px) const;

private:
    struct UiTransform {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
    };

    UiTransform ResolveUiTransform(project::GameObjectData const& object) const;

    std::vector<UiRuntimeNode> nodes_{};
    uint32_t viewport_width_ = 0;
    uint32_t viewport_height_ = 0;
};

} // namespace ave::ui
