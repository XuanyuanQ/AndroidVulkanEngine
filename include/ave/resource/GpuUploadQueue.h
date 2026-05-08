#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ave::resource {

struct UploadRequest {
    uint32_t asset_id = 0;
    std::string debug_name;
    size_t byte_size = 0;
};

class GpuUploadQueue {
public:
    void Enqueue(UploadRequest request);
    std::vector<UploadRequest> Drain();

private:
    std::mutex mutex_;
    std::vector<UploadRequest> requests_;
};

} // namespace ave::resource
