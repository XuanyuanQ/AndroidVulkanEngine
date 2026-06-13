#pragma once

#include <cstdint>
#include <filesystem>

namespace ave_preview {

struct PreviewArgs {
    std::filesystem::path project_dir;
    std::filesystem::path compiled_shader_dir;
    uint32_t width = 1280;
    uint32_t height = 720;
};

PreviewArgs ParsePreviewArgs(int argc, char** argv);

} // namespace ave_preview
