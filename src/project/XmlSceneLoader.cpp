#include "ave/project/XmlSceneLoader.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace ave::project {

namespace {

std::string Attribute(std::string const& tag, std::string const& name, std::string fallback = {})
{
    std::regex const pattern(name + "=\"([^\"]*)\"");
    std::smatch match;
    return std::regex_search(tag, match, pattern) ? match[1].str() : std::move(fallback);
}

std::array<float, 3> Float3(std::string const& text, std::array<float, 3> fallback)
{
    if (text.empty()) {
        return fallback;
    }

    std::array<float, 3> value = fallback;
    char comma = 0;
    std::stringstream stream(text);
    stream >> value[0] >> comma >> value[1] >> comma >> value[2];
    return value;
}

std::array<float, 4> Float4(std::string const& text, std::array<float, 4> fallback)
{
    if (text.empty()) {
        return fallback;
    }

    std::array<float, 4> value = fallback;
    char comma = 0;
    std::stringstream stream(text);
    stream >> value[0] >> comma >> value[1] >> comma >> value[2] >> comma >> value[3];
    return value;
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
    std::regex const pattern(R"(<Project\b[^>]*>)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
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
    std::regex const scene_pattern(R"(<Scene\b[^>]*>)");
    std::smatch scene_match;
    if (!std::regex_search(text, scene_match, scene_pattern)) {
        throw std::runtime_error("Scene XML must contain a <Scene> root tag.");
    }

    SceneDocument scene;
    scene.name = Attribute(scene_match[0].str(), "name", "MainScene");

    std::regex const object_pattern(R"(<GameObject\b([^>]*)>([\s\S]*?)</GameObject>)");
    for (std::sregex_iterator it(text.begin(), text.end(), object_pattern), end; it != end; ++it) {
        auto const object_tag = "<GameObject" + (*it)[1].str() + ">";
        auto const body = (*it)[2].str();

        GameObjectData object;
        object.id = Attribute(object_tag, "id");
        object.name = Attribute(object_tag, "name", object.id);
        object.parent = Attribute(object_tag, "parent");

        auto transform_tags = MatchTags(body, "Transform");
        if (!transform_tags.empty()) {
            object.transform.position = Float3(Attribute(transform_tags.front(), "position"), object.transform.position);
            object.transform.rotation = Float3(Attribute(transform_tags.front(), "rotation"), object.transform.rotation);
            object.transform.scale = Float3(Attribute(transform_tags.front(), "scale"), object.transform.scale);
        }

        auto triangle_tags = MatchTags(body, "TriangleRenderer");
        if (!triangle_tags.empty()) {
            object.has_triangle = true;
            object.triangle.color = Float4(Attribute(triangle_tags.front(), "color"), object.triangle.color);
            object.triangle.material = Attribute(triangle_tags.front(), "material");
        }

        auto mesh_tags = MatchTagBodies(body, "MeshRenderer");
        if (!mesh_tags.empty()) {
            object.has_mesh = true;
            object.mesh.material = Attribute(mesh_tags.front().first, "material");

            for (auto const& vertex_tag : MatchTags(mesh_tags.front().second, "Vertex")) {
                VertexData vertex{};
                vertex.position = Float3(Attribute(vertex_tag, "position"), vertex.position);
                vertex.color = Float4(Attribute(vertex_tag, "color"), vertex.color);
                object.mesh.vertices.push_back(vertex);
            }
        }

        auto script_tags = MatchTags(body, "Script");
        if (!script_tags.empty()) {
            object.has_script = true;
            object.script.java_class = Attribute(script_tags.front(), "class");
        }

        auto button_tags = MatchTags(body, "Button");
        if (!button_tags.empty()) {
            object.has_button = true;
            object.button.target = Attribute(button_tags.front(), "target");
            object.button.method = Attribute(button_tags.front(), "method");
        }

        scene.objects.push_back(std::move(object));
    }

    return scene;
}

} // namespace ave::project
