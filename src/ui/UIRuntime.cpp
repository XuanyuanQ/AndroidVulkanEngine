#include "ave/ui/UIRuntime.h"
#include "LogUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <type_traits>

namespace ave::ui {

namespace {

glm::vec2 SanitizeUiSize(glm::vec2 const& size)
{
    return glm::vec2{
        std::max(size.x, 0.001f),
        std::max(size.y, 0.001f),
    };
}

glm::vec2 EstimateTextSize(project::TextComponentData const& text)
{
    float const font_size = std::max(text.size, 0.01f);
    float const width = std::max(static_cast<float>(text.value.size()) * font_size * 0.8f, font_size);
    return SanitizeUiSize({width, font_size});
}

glm::vec2 ResolveDefaultUiSize(project::ComponentData const& components, glm::vec3 const& transform_scale)
{
    if (components.ui_layout.has_value() && components.ui_layout->size.x > 0.0f && components.ui_layout->size.y > 0.0f) {
        return SanitizeUiSize(components.ui_layout->size);
    }

    if (components.slider.has_value()) {
        return {0.52f, 0.08f};
    }

    if (components.progress_bar.has_value()) {
        return {0.50f, 0.06f};
    }

    if (components.button.has_value()) {
        return {0.32f, 0.12f};
    }

    if (components.text.has_value() && !components.image.has_value()) {
        return EstimateTextSize(*components.text);
    }

    if (components.image.has_value()) {
        return SanitizeUiSize({
            std::max(transform_scale.x * 0.25f, 0.05f),
            std::max(transform_scale.y * 0.25f, 0.05f),
        });
    }

    return {0.25f, 0.25f};
}

glm::vec2 MapInputNdcToUiNdc(float ndc_x, float ndc_y, uint32_t rotation)
{
    // 将输入的 uint32_t 转换为我们定义的枚举类型
    ScreenRotation rot = static_cast<ScreenRotation>(rotation & 3U);
    
    switch (rot) {
    case ScreenRotation::Rotation90:
        return {ndc_y, ndc_x};
    case ScreenRotation::Rotation180:
        return {-ndc_x, -ndc_y};
    case ScreenRotation::Rotation270:
        return {-ndc_y, -ndc_x};
    case ScreenRotation::Rotation0:
    default:
        return {ndc_x, ndc_y};
    }
}
} // namespace

void UIRuntime::Clear()
{
    nodes_.clear();
    object_to_node_.clear();
    pressed_button_id_.clear();
    active_slider_id_.clear();
}

void UIRuntime::SetViewportSize(uint32_t width, uint32_t height)
{
    viewport_width_ = width;
    viewport_height_ = height;
    if (input_viewport_width_ == 0 || input_viewport_height_ == 0) {
        input_viewport_width_ = width;
        input_viewport_height_ = height;
    }
}

void UIRuntime::SetInputViewportSize(uint32_t width, uint32_t height, uint32_t rotation)
{
    if (width == 0 || height == 0) {
        return;
    }
    input_viewport_width_ = width;
    input_viewport_height_ = height;
    input_rotation_ = rotation & 3U;
}

void UIRuntime::RebuildFromScene(project::SceneData const& scene)
{
    nodes_.clear();
    object_to_node_.clear();

    LOGI("UIRuntime::RebuildFromScene: processing %zu objects", scene.objects.size());

    for (auto const& object : scene.objects) {
        auto const& components = object.components;
        auto const ui_transform = ResolveUiTransform(object);
        glm::vec2 const position{ui_transform.position.x, ui_transform.position.y};
        glm::vec2 const layout_size = ResolveDefaultUiSize(components, ui_transform.scale);

        if (components.image.has_value() && components.button.has_value()) {
            auto const& image = *components.image;
            auto const& button = *components.button;

            UiButtonNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.texture_id = image.texture;
            node.position = position;
            node.size = layout_size;
            node.rotation = ui_transform.rotation;
            node.scale = ui_transform.scale;
            node.color = image.color;
            node.pressed_color = image.color * glm::vec4{0.78f, 0.86f, 1.0f, 1.0f};
            if (components.text.has_value()) {
                auto const& text = *components.text;
                node.label = text.value;
                node.label_color = text.color;
                node.label_size = text.size;
            }
            node.depth = ui_transform.position.z;
            node.target = button.target;
            node.method = button.method;
            object_to_node_[node.object_id] = nodes_.size();
            nodes_.emplace_back(std::move(node));
            LOGI("UIRuntime::RebuildFromScene: created button '%s' (id=%s) with target=%s, method=%s",
                 node.debug_name.c_str(), node.object_id.c_str(), node.target.c_str(), node.method.c_str());
            continue;
        }

        if (components.image.has_value()) {
            auto const& image = *components.image;

            UiImageNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.texture_id = image.texture;
            node.position = position;
            node.size = layout_size;
            node.rotation = ui_transform.rotation;
            node.scale = ui_transform.scale;
            node.color = image.color;
            node.depth = ui_transform.position.z;
            object_to_node_[node.object_id] = nodes_.size();
            nodes_.emplace_back(std::move(node));
        }

        if (components.text.has_value() && !components.button.has_value()) {
            auto const& text = *components.text;

            UiTextNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.text = text.value;
            node.position = position;
            node.rotation = ui_transform.rotation;
            node.scale = ui_transform.scale;
            node.color = text.color;
            node.size = text.size;
            node.bounds_size = layout_size;
            node.depth = ui_transform.position.z;
            object_to_node_[node.object_id] = nodes_.size();
            nodes_.emplace_back(std::move(node));
        }

        if (components.progress_bar.has_value()) {
            auto const& progress_bar = *components.progress_bar;

            UiProgressBarNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.position = position;
            node.size = layout_size;
            node.rotation = ui_transform.rotation;
            node.scale = ui_transform.scale;
            node.depth = ui_transform.position.z;
            float const denominator = std::max(progress_bar.max_value - progress_bar.min_value, 0.0001f);
            float const normalized = std::clamp((progress_bar.value - progress_bar.min_value) / denominator, 0.0f, 1.0f);
            node.value = progress_bar.value;
            node.min_value = progress_bar.min_value;
            node.max_value = progress_bar.max_value;
            node.fill_color = {
                0.20f + 0.60f * normalized,
                0.75f,
                0.30f,
                1.0f,
            };
            object_to_node_[node.object_id] = nodes_.size();
            nodes_.emplace_back(std::move(node));
        }

        if (components.slider.has_value()) {
            auto const& slider = *components.slider;

            UiSliderNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.position = position;
            node.size = layout_size;
            node.rotation = ui_transform.rotation;
            node.scale = ui_transform.scale;
            node.depth = ui_transform.position.z;
            node.value = std::clamp(slider.value, slider.min_value, slider.max_value);
            node.min_value = slider.min_value;
            node.max_value = slider.max_value;
            node.target = slider.target;
            node.method = slider.method;
            object_to_node_[node.object_id] = nodes_.size();
            nodes_.emplace_back(std::move(node));
        }
    }
}

void UIRuntime::Update(float delta_time)
{
    float const safe_delta = std::clamp(delta_time, 0.0f, 0.1f);
    for (auto& node : nodes_) {
        if (!std::holds_alternative<UiProgressBarNode>(node)) {
            continue;
        }

        auto& progress = std::get<UiProgressBarNode>(node);
        if (!progress.auto_animate) {
            continue;
        }
        float const range = std::max(progress.max_value - progress.min_value, 0.0001f);
        progress.value += safe_delta * range * 0.35f;
        while (progress.value > progress.max_value) {
            progress.value -= range;
        }

        float const normalized = std::clamp((progress.value - progress.min_value) / range, 0.0f, 1.0f);
        progress.fill_color = {
            0.20f + 0.60f * normalized,
            0.75f,
            0.30f,
            1.0f,
        };
    }
}

void UIRuntime::BuildFrameUi(std::vector<core::FrameUiData>& out_items) const
{
    out_items.clear();
    out_items.reserve(nodes_.size() * 2);

    for (auto const& node : nodes_) {
        std::visit([this, &out_items](auto const& typed_node) {
            using T = std::decay_t<decltype(typed_node)>;
            if constexpr (std::is_same_v<T, UiImageNode>) {
                core::FrameUiData item{};
                item.object_id = typed_node.object_id;
                item.debug_name = typed_node.debug_name;
                item.texture_id = typed_node.texture_id;
                item.position = typed_node.position;
                item.size = typed_node.size;
                item.color = typed_node.color;
                item.depth = typed_node.depth;
                item.visible = typed_node.visible;
                item.interactable = false;
                item.kind = core::FrameUiData::Kind::Image;
                out_items.push_back(std::move(item));
            } else if constexpr (std::is_same_v<T, UiButtonNode>) {
                core::FrameUiData item{};
                item.object_id = typed_node.object_id;
                item.debug_name = typed_node.debug_name;
                item.texture_id = typed_node.texture_id;
                item.position = typed_node.position;
                item.size = typed_node.size;
                item.color = typed_node.pressed ? typed_node.pressed_color : typed_node.color;
                item.depth = typed_node.depth;
                item.visible = typed_node.visible;
                item.interactable = typed_node.interactable;
                item.kind = core::FrameUiData::Kind::ButtonBackground;
                out_items.push_back(std::move(item));
                if (!typed_node.label.empty()) {
                    AppendTextItems(out_items,
                                    typed_node.object_id,
                                    typed_node.debug_name + " Label",
                                    typed_node.label,
                                    typed_node.position,
                                    typed_node.label_color,
                                    typed_node.label_size,
                                    typed_node.depth + 0.002f,
                                    core::FrameUiData::Kind::ButtonLabel);
                }
            } else if constexpr (std::is_same_v<T, UiTextNode>) {
                AppendTextItems(out_items,
                                typed_node.object_id,
                                typed_node.debug_name,
                                typed_node.text,
                                typed_node.position,
                                typed_node.color,
                                typed_node.size,
                                typed_node.depth,
                                core::FrameUiData::Kind::TextGlyph);
            } else if constexpr (std::is_same_v<T, UiProgressBarNode>) {
                core::FrameUiData background{};
                background.object_id = typed_node.object_id;
                background.debug_name = typed_node.debug_name + " Background";
                background.position = typed_node.position;
                background.size = typed_node.size;
                background.color = typed_node.background_color;
                background.depth = typed_node.depth;
                background.visible = typed_node.visible;
                background.kind = core::FrameUiData::Kind::ProgressBarBackground;
                out_items.push_back(std::move(background));

                float const denominator = std::max(typed_node.max_value - typed_node.min_value, 0.0001f);
                float const normalized = std::clamp((typed_node.value - typed_node.min_value) / denominator, 0.0f, 1.0f);
                core::FrameUiData fill{};
                fill.object_id = typed_node.object_id;
                fill.debug_name = typed_node.debug_name + " Fill";
                fill.position = typed_node.position;
                fill.size = typed_node.size;
                fill.color = typed_node.fill_color;
                fill.fill_amount = normalized;
                fill.depth = typed_node.depth + 0.001f;
                fill.visible = typed_node.visible && normalized > 0.0f;
                fill.kind = core::FrameUiData::Kind::ProgressBarFill;
                out_items.push_back(std::move(fill));
            } else if constexpr (std::is_same_v<T, UiSliderNode>) {
                core::FrameUiData background{};
                background.object_id = typed_node.object_id;
                background.debug_name = typed_node.debug_name + " Background";
                background.position = typed_node.position;
                background.size = typed_node.size;
                background.color = typed_node.background_color;
                background.depth = typed_node.depth;
                background.visible = typed_node.visible;
                background.interactable = typed_node.interactable;
                background.kind = core::FrameUiData::Kind::ProgressBarBackground;
                out_items.push_back(std::move(background));

                float const denominator = std::max(typed_node.max_value - typed_node.min_value, 0.0001f);
                float const normalized = std::clamp((typed_node.value - typed_node.min_value) / denominator, 0.0f, 1.0f);
                core::FrameUiData fill{};
                fill.object_id = typed_node.object_id;
                fill.debug_name = typed_node.debug_name + " Fill";
                fill.position = typed_node.position;
                fill.size = typed_node.size;
                fill.color = typed_node.fill_color;
                fill.fill_amount = normalized;
                fill.depth = typed_node.depth + 0.001f;
                fill.visible = typed_node.visible && normalized > 0.0f;
                fill.interactable = typed_node.interactable;
                fill.kind = core::FrameUiData::Kind::ProgressBarFill;
                out_items.push_back(std::move(fill));
            }
        }, node);
    }

    std::stable_sort(out_items.begin(), out_items.end(), [](core::FrameUiData const& a, core::FrameUiData const& b) {
        if (a.render_queue != b.render_queue) {
            return a.render_queue < b.render_queue;
        }
        return a.depth < b.depth;
    });
}

std::optional<UIRuntime::UiAction> UIRuntime::HandlePointerDown(float x_px, float y_px)
{
    pressed_button_id_.clear();
    active_slider_id_.clear();

    if (UiSliderNode const* slider = HitTestSlider(x_px, y_px)) {
        active_slider_id_ = slider->object_id;
        if (auto* mutable_slider = FindSliderNode(active_slider_id_)) {
            mutable_slider->value = SliderValueFromPointer(*mutable_slider, x_px, y_px);
            LOGI("HandlePointerDown: slider '%s' hit value=%.4f", mutable_slider->debug_name.c_str(), mutable_slider->value);
            return UiAction{
                .type = ActionType::ValueChanged,
                .target = mutable_slider->target,
                .method = mutable_slider->method,
                .source_id = mutable_slider->object_id,
                .value = mutable_slider->value,
            };
        }
        return UiAction{.type = ActionType::ValueChanged};
    }

    UiButtonNode const* button = HitTestButton(x_px, y_px);
    if (button == nullptr) {
        LOGD("HandlePointerDown: no button hit at (%.2f, %.2f)", x_px, y_px);
        return std::nullopt;
    }

    pressed_button_id_ = button->object_id;
    if (auto* mutable_button = FindButtonNode(pressed_button_id_)) {
        mutable_button->pressed = true;
    }
    LOGI("HandlePointerDown: button '%s' hit at (%.2f, %.2f), target=%s, method=%s",
         button->debug_name.c_str(), x_px, y_px, button->target.c_str(), button->method.c_str());
    return UiAction{.type = ActionType::None};
}

std::optional<UIRuntime::UiAction> UIRuntime::HandlePointerMove(float x_px, float y_px)
{
    if (!pressed_button_id_.empty()) {
        return UiAction{.type = ActionType::None};
    }

    if (active_slider_id_.empty()) {
        return std::nullopt;
    }

    auto* slider = FindSliderNode(active_slider_id_);
    if (slider == nullptr) {
        active_slider_id_.clear();
        return std::nullopt;
    }

    slider->value = SliderValueFromPointer(*slider, x_px, y_px);
    return UiAction{
        .type = ActionType::ValueChanged,
        .target = slider->target,
        .method = slider->method,
        .source_id = slider->object_id,
        .value = slider->value,
    };
}

std::optional<UIRuntime::UiAction> UIRuntime::HandlePointerCancel()
{
    if (auto* button = FindButtonNode(pressed_button_id_)) {
        button->pressed = false;
    }
    bool const had_capture = !pressed_button_id_.empty() || !active_slider_id_.empty();
    pressed_button_id_.clear();
    active_slider_id_.clear();
    return had_capture ? std::optional<UiAction>{UiAction{.type = ActionType::None}} : std::nullopt;
}

std::optional<UIRuntime::UiAction> UIRuntime::HandlePointerUp(float x_px, float y_px)
{
    if (!active_slider_id_.empty()) {
        auto* slider = FindSliderNode(active_slider_id_);
        active_slider_id_.clear();
        if (slider == nullptr) {
            return UiAction{.type = ActionType::None};
        }
        slider->value = SliderValueFromPointer(*slider, x_px, y_px);
        return UiAction{
            .type = ActionType::ValueChanged,
            .target = slider->target,
            .method = slider->method,
            .source_id = slider->object_id,
            .value = slider->value,
        };
    }

    if (!pressed_button_id_.empty()) {
        UiButtonNode const* pressed_button = FindButtonNode(pressed_button_id_);
        if (pressed_button == nullptr) {
            HandlePointerCancel();
            return UiAction{.type = ActionType::None};
        }

        UiAction action{
            .type = ActionType::Click,
            .target = pressed_button->target,
            .method = pressed_button->method,
            .source_id = pressed_button->object_id,
        };
        LOGI("HandlePointerUp: captured button '%s' clicked, target=%s, method=%s",
             pressed_button->debug_name.c_str(), pressed_button->target.c_str(), pressed_button->method.c_str());
        HandlePointerCancel();
        return action;
    }

    LOGD("HandlePointerUp: no captured button, ignoring release at (%.2f, %.2f)", x_px, y_px);
    HandlePointerCancel();
    return std::nullopt;
}

UiButtonNode* UIRuntime::FindButtonNode(std::string const& object_id)
{
    for (auto& node : nodes_) {
        if (!std::holds_alternative<UiButtonNode>(node)) {
            continue;
        }
        auto& button = std::get<UiButtonNode>(node);
        if (button.object_id == object_id) {
            return &button;
        }
    }
    return nullptr;
}

UiSliderNode* UIRuntime::FindSliderNode(std::string const& object_id)
{
    for (auto& node : nodes_) {
        if (!std::holds_alternative<UiSliderNode>(node)) {
            continue;
        }
        auto& slider = std::get<UiSliderNode>(node);
        if (slider.object_id == object_id) {
            return &slider;
        }
    }
    return nullptr;
}

UiButtonNode const* UIRuntime::HitTestButton(float x_px, float y_px) const
{
    uint32_t const input_width = input_viewport_width_ != 0 ? input_viewport_width_ : viewport_width_;
    uint32_t const input_height = input_viewport_height_ != 0 ? input_viewport_height_ : viewport_height_;
    if (input_width == 0 || input_height == 0) {
        LOGD("HitTestButton: input viewport is zero");
        return nullptr;
    }

    float const raw_ndc_x = (x_px / static_cast<float>(input_width)) * 2.0f - 1.0f;
    float const raw_ndc_y = 1.0f - (y_px / static_cast<float>(input_height)) * 2.0f;
    glm::vec2 const ui_ndc = MapInputNdcToUiNdc(raw_ndc_x, raw_ndc_y, input_rotation_);
    float const ndc_x = ui_ndc.x;
    float const ndc_y = ui_ndc.y;
    float const aspect_ratio = static_cast<float>(input_height) / static_cast<float>(input_width);

    LOGD("HitTestButton: px=(%.2f, %.2f) -> raw_ndc=(%.4f, %.4f), ui_ndc=(%.4f, %.4f), input viewport=%ux%u, rotation=%u, aspect=%.4f",
         x_px, y_px, raw_ndc_x, raw_ndc_y, ndc_x, ndc_y, input_width, input_height, input_rotation_, aspect_ratio);

    for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
        if (!std::holds_alternative<UiButtonNode>(*it)) {
            continue;
        }

        auto const& button = std::get<UiButtonNode>(*it);
        if (!button.visible || !button.interactable) {
            LOGD("HitTestButton: button '%s' skipped (visible=%d, interactable=%d)",
                 button.debug_name.c_str(), button.visible, button.interactable);
            continue;
        }

        float const hit_slop = 0.18f;
        float const half_w = (button.size.x / aspect_ratio) * 0.5f + hit_slop;
        float const half_h = button.size.y * 0.5f + hit_slop;

        float const left = button.position.x - half_w;
        float const right = button.position.x + half_w;
        float const top = button.position.y + half_h;
        float const bottom = button.position.y - half_h;

        LOGD("HitTestButton: button '%s' pos=(%.4f, %.4f) size=(%.4f, %.4f) bounds=[(%.4f, %.4f) to (%.4f, %.4f)]",
             button.debug_name.c_str(), button.position.x, button.position.y,
             button.size.x, button.size.y, left, bottom, right, top);

        bool const hit =
            ndc_x >= left && ndc_x <= right &&
            ndc_y >= bottom && ndc_y <= top;

        if (hit) {
            LOGI("HitTestButton: button '%s' HIT!", button.debug_name.c_str());
            return &button;
        }
    }

    LOGD("HitTestButton: no button hit");
    return nullptr;
}

UiSliderNode const* UIRuntime::HitTestSlider(float x_px, float y_px) const
{
    uint32_t const input_width = input_viewport_width_ != 0 ? input_viewport_width_ : viewport_width_;
    uint32_t const input_height = input_viewport_height_ != 0 ? input_viewport_height_ : viewport_height_;
    if (input_width == 0 || input_height == 0) {
        return nullptr;
    }

    float const raw_ndc_x = (x_px / static_cast<float>(input_width)) * 2.0f - 1.0f;
    float const raw_ndc_y = 1.0f - (y_px / static_cast<float>(input_height)) * 2.0f;
    glm::vec2 const ui_ndc = MapInputNdcToUiNdc(raw_ndc_x, raw_ndc_y, input_rotation_);
    float const ndc_x = ui_ndc.x;
    float const ndc_y = ui_ndc.y;
    float const aspect_ratio = static_cast<float>(input_height) / static_cast<float>(input_width);

    for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
        if (!std::holds_alternative<UiSliderNode>(*it)) {
            continue;
        }

        auto const& slider = std::get<UiSliderNode>(*it);
        if (!slider.visible || !slider.interactable) {
            continue;
        }

        float const half_w = (slider.size.x / aspect_ratio) * 0.5f;
        float const half_h = std::max(slider.size.y, 0.08f) * 0.5f;
        bool const hit =
            ndc_x >= slider.position.x - half_w && ndc_x <= slider.position.x + half_w &&
            ndc_y >= slider.position.y - half_h && ndc_y <= slider.position.y + half_h;
        if (hit) {
            return &slider;
        }
    }

    return nullptr;
}

float UIRuntime::SliderValueFromPointer(UiSliderNode const& slider, float x_px, float y_px) const
{
    uint32_t const input_width = input_viewport_width_ != 0 ? input_viewport_width_ : viewport_width_;
    uint32_t const input_height = input_viewport_height_ != 0 ? input_viewport_height_ : viewport_height_;
    if (input_width == 0 || input_height == 0) {
        return slider.value;
    }
    float const raw_ndc_x = (x_px / static_cast<float>(input_width)) * 2.0f - 1.0f;
    float const raw_ndc_y = 1.0f - (y_px / static_cast<float>(input_height)) * 2.0f;
    float const ndc_x = MapInputNdcToUiNdc(raw_ndc_x, raw_ndc_y, input_rotation_).x;
    float const aspect_ratio = static_cast<float>(input_height) / static_cast<float>(input_width);
    float const half_w = (slider.size.x / aspect_ratio) * 0.5f;
    if (half_w <= 0.0001f) {
        return slider.value;
    }

    float const normalized = std::clamp((ndc_x - (slider.position.x - half_w)) / (half_w * 2.0f), 0.0f, 1.0f);
    return slider.min_value + (slider.max_value - slider.min_value) * normalized;
}

void UIRuntime::AppendTextItems(std::vector<core::FrameUiData>& out_items,
                                std::string const& object_id,
                                std::string const& debug_name,
                                std::string const& text,
                                glm::vec2 position,
                                glm::vec4 color,
                                float size,
                                float depth,
                                core::FrameUiData::Kind kind) const
{
    if (text.empty()) {
        return;
    }

    float const font_size = std::max(size, 0.01f);
    float const char_width = font_size * 0.8f;
    float const total_width = char_width * static_cast<float>(text.size());
    float const start_x = position.x - total_width * 0.5f + char_width * 0.5f;

    for (size_t i = 0; i < text.size(); ++i) {
        uint8_t const c = static_cast<uint8_t>(text[i]);
        
        // Calculate UV coordinates in the 16x8 glyph atlas (128 ASCII cells)
        uint32_t const col = c % 16;
        uint32_t const row = c / 16;
        float const u_min = static_cast<float>(col) / 16.0f;
        float const u_max = static_cast<float>(col + 1) / 16.0f;
        float const v_min = static_cast<float>(row) / 8.0f;
        float const v_max = static_cast<float>(row + 1) / 8.0f;

        core::FrameUiData glyph{};
        glyph.object_id = object_id;
        glyph.debug_name = debug_name + " Char " + std::to_string(i);
        glyph.texture_id = "__ave_font_atlas";
        glyph.position = { start_x + static_cast<float>(i) * char_width, position.y };
        glyph.size = { char_width, font_size };
        glyph.uv_min = { u_min, v_max };
        glyph.uv_max = { u_max, v_min };
        glyph.color = color;
        glyph.depth = depth - 0.0001f * static_cast<float>(i);
        glyph.visible = true;
        glyph.kind = kind;

        out_items.push_back(std::move(glyph));
    }
}

bool UIRuntime::SetObjectPosition(std::string const& object_id, glm::vec3 const& position)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&position](auto& node) {
        node.position = {position.x, position.y};
        node.depth = position.z;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::SetObjectRotation(std::string const& object_id, glm::vec3 const& rotation)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&rotation](auto& node) {
        node.rotation = rotation;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::SetObjectScale(std::string const& object_id, glm::vec3 const& scale)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&scale](auto& node) {
        node.scale = scale;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::SetObjectVisible(std::string const& object_id, bool visible)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([visible](auto& node) {
        node.visible = visible;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::SetObjectTexture(std::string const& object_id, std::string const& texture_id)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    return std::visit([&texture_id](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiSliderNode> || std::is_same_v<T, UiTextNode>) {
            return false;
        } else {
            node.texture_id = texture_id;
            return true;
        }
    }, nodes_[index]);
}

bool UIRuntime::SetObjectColor(std::string const& object_id, glm::vec4 const& color)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&color](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiSliderNode>) {
            node.fill_color = color;
        } else {
            node.color = color;
        }
    }, nodes_[index]);
    return true;
}

bool UIRuntime::SetObjectProgress(std::string const& object_id, float value)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    return std::visit([value](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
            node.value = std::clamp(value, node.min_value, node.max_value);
            node.auto_animate = false;
            return true;
        } else if constexpr (std::is_same_v<T, UiSliderNode>) {
            node.value = std::clamp(value, node.min_value, node.max_value);
            return true;
        } else {
            return false;
        }
    }, nodes_[index]);
}

bool UIRuntime::GetObjectPosition(std::string const& object_id, glm::vec3& out_position) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&out_position](auto const& node) {
        out_position = {node.position.x, node.position.y, node.depth};
    }, nodes_[index]);
    return true;
}

bool UIRuntime::GetObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&out_rotation](auto const& node) {
        out_rotation = node.rotation;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::GetObjectScale(std::string const& object_id, glm::vec3& out_scale) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&out_scale](auto const& node) {
        out_scale = node.scale;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::GetObjectVisible(std::string const& object_id, bool& out_visible) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&out_visible](auto const& node) {
        out_visible = node.visible;
    }, nodes_[index]);
    return true;
}

bool UIRuntime::GetObjectTexture(std::string const& object_id, std::string& out_texture_id) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    return std::visit([&out_texture_id](auto const& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiSliderNode> || std::is_same_v<T, UiTextNode>) {
            return false;
        } else {
            out_texture_id = node.texture_id;
            return true;
        }
    }, nodes_[index]);
}

bool UIRuntime::GetObjectColor(std::string const& object_id, glm::vec4& out_color) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&out_color](auto const& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiSliderNode>) {
            out_color = node.fill_color;
        } else {
            out_color = node.color;
        }
    }, nodes_[index]);
    return true;
}

bool UIRuntime::GetObjectProgress(std::string const& object_id, float& out_value) const
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    return std::visit([&out_value](auto const& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiSliderNode>) {
            out_value = node.value;
            return true;
        } else {
            return false;
        }
    }, nodes_[index]);
}

UIRuntime::UiTransform UIRuntime::ResolveUiTransform(project::GameObjectData const& object) const
{
    UiTransform transform{};
    if (object.components.transform.has_value()) {
        auto const& source = *object.components.transform;
        transform.position = source.position;
        transform.rotation = source.rotation;
        transform.scale = source.scale;
    }
    return transform;
}

size_t UIRuntime::FindNodeIndex(std::string const& object_id) const
{
    auto const found = object_to_node_.find(object_id);
    return found != object_to_node_.end() ? found->second : nodes_.size();
}

void UIRuntime::RefreshNodeSize(UiRuntimeNode& node)
{
    std::visit([](auto& typed_node) {
        using T = std::decay_t<decltype(typed_node)>;
        if constexpr (std::is_same_v<T, UiTextNode>) {
            typed_node.size = std::max(typed_node.size, 0.01f);
            typed_node.bounds_size = SanitizeUiSize(typed_node.bounds_size);
        } else {
            typed_node.size = SanitizeUiSize(typed_node.size);
        }
    }, node);
}

} // namespace ave::ui
