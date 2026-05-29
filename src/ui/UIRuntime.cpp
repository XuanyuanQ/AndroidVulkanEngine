#include "ave/ui/UIRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <type_traits>

namespace ave::ui {

namespace {

glm::vec2 ClampUiPosition(glm::vec2 const& position)
{
    return glm::vec2{
        std::clamp(position.x, -1.0f, 1.0f),
        std::clamp(position.y, -1.0f, 1.0f),
    };
}

glm::vec2 ClampUiSize(glm::vec2 const& size)
{
    return glm::vec2{
        std::clamp(size.x, 0.01f, 2.0f),
        std::clamp(size.y, 0.01f, 2.0f),
    };
}

uint8_t GlyphColumn(char c, int column)
{
    static constexpr std::array<uint8_t, 5> kSpace{0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr std::array<uint8_t, 5> kUnknown{0x7f, 0x41, 0x5d, 0x41, 0x7f};

    static constexpr std::array<std::array<uint8_t, 5>, 10> kDigits{{
        {0x3e, 0x51, 0x49, 0x45, 0x3e}, // 0
        {0x00, 0x42, 0x7f, 0x40, 0x00}, // 1
        {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
        {0x21, 0x41, 0x45, 0x4b, 0x31}, // 3
        {0x18, 0x14, 0x12, 0x7f, 0x10}, // 4
        {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
        {0x3c, 0x4a, 0x49, 0x49, 0x30}, // 6
        {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
        {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
        {0x06, 0x49, 0x49, 0x29, 0x1e}, // 9
    }};

    static constexpr std::array<std::array<uint8_t, 5>, 26> kLetters{{
        {0x7e, 0x11, 0x11, 0x11, 0x7e}, // A
        {0x7f, 0x49, 0x49, 0x49, 0x36}, // B
        {0x3e, 0x41, 0x41, 0x41, 0x22}, // C
        {0x7f, 0x41, 0x41, 0x22, 0x1c}, // D
        {0x7f, 0x49, 0x49, 0x49, 0x41}, // E
        {0x7f, 0x09, 0x09, 0x09, 0x01}, // F
        {0x3e, 0x41, 0x49, 0x49, 0x7a}, // G
        {0x7f, 0x08, 0x08, 0x08, 0x7f}, // H
        {0x00, 0x41, 0x7f, 0x41, 0x00}, // I
        {0x20, 0x40, 0x41, 0x3f, 0x01}, // J
        {0x7f, 0x08, 0x14, 0x22, 0x41}, // K
        {0x7f, 0x40, 0x40, 0x40, 0x40}, // L
        {0x7f, 0x02, 0x0c, 0x02, 0x7f}, // M
        {0x7f, 0x04, 0x08, 0x10, 0x7f}, // N
        {0x3e, 0x41, 0x41, 0x41, 0x3e}, // O
        {0x7f, 0x09, 0x09, 0x09, 0x06}, // P
        {0x3e, 0x41, 0x51, 0x21, 0x5e}, // Q
        {0x7f, 0x09, 0x19, 0x29, 0x46}, // R
        {0x46, 0x49, 0x49, 0x49, 0x31}, // S
        {0x01, 0x01, 0x7f, 0x01, 0x01}, // T
        {0x3f, 0x40, 0x40, 0x40, 0x3f}, // U
        {0x1f, 0x20, 0x40, 0x20, 0x1f}, // V
        {0x7f, 0x20, 0x18, 0x20, 0x7f}, // W
        {0x63, 0x14, 0x08, 0x14, 0x63}, // X
        {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
        {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    }};

    char const upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper == ' ') {
        return kSpace[static_cast<size_t>(column)];
    }
    if (upper >= '0' && upper <= '9') {
        return kDigits[static_cast<size_t>(upper - '0')][static_cast<size_t>(column)];
    }
    if (upper >= 'A' && upper <= 'Z') {
        return kLetters[static_cast<size_t>(upper - 'A')][static_cast<size_t>(column)];
    }
    return kUnknown[static_cast<size_t>(column)];
}

} // namespace

void UIRuntime::Clear()
{
    nodes_.clear();
    object_to_node_.clear();
    pressed_button_id_.clear();
}

void UIRuntime::SetViewportSize(uint32_t width, uint32_t height)
{
    viewport_width_ = width;
    viewport_height_ = height;
}

void UIRuntime::RebuildFromScene(project::SceneData const& scene)
{
    nodes_.clear();
    object_to_node_.clear();

    for (auto const& object : scene.objects) {
        auto const& components = object.components;
        auto const ui_transform = ResolveUiTransform(object);
        glm::vec2 const position = ClampUiPosition({ui_transform.position.x, ui_transform.position.y});
        glm::vec2 const image_size{
            std::max(ui_transform.scale.x * 0.25f, 0.05f),
            std::max(ui_transform.scale.y * 0.25f, 0.05f),
        };

        if (components.image.has_value() && components.button.has_value()) {
            auto const& image = *components.image;
            auto const& button = *components.button;

            UiButtonNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.texture_id = image.texture;
            node.position = position;
            node.size = ClampUiSize(image_size);
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
            continue;
        }

        if (components.image.has_value()) {
            auto const& image = *components.image;

            UiImageNode node{};
            node.object_id = object.id;
            node.debug_name = object.name.empty() ? object.id : object.name;
            node.texture_id = image.texture;
            node.position = position;
            node.size = ClampUiSize(image_size);
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
            node.size = ClampUiSize({
                std::max(ui_transform.scale.x * 0.60f, 0.10f),
                std::max(ui_transform.scale.y * 0.08f, 0.02f),
            });
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
    }
}

void UIRuntime::Update(float delta_time)
{
    (void)delta_time;
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
                fill.size = {typed_node.size.x * normalized, typed_node.size.y};
                fill.color = typed_node.fill_color;
                fill.depth = typed_node.depth + 0.001f;
                fill.visible = typed_node.visible && normalized > 0.0f;
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

bool UIRuntime::HandlePointerDown(float x_px, float y_px)
{
    pressed_button_id_.clear();
    UiButtonNode const* button = HitTestButton(x_px, y_px);
    if (button == nullptr) {
        return false;
    }

    pressed_button_id_ = button->object_id;
    if (auto* mutable_button = FindButtonNode(pressed_button_id_)) {
        mutable_button->pressed = true;
    }
    return true;
}

void UIRuntime::HandlePointerCancel()
{
    if (auto* button = FindButtonNode(pressed_button_id_)) {
        button->pressed = false;
    }
    pressed_button_id_.clear();
}

std::optional<UIRuntime::ButtonAction> UIRuntime::HandlePointerUp(float x_px, float y_px)
{
    UiButtonNode const* button = HitTestButton(x_px, y_px);
    if (button == nullptr) {
        HandlePointerCancel();
        return std::nullopt;
    }
    if (!pressed_button_id_.empty() && pressed_button_id_ != button->object_id) {
        HandlePointerCancel();
        return std::nullopt;
    }

    ButtonAction action{
        .target = button->target,
        .method = button->method,
    };
    HandlePointerCancel();
    return action;
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

UiButtonNode const* UIRuntime::HitTestButton(float x_px, float y_px) const
{
    if (viewport_width_ == 0 || viewport_height_ == 0) {
        return nullptr;
    }

    float const ndc_x = (x_px / static_cast<float>(viewport_width_)) * 2.0f - 1.0f;
    float const ndc_y = 1.0f - (y_px / static_cast<float>(viewport_height_)) * 2.0f;
    float const aspect_ratio = static_cast<float>(viewport_height_) / static_cast<float>(viewport_width_);

    for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
        if (!std::holds_alternative<UiButtonNode>(*it)) {
            continue;
        }

        auto const& button = std::get<UiButtonNode>(*it);
        if (!button.visible || !button.interactable) {
            continue;
        }

        float const half_w = (button.size.x / aspect_ratio) * 0.5f;
        float const half_h = button.size.y * 0.5f;

        bool const hit =
            ndc_x >= (button.position.x - half_w) && ndc_x <= (button.position.x + half_w) &&
            ndc_y >= (button.position.y - half_h) && ndc_y <= (button.position.y + half_h);

        if (hit) {
            return &button;
        }
    }

    return nullptr;
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

    float const cell = std::max(size, 0.01f) / 7.0f;
    float const char_width = cell * 6.0f;
    float const total_width = char_width * static_cast<float>(text.size());
    glm::vec2 const start{
        position.x - total_width * 0.5f + cell * 0.5f,
        position.y + cell * 3.0f,
    };

    for (size_t char_index = 0; char_index < text.size(); ++char_index) {
        char const c = text[char_index];
        for (int column = 0; column < 5; ++column) {
            uint8_t const bits = GlyphColumn(c, column);
            for (int row = 0; row < 7; ++row) {
                if ((bits & (1u << row)) == 0) {
                    continue;
                }

                core::FrameUiData glyph{};
                glyph.object_id = object_id;
                glyph.debug_name = debug_name;
                glyph.position = {
                    start.x + static_cast<float>(char_index) * char_width + static_cast<float>(column) * cell,
                    start.y - static_cast<float>(row) * cell,
                };
                glyph.size = {cell * 0.82f, cell * 0.82f};
                glyph.color = color;
                glyph.depth = depth - 0.0001f * static_cast<float>(char_index * 35 + static_cast<size_t>(row * 5 + column));
                glyph.visible = true;
                glyph.kind = kind;
                out_items.push_back(std::move(glyph));
            }
        }
    }
}

bool UIRuntime::SetObjectPosition(std::string const& object_id, glm::vec3 const& position)
{
    size_t const index = FindNodeIndex(object_id);
    if (index >= nodes_.size()) {
        return false;
    }

    std::visit([&position](auto& node) {
        node.position = ClampUiPosition({position.x, position.y});
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
    RefreshNodeSize(nodes_[index]);
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
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiTextNode>) {
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
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
            node.fill_color = color;
        } else {
            node.color = color;
        }
    }, nodes_[index]);
    return true;
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
        if constexpr (std::is_same_v<T, UiProgressBarNode> || std::is_same_v<T, UiTextNode>) {
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
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
            out_color = node.fill_color;
        } else {
            out_color = node.color;
        }
    }, nodes_[index]);
    return true;
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
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
            typed_node.size = ClampUiSize({
                std::max(typed_node.scale.x * 0.60f, 0.10f),
                std::max(typed_node.scale.y * 0.08f, 0.02f),
            });
        } else if constexpr (std::is_same_v<T, UiTextNode>) {
            typed_node.size = std::max(typed_node.scale.y * 0.08f, 0.01f);
        } else {
            typed_node.size = ClampUiSize({
                std::max(typed_node.scale.x * 0.25f, 0.05f),
                std::max(typed_node.scale.y * 0.25f, 0.05f),
            });
        }
    }, node);
}

} // namespace ave::ui
