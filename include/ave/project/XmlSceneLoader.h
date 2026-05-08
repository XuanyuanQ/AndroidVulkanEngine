#pragma once

#include "ave/project/ProjectConfig.h"
#include "ave/project/SceneDocument.h"

#include <filesystem>
#include <string>

namespace ave::project {

class XmlSceneLoader {
public:
    ProjectConfig LoadProject(std::filesystem::path const& project_xml) const;
    SceneDocument LoadScene(std::filesystem::path const& scene_xml) const;

private:
    static std::string ReadText(std::filesystem::path const& path);
};

} // namespace ave::project
