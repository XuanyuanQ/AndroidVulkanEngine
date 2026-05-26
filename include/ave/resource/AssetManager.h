#pragma once

#include "ave/core/JobSystem.h"

#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace ave::resource {

struct AssetHandle {
    uint32_t id = 0;
};

struct PendingAsset {
    AssetHandle handle{};
    std::string path;
};

class AssetManager {
public:
    void Initialize(core::JobSystem& jobs);
    std::future<AssetHandle> LoadAsync(std::string path);
    std::vector<PendingAsset> CompletedAssets() const;

private:
    core::JobSystem* jobs_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<PendingAsset> completed_;
    uint32_t next_asset_id_ = 1;
};

} // namespace ave::resource
