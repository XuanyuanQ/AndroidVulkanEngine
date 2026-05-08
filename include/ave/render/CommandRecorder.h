#pragma once

#include "ave/core/FrameData.h"
#include "ave/core/JobSystem.h"

#include <string>
#include <vector>

namespace ave::render {

struct RecordedCommandBuffer {
    uint32_t worker_index = 0;
    std::vector<std::string> debug_commands;
};

class CommandRecorder {
public:
    std::vector<RecordedCommandBuffer> RecordSceneParallel(core::FrameData const& frame, core::JobSystem& jobs);
};

} // namespace ave::render
