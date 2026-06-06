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

// 定义屏幕旋转方向的强类型枚举
enum class ScreenRotation : uint32_t {
    Rotation0   = 0U, // 默认方向（不旋转）
    Rotation90  = 1U, // 顺时针旋转 90 度
    Rotation180 = 2U, // 顺时针旋转 180 度
    Rotation270 = 3U  // 顺时针旋转 270 度
};

struct UiImageNode {
    std::string object_id;
    std::string debug_name;
    std::string texture_id;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{100.0f, 100.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
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
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 pressed_color{0.70f, 0.78f, 0.92f, 0.90f};
    std::string label;
    glm::vec4 label_color{1.0f, 1.0f, 1.0f, 1.0f};
    float label_size = 0.06f;
    float depth = 0.0f;
    bool visible = true;
    bool interactable = true;
    bool pressed = false;
    std::string target;
    std::string method;
};

struct UiTextNode {
    std::string object_id;
    std::string debug_name;
    std::string text;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float size = 0.08f;
    glm::vec2 bounds_size{0.25f, 0.10f};
    float depth = 0.0f;
    bool visible = true;
};

struct UiProgressBarNode {
    std::string object_id;
    std::string debug_name;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{160.0f, 24.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec4 background_color{0.18f, 0.18f, 0.20f, 0.9f};
    glm::vec4 fill_color{0.45f, 0.82f, 0.32f, 1.0f};
    float depth = 0.0f;
    bool visible = true;
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool auto_animate = true;
};

struct UiSliderNode {
    std::string object_id;
    std::string debug_name;
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{160.0f, 24.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec4 background_color{0.18f, 0.18f, 0.20f, 0.9f};
    glm::vec4 fill_color{0.35f, 0.65f, 1.0f, 1.0f};
    float depth = 0.0f;
    bool visible = true;
    bool interactable = true;
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    std::string target;
    std::string method;
};

using UiRuntimeNode = std::variant<UiImageNode, UiButtonNode, UiTextNode, UiProgressBarNode, UiSliderNode>;

class UIRuntime {
public:
    enum class ActionType {
        None,
        Click,
        ValueChanged,
    };

    struct UiAction {
        ActionType type = ActionType::None;
        std::string target;
        std::string method;
        std::string source_id;
        float value = 0.0f;
    };

    void Clear();
    void SetViewportSize(uint32_t width, uint32_t height);
    void SetInputViewportSize(uint32_t width, uint32_t height, uint32_t rotation);
    void RebuildFromScene(project::SceneData const& scene);
    void Update(float delta_time);
    void BuildFrameUi(std::vector<core::FrameUiData>& out_items) const;

    bool SetObjectPosition(std::string const& object_id, glm::vec3 const& position);
    bool SetObjectRotation(std::string const& object_id, glm::vec3 const& rotation);
    bool SetObjectScale(std::string const& object_id, glm::vec3 const& scale);
    bool SetObjectVisible(std::string const& object_id, bool visible);
    bool SetObjectTexture(std::string const& object_id, std::string const& texture_id);
    bool SetObjectText(std::string const& object_id, std::string const& text);
    bool SetObjectColor(std::string const& object_id, glm::vec4 const& color);
    bool SetObjectProgress(std::string const& object_id, float value);
    bool GetObjectPosition(std::string const& object_id, glm::vec3& out_position) const;
    bool GetObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const;
    bool GetObjectScale(std::string const& object_id, glm::vec3& out_scale) const;
    bool GetObjectVisible(std::string const& object_id, bool& out_visible) const;
    bool GetObjectTexture(std::string const& object_id, std::string& out_texture_id) const;
    bool GetObjectColor(std::string const& object_id, glm::vec4& out_color) const;
    bool GetObjectProgress(std::string const& object_id, float& out_value) const;
    std::optional<UiAction> HandlePointerDown(float x_px, float y_px);
    std::optional<UiAction> HandlePointerMove(float x_px, float y_px);
    std::optional<UiAction> HandlePointerCancel();
    std::optional<UiAction> HandlePointerUp(float x_px, float y_px);

private:
    struct UiTransform {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
    };

    UiTransform ResolveUiTransform(project::GameObjectData const& object) const;
    size_t FindNodeIndex(std::string const& object_id) const;
    void RefreshNodeSize(UiRuntimeNode& node);
    UiButtonNode* FindButtonNode(std::string const& object_id);
    UiSliderNode* FindSliderNode(std::string const& object_id);
    UiButtonNode const* HitTestButton(float x_px, float y_px) const;
    UiSliderNode const* HitTestSlider(float x_px, float y_px) const;
    float SliderValueFromPointer(UiSliderNode const& slider, float x_px, float y_px) const;
    void AppendTextItems(std::vector<core::FrameUiData>& out_items,
                         std::string const& object_id,
                         std::string const& debug_name,
                         std::string const& text,
                         glm::vec2 position,
                         glm::vec4 color,
                         float size,
                         float depth,
                         core::FrameUiData::Kind kind) const;

    std::vector<UiRuntimeNode> nodes_{};
    std::unordered_map<std::string, size_t> object_to_node_{};
    uint32_t viewport_width_ = 0;
    uint32_t viewport_height_ = 0;
    uint32_t input_viewport_width_ = 0;
    uint32_t input_viewport_height_ = 0;
    uint32_t input_rotation_ = 0;
    std::string pressed_button_id_;
    std::string active_slider_id_;
};

} // namespace ave::ui
