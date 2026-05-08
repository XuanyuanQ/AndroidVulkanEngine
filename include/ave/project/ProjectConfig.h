#pragma once

#include <string>

namespace ave::project {

struct ProjectConfig {
    std::string name;
    std::string package_name;
    std::string entry_scene;
    std::string orientation = "landscape";
};

} // namespace ave::project
