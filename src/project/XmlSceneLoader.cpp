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

void PopulateHierarchyChildren(SceneDocument& scene)
{
    std::unordered_map<std::string, size_t> object_indices;
    object_indices.reserve(scene.objects.size());
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        scene.objects[i].hierarchy.children.clear();
        if (!scene.objects[i].id.empty()) {
            object_indices[scene.objects[i].id] = i;
        }
    }

    for (auto const& object : scene.objects) {
        if (object.hierarchy.parent.empty()) {
            continue;
        }
        auto const parent = object_indices.find(object.hierarchy.parent);
        if (parent != object_indices.end()) {
            scene.objects[parent->second].hierarchy.children.push_back(object.id);
        }
    }
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

    std::function<void(ElementMatch const&, std::string const&)> parse_game_object;
    parse_game_object = [&](ElementMatch const& element, std::string const& parent_id) {
        auto const& object_tag = element.tag;
        auto const& body = element.body;
        auto const component_body = RemoveDirectChildElements(body, "GameObject");

        GameObjectData object;
        object.id = Attribute(object_tag, "id");
        object.name = Attribute(object_tag, "name", object.id);
        object.hierarchy.parent = parent_id;
        std::string const object_id = object.id;

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
                    vertex.tangent = Float3(Attribute(vertex_tag, "tangent"), vertex.tangent);
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
            object.components.button = std::move(button);
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

        scene.objects.push_back(std::move(object));
        for (auto const& child : DirectChildElements(body, "GameObject")) {
            parse_game_object(child, object_id);
        }
    };

    for (auto const& object : DirectChildElements(scene_root->body, "GameObject")) {
        parse_game_object(object, "");
    }

    PopulateHierarchyChildren(scene);

    return scene;
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
        }
    }

    return doc;
}

} // namespace ave::project
