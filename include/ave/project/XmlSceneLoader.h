#pragma once

#include "ave/project/ProjectConfig.h"
#include "ave/project/SceneDocument.h"

#include <filesystem>
#include <cstring>
#include <glm/glm.hpp>
#include <string>

namespace ave::project {

struct MaterialDocument {
    std::string name;
    std::string shader;
    glm::vec4 base_color{0.5f, 0.5f, 0.5f, 1.0f};
    std::string base_color_texture;
    float metallic = 0.0f;
    float roughness = 0.5f;
};

class XmlSceneLoader {
public:
    ProjectConfig LoadProject(std::filesystem::path const& project_xml) const;
    SceneDocument LoadScene(std::filesystem::path const& scene_xml) const;
    ProjectConfig LoadProjectText(std::string const& text) const;
    SceneDocument LoadSceneText(std::string const& text) const;
    MaterialDocument LoadMaterialText(std::string const& text) const;

private:
    static std::string ReadText(std::filesystem::path const& path);
};

} // namespace ave::project
