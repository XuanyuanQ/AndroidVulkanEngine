#include "ave/project/XmlSceneLoader.h"
#include "LogUtil.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <cctype>
#include <optional>
#include <algorithm>
#include <glm/glm.hpp>

namespace ave::project {

namespace {

std::string StripXmlComments(std::string text)
{
    static std::regex const comment_pattern(R"(<!--[\s\S]*?-->)");
    return std::regex_replace(text, comment_pattern, "");
}

std::string Attribute(std::string const& tag, std::string const& name, std::string fallback = {})
{
    std::regex const pattern(name + "=\"([^\"]*)\"");
    std::smatch match;
    return std::regex_search(tag, match, pattern) ? match[1].str() : std::move(fallback);
}

std::unordered_map<std::string, std::string> Attributes(std::string const& tag)
{
    std::unordered_map<std::string, std::string> attributes;
    std::regex const pattern("(\\w+)=\"([^\"]*)\"");
    for (std::sregex_iterator it(tag.begin(), tag.end(), pattern), end; it != end; ++it) {
        attributes[(*it)[1].str()] = (*it)[2].str();
    }
    return attributes;
}

glm::vec3 Float3(std::string const& text, glm::vec3 fallback)
{
    if (text.empty()) {
        return fallback;
    }

    glm::vec3 value = fallback;
    char comma = 0;
    std::stringstream stream(text);
    stream >> value.x >> comma >> value.y >> comma >> value.z;
    return value;
}

glm::vec4 Float4(std::string const& text, glm::vec4 fallback)
{
    if (text.empty()) {
        return fallback;
    }

    glm::vec4 value = fallback;
    char comma = 0;
    std::stringstream stream(text);
    stream >> value.x >> comma >> value.y >> comma >> value.z >> comma >> value.w;
    return value;
}

glm::vec2 Float2(std::string const& text, glm::vec2 fallback)
{
    if (text.empty()) {
        return fallback;
    }

    glm::vec2 value = fallback;
    char comma = 0;
    std::stringstream stream(text);
    stream >> value.x >> comma >> value.y;
    return value;
}

std::vector<uint32_t> UIntList(std::string const& text)
{
    std::vector<uint32_t> values;
    std::stringstream stream(text);
    while (stream.good()) {
        uint32_t value = 0;
        char comma = 0;
        stream >> value;
        if (stream.fail()) {
            break;
        }
        values.push_back(value);
        stream >> comma;
    }
    return values;
}

std::vector<std::string> MatchTags(std::string const& text, std::string const& tag_name)
{
    std::vector<std::string> tags;
    std::regex const pattern("<" + tag_name + R"(\b[^>]*>)");
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        tags.push_back(it->str());
    }
    return tags;
}

std::vector<std::pair<std::string, std::string>> MatchTagBodies(std::string const& text, std::string const& tag_name)
{
    std::vector<std::pair<std::string, std::string>> matches;
    std::regex const pattern("<" + tag_name + R"(\b([^>]*)>([\s\S]*?)</)" + tag_name + ">");
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        matches.push_back({"<" + tag_name + (*it)[1].str() + ">", (*it)[2].str()});
    }
    return matches;
}

struct ElementMatch {
    std::string tag;
    std::string body;
    size_t start = 0;
    size_t end = 0;
};

size_t FindTagEnd(std::string const& text, size_t tag_start)
{
    bool in_quote = false;
    for (size_t i = tag_start; i < text.size(); ++i) {
        if (text[i] == '"') {
            in_quote = !in_quote;
        } else if (text[i] == '>' && !in_quote) {
            return i;
        }
    }
    return std::string::npos;
}

size_t FindTagStart(std::string const& text, std::string const& tag_name, size_t offset)
{
    std::string const needle = "<" + tag_name;
    size_t pos = offset;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        size_t const next = pos + needle.size();
        if (next >= text.size() || std::isspace(static_cast<unsigned char>(text[next])) || text[next] == '>' || text[next] == '/') {
            return pos;
        }
        pos = next;
    }
    return std::string::npos;
}

std::optional<ElementMatch> FindNextElement(std::string const& text, std::string const& tag_name, size_t offset)
{
    size_t const start = FindTagStart(text, tag_name, offset);
    if (start == std::string::npos) {
        return std::nullopt;
    }

    size_t const open_end = FindTagEnd(text, start);
    if (open_end == std::string::npos) {
        return std::nullopt;
    }

    std::string const open_tag = text.substr(start, open_end - start + 1);
    if (open_tag.size() >= 2 && open_tag[open_tag.size() - 2] == '/') {
        return ElementMatch{open_tag, "", start, open_end + 1};
    }

    std::string const close_tag = "</" + tag_name + ">";
    size_t cursor = open_end + 1;
    int depth = 1;
    while (cursor < text.size()) {
        size_t const next_open = FindTagStart(text, tag_name, cursor);
        size_t const next_close = text.find(close_tag, cursor);
        if (next_close == std::string::npos) {
            return std::nullopt;
        }

        if (next_open != std::string::npos && next_open < next_close) {
            size_t const nested_open_end = FindTagEnd(text, next_open);
            if (nested_open_end == std::string::npos) {
                return std::nullopt;
            }
            std::string const nested_open_tag = text.substr(next_open, nested_open_end - next_open + 1);
            if (!(nested_open_tag.size() >= 2 && nested_open_tag[nested_open_tag.size() - 2] == '/')) {
                ++depth;
            }
            cursor = nested_open_end + 1;
            continue;
        }

        --depth;
        if (depth == 0) {
            return ElementMatch{
                open_tag,
                text.substr(open_end + 1, next_close - open_end - 1),
                start,
                next_close + close_tag.size(),
            };
        }
        cursor = next_close + close_tag.size();
    }

    return std::nullopt;
}

std::vector<ElementMatch> DirectChildElements(std::string const& text, std::string const& tag_name)
{
    std::vector<ElementMatch> elements;
    size_t cursor = 0;
    while (auto element = FindNextElement(text, tag_name, cursor)) {
        elements.push_back(*element);
        cursor = element->end;
    }
    return elements;
}

std::string RemoveDirectChildElements(std::string const& text, std::string const& tag_name)
{
    std::string result;
    size_t cursor = 0;
    for (auto const& element : DirectChildElements(text, tag_name)) {
        result.append(text.substr(cursor, element.start - cursor));
        cursor = element.end;
    }
    result.append(text.substr(cursor));
    return result;
}

GameObjectData ParseGameObjectData(ElementMatch const& element, std::string const& parent_id)
{
    auto const& object_tag = element.tag;
    auto const component_body = RemoveDirectChildElements(element.body, "GameObject");

    GameObjectData object;
    object.id = Attribute(object_tag, "id");
    object.name = Attribute(object_tag, "name", object.id);
    object.hierarchy.parent = parent_id;

    auto transform_tags = MatchTags(component_body, "Transform");
    if (!transform_tags.empty()) {
        TransformData transform{};
        transform.position = Float3(Attribute(transform_tags.front(), "position"), transform.position);
        transform.rotation = Float3(Attribute(transform_tags.front(), "rotation"), transform.rotation);
        transform.scale = Float3(Attribute(transform_tags.front(), "scale"), transform.scale);
        object.components.transform = std::move(transform);
    }

    auto triangle_tags = MatchTags(component_body, "TriangleRenderer");
    if (!triangle_tags.empty()) {
        TriangleRendererData triangle{};
        triangle.color = Float4(Attribute(triangle_tags.front(), "color"), triangle.color);
        triangle.material = Attribute(triangle_tags.front(), "material");
        object.components.triangle_renderer = std::move(triangle);
    }

    auto mesh_tags = MatchTagBodies(component_body, "MeshRenderer");
    auto mesh_inline_tags = MatchTags(component_body, "MeshRenderer");
    if (!mesh_tags.empty() || !mesh_inline_tags.empty()) {
        MeshRendererData mesh{};
        std::string tag_text;
        std::string tag_body;
        if (!mesh_tags.empty()) {
            tag_text = mesh_tags.front().first;
            tag_body = mesh_tags.front().second;
        } else {
            tag_text = mesh_inline_tags.front();
        }
        mesh.mesh = Attribute(tag_text, "mesh");
        mesh.material = Attribute(tag_text, "material");
        mesh.topology = Attribute(tag_text, "topology", mesh.topology);

        std::string cs = Attribute(tag_text, "casts_shadow");
        if (cs.empty()) cs = Attribute(tag_text, "castsShadow");
        mesh.casts_shadow = cs.empty() || (cs == "true");

        std::string rs = Attribute(tag_text, "receives_shadow");
        if (rs.empty()) rs = Attribute(tag_text, "receivesShadow");
        mesh.receives_shadow = rs.empty() || (rs == "true");

        if (!tag_body.empty()) {
            for (auto const& vertex_tag : MatchTags(tag_body, "Vertex")) {
                VertexData vertex{};
                vertex.position = Float3(Attribute(vertex_tag, "position"), vertex.position);
                vertex.normal = Float3(Attribute(vertex_tag, "normal"), vertex.normal);
                vertex.tangent = Float4(Attribute(vertex_tag, "tangent"), vertex.tangent);
                vertex.texcoord0 = Float2(Attribute(vertex_tag, "texcoord0"), vertex.texcoord0);
                vertex.texcoord1 = Float2(Attribute(vertex_tag, "texcoord1"), vertex.texcoord1);
                vertex.color = Float4(Attribute(vertex_tag, "color"), vertex.color);
                mesh.vertices.push_back(vertex);
            }

            auto index_tags = MatchTagBodies(tag_body, "Indices");
            if (!index_tags.empty()) {
                mesh.indices = UIntList(index_tags.front().second);
            }
        }

        object.components.mesh_renderer = std::move(mesh);
    }

    auto camera_tags = MatchTags(component_body, "Camera");
    if (!camera_tags.empty()) {
        CameraData camera{};
        camera.fov = std::stof(Attribute(camera_tags.front(), "fov", std::to_string(camera.fov)));
        camera.near_plane = std::stof(Attribute(camera_tags.front(), "near", std::to_string(camera.near_plane)));
        camera.far_plane = std::stof(Attribute(camera_tags.front(), "far", std::to_string(camera.far_plane)));
        camera.clear_flags = Attribute(camera_tags.front(), "clearFlags", camera.clear_flags);
        object.components.camera = std::move(camera);
    }

    auto directional_light_tags = MatchTags(component_body, "DirectionalLight");
    if (!directional_light_tags.empty()) {
        LightData light{};
        light.type = LightType::Directional;
        light.color = Float3(Attribute(directional_light_tags.front(), "color"), light.color);
        light.intensity = std::stof(Attribute(directional_light_tags.front(), "intensity", std::to_string(light.intensity)));
        light.cast_shadows = Attribute(directional_light_tags.front(), "castShadows", "false") == "true";
        object.components.light = std::move(light);
    }

    auto point_light_tags = MatchTags(component_body, "PointLight");
    if (!point_light_tags.empty()) {
        LightData light{};
        light.type = LightType::Point;
        light.color = Float3(Attribute(point_light_tags.front(), "color"), light.color);
        light.intensity = std::stof(Attribute(point_light_tags.front(), "intensity", std::to_string(light.intensity)));
        light.range = std::stof(Attribute(point_light_tags.front(), "range", std::to_string(light.range)));
        light.cast_shadows = Attribute(point_light_tags.front(), "castShadows", "false") == "true";
        object.components.light = std::move(light);
    }

    auto spot_light_tags = MatchTags(component_body, "SpotLight");
    if (!spot_light_tags.empty()) {
        LightData light{};
        light.type = LightType::Spot;
        light.color = Float3(Attribute(spot_light_tags.front(), "color"), light.color);
        light.intensity = std::stof(Attribute(spot_light_tags.front(), "intensity", std::to_string(light.intensity)));
        light.range = std::stof(Attribute(spot_light_tags.front(), "range", std::to_string(light.range)));
        light.inner_angle = std::stof(Attribute(spot_light_tags.front(), "innerAngle", std::to_string(light.inner_angle)));
        light.outer_angle = std::stof(Attribute(spot_light_tags.front(), "outerAngle", std::to_string(light.outer_angle)));
        light.cast_shadows = Attribute(spot_light_tags.front(), "castShadows", "false") == "true";
        object.components.light = std::move(light);
    }

    auto script_tags = MatchTags(component_body, "Script");
    if (!script_tags.empty()) {
        LOGI("Found Script component in GameObject %s with class %s and method %s",
             object.id.c_str(),
             Attribute(script_tags.front(), "class").c_str(),
             Attribute(script_tags.front(), "method").c_str());
        ScriptBindingData script{};
        script.java_class = Attribute(script_tags.front(), "class");
        script.method = Attribute(script_tags.front(), "method");
        script.target_object = Attribute(script_tags.front(), "target", object.id);
        script.parameters = Attributes(script_tags.front());
        script.parameters["target"] = script.target_object;
        script.parameters.erase("class");
        script.parameters.erase("method");
        object.components.script = std::move(script);
    }

    auto button_tags = MatchTags(component_body, "Button");
    if (!button_tags.empty()) {
        ButtonComponentData button{};
        button.target = Attribute(button_tags.front(), "target");
        button.method = Attribute(button_tags.front(), "method");
        if (button.method.empty()) {
            button.method = Attribute(button_tags.front(), "onClick");
        }
        object.components.button = std::move(button);
    }

    auto ui_layout_tags = MatchTags(component_body, "UILayout");
    if (ui_layout_tags.empty()) {
        ui_layout_tags = MatchTags(component_body, "RectTransform");
    }
    if (!ui_layout_tags.empty()) {
        UILayoutComponentData ui_layout{};
        ui_layout.size = Float2(Attribute(ui_layout_tags.front(), "size"), ui_layout.size);
        object.components.ui_layout = std::move(ui_layout);
    }

    auto text_tags = MatchTags(component_body, "Text");
    if (!text_tags.empty()) {
        TextComponentData text{};
        text.value = Attribute(text_tags.front(), "value");
        if (text.value.empty()) {
            text.value = Attribute(text_tags.front(), "text");
        }
        text.color = Float4(Attribute(text_tags.front(), "color"), text.color);
        text.size = std::stof(Attribute(text_tags.front(), "size", std::to_string(text.size)));
        object.components.text = std::move(text);
    }

    auto image_tags = MatchTags(component_body, "Image");
    if (!image_tags.empty()) {
        ImageComponentData image{};
        image.texture = Attribute(image_tags.front(), "texture");
        image.color = Float4(Attribute(image_tags.front(), "color"), image.color);
        object.components.image = std::move(image);
    }

    auto progress_bar_tags = MatchTags(component_body, "ProgressBar");
    if (!progress_bar_tags.empty()) {
        ProgressBarComponentData progress_bar{};
        progress_bar.value = std::stof(Attribute(progress_bar_tags.front(), "value", std::to_string(progress_bar.value)));
        progress_bar.min_value = std::stof(Attribute(progress_bar_tags.front(), "min", std::to_string(progress_bar.min_value)));
        progress_bar.max_value = std::stof(Attribute(progress_bar_tags.front(), "max", std::to_string(progress_bar.max_value)));
        object.components.progress_bar = std::move(progress_bar);
    }

    auto slider_tags = MatchTags(component_body, "Slider");
    if (!slider_tags.empty()) {
        SliderComponentData slider{};
        slider.value = std::stof(Attribute(slider_tags.front(), "value", std::to_string(slider.value)));
        slider.min_value = std::stof(Attribute(slider_tags.front(), "min", std::to_string(slider.min_value)));
        slider.max_value = std::stof(Attribute(slider_tags.front(), "max", std::to_string(slider.max_value)));
        slider.target = Attribute(slider_tags.front(), "target");
        slider.method = Attribute(slider_tags.front(), "method");
        if (slider.method.empty()) {
            slider.method = Attribute(slider_tags.front(), "onValueChanged");
        }
        object.components.slider = std::move(slider);
    }

    return object;
}

std::string PrefabInstanceId(std::string const& instance_id, std::string const& prefab_id, std::string const& original_id)
{
    if (original_id.empty() || original_id == prefab_id) {
        return instance_id;
    }
    return instance_id + "/" + original_id;
}

void RewritePrefabObjectIds(std::vector<GameObjectData>& objects, std::string const& instance_id)
{
    if (objects.empty()) {
        return;
    }

    std::string const prefab_root_id = objects.front().id;
    std::unordered_map<std::string, std::string> id_map;
    id_map.reserve(objects.size());
    for (auto const& object : objects) {
        id_map[object.id] = PrefabInstanceId(instance_id, prefab_root_id, object.id);
    }

    for (auto& object : objects) {
        object.id = id_map[object.id];
        if (!object.hierarchy.parent.empty()) {
            if (auto const found = id_map.find(object.hierarchy.parent); found != id_map.end()) {
                object.hierarchy.parent = found->second;
            }
        }
        for (auto& child : object.hierarchy.children) {
            if (auto const found = id_map.find(child); found != id_map.end()) {
                child = found->second;
            }
        }

        if (object.components.script.has_value()) {
            auto& script = *object.components.script;
            if (auto const found = id_map.find(script.target_object); found != id_map.end()) {
                script.target_object = found->second;
                script.parameters["target"] = script.target_object;
            }
            for (auto& [_, value] : script.parameters) {
                if (auto const found = id_map.find(value); found != id_map.end()) {
                    value = found->second;
                }
            }
        }
    }
}

void ApplyRootPrefabOverride(GameObjectData& root, GameObjectData const& instance)
{
    root.name = instance.name.empty() ? root.name : instance.name;
    if (instance.components.triangle_renderer.has_value()) {
        root.components.triangle_renderer = instance.components.triangle_renderer;
    }
    if (instance.components.mesh_renderer.has_value()) {
        root.components.mesh_renderer = instance.components.mesh_renderer;
    }
    if (instance.components.camera.has_value()) {
        root.components.camera = instance.components.camera;
    }
    if (instance.components.light.has_value()) {
        root.components.light = instance.components.light;
    }
    if (instance.components.script.has_value()) {
        root.components.script = instance.components.script;
    }
    if (instance.components.image.has_value()) {
        root.components.image = instance.components.image;
    }
    if (instance.components.button.has_value()) {
        root.components.button = instance.components.button;
    }
    if (instance.components.ui_layout.has_value()) {
        root.components.ui_layout = instance.components.ui_layout;
    }
    if (instance.components.text.has_value()) {
        root.components.text = instance.components.text;
    }
    if (instance.components.progress_bar.has_value()) {
        root.components.progress_bar = instance.components.progress_bar;
    }
    if (instance.components.slider.has_value()) {
        root.components.slider = instance.components.slider;
    }
}

void ApplyRootTransformOverride(GameObjectData& root, ElementMatch const& instance_element)
{
    auto const component_body = RemoveDirectChildElements(instance_element.body, "GameObject");
    auto transform_tags = MatchTags(component_body, "Transform");
    if (transform_tags.empty()) {
        return;
    }

    if (!root.components.transform.has_value()) {
        root.components.transform = TransformData{};
    }

    auto& transform = *root.components.transform;
    auto const& tag = transform_tags.front();
    if (!Attribute(tag, "position").empty()) {
        transform.position = Float3(Attribute(tag, "position"), transform.position);
    }
    if (!Attribute(tag, "rotation").empty()) {
        transform.rotation = Float3(Attribute(tag, "rotation"), transform.rotation);
    }
    if (!Attribute(tag, "scale").empty()) {
        transform.scale = Float3(Attribute(tag, "scale"), transform.scale);
    }
}

void PopulateHierarchyChildren(std::vector<GameObjectData>& objects)
{
    std::unordered_map<std::string, size_t> object_indices;
    object_indices.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        objects[i].hierarchy.children.clear();
        if (!objects[i].id.empty()) {
            object_indices[objects[i].id] = i;
        }
    }

    for (auto const& object : objects) {
        if (object.hierarchy.parent.empty()) {
            continue;
        }
        auto const parent = object_indices.find(object.hierarchy.parent);
        if (parent != object_indices.end()) {
            objects[parent->second].hierarchy.children.push_back(object.id);
        }
    }
}

void PopulateHierarchyChildren(SceneDocument& scene)
{
    PopulateHierarchyChildren(scene.objects);
}

} // namespace

std::string XmlSceneLoader::ReadText(std::filesystem::path const& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ProjectConfig XmlSceneLoader::LoadProject(std::filesystem::path const& project_xml) const
{
    auto const text = ReadText(project_xml);
    return LoadProjectText(text);
}

ProjectConfig XmlSceneLoader::LoadProjectText(std::string const& text) const
{
    std::string const sanitized_text = StripXmlComments(text);
    std::regex const pattern(R"(<Project\b[^>]*>)");
    std::smatch match;
    if (!std::regex_search(sanitized_text, match, pattern)) {
        throw std::runtime_error("Project XML must contain a <Project> root tag.");
    }

    auto const tag = match[0].str();
    ProjectConfig config;
    config.name = Attribute(tag, "name", "AveGame");
    config.package_name = Attribute(tag, "package", "com.ave.game");
    config.entry_scene = Attribute(tag, "entryScene", "scenes/main.scene.xml");
    config.orientation = Attribute(tag, "orientation", "landscape");
    return config;
}

void XmlSceneLoader::SetTextAssetLoader(TextAssetLoader loader)
{
    text_asset_loader_ = std::move(loader);
}

SceneDocument XmlSceneLoader::LoadScene(std::filesystem::path const& scene_xml) const
{
    auto const text = ReadText(scene_xml);
    return LoadSceneText(text);
}

SceneDocument XmlSceneLoader::LoadSceneText(std::string const& text) const
{
    std::string const sanitized_text = StripXmlComments(text);
    std::regex const scene_pattern(R"(<Scene\b[^>]*>)");
    std::smatch scene_match;
    if (!std::regex_search(sanitized_text, scene_match, scene_pattern)) {
        throw std::runtime_error("Scene XML must contain a <Scene> root tag.");
    }

    SceneDocument scene;
    scene.version = Attribute(scene_match[0].str(), "version", "1");
    scene.name = Attribute(scene_match[0].str(), "name", "MainScene");

    auto environment_tags = MatchTags(sanitized_text, "Environment");
    if (!environment_tags.empty()) {
        scene.environment.clear_color = Float4(Attribute(environment_tags.front(), "clearColor"), scene.environment.clear_color);
        scene.environment.ambient_color = Float3(Attribute(environment_tags.front(), "ambientColor"), scene.environment.ambient_color);
    }

    auto const scene_root = FindNextElement(sanitized_text, "Scene", 0);
    if (!scene_root.has_value()) {
        throw std::runtime_error("Scene XML must contain a closed <Scene> root element.");
    }

    std::unordered_map<std::string, std::string> seen_object_ids;
    std::unordered_map<std::string, std::string> seen_object_names;
    std::vector<std::string> prefab_stack;
    std::function<void(ElementMatch const&, std::string const&)> parse_game_object;
    parse_game_object = [&](ElementMatch const& element, std::string const& parent_id) {
        std::string const prefab_path = Attribute(element.tag, "prefab");
        if (!prefab_path.empty()) {
            if (!text_asset_loader_) {
                throw std::runtime_error("Scene uses prefab '" + prefab_path + "' but no text asset loader is configured.");
            }
            if (std::find(prefab_stack.begin(), prefab_stack.end(), prefab_path) != prefab_stack.end()) {
                throw std::runtime_error("Recursive prefab reference detected: " + prefab_path);
            }

            GameObjectData instance = ParseGameObjectData(element, parent_id);
            if (instance.id.empty()) {
                throw std::runtime_error("Prefab instance must define an id for '" + prefab_path + "'.");
            }

            prefab_stack.push_back(prefab_path);
            auto const prefab_text = text_asset_loader_(prefab_path);
            if (prefab_text.empty()) {
                prefab_stack.pop_back();
                throw std::runtime_error("Prefab asset not found or empty: " + prefab_path);
            }
            auto prefab = LoadPrefabText(prefab_text);
            prefab_stack.pop_back();
            if (prefab.objects.empty()) {
                throw std::runtime_error("Prefab '" + prefab_path + "' has no root GameObject.");
            }

            std::vector<GameObjectData> expanded;
            expanded.reserve(prefab.objects.size());
            for (auto root : prefab.objects) {
                if (root.hierarchy.parent.empty()) {
                    root.hierarchy.parent = parent_id;
                }
                expanded.push_back(std::move(root));
            }

            RewritePrefabObjectIds(expanded, instance.id);
            ApplyRootPrefabOverride(expanded.front(), instance);
            ApplyRootTransformOverride(expanded.front(), element);
            expanded.front().hierarchy.parent = parent_id;

            for (auto& object : expanded) {
                scene.objects.push_back(std::move(object));
            }
            return;
        }

        GameObjectData object = ParseGameObjectData(element, parent_id);
        std::string const object_id = object.id;
        if (object.id.empty()) {
            throw std::runtime_error("GameObject must define a non-empty id attribute.");
        }

        if (auto const existing = seen_object_ids.find(object.id); existing != seen_object_ids.end()) {
            throw std::runtime_error(
                "Scene XML has duplicate GameObject id '" + object.id +
                "' (used by '" + existing->second + "' and '" + object.name + "').");
        }
        seen_object_ids.emplace(object.id, object.name);

        if (auto const existing = seen_object_names.find(object.name); existing != seen_object_names.end()) {
            throw std::runtime_error(
                "Scene XML has duplicate GameObject name '" + object.name +
                "' (used by id '" + existing->second + "' and id '" + object.id + "').");
        }
        seen_object_names.emplace(object.name, object.id);

        scene.objects.push_back(std::move(object));
        for (auto const& child : DirectChildElements(element.body, "GameObject")) {
            parse_game_object(child, object_id);
        }
    };

    for (auto const& object : DirectChildElements(scene_root->body, "GameObject")) {
        parse_game_object(object, "");
    }

    PopulateHierarchyChildren(scene);

    return scene;
}

PrefabDocument XmlSceneLoader::LoadPrefabText(std::string const& text) const
{
    std::string const sanitized_text = StripXmlComments(text);
    std::regex const prefab_pattern(R"(<Prefab\b[^>]*>)");
    std::smatch prefab_match;
    if (!std::regex_search(sanitized_text, prefab_match, prefab_pattern)) {
        throw std::runtime_error("Prefab XML must contain a <Prefab> root tag.");
    }

    auto const prefab_root = FindNextElement(sanitized_text, "Prefab", 0);
    if (!prefab_root.has_value()) {
        throw std::runtime_error("Prefab XML must contain a closed <Prefab> root element.");
    }

    PrefabDocument prefab;
    prefab.name = Attribute(prefab_match[0].str(), "name");

    std::function<void(ElementMatch const&, std::string const&)> parse_prefab_object;
    parse_prefab_object = [&](ElementMatch const& element, std::string const& parent_id) {
        GameObjectData object = ParseGameObjectData(element, parent_id);
        std::string const object_id = object.id;
        prefab.objects.push_back(std::move(object));
        for (auto const& child : DirectChildElements(element.body, "GameObject")) {
            parse_prefab_object(child, object_id);
        }
    };

    for (auto const& object : DirectChildElements(prefab_root->body, "GameObject")) {
        parse_prefab_object(object, "");
    }

    PopulateHierarchyChildren(prefab.objects);
    return prefab;
}

MaterialDocument XmlSceneLoader::LoadMaterialText(std::string const& text) const
{
    std::string const sanitized_text = StripXmlComments(text);
    std::regex const pattern(R"(<Material\b[^>]*>)");
    std::smatch match;
    if (!std::regex_search(sanitized_text, match, pattern)) {
        throw std::runtime_error("Material XML must contain a <Material> root tag.");
    }

    auto const tag = match[0].str();
    MaterialDocument doc;
    doc.name = Attribute(tag, "name");
    doc.shader = Attribute(tag, "shader");

    auto color_tags = MatchTags(sanitized_text, "Color");
    for (auto const& color_tag : color_tags) {
        auto name = Attribute(color_tag, "name");
        if (name == "baseColor") {
            doc.base_color = Float4(Attribute(color_tag, "value"), doc.base_color);
        }
    }

    auto float_tags = MatchTags(sanitized_text, "Float");
    for (auto const& float_tag : float_tags) {
        auto name = Attribute(float_tag, "name");
        if (name == "metallic") {
            doc.metallic = std::stof(Attribute(float_tag, "value", std::to_string(doc.metallic)));
        } else if (name == "roughness") {
            doc.roughness = std::stof(Attribute(float_tag, "value", std::to_string(doc.roughness)));
        } else if (name == "normalScale" || name == "normal_scale") {
            doc.normal_scale = std::stof(Attribute(float_tag, "value", std::to_string(doc.normal_scale)));
        }
    }

    auto texture_tags = MatchTags(sanitized_text, "Texture");
    for (auto const& texture_tag : texture_tags) {
        auto name = Attribute(texture_tag, "name");
        if (name == "baseColor") {
            doc.base_color_texture = Attribute(texture_tag, "path");
            if (doc.base_color_texture.empty()) {
                doc.base_color_texture = Attribute(texture_tag, "texture");
            }
        } else if (name == "normal") {
            doc.normal_texture = Attribute(texture_tag, "path");
            if (doc.normal_texture.empty()) {
                doc.normal_texture = Attribute(texture_tag, "texture");
            }
        } else if (name == "metallicRoughness" || name == "metallic_roughness") {
            doc.metallic_roughness_texture = Attribute(texture_tag, "path");
            if (doc.metallic_roughness_texture.empty()) {
                doc.metallic_roughness_texture = Attribute(texture_tag, "texture");
            }
        }
    }

    return doc;
}

} // namespace ave::project
