#include "ave/ui/UIRuntime.h"

#include <algorithm>
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

} // namespace

void UIRuntime::Clear()
{
    nodes_.clear();
    object_to_node_.clear();
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
        std::visit([&out_items](auto const& typed_node) {
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
                item.color = typed_node.color;
                item.depth = typed_node.depth;
                item.visible = typed_node.visible;
                item.interactable = typed_node.interactable;
                item.kind = core::FrameUiData::Kind::ButtonBackground;
                out_items.push_back(std::move(item));
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
                fill.depth = typed_node.depth - 0.001f;
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

std::optional<UIRuntime::ButtonAction> UIRuntime::HandlePointerUp(float x_px, float y_px) const
{
    if (viewport_width_ == 0 || viewport_height_ == 0) {
        return std::nullopt;
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
            return ButtonAction{
                .target = button.target,
                .method = button.method,
            };
        }
    }

    return std::nullopt;
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
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
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
        if constexpr (std::is_same_v<T, UiProgressBarNode>) {
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
        } else {
            typed_node.size = ClampUiSize({
                std::max(typed_node.scale.x * 0.25f, 0.05f),
                std::max(typed_node.scale.y * 0.25f, 0.05f),
            });
        }
    }, node);
}

} // namespace ave::ui
