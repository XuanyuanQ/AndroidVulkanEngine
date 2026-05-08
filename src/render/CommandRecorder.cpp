#include "ave/render/CommandRecorder.h"

#include <algorithm>

namespace ave::render {

std::vector<RecordedCommandBuffer> CommandRecorder::RecordSceneParallel(core::FrameData const& frame, core::JobSystem& jobs)
{
    size_t const renderable_count = frame.renderables.size();
    if (renderable_count == 0) {
        return {};
    }

    size_t const worker_count = std::max<size_t>(1, jobs.ThreadCount());
    size_t const batch_size = std::max<size_t>(1, (renderable_count + worker_count - 1) / worker_count);
    size_t const batch_count = (renderable_count + batch_size - 1) / batch_size;
    std::vector<RecordedCommandBuffer> command_buffers(batch_count);

    jobs.ParallelFor(batch_count, 1, [&](size_t begin, size_t end) {
        for (size_t batch = begin; batch < end; ++batch) {
            size_t const item_begin = batch * batch_size;
            size_t const item_end = std::min(renderable_count, item_begin + batch_size);

            auto& recorded = command_buffers[batch];
            recorded.worker_index = static_cast<uint32_t>(batch);
            recorded.debug_commands.reserve(item_end - item_begin);

            for (size_t item = item_begin; item < item_end; ++item) {
                recorded.debug_commands.push_back(frame.renderables[item].debug_name);
            }
        }
    });

    return command_buffers;
}

} // namespace ave::render
