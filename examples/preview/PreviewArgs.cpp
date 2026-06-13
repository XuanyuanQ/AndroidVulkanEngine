#include "PreviewArgs.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ave_preview {

PreviewArgs ParsePreviewArgs(int argc, char** argv)
{
    if (argc < 2) {
        throw std::runtime_error("usage: ave_preview <project-dir> [compiled-shader-dir] [width] [height]");
    }

    PreviewArgs args{};
    args.project_dir = std::filesystem::absolute(argv[1]).lexically_normal();
    if (argc >= 3) {
        args.compiled_shader_dir = std::filesystem::absolute(argv[2]).lexically_normal();
    }
    if (argc >= 4) {
        args.width = static_cast<uint32_t>(std::max(std::stoi(argv[3]), 1));
    }
    if (argc >= 5) {
        args.height = static_cast<uint32_t>(std::max(std::stoi(argv[4]), 1));
    }
    return args;
}

} // namespace ave_preview
