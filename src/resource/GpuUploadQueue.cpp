#include "ave/resource/GpuUploadQueue.h"

namespace ave::resource {

void GpuUploadQueue::Enqueue(UploadRequest request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back(std::move(request));
}

std::vector<UploadRequest> GpuUploadQueue::Drain()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto drained = std::move(requests_);
    requests_.clear();
    return drained;
}

} // namespace ave::resource
